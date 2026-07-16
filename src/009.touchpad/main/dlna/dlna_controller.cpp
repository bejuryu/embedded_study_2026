#include "dlna_controller.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>

#include "dlna_event_listener.hpp"
#include "dlna_parser.hpp"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "image_pipeline.hpp"

namespace {
std::string get_local_ip() {
  esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (netif) {
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
      char ip_str[16];
      esp_ip4addr_ntoa(&ip_info.ip, ip_str, sizeof(ip_str));
      return std::string(ip_str);
    }
  }
  return "127.0.0.1";
}
}  // namespace

// display context 연동을 위한 전방 선언 헤더
#include "display/context.hpp"

namespace Ble::Dlna {

static const char* TAG = "DlnaController";

namespace {
struct ArtDecodeJob {
  std::string title;
  std::string artist;
  std::string art_url;
  void*       display_ctx;
};

// 앨범아트 다운로드 및 엇갈림 방지 일괄 드로우 커밋 태스크 (5.3.② 및 7.3 사양)
void art_decode_task(void* arg) {
  auto*     job        = static_cast<ArtDecodeJob*>(arg);
  uint16_t* rgb565_buf = nullptr;
  uint32_t  w = 0, h = 0;

  // ImagePipeline을 통한 비동기 다운로드 및 디코딩 (Content-Length 및 SOF0/IHDR 해상도 640px 가드 포함)
  bool ok = ImagePipeline::download_and_decode(job->art_url, &rgb565_buf, w, h);

  auto* display = static_cast<Display::Context*>(job->display_ctx);
  if (display) {
    if (ok && rgb565_buf != nullptr) {
      // [5.3.② 사양: 엇갈림 없는 텍스트-이미지 일괄 드로우 커밋]
      display->set_media_album_art(rgb565_buf, w, h);
      display->update_media_track(job->title.c_str(), job->artist.c_str());
    } else {
      // [7.3 사양: 2.5초 타임아웃/실패 시 기본 플레이스홀더 강제 폴백 드로우 커밋]
      display->set_media_album_art(nullptr, 0, 0);
      display->update_media_track(job->title.c_str(), job->artist.c_str());
    }
  } else {
    if (rgb565_buf) heap_caps_free(rgb565_buf);  // Context가 먼저 소멸한 경우 누수 방어
  }

  delete job;
  vTaskDelete(nullptr);
}
}  // namespace

DlnaController& DlnaController::instance() {
  static DlnaController inst;
  return inst;
}

bool DlnaController::initialize() {
  if (state_ != DlnaState::UNINITIALIZED) return true;

  command_queue_ = xQueueCreate(1, sizeof(DlnaCommand));
  event_queue_   = xQueueCreate(10, sizeof(std::string*));

  state_ = DlnaState::IDLE;

  xTaskCreate(dlna_task, "dlna_worker_task", 8192, this, 4, &dlna_task_handle_);
  return (dlna_task_handle_ != nullptr);
}

void DlnaController::start_search() {
  DlnaCommand cmd{};
  cmd.type = DlnaCmdType::START_SEARCH;
  if (command_queue_) {
    xQueueSend(command_queue_, &cmd, 0);
  }
}

void DlnaController::select_target(const std::string& udn) {
  DlnaCommand cmd{};
  cmd.type = DlnaCmdType::SELECT_TARGET;
  std::memset(cmd.target_udn, 0, sizeof(cmd.target_udn));
  udn.copy(cmd.target_udn, sizeof(cmd.target_udn) - 1);
  if (command_queue_) {
    xQueueSend(command_queue_, &cmd, 0);
  }
}

void DlnaController::send_media_control(const char* action) {
  DlnaCommand cmd{};
  if (strcmp(action, "Play") == 0)
    cmd.type = DlnaCmdType::PLAY;
  else if (strcmp(action, "Pause") == 0)
    cmd.type = DlnaCmdType::PAUSE;
  else if (strcmp(action, "Next") == 0)
    cmd.type = DlnaCmdType::NEXT;
  else if (strcmp(action, "Previous") == 0)
    cmd.type = DlnaCmdType::PREVIOUS;
  else if (strcmp(action, "Shuffle_On") == 0)
    cmd.type = DlnaCmdType::SHUFFLE_ON;
  else if (strcmp(action, "Shuffle_Off") == 0)
    cmd.type = DlnaCmdType::SHUFFLE_OFF;
  else if (strcmp(action, "Repeat_On") == 0)
    cmd.type = DlnaCmdType::REPEAT_ON;
  else if (strcmp(action, "Repeat_Off") == 0)
    cmd.type = DlnaCmdType::REPEAT_OFF;

  if (command_queue_) {
    xQueueSend(command_queue_, &cmd, 0);
  }
}

void DlnaController::set_volume(uint8_t volume) {
  DlnaCommand cmd{};
  cmd.type  = DlnaCmdType::SET_VOLUME;
  cmd.value = volume;
  if (command_queue_) {
    xQueueOverwrite(command_queue_, &cmd);
  }
}

void DlnaController::send_ssdp_msearch() {
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) {
    ESP_LOGE(TAG, "Failed to create SSDP socket");
    return;
  }

  // UDP 응답 수신을 위해 로컬 소켓 바인딩(bind) 필수 적용
  struct sockaddr_in local_addr{};
  local_addr.sin_family      = AF_INET;
  local_addr.sin_port        = htons(0);  // 가용 포트 자동 배정
  local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(sock, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
    ESP_LOGE(TAG, "Failed to bind SSDP socket");
    close(sock);
    return;
  }

  // 멀티캐스트 패킷의 유효 홉(TTL) 확장 설정
  int ttl = 4;
  setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

  struct sockaddr_in dest_addr{};
  dest_addr.sin_family      = AF_INET;
  dest_addr.sin_port        = htons(Config::kSsdpMulticastPort);
  dest_addr.sin_addr.s_addr = inet_addr(Config::kSsdpMulticastIp.data());

  std::string msearch_payload =
      "M-SEARCH * HTTP/1.1\r\n"
      "HOST: 239.255.255.250:1900\r\n"
      "MAN: \"ssdp:discover\"\r\n"
      "MX: 3\r\n"
      "ST: urn:schemas-upnp-org:device:MediaRenderer:1\r\n"
      "USER-AGENT: M5Stack-TAB5/1.0 UPnP/1.1\r\n\r\n";

  sendto(sock, msearch_payload.c_str(), msearch_payload.length(), 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr));

  struct timeval tv{};
  tv.tv_sec  = Config::kSsdpSearchTimeoutS;
  tv.tv_usec = 0;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  char               buf[1024];
  struct sockaddr_in sender_addr{};
  socklen_t          addr_len = sizeof(sender_addr);

  ESP_LOGI(TAG, "SSDP M-SEARCH query sent. Listening for responses...");

  while (state_ == DlnaState::SEARCHING) {
    int len = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&sender_addr, &addr_len);
    if (len < 0) {
      break;
    }
    buf[len]              = '\0';
    std::string sender_ip = inet_ntoa(sender_addr.sin_addr);
    handle_ssdp_packet(buf, len, sender_ip);
  }

  close(sock);
  if (state_ == DlnaState::SEARCHING) {
    state_ = DlnaState::IDLE;
  }
  ESP_LOGI(TAG, "SSDP search finished. Discovered %d DMR speakers.", devices_.size());
}

namespace {
std::string extract_value(const std::string& str, const std::string& start_tag, const std::string& end_tag) {
  size_t start = str.find(start_tag);
  if (start != std::string::npos) {
    size_t end = str.find(end_tag, start);
    if (end != std::string::npos) {
      return str.substr(start + start_tag.length(), end - (start + start_tag.length()));
    }
  }
  return "";
}

std::string extract_header_value(const std::string& packet, const std::string& header_name) {
  size_t pos = packet.find(header_name);
  if (pos != std::string::npos) {
    size_t end = packet.find("\r\n", pos);
    if (end != std::string::npos) {
      size_t val_start = pos + header_name.length();
      while (val_start < end && (packet[val_start] == ' ' || packet[val_start] == ':')) {
        val_start++;
      }
      return packet.substr(val_start, end - val_start);
    }
  }
  return "";
}
}  // namespace

void DlnaController::handle_ssdp_packet(const char* packet, size_t len, const std::string& sender_ip) {
  std::string pkt(packet, len);
  std::string location = extract_header_value(pkt, "LOCATION");
  if (location.empty()) return;

  std::string usn       = extract_header_value(pkt, "USN");
  std::string udn       = usn;
  size_t      colon_pos = usn.find("::");
  if (colon_pos != std::string::npos) {
    udn = usn.substr(0, colon_pos);
  }

  // 1. SSDP UDN 중복 제거 필터 (8.1절 사양)
  {
    std::lock_guard<std::mutex> lock(device_mutex_);
    for (const auto& dev : devices_) {
      if (dev.udn == udn) {
        return;
      }
    }
  }

  // 2. description.xml 파일 GET (3초 타임아웃)
  esp_http_client_config_t config = {};
  config.url                      = location.c_str();
  config.timeout_ms               = 3000;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) return;

  std::string xml_desc;
  esp_err_t   err = esp_http_client_open(client, 0);
  if (err == ESP_OK) {
    esp_http_client_fetch_headers(client);
    char buf[512];
    int  read_len = 0;
    while ((read_len = esp_http_client_read(client, buf, sizeof(buf) - 1)) > 0) {
      buf[read_len] = '\0';
      xml_desc.append(buf, read_len);
    }
  }
  esp_http_client_cleanup(client);

  if (xml_desc.empty()) return;

  std::string friendly_name = extract_value(xml_desc, "<friendlyName>", "</friendlyName>");
  if (friendly_name.empty()) friendly_name = "Unknown DMR Speaker";

  std::string base_url  = location;
  size_t      slash_pos = location.find("/", 7);
  if (slash_pos != std::string::npos) {
    base_url = location.substr(0, slash_pos);
  }

  auto resolve_url = [&](const std::string& path) -> std::string {
    if (path.rfind("http", 0) == 0) return path;
    if (path.rfind("/", 0) == 0) return base_url + path;
    return base_url + "/" + path;
  };

  std::string control_url;
  std::string event_sub_url;
  std::string rendering_ctrl_url;

  size_t av_pos = xml_desc.find("urn:schemas-upnp-org:service:AVTransport:1");
  if (av_pos != std::string::npos) {
    control_url   = resolve_url(extract_value(xml_desc.substr(av_pos), "<controlURL>", "</controlURL>"));
    event_sub_url = resolve_url(extract_value(xml_desc.substr(av_pos), "<eventSubURL>", "</eventSubURL>"));
  }

  size_t rc_pos = xml_desc.find("urn:schemas-upnp-org:service:RenderingControl:1");
  if (rc_pos != std::string::npos) {
    rendering_ctrl_url = resolve_url(extract_value(xml_desc.substr(rc_pos), "<controlURL>", "</controlURL>"));
  }

  bool        has_oh_playlist = false;
  std::string oh_playlist_url;
  size_t      oh_pos = xml_desc.find("av-openhome-org:service:Playlist:");
  if (oh_pos == std::string::npos) {
    oh_pos = xml_desc.find("urn:av-openhome-org:service:Playlist:1");
  }
  if (oh_pos != std::string::npos) {
    oh_playlist_url = resolve_url(extract_value(xml_desc.substr(oh_pos), "<controlURL>", "</controlURL>"));
    has_oh_playlist = true;
    ESP_LOGI(TAG, "Device supports OpenHome Playlist - URL: %s", oh_playlist_url.c_str());
  }

  DlnaDevice dev{.friendly_name      = friendly_name,
                 .udn                = udn,
                 .control_url        = control_url,
                 .event_sub_url      = event_sub_url,
                 .rendering_ctrl_url = rendering_ctrl_url,
                 .ip_address         = sender_ip,
                 .has_oh_playlist    = has_oh_playlist,
                 .oh_playlist_url    = oh_playlist_url};

  {
    std::lock_guard<std::mutex> lock(device_mutex_);
    devices_.push_back(dev);
  }
  ESP_LOGI(TAG, "Discovered Target: %s", dev.friendly_name.c_str());
}

namespace {
esp_err_t soap_http_event_handler(esp_http_client_event_t* evt) {
  if (evt->event_id == HTTP_EVENT_ON_DATA) {
    if (evt->user_data) {
      auto* response = static_cast<std::string*>(evt->user_data);
      response->append(static_cast<char*>(evt->data), evt->data_len);
    }
  }
  return ESP_OK;
}

esp_err_t subscribe_http_event_handler(esp_http_client_event_t* evt) {
  if (evt->event_id == HTTP_EVENT_ON_HEADER) {
    ESP_LOGI("DlnaGenaSub", "Received Header: %s: %s", evt->header_key, evt->header_value);

    auto* controller = static_cast<DlnaController*>(evt->user_data);
    if (controller) {
      std::string key = evt->header_key;
      std::transform(key.begin(), key.end(), key.begin(), ::tolower);

      if (key == "sid") {
        controller->set_subscription_sid(evt->header_value);
      } else if (key == "timeout") {
        std::string to_str  = evt->header_value;
        size_t      sec_pos = to_str.find("Second-");
        if (sec_pos != std::string::npos) {
          controller->set_subscription_timeout(strtol(to_str.substr(sec_pos + 7).c_str(), nullptr, 10));
        }
      }
    }
  }
  return ESP_OK;
}
}  // namespace

bool DlnaController::send_soap_request(const std::string& url, const std::string& service_type, const std::string& action, const std::string& body, std::string* out_response) {
  if (url.empty()) return false;

  ESP_LOGI(TAG, "Sending SOAP Request - URL: %s, Action: %s", url.c_str(), action.c_str());

  esp_http_client_config_t config = {};
  config.url                      = url.c_str();
  config.method                   = HTTP_METHOD_POST;
  config.timeout_ms               = 3000;
  if (out_response) {
    out_response->clear();
    config.event_handler = soap_http_event_handler;
    config.user_data     = out_response;
  }

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) return false;

  std::string soap_action = "\"" + service_type + "#" + action + "\"";
  esp_http_client_set_header(client, "Content-Type", "text/xml; charset=\"utf-8\"");
  esp_http_client_set_header(client, "SOAPACTION", soap_action.c_str());

  std::string payload =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
      "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
      "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
      "  <s:Body>\r\n"
      "    <u:" +
      action + " xmlns:u=\"" + service_type +
      "\">\r\n"
      "      " +
      body +
      "\r\n"
      "    </u:" +
      action +
      ">\r\n"
      "  </s:Body>\r\n"
      "</s:Envelope>\r\n";

  esp_http_client_set_post_field(client, payload.c_str(), payload.length());

  esp_err_t err     = esp_http_client_perform(client);
  bool      success = false;

  if (err == ESP_OK) {
    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "SOAP Response HTTP Status: %d", status);
    if (status == 200) {
      success = true;
      if (out_response) {
        ESP_LOGI(TAG, "SOAP Response XML: %s", out_response->c_str());
      }
    } else {
      if (out_response) {
        ESP_LOGW(TAG, "SOAP Error Response XML: %s", out_response->c_str());
      }
    }
  } else {
    ESP_LOGE(TAG, "SOAP HTTP Client Perform Failed: esp_err: 0x%x", err);
  }

  esp_http_client_cleanup(client);
  return success;
}

bool DlnaController::subscribe_events() {
  if (active_target_.event_sub_url.empty()) return false;

  char        listen_url[128];
  std::string local_ip   = get_local_ip();
  std::string format_str = "<" + std::string(Config::kGenaCallbackUrlFormat) + ">";
  snprintf(listen_url, sizeof(listen_url), format_str.c_str(), local_ip.c_str(), Config::kGenaListenerPort);

  subscription_sid_.clear();
  subscription_timeout_ = 1800;

  esp_http_client_config_t config = {};
  config.url                      = active_target_.event_sub_url.c_str();
  config.method                   = HTTP_METHOD_SUBSCRIBE;
  config.timeout_ms               = 3000;
  config.event_handler            = subscribe_http_event_handler;
  config.user_data                = this;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) return false;

  esp_http_client_set_header(client, "CALLBACK", listen_url);
  esp_http_client_set_header(client, "NT", "upnp:event");
  esp_http_client_set_header(client, "TIMEOUT", "Second-1800");

  ESP_LOGI(TAG, "Sending GENA SUBSCRIBE request...");
  esp_err_t err     = esp_http_client_perform(client);
  bool      success = false;

  if (err == ESP_OK) {
    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "GENA SUBSCRIBE Response Status: %d", status);
    if (status == 200) {
      success = true;
      ESP_LOGI(TAG, "GENA Subscription Successful. Captured SID: '%s', Timeout: %lu", subscription_sid_.c_str(), subscription_timeout_);
    }
  } else {
    ESP_LOGE(TAG, "GENA SUBSCRIBE request failed: esp_err: 0x%x", err);
  }

  esp_http_client_cleanup(client);
  return success;
}

bool DlnaController::renew_subscription() {
  if (subscription_sid_.empty() || active_target_.event_sub_url.empty()) return false;

  esp_http_client_config_t config = {};
  config.url                      = active_target_.event_sub_url.c_str();
  config.method                   = HTTP_METHOD_SUBSCRIBE;
  config.timeout_ms               = 3000;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) return false;

  esp_http_client_set_header(client, "SID", subscription_sid_.c_str());
  esp_http_client_set_header(client, "TIMEOUT", "Second-1800");

  esp_err_t err     = esp_http_client_perform(client);
  bool      success = (err == ESP_OK && esp_http_client_get_status_code(client) == 200);
  esp_http_client_cleanup(client);
  return success;
}

void DlnaController::unsubscribe_events() {
  if (subscription_sid_.empty() || active_target_.event_sub_url.empty()) return;

  esp_http_client_config_t config = {};
  config.url                      = active_target_.event_sub_url.c_str();
  config.method                   = HTTP_METHOD_UNSUBSCRIBE;
  config.timeout_ms               = 2000;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client) {
    esp_http_client_set_header(client, "SID", subscription_sid_.c_str());
    esp_http_client_perform(client);
    esp_http_client_cleanup(client);
  }
  subscription_sid_.clear();
}

bool DlnaController::check_renderer_alive() {
  if (active_target_.ip_address.empty()) return false;

  int         port      = 80;  // default
  std::string url       = active_target_.control_url;
  size_t      colon_pos = url.find(":", 7);  // Skip "http://"
  if (colon_pos != std::string::npos) {
    size_t      slash_pos = url.find("/", colon_pos);
    std::string port_str;
    if (slash_pos != std::string::npos) {
      port_str = url.substr(colon_pos + 1, slash_pos - colon_pos - 1);
    } else {
      port_str = url.substr(colon_pos + 1);
    }
    if (!port_str.empty()) {
      char* endptr = nullptr;
      long  val    = strtol(port_str.c_str(), &endptr, 10);
      if (endptr != port_str.c_str() && val > 0 && val <= 65535) {
        port = static_cast<int>(val);
      }
    }
  }

  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) return false;

  struct timeval tv{};
  tv.tv_sec  = 1;
  tv.tv_usec = 0;
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  struct sockaddr_in addr{};
  addr.sin_family      = AF_INET;
  addr.sin_port        = htons(port);
  addr.sin_addr.s_addr = inet_addr(active_target_.ip_address.c_str());

  bool alive = (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0);
  close(sock);
  return alive;
}

void DlnaController::dlna_task(void* arg) {
  auto*             self = static_cast<DlnaController*>(arg);
  DlnaEventListener listener(self->event_queue_);
  listener.start();

  uint32_t last_ping_time   = 0;
  uint32_t last_sub_renewal = 0;
  uint32_t last_pos_sync    = 0;

  while (true) {
    DlnaCommand cmd{};
    if (xQueueReceive(self->command_queue_, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
      if (cmd.type == DlnaCmdType::START_SEARCH) {
        self->state_ = DlnaState::SEARCHING;
        {
          std::lock_guard<std::mutex> lock(self->device_mutex_);
          self->devices_.clear();
        }
        self->send_ssdp_msearch();
      } else if (cmd.type == DlnaCmdType::SELECT_TARGET) {
        std::string                 udn(cmd.target_udn);
        std::lock_guard<std::mutex> lock(self->device_mutex_);
        for (const auto& dev : self->devices_) {
          if (dev.udn == udn) {
            self->active_target_    = dev;
            self->state_            = DlnaState::CONNECTING;
            self->ping_retry_count_ = 0;
            ESP_LOGI(TAG, "Selected target DMR: %s (IP: %s)", self->active_target_.friendly_name.c_str(), self->active_target_.ip_address.c_str());
            break;
          }
        }
      } else if (self->state_ == DlnaState::SUBSCRIBED) {
        switch (cmd.type) {
          case DlnaCmdType::PLAY:
            self->send_soap_request(self->active_target_.control_url, "urn:schemas-upnp-org:service:AVTransport:1", "Play", "<InstanceID>0</InstanceID><Speed>1</Speed>");
            break;
          case DlnaCmdType::PAUSE:
            self->send_soap_request(self->active_target_.control_url, "urn:schemas-upnp-org:service:AVTransport:1", "Pause", "<InstanceID>0</InstanceID>");
            break;
          case DlnaCmdType::NEXT: {
            if (self->active_target_.has_oh_playlist && !self->active_target_.oh_playlist_url.empty()) {
              ESP_LOGI(TAG, "DMR supports OpenHome Playlist. Sending OpenHome Next...");
              self->send_soap_request(self->active_target_.oh_playlist_url, "urn:av-openhome-org:service:Playlist:1", "Next", "");
            } else {
              std::string media_info_xml;
              bool        has_next_uri = false;
              if (self->send_soap_request(self->active_target_.control_url, "urn:schemas-upnp-org:service:AVTransport:1", "GetMediaInfo", "<InstanceID>0</InstanceID>",
                                          &media_info_xml)) {
                std::string next_uri;
                if (DlnaParser::parse_next_uri(media_info_xml, next_uri) && !next_uri.empty()) {
                  has_next_uri = true;
                }
              }

              if (has_next_uri) {
                ESP_LOGI(TAG, "DMR has next track URI pre-registered. Sending standard Next...");
                if (self->send_soap_request(self->active_target_.control_url, "urn:schemas-upnp-org:service:AVTransport:1", "Next", "<InstanceID>0</InstanceID>")) {
                  self->send_soap_request(self->active_target_.control_url, "urn:schemas-upnp-org:service:AVTransport:1", "Play", "<InstanceID>0</InstanceID><Speed>1</Speed>");
                }
              } else {
                ESP_LOGI(TAG, "No next track pre-registered. Triggering Seek-End Fallback...");
                uint32_t    rel_ms = 0, dur_ms = 0;
                std::string pos_xml;
                if (self->send_soap_request(self->active_target_.control_url, "urn:schemas-upnp-org:service:AVTransport:1", "GetPositionInfo", "<InstanceID>0</InstanceID>",
                                            &pos_xml)) {
                  DlnaParser::parse_position_info(pos_xml, rel_ms, dur_ms);
                }

                if (dur_ms > 2000) {
                  uint32_t seek_target_ms = dur_ms - 1000;  // 종료 1초 전으로 점프
                  uint32_t target_sec     = seek_target_ms / 1000;
                  uint32_t hh             = target_sec / 3600;
                  uint32_t mm             = (target_sec % 3600) / 60;
                  uint32_t ss             = target_sec % 60;
                  char     seek_time[32];
                  snprintf(seek_time, sizeof(seek_time), "%02lu:%02lu:%02lu", (unsigned long)hh, (unsigned long)mm, (unsigned long)ss);

                  std::string seek_body = "<InstanceID>0</InstanceID><Unit>REL_TIME</Unit><Target>" + std::string(seek_time) + "</Target>";
                  self->send_soap_request(self->active_target_.control_url, "urn:schemas-upnp-org:service:AVTransport:1", "Seek", seek_body);
                } else {
                  // 재생 길이 정보가 없으면 일반 Next 전송 시도
                  self->send_soap_request(self->active_target_.control_url, "urn:schemas-upnp-org:service:AVTransport:1", "Next", "<InstanceID>0</InstanceID>");
                }
              }
            }
            break;
          }
          case DlnaCmdType::PREVIOUS: {
            if (self->active_target_.has_oh_playlist && !self->active_target_.oh_playlist_url.empty()) {
              ESP_LOGI(TAG, "DMR supports OpenHome Playlist. Sending OpenHome Previous...");
              self->send_soap_request(self->active_target_.oh_playlist_url, "urn:av-openhome-org:service:Playlist:1", "Previous", "");
            } else {
              uint32_t    rel_ms = 0, dur_ms = 0;
              std::string pos_xml;
              if (self->send_soap_request(self->active_target_.control_url, "urn:schemas-upnp-org:service:AVTransport:1", "GetPositionInfo", "<InstanceID>0</InstanceID>",
                                          &pos_xml)) {
                DlnaParser::parse_position_info(pos_xml, rel_ms, dur_ms);
              }

              if (rel_ms > 3000) {
                // 3초 초과 재생 시에는 현재 곡을 처음 시점(0초)으로 Seek 리셋
                ESP_LOGI(TAG, "Track played > 3s. Seeking to track start (0s)...");
                std::string seek_body = "<InstanceID>0</InstanceID><Unit>REL_TIME</Unit><Target>00:00:00</Target>";
                self->send_soap_request(self->active_target_.control_url, "urn:schemas-upnp-org:service:AVTransport:1", "Seek", seek_body);
              } else {
                // 3초 미만일 때는 이전 곡 명령 전송
                ESP_LOGI(TAG, "Track played < 3s. Sending standard Previous...");
                if (self->send_soap_request(self->active_target_.control_url, "urn:schemas-upnp-org:service:AVTransport:1", "Previous", "<InstanceID>0</InstanceID>")) {
                  self->send_soap_request(self->active_target_.control_url, "urn:schemas-upnp-org:service:AVTransport:1", "Play", "<InstanceID>0</InstanceID><Speed>1</Speed>");
                }
              }
            }
            break;
          }
          case DlnaCmdType::SET_VOLUME: {
            std::string vol_body = "<InstanceID>0</InstanceID><Channel>Master</Channel><DesiredVolume>" + std::to_string(cmd.value) + "</DesiredVolume>";
            self->send_soap_request(self->active_target_.rendering_ctrl_url, "urn:schemas-upnp-org:service:RenderingControl:1", "SetVolume", vol_body);
            break;
          }
          case DlnaCmdType::SHUFFLE_ON:
            ESP_LOGI(TAG, "Requesting SetPlayMode: SHUFFLE");
            self->send_soap_request(self->active_target_.control_url, "urn:schemas-upnp-org:service:AVTransport:1", "SetPlayMode",
                                    "<InstanceID>0</InstanceID><NewPlayMode>SHUFFLE</NewPlayMode>");
            break;
          case DlnaCmdType::SHUFFLE_OFF:
            ESP_LOGI(TAG, "Requesting SetPlayMode: NORMAL");
            self->send_soap_request(self->active_target_.control_url, "urn:schemas-upnp-org:service:AVTransport:1", "SetPlayMode",
                                    "<InstanceID>0</InstanceID><NewPlayMode>NORMAL</NewPlayMode>");
            break;
          case DlnaCmdType::REPEAT_ON:
            ESP_LOGI(TAG, "Requesting SetPlayMode: REPEAT_ALL");
            self->send_soap_request(self->active_target_.control_url, "urn:schemas-upnp-org:service:AVTransport:1", "SetPlayMode",
                                    "<InstanceID>0</InstanceID><NewPlayMode>REPEAT_ALL</NewPlayMode>");
            break;
          case DlnaCmdType::REPEAT_OFF:
            ESP_LOGI(TAG, "Requesting SetPlayMode: NORMAL");
            self->send_soap_request(self->active_target_.control_url, "urn:schemas-upnp-org:service:AVTransport:1", "SetPlayMode",
                                    "<InstanceID>0</InstanceID><NewPlayMode>NORMAL</NewPlayMode>");
            break;
          default:
            break;
        }
      }
    }

    if (self->state_ == DlnaState::CONNECTING) {
      if (self->subscribe_events()) {
        self->state_     = DlnaState::SUBSCRIBED;
        last_sub_renewal = xTaskGetTickCount();
        last_ping_time   = xTaskGetTickCount();
        last_pos_sync    = xTaskGetTickCount();
        self->current_title_.clear();
        ESP_LOGI(TAG, "Speaker connected and GENA session established. Triggering initial SOAP query...");

        // 1. GetPositionInfo 초기 조회 (곡 메타데이터 및 시간 동기화)
        std::string pos_xml;
        std::string title, artist, art_url;
        bool        meta_ok = false;

        if (self->send_soap_request(self->active_target_.control_url, "urn:schemas-upnp-org:service:AVTransport:1", std::string(Config::kSoapActionGetPositionInfo),
                                    "<InstanceID>0</InstanceID>", &pos_xml)) {
          uint32_t rel_ms = 0, dur_ms = 0;
          if (DlnaParser::parse_position_info(pos_xml, rel_ms, dur_ms)) {
            auto* display = static_cast<Display::Context*>(self->display_ctx_);
            if (display) display->update_media_progress(rel_ms, dur_ms);
          }
          meta_ok = DlnaParser::parse_metadata(pos_xml, title, artist, art_url);
        }

        // [해결책 A Fallback]: GetPositionInfo에서 메타데이터를 못 구한 경우 GetMediaInfo 조회
        if (!meta_ok) {
          std::string media_xml;
          ESP_LOGI(TAG, "Metadata empty in initial GetPositionInfo. Trying GetMediaInfo fallback...");
          if (self->send_soap_request(self->active_target_.control_url, "urn:schemas-upnp-org:service:AVTransport:1", "GetMediaInfo", "<InstanceID>0</InstanceID>", &media_xml)) {
            meta_ok = DlnaParser::parse_metadata(media_xml, title, artist, art_url);
          }
        }

        if (meta_ok) {
          self->current_title_ = title;
          auto* job            = new ArtDecodeJob{.title = title, .artist = artist, .art_url = art_url, .display_ctx = self->display_ctx_};
          xTaskCreate(art_decode_task, "art_async_loader", 6144, job, 3, nullptr);
        }

        // 2. GetTransportInfo 초기 조회 (재생 상태 동기화)
        std::string trans_xml;
        if (self->send_soap_request(self->active_target_.control_url, "urn:schemas-upnp-org:service:AVTransport:1", "GetTransportInfo", "<InstanceID>0</InstanceID>", &trans_xml)) {
          std::string play_state;
          if (DlnaParser::parse_state(trans_xml, play_state)) {
            auto* display = static_cast<Display::Context*>(self->display_ctx_);
            if (display) display->update_media_play_state(play_state);
          }
        }

        // 3. GetVolume 초기 조회 (볼륨 동기화)
        std::string vol_xml;
        if (self->send_soap_request(self->active_target_.rendering_ctrl_url, "urn:schemas-upnp-org:service:RenderingControl:1", "GetVolume",
                                    "<InstanceID>0</InstanceID><Channel>Master</Channel>", &vol_xml)) {
          uint8_t volume = 0;
          if (DlnaParser::parse_volume(vol_xml, volume)) {
            auto* display = static_cast<Display::Context*>(self->display_ctx_);
            if (display) display->update_media_volume(volume);
          }
        }
      } else {
        self->state_ = DlnaState::ERROR_DISCONNECTED;
        ESP_LOGE(TAG, "Failed to connect to speaker events sub.");
      }
    }

    if (self->state_ == DlnaState::SUBSCRIBED) {
      uint32_t now = xTaskGetTickCount();

      if (pdTICKS_TO_MS(now - last_ping_time) >= Config::kSpeakerPingIntervalMs) {
        last_ping_time = now;
        if (!self->check_renderer_alive()) {
          self->state_ = DlnaState::ERROR_DISCONNECTED;
          self->unsubscribe_events();

          auto* display = static_cast<Display::Context*>(self->display_ctx_);
          if (display) {
            // 오프라인 상태 UI 연동 갱신
            display->update_media_play_state("STOPPED");
            display->update_media_track("Speaker Disconnected", "AP Isolation check or Power check required");
          }
          ESP_LOGE(TAG, "Speaker Ping failed - state transitioned to ERROR_DISCONNECTED");
        }
      }

      // 5초 주기 재생 상태/시간/볼륨 동기화(폴링) 처리
      if (pdTICKS_TO_MS(now - last_pos_sync) >= Config::kProgressSyncIntervalMs) {
        last_pos_sync = now;

        // 1. GetPositionInfo 조회 (시간 및 메타데이터 동기화)
        std::string pos_xml;
        std::string title, artist, art_url;
        bool        meta_ok = false;

        if (self->send_soap_request(self->active_target_.control_url, "urn:schemas-upnp-org:service:AVTransport:1", std::string(Config::kSoapActionGetPositionInfo),
                                    "<InstanceID>0</InstanceID>", &pos_xml)) {
          uint32_t rel_ms = 0, dur_ms = 0;
          if (DlnaParser::parse_position_info(pos_xml, rel_ms, dur_ms)) {
            auto* display = static_cast<Display::Context*>(self->display_ctx_);
            if (display) {
              display->update_media_progress(rel_ms, dur_ms);
            }
          }
          meta_ok = DlnaParser::parse_metadata(pos_xml, title, artist, art_url);
        }

        // [해결책 A Fallback]: GetPositionInfo에서 메타데이터를 못 구한 경우 GetMediaInfo 조회
        if (!meta_ok) {
          std::string media_xml;
          ESP_LOGI(TAG, "Metadata empty in GetPositionInfo. Trying GetMediaInfo fallback...");
          if (self->send_soap_request(self->active_target_.control_url, "urn:schemas-upnp-org:service:AVTransport:1", "GetMediaInfo", "<InstanceID>0</InstanceID>", &media_xml)) {
            meta_ok = DlnaParser::parse_metadata(media_xml, title, artist, art_url);
          }
        }

        if (meta_ok) {
          if (title != self->current_title_) {
            self->current_title_ = title;
            auto* job            = new ArtDecodeJob{.title = title, .artist = artist, .art_url = art_url, .display_ctx = self->display_ctx_};
            xTaskCreate(art_decode_task, "art_async_loader", 6144, job, 3, nullptr);
          }
        }

        // 2. GetTransportInfo 조회 (재생 상태 동기화)
        std::string trans_xml;
        if (self->send_soap_request(self->active_target_.control_url, "urn:schemas-upnp-org:service:AVTransport:1", "GetTransportInfo", "<InstanceID>0</InstanceID>", &trans_xml)) {
          std::string play_state;
          if (DlnaParser::parse_state(trans_xml, play_state)) {
            auto* display = static_cast<Display::Context*>(self->display_ctx_);
            if (display) {
              display->update_media_play_state(play_state);
            }
          }
        }

        // 3. GetVolume 조회 (볼륨 정보 동기화)
        std::string vol_xml;
        if (self->send_soap_request(self->active_target_.rendering_ctrl_url, "urn:schemas-upnp-org:service:RenderingControl:1", "GetVolume",
                                    "<InstanceID>0</InstanceID><Channel>Master</Channel>", &vol_xml)) {
          uint8_t volume = 0;
          if (DlnaParser::parse_volume(vol_xml, volume)) {
            auto* display = static_cast<Display::Context*>(self->display_ctx_);
            if (display) {
              display->update_media_volume(volume);
            }
          }
        }

        // 4. OpenHome Playlist 조회 (오픈홈 서비스 지원 스피커에 연결된 경우)
        if (self->active_target_.has_oh_playlist) {
          self->update_openhome_playlist();
        }
      }

      uint32_t margin_ticks = pdMS_TO_TICKS(self->subscription_timeout_ * 1000 * Config::kGenaRenewalMarginRatio);
      if (now - last_sub_renewal >= margin_ticks) {
        last_sub_renewal = now;
        if (!self->renew_subscription()) {
          ESP_LOGW(TAG, "GENA renewal failed. Trying to re-subscribe...");
          self->subscribe_events();
        }
      }
    }

    if (self->state_ == DlnaState::ERROR_DISCONNECTED) {
      uint32_t now = xTaskGetTickCount();
      if (pdTICKS_TO_MS(now - last_ping_time) >= Config::kSpeakerPingIntervalMs) {
        last_ping_time = now;
        self->ping_retry_count_++;
        ESP_LOGI(TAG, "Speaker offline. Retrying TCP ping... (%d/10)", self->ping_retry_count_);
        if (self->check_renderer_alive()) {
          ESP_LOGI(TAG, "Speaker back online! Re-initiating connection...");
          self->state_            = DlnaState::CONNECTING;
          self->ping_retry_count_ = 0;
        } else if (self->ping_retry_count_ >= 10) {
          ESP_LOGW(TAG, "Ping retry limit reached. Returning to IDLE.");
          self->state_            = DlnaState::IDLE;
          self->ping_retry_count_ = 0;

          auto* display = static_cast<Display::Context*>(self->display_ctx_);
          if (display) {
            display->update_media_track("Disconnected", "Select a speaker again");
            display->update_media_play_state("STOPPED");
          }
        }
      }
    }

    // ── 5. 비동기 GENA Event 수신 큐 소비 ─────────────────────────
    std::string* p_xml = nullptr;
    if (xQueueReceive(self->event_queue_, &p_xml, 0) == pdTRUE && p_xml != nullptr) {
      std::string raw_xml = *p_xml;
      delete p_xml;

      std::string title, artist, art_url;
      uint8_t     volume = 0;
      std::string play_state;

      // 5.1. 볼륨 이벤트 파싱 및 동기화
      if (DlnaParser::parse_volume(raw_xml, volume)) {
        auto* display = static_cast<Display::Context*>(self->display_ctx_);
        if (display) {
          display->update_media_volume(volume);
        }
      }

      // 5.2. 재생 상태 이벤트 파싱 및 동기화
      if (DlnaParser::parse_state(raw_xml, play_state)) {
        auto* display = static_cast<Display::Context*>(self->display_ctx_);
        if (display) {
          display->update_media_play_state(play_state);
        }
      }

      // 5.3. 곡 메타데이터 이벤트 파싱 및 일괄 드로우 커밋 연동
      if (DlnaParser::parse_metadata(raw_xml, title, artist, art_url)) {
        if (title != self->current_title_) {
          self->current_title_ = title;
          ESP_LOGI(TAG, "GENA Meta received: Title: %s, Artist: %s, Cover: %s", title.c_str(), artist.c_str(), art_url.c_str());

          // [5.3.② 및 7.3 사양: 엇갈림 없는 2.5초 데드라인 비동기 디코딩 스레드 발동]
          auto* job = new ArtDecodeJob{.title = title, .artist = artist, .art_url = art_url, .display_ctx = self->display_ctx_};

          // 단발성 비동기 드로우 커밋 태스크 구동 (우선순위 3, 2.5초 다운로드 수행 대행)
          xTaskCreate(art_decode_task, "art_async_loader", 6144, job, 3, nullptr);
        }
      }
    }
  }
}

std::vector<DlnaTrack> DlnaController::get_playlist() {
  std::lock_guard<std::mutex> lock(playlist_mutex_);
  return playlist_;
}

bool DlnaController::update_openhome_playlist() {
  if (!active_target_.has_oh_playlist || active_target_.oh_playlist_url.empty()) {
    return false;
  }

  // 1. IdArray 요청 송수신 (대기열 ID 목록 및 Token 수집)
  std::string id_arr_xml;
  if (!send_soap_request(active_target_.oh_playlist_url, "urn:av-openhome-org:service:Playlist:1", "IdArray", "", &id_arr_xml)) {
    ESP_LOGE(TAG, "Failed to send IdArray soap request");
    return false;
  }

  uint32_t    token = 0;
  std::string id_list;
  if (!DlnaParser::parse_id_array(id_arr_xml, token, id_list)) {
    ESP_LOGE(TAG, "Failed to parse IdArray response");
    return false;
  }

  // 2. 캐싱 토큰 검사: 변동사항이 없으면 파싱 생략
  if (token == last_playlist_token_) {
    return true;
  }

  if (id_list.empty()) {
    std::lock_guard<std::mutex> lock(playlist_mutex_);
    playlist_.clear();
    last_playlist_token_ = token;
    ESP_LOGI(TAG, "OpenHome Playlist is empty.");
    return true;
  }

  // 3. ReadList 요청 송수신 (ID 리스트에 대한 실제 곡 메타데이터 수집)
  std::string read_list_xml;
  std::string body = "<IdList>" + id_list + "</IdList>";
  if (!send_soap_request(active_target_.oh_playlist_url, "urn:av-openhome-org:service:Playlist:1", "ReadList", body, &read_list_xml)) {
    ESP_LOGE(TAG, "Failed to send ReadList soap request");
    return false;
  }

  // 4. 메타데이터 파싱 및 적재
  std::vector<DlnaTrack> new_tracks;
  if (!DlnaParser::parse_playlist_tracks(read_list_xml, new_tracks)) {
    ESP_LOGE(TAG, "Failed to parse playlist tracks XML");
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(playlist_mutex_);
    playlist_            = std::move(new_tracks);
    last_playlist_token_ = token;
  }

  ESP_LOGI(TAG, "OpenHome Playlist synchronized: %d tracks found.", playlist_.size());
  for (const auto& track : playlist_) {
    ESP_LOGI(TAG, "  - Track [%lu] : %s - %s", (unsigned long)track.id, track.title.c_str(), track.artist.c_str());
  }

  return true;
}

}  // namespace Ble::Dlna
