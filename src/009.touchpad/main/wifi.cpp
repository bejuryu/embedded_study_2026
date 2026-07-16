#include "wifi.hpp"

#include <cstring>
#include <vector>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_defaults.h"
#include "esp_wifi.h"
// #include "predefine.h"
// #include "utility.hpp"

static auto*          TAG                = "WIFI";
static constexpr auto WIFI_CONNECTED_BIT = BIT0;
static constexpr auto WIFI_FAIL_BIT      = BIT1;

bool WiFi::initialize_station() {
  s_wifi_event_group = xEventGroupCreate();

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  const auto station_network_interface = esp_netif_create_default_wifi_sta();
  assert(station_network_interface != nullptr);
  /*
    std::string hostname = get_sanitized_hostname(HOST_NAME);
    esp_err_t hostname_err = esp_netif_set_hostname(station_network_interface, hostname.c_str());
    if (hostname_err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to set DHCP Hostname: %s", esp_err_to_name(hostname_err));
    } else {
      ESP_LOGI(TAG, "DHCP Hostname set to: %s", hostname.c_str());
    }
  */
  const wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));

  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler_wifi, this));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler_wifi, this));

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());
  return true;
}

void WiFi::finalize_station() { esp_wifi_stop(); }

void WiFi::connect(const std::string& ssid, const std::string& password) {
  is_connecting_ = false;

  wifi_config_t wifi_config = {};

  if (ssid.empty()) {
    ESP_LOGI(TAG, "SSID is empty in config. Trying to connect using previously saved credentials in NVS...");

    if (esp_wifi_get_config(WIFI_IF_STA, &wifi_config) != ESP_OK) {
      ESP_LOGE(TAG, "Failed to get saved credentials from NVS");
      return;
    }

    const esp_err_t ret = esp_wifi_connect();
    if (ret == ESP_OK) {
      is_connecting_  = true;
      connected_ssid_ = reinterpret_cast<const char*>(wifi_config.sta.ssid);
    } else {
      ESP_LOGE(TAG, "Failed to start connection with saved credentials: %s", esp_err_to_name(ret));
    }
  } else {
    std::memset(wifi_config.sta.ssid, 0, sizeof(wifi_config.sta.ssid));
    ssid.copy(reinterpret_cast<char*>(wifi_config.sta.ssid), sizeof(wifi_config.sta.ssid) - 1);

    std::memset(wifi_config.sta.password, 0, sizeof(wifi_config.sta.password));
    password.copy(reinterpret_cast<char*>(wifi_config.sta.password), sizeof(wifi_config.sta.password) - 1);

    if (password.empty()) {
      wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    } else {
      wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA_PSK;
    }
    wifi_config.sta.pmf_cfg.capable  = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_LOGI(TAG, "New credentials provided. Saving and connecting to AP SSID: [%s]...", wifi_config.sta.ssid);

    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret == ESP_OK) {
      ret = esp_wifi_connect();
      if (ret == ESP_OK) {
        is_connecting_  = true;
        connected_ssid_ = ssid;
      }
    }
  }
}

void WiFi::disconnect() {}

bool WiFi::is_connected() const {
  if (s_wifi_event_group == nullptr) return false;
  return (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT) != 0;
}

bool WiFi::is_connecting() const { return is_connecting_; }

std::string WiFi::get_ip_v4_address() const {
  if (!is_connected()) return {};
  constexpr esp_netif_inherent_config_t sta_config = ESP_NETIF_INHERENT_DEFAULT_WIFI_STA();

  auto* net_interface = esp_netif_get_handle_from_ifkey(sta_config.if_key);
  if (net_interface == nullptr) return {};
  esp_netif_ip_info_t ip4_addr;
  if (esp_netif_get_ip_info(net_interface, &ip4_addr) != ESP_OK) return {};
  char ip_address_str[16]{};
  esp_ip4addr_ntoa(&ip4_addr.ip, ip_address_str, sizeof(ip_address_str));
  return {ip_address_str};
}

std::string WiFi::get_connected_ssid() const { return connected_ssid_; }

std::vector<wifi_ap_record_t> WiFi::scan() {
  wifi_scan_config_t wifi_scan_config = {};
  wifi_scan_config.show_hidden        = true;

  auto err = esp_wifi_scan_start(&wifi_scan_config, true);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start scan for WiFi network scan: %s", esp_err_to_name(err));
    return {};
  }
  uint16_t ap_count = 0;
  err               = esp_wifi_scan_get_ap_num(&ap_count);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "%s", esp_err_to_name(err));
    return {};
  }
  if (ap_count == 0) {
    ESP_LOGI(TAG, "can not search wifi ap");
    return {};
  }

  std::vector<wifi_ap_record_t> scan_results(ap_count);

  ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, scan_results.data()));

  for (const auto& ap_record : scan_results) {
    ESP_LOGI(TAG, "SSID: %-16s | RSSI: %d dBm | Channel: %d", ap_record.ssid, ap_record.rssi, ap_record.primary);
  }
  return scan_results;
}

bool WiFi::exist_ssid(const std::string& ssid, const std::vector<wifi_ap_record_t>& scan_results) {
  for (const auto& ap_record : scan_results) {
    if (ssid == reinterpret_cast<const char*>(ap_record.ssid)) return true;
  }
  return false;
}

void WiFi::event_handler_wifi(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
  ESP_LOGI(TAG, "Event type %s is %d", event_base, event_id);
  if (event_base == WIFI_EVENT) {
    switch (event_id) {
      case WIFI_EVENT_STA_START: {
        ESP_LOGI(TAG, "WIFI START started");
        break;
      }
      case WIFI_EVENT_STA_STOP: {
        ESP_LOGI(TAG, "WIFI STOP started");
        break;
      }
      case WIFI_EVENT_STA_DISCONNECTED: {
        ESP_LOGI(TAG, "WIFI Station Disconnected");

        const wifi_event_sta_disconnected_t* disconnected = static_cast<wifi_event_sta_disconnected_t*>(event_data);
        const uint8_t                        reason       = disconnected->reason;
        switch (reason) {
          case WIFI_REASON_AUTH_FAIL:
          case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
          case WIFI_REASON_HANDSHAKE_TIMEOUT: {
            ESP_LOGE(TAG, "WIFI Connection invalid password");
            break;
          }
          default: {
            ESP_LOGE(TAG, "WIFI Connection failed: %d", reason);
            break;
          }
        }
        if (arg != nullptr) {
          auto* wifi_args           = static_cast<WiFi*>(arg);
          wifi_args->is_connecting_ = false;
          wifi_args->connected_ssid_.clear();
          xEventGroupClearBits(wifi_args->s_wifi_event_group, WIFI_CONNECTED_BIT);
          xEventGroupSetBits(wifi_args->s_wifi_event_group, WIFI_FAIL_BIT);
        } else {
          ESP_LOGE(TAG, "wifi args is null");
        }
        break;
      }
    }
  } else if (event_base == IP_EVENT) {
    switch (event_id) {
      case IP_EVENT_STA_GOT_IP: {
        const auto* got_ip = static_cast<ip_event_got_ip_t*>(event_data);
        ESP_LOGI(TAG, "WIFI GOT IP: " IPSTR, IP2STR(&got_ip->ip_info.ip));

        if (arg != nullptr) {
          auto* wifi_args = static_cast<WiFi*>(arg);

          wifi_args->is_connecting_ = false;
          xEventGroupClearBits(wifi_args->s_wifi_event_group, WIFI_FAIL_BIT);
          xEventGroupSetBits(wifi_args->s_wifi_event_group, WIFI_CONNECTED_BIT);
        } else {
          ESP_LOGE(TAG, "wifi args is null");
        }
        break;
      }
      case IP_EVENT_STA_LOST_IP: {
        ESP_LOGI(TAG, "WIFI LOST IP");

        if (arg != nullptr) {
          auto* wifi_args = static_cast<WiFi*>(arg);

          wifi_args->is_connecting_ = false;
          wifi_args->connected_ssid_.clear();
          xEventGroupClearBits(wifi_args->s_wifi_event_group, WIFI_CONNECTED_BIT);
          xEventGroupSetBits(wifi_args->s_wifi_event_group, WIFI_FAIL_BIT);
        } else {
          ESP_LOGE(TAG, "wifi args is null");
        }

        break;
      }
      default: {
        ESP_LOGI(TAG, "WIFI IP Event: %d", event_id);
        break;
      }
    }
  }
}

bool WiFi::connect_from_json_list(const std::string& json_path) {
  ESP_LOGI(TAG, "Opening Wi-Fi configuration file: %s", json_path.c_str());
  FILE* f = fopen(json_path.c_str(), "r");
  if (f == nullptr) {
    ESP_LOGW(TAG, "Failed to open %s. Fallback to NVS connection.", json_path.c_str());
    connect("", "");
    return false;
  }

  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);

  char* json_buf = static_cast<char*>(malloc(fsize + 1));
  if (json_buf == nullptr) {
    fclose(f);
    ESP_LOGE(TAG, "Failed to allocate memory for JSON read.");
    connect("", "");
    return false;
  }

  size_t read_size    = fread(json_buf, 1, fsize, f);
  json_buf[read_size] = '\0';
  fclose(f);

  cJSON* root = cJSON_Parse(json_buf);
  free(json_buf);

  if (root == nullptr) {
    ESP_LOGE(TAG, "JSON parse error. Fallback to NVS.");
    connect("", "");
    return false;
  }

  cJSON* wifi_array = cJSON_GetObjectItem(root, "wifi");
  if (wifi_array == nullptr || !cJSON_IsArray(wifi_array)) {
    ESP_LOGE(TAG, "wifi key missing or not array. Fallback to NVS.");
    cJSON_Delete(root);
    connect("", "");
    return false;
  }

  std::vector<ApCredential> ap_creds;
  int                       array_size = cJSON_GetArraySize(wifi_array);
  for (int i = 0; i < array_size; ++i) {
    cJSON* item     = cJSON_GetArrayItem(wifi_array, i);
    cJSON* ssid_obj = cJSON_GetObjectItem(item, "ssid");
    cJSON* pwd_obj  = cJSON_GetObjectItem(item, "password");
    if (ssid_obj && cJSON_IsString(ssid_obj)) {
      ApCredential cred{.ssid = ssid_obj->valuestring, .password = (pwd_obj && cJSON_IsString(pwd_obj)) ? pwd_obj->valuestring : ""};
      ap_creds.push_back(cred);
    }
  }
  cJSON_Delete(root);

  if (ap_creds.empty()) {
    ESP_LOGW(TAG, "Parsed AP list is empty. Fallback to NVS.");
    connect("", "");
    return false;
  }

  ESP_LOGI(TAG, "Loaded %d AP credentials from JSON. Initiating channel scan...", ap_creds.size());

  auto scan_records = scan();
  if (scan_records.empty()) {
    ESP_LOGW(TAG, "No Wi-Fi networks found via scan. Connecting with NVS.");
    connect("", "");
    return false;
  }

  ApCredential matched_ap{};
  bool         found = false;

  for (const auto& cred : ap_creds) {
    if (exist_ssid(cred.ssid, scan_records)) {
      matched_ap = cred;
      found      = true;
      break;
    }
  }

  if (found) {
    ESP_LOGI(TAG, "Target AP discovered: [%s]. Establishing connection...", matched_ap.ssid.c_str());
    connect(matched_ap.ssid, matched_ap.password);
    return true;
  }

  ESP_LOGW(TAG, "None of the configured APs are visible. Fallback to NVS.");
  connect("", "");
  return false;
}
