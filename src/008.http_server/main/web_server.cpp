#include "web_server.hpp"

#include <array>
#include <filesystem>
#include <vector>

#include "cJSON.h"
#include "embedded_data.h"
#include "esp_https_server.h"
#include "esp_log.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include "sdkconfig.h"
#include "utility.hpp"

namespace {

constexpr const char* TAG = "WebServer";

// ─── HTTP Content Types ───
constexpr const char* kContentTypeHtmlUtf8 = "text/html; charset=utf-8";
constexpr const char* kContentTypeJsonUtf8 = "application/json; charset=utf-8";
constexpr const char* kContentTypeJson = "application/json";
constexpr const char* kContentTypeOctetStream = "application/octet-stream";

// ─── HTTP Header Keys ───
constexpr const char* kHeaderContentLength = "Content-Length";
constexpr const char* kHeaderContentDisposition = "Content-Disposition";
constexpr const char* kHeaderContentType = "content-type";

// ─── HTTP Raw Headers ───
constexpr const char* kHttpRawStatusOk = "HTTP/1.1 200 OK\r\n";
constexpr const char* kHttpRawContentTypeOctetStream = "Content-Type: application/octet-stream\r\n";
constexpr const char* kHttpRawContentDispositionPrefix = "Content-Disposition: attachment; filename=\"";
constexpr const char* kHttpRawContentLengthPrefix = "Content-Length: ";
constexpr const char* kHttpRawConnectionClose = "Connection: close\r\n";
constexpr const char* kHttpRawCRLF = "\r\n";
constexpr const char* kHttpRawQuoteCRLF = "\"\r\n";

// ─── Query / JSON Keys ───
constexpr const char* kKeyPath = "path";
constexpr const char* kKeyFiles = "files";
constexpr const char* kKeyName = "name";
constexpr const char* kKeyIsDir = "is_dir";
constexpr const char* kKeySize = "size";
constexpr const char* kKeyRootDir = "/";
constexpr const char* kKeySDCardRoot = CONFIG_BSP_SD_MOUNT_POINT;

// ─── Network I/O Block Sizes ───
// 수신(업로드)용 블록 크기: TCP 수신 윈도우 크기에 맞춤
#ifdef CONFIG_LWIP_TCP_WND_DEFAULT
constexpr size_t kRecvBlockSize = CONFIG_LWIP_TCP_WND_DEFAULT;
#else
constexpr size_t kRecvBlockSize = 4096;
#endif

// 송신(다운로드)용 블록 크기: TCP 송신 버퍼 크기에 맞춤 (최소 16KB 보장)
#ifdef CONFIG_LWIP_TCP_SND_BUF_DEFAULT
constexpr size_t kSendBlockSize = CONFIG_LWIP_TCP_SND_BUF_DEFAULT >= 16384 ? CONFIG_LWIP_TCP_SND_BUF_DEFAULT : 16384;
#else
constexpr size_t kSendBlockSize = 16384;
#endif

// ─── File I/O Buffer ───
// SD 카드 읽기/쓰기 시 stdio 내부 버퍼 크기. TCP 블록과 정합성 유지.
constexpr size_t kFileIOBufferSize = kSendBlockSize;

// ─── Ring Buffer ───
// TCP 버스트를 흡수하기 위해 I/O 블록의 4배 크기로 설정
constexpr size_t kRingBufferMultiplier = 4;
constexpr size_t kUploadRingBufferSize = kRecvBlockSize * kRingBufferMultiplier;
constexpr size_t kDownloadRingBufferSize = kSendBlockSize * kRingBufferMultiplier;

// ─── Background Task ───
constexpr size_t kSDTaskStackSize = 4096;
constexpr UBaseType_t kSDTaskPriority = 5;
constexpr BaseType_t kSDTaskCore = 1;

// ─── Timeout ───
constexpr TickType_t kRingbufferRecvTimeout = pdMS_TO_TICKS(500);
constexpr TickType_t kRingbufferSendTimeout = pdMS_TO_TICKS(3000);

// ─── Speedtest ───
constexpr size_t kSpeedtestDefaultSize = 10 * 1024 * 1024;  // 10 MB

// ─── HTTP Header Buffer ───
constexpr size_t kMaxContentTypeLength = 128;

// ─── Query String ───
constexpr size_t kMaxQueryLength = 512;

struct AsyncIOContext {
  RingbufHandle_t ringbuf = nullptr;
  FILE* fd = nullptr;
  volatile bool is_done = false;
  volatile bool has_error = false;
  SemaphoreHandle_t done_sem = nullptr;
};

static void enable_tcp_nodelay(httpd_req_t* req) {
  int sockfd = httpd_req_to_sockfd(req);
  if (sockfd >= 0) {
    int enable = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable));
  }
}

/// 업로드용: 링버퍼에서 꺼내서 파일에 쓰는 백그라운드 태스크
static void sd_write_task(void* pvParameters) {
  auto* ctx = static_cast<AsyncIOContext*>(pvParameters);
  while (!ctx->has_error) {
    size_t item_size = 0;
    char* item = static_cast<char*>(xRingbufferReceiveUpTo(ctx->ringbuf, &item_size, kRingbufferRecvTimeout, kUploadRingBufferSize));
    if (item != nullptr) {
      size_t written = fwrite(item, 1, item_size, ctx->fd);
      vRingbufferReturnItem(ctx->ringbuf, item);
      if (written != item_size) {
        ESP_LOGE(TAG, "Async SD write failed");
        ctx->has_error = true;
        break;
      }
    } else if (ctx->is_done) {
      break;
    }
  }
  xSemaphoreGive(ctx->done_sem);
  vTaskDelete(nullptr);
}

/// 다운로드용: 파일에서 읽어서 링버퍼에 넣는 백그라운드 태스크
static void sd_read_task(void* pvParameters) {
  auto* ctx = static_cast<AsyncIOContext*>(pvParameters);
  std::vector<char> buf(kSendBlockSize);
  while (!ctx->has_error) {
    size_t read_bytes = fread(buf.data(), 1, kSendBlockSize, ctx->fd);
    if (read_bytes == 0) break;
    BaseType_t sent = xRingbufferSend(ctx->ringbuf, buf.data(), read_bytes, kRingbufferSendTimeout);
    if (sent != pdTRUE) {
      ESP_LOGE(TAG, "Ringbuffer send timeout during download");
      ctx->has_error = true;
      break;
    }
  }
  ctx->is_done = true;
  xSemaphoreGive(ctx->done_sem);
  vTaskDelete(nullptr);
}

/// 쿼리 스트링을 파싱하는 내부 구현. require_all이 true이면 키가 없을 때 에러 응답을 보낸다.
static std::map<std::string, std::string> parse_query_string_impl(httpd_req_t* request, std::initializer_list<std::string_view> query_keys, bool require_all) {
  std::map<std::string, std::string> query_map;

  std::array<char, kMaxQueryLength> query{};
  const auto query_len = httpd_req_get_url_query_len(request);

  if (query_len >= query.size()) {
    ESP_LOGE(TAG, "Query too long: %d", query_len);
    httpd_resp_send_err(request, HTTPD_414_URI_TOO_LONG, "Query too long");
    return {};
  }

  if (httpd_req_get_url_query_str(request, query.data(), query.size()) != ESP_OK) {
    if (require_all) {
      ESP_LOGE(TAG, "Failed to get query string");
      httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to get query string");
      return {};
    }
    return query_map;
  }

  for (const auto& key : query_keys) {
    std::array<char, kMaxQueryLength> value_buf{};
    std::string key_str(key);
    if (httpd_query_key_value(query.data(), key_str.c_str(), value_buf.data(), value_buf.size()) != ESP_OK) {
      if (require_all) {
        ESP_LOGE(TAG, "Failed to get %s query string", key_str.c_str());
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to get query string");
        return {};
      }
      continue;
    }

    example_uri_decode(value_buf.data(), reinterpret_cast<unsigned char*>(value_buf.data()), value_buf.size());
    query_map[key_str] = std::string(value_buf.data());
  }
  return query_map;
}

}  // namespace

bool WebServer::start(const AppConfig& config) {
  stop();

  bool started = false;
  if (config.http.port > 0) {
    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    http_config.server_port = config.http.port;
    ESP_ERROR_CHECK(httpd_start(&http_server_handle_, &http_config));

    for (const auto& uri : uri_handlers) {
      ESP_ERROR_CHECK(httpd_register_uri_handler(http_server_handle_, &uri));
    }

    ESP_LOGI(TAG, "HTTP server started on port %d", config.http.port);
    started = true;
  }
  if (config.https.port > 0) {
    httpd_ssl_config_t https_config = HTTPD_SSL_CONFIG_DEFAULT();
    https_config.httpd.server_port = config.https.port;
    https_config.servercert = https_server_cert;
    https_config.prvtkey_pem = https_server_key;
    https_config.servercert_len = https_server_cert_len + 1;
    https_config.prvtkey_len = https_server_key_len + 1;

    ESP_ERROR_CHECK(httpd_ssl_start(&https_server_handle_, &https_config));

    for (const auto& uri : uri_handlers) {
      ESP_ERROR_CHECK(httpd_register_uri_handler(https_server_handle_, &uri));
    }

    ESP_LOGI(TAG, "HTTPS server started on port %d", config.https.port);
    started = true;
  }
  return started;
}

bool WebServer::is_started() const { return http_server_handle_ != nullptr || https_server_handle_ != nullptr; }

void WebServer::stop() {
  if (http_server_handle_ != nullptr) {
    for (const auto& uri : uri_handlers) {
      ESP_ERROR_CHECK(httpd_unregister_uri_handler(http_server_handle_, uri.uri, uri.method));
    }
    ESP_ERROR_CHECK(httpd_stop(http_server_handle_));
    http_server_handle_ = nullptr;
  }
  if (https_server_handle_ != nullptr) {
    for (const auto& uri : uri_handlers) {
      ESP_ERROR_CHECK(httpd_unregister_uri_handler(https_server_handle_, uri.uri, uri.method));
    }
    ESP_ERROR_CHECK(httpd_stop(https_server_handle_));
    https_server_handle_ = nullptr;
  }
}

esp_err_t WebServer::handler_get_root(httpd_req_t* req) {
  ESP_LOGI(TAG, "GET %s", req->uri);
  httpd_resp_set_type(req, kContentTypeHtmlUtf8);
  httpd_resp_send(req, index_html, static_cast<ssize_t>(index_html_len));
  return ESP_OK;
}

esp_err_t WebServer::handler_get_api_status(httpd_req_t* req) {
  ESP_LOGI(TAG, "GET %s", req->uri);
  auto json_status = R"({"status":"ok"})";

  httpd_resp_set_type(req, kContentTypeJsonUtf8);
  httpd_resp_send(req, json_status, static_cast<ssize_t>(strlen(json_status)));
  return ESP_OK;
}

esp_err_t WebServer::handler_get_api_files(httpd_req_t* req) {
  ESP_LOGI(TAG, "GET %s", req->uri);
  auto parsed_query = parse_query_string(req, {kKeyPath});
  if (parsed_query.empty()) return ESP_FAIL;

  const auto path = parsed_query[kKeyPath];
  ESP_LOGI(TAG, "path_query: %s", path.c_str());

  const auto target_dir = std::filesystem::path{std::string(kKeySDCardRoot) + path};

  if (!(exists(target_dir) && is_directory(target_dir))) {
    ESP_LOGE(TAG, "path %s not exist", target_dir.string().c_str());
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "path not exist");
    return ESP_FAIL;
  }

  cJSON* root = cJSON_CreateObject();
  cJSON* files = cJSON_CreateArray();
  cJSON_AddItemToObject(root, kKeyFiles, files);

  auto add_file_item = [files](const std::string& name, const bool is_dir, const uint64_t file_size) {
    cJSON* file_item = cJSON_CreateObject();
    cJSON_AddStringToObject(file_item, kKeyName, name.c_str());
    cJSON_AddBoolToObject(file_item, kKeyIsDir, is_dir);
    cJSON_AddNumberToObject(file_item, kKeySize, static_cast<double>(file_size));
    cJSON_AddItemToArray(files, file_item);
  };

  if (path != kKeyRootDir) {
    add_file_item("..", true, 0);
  }
  for (const auto& entry : std::filesystem::directory_iterator(target_dir)) {
    const auto& file_name = entry.path().filename();
    const auto is_dir = is_directory(entry.path());
    add_file_item(file_name.string(), is_dir, is_dir ? 0 : entry.file_size());
  }

  auto* json_str = cJSON_PrintUnformatted(root);

  httpd_resp_set_type(req, kContentTypeJsonUtf8);
  httpd_resp_send(req, json_str, static_cast<ssize_t>(strlen(json_str)));

  free(json_str);
  cJSON_Delete(root);

  return ESP_OK;
}

esp_err_t WebServer::handler_post_api_files(httpd_req_t* req) {
  ESP_LOGI(TAG, "POST %s", req->uri);
  char content_type[kMaxContentTypeLength]{};
  if (httpd_req_get_hdr_value_str(req, kHeaderContentType, content_type, sizeof(content_type)) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get content type");
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to get content type");
    return ESP_FAIL;
  }
  if (std::string_view(content_type).starts_with(kContentTypeJson)) {
    return handler_post_api_files_mkdir(req);
  }
  return handler_post_api_files_upload(req);
}

esp_err_t WebServer::handler_post_api_files_mkdir(httpd_req_t* req) {
  std::string json_str(req->content_len, '\0');
  if (httpd_req_recv(req, json_str.data(), req->content_len) < 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to receive request body");
    return ESP_FAIL;
  }
  auto* json_root = cJSON_Parse(json_str.c_str());
  if (json_root == nullptr) {
    ESP_LOGE(TAG, "Failed to parse JSON: %s", json_str.c_str());
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to parse JSON");
    return ESP_FAIL;
  }
  auto* json_path = cJSON_GetObjectItem(json_root, kKeyPath);
  if (json_path == nullptr || !cJSON_IsString(json_path) || json_path->valuestring == nullptr) {
    ESP_LOGE(TAG, "Failed to get path from JSON: %s", json_str.c_str());
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to get path from JSON");
    cJSON_Delete(json_root);
    return ESP_FAIL;
  }
  const auto create_dir_path = std::string(kKeySDCardRoot) + std::string(json_path->valuestring);
  cJSON_Delete(json_root);
  std::error_code ec;
  std::filesystem::create_directories(create_dir_path, ec);
  if (ec) {
    ESP_LOGE(TAG, "Failed to create directory: %s", ec.message().c_str());
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to create directory");
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, kContentTypeJsonUtf8);
  httpd_resp_sendstr(req, R"({"status":"ok"})");

  return ESP_OK;
}

esp_err_t WebServer::handler_post_api_files_upload(httpd_req_t* req) {
  auto parsed_query = parse_query_string(req, {kKeyPath});
  if (parsed_query.empty()) {
    ESP_LOGE(TAG, "Failed to get path from query string");
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to get path from query string");
    return ESP_FAIL;
  }
  const auto upload_path = std::string(kKeySDCardRoot) + parsed_query[kKeyPath];

  std::filesystem::path target_file_path(upload_path);
  std::error_code ec;
  std::filesystem::create_directories(target_file_path.parent_path(), ec);
  if (ec) {
    ESP_LOGE(TAG, "Failed to create parent directories: %s", ec.message().c_str());
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create parent directories");
    return ESP_FAIL;
  }

  FILE* fd = fopen(upload_path.c_str(), "wb");
  if (fd == nullptr) {
    ESP_LOGE(TAG, "Failed to open file for writing: %s", upload_path.c_str());
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open file for writing");
    return ESP_FAIL;
  }
  setvbuf(fd, nullptr, _IOFBF, kFileIOBufferSize);

  RingbufHandle_t ringbuf = xRingbufferCreate(kUploadRingBufferSize, RINGBUF_TYPE_BYTEBUF);
  SemaphoreHandle_t done_sem = xSemaphoreCreateBinary();
  if (ringbuf == nullptr || done_sem == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate memory resources for async upload");
    fclose(fd);
    std::filesystem::remove(upload_path);
    if (ringbuf) vRingbufferDelete(ringbuf);
    if (done_sem) vSemaphoreDelete(done_sem);
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_sendstr(req, "Out of memory/buffers");
    return ESP_FAIL;
  }

  AsyncIOContext context;
  context.ringbuf = ringbuf;
  context.fd = fd;
  context.is_done = false;
  context.has_error = false;
  context.done_sem = done_sem;

  BaseType_t task_ret = xTaskCreatePinnedToCore(sd_write_task, "sd_write_task", kSDTaskStackSize, &context, kSDTaskPriority, nullptr, kSDTaskCore);

  if (task_ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create background writing task");
    fclose(fd);
    std::filesystem::remove(upload_path);
    vRingbufferDelete(ringbuf);
    vSemaphoreDelete(done_sem);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create background task");
    return ESP_FAIL;
  }

  size_t block_size = kRecvBlockSize;
  std::vector<char> buf(block_size);

  size_t remaining = req->content_len;
  esp_err_t ret_status = ESP_OK;

  while (remaining > 0) {
    size_t to_recv = std::min(block_size, remaining);
    int received = httpd_req_recv(req, buf.data(), to_recv);
    if (received <= 0) {
      if (received == HTTPD_SOCK_ERR_TIMEOUT) {
        continue;
      }
      ESP_LOGE(TAG, "Socket error or timeout during file receive: %d", received);
      context.has_error = true;
      ret_status = ESP_FAIL;
      break;
    }

    if (context.has_error) {
      ret_status = ESP_FAIL;
      break;
    }

    BaseType_t sent = xRingbufferSend(context.ringbuf, buf.data(), received, kRingbufferSendTimeout);
    if (sent != pdTRUE) {
      ESP_LOGE(TAG, "Ringbuffer send timeout (SD card is too slow)");
      context.has_error = true;
      ret_status = ESP_FAIL;
      break;
    }
    remaining -= received;
  }

  context.is_done = true;

  xSemaphoreTake(context.done_sem, portMAX_DELAY);

  fclose(fd);
  vRingbufferDelete(ringbuf);
  vSemaphoreDelete(done_sem);

  if (ret_status != ESP_OK || context.has_error) {
    ESP_LOGE(TAG, "Upload failed during streaming process");
    std::filesystem::remove(upload_path);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Upload failed");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "File uploaded successfully: %s", upload_path.c_str());

  httpd_resp_set_status(req, "201 Created");
  httpd_resp_set_type(req, kContentTypeJsonUtf8);
  httpd_resp_sendstr(req, R"({"status":"ok"})");
  return ESP_OK;
}

esp_err_t WebServer::handler_delete_api_files(httpd_req_t* req) {
  ESP_LOGI(TAG, "DELETE %s", req->uri);
  auto parsed_query = parse_query_string(req, {kKeyPath});
  if (parsed_query.empty()) return ESP_FAIL;

  const auto path = parsed_query[kKeyPath];
  ESP_LOGI(TAG, "path_query: %s", path.c_str());

  const auto target_path = std::filesystem::path{std::string(kKeySDCardRoot) + path};
  if (!exists(target_path)) {
    ESP_LOGE(TAG, "Target path does not exist: %s", target_path.c_str());
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Target path does not exist");
    return ESP_FAIL;
  }
  if (is_directory(target_path)) {
    ESP_LOGE(TAG, "Target path is a directory: %s", target_path.c_str());
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Target path is a directory");
    return ESP_FAIL;
  }
  if (!remove(target_path)) {
    ESP_LOGE(TAG, "Failed to remove file: %s", target_path.c_str());
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to remove file");
    return ESP_FAIL;
  }
  ESP_LOGI(TAG, "File removed successfully: %s", target_path.c_str());

  httpd_resp_set_type(req, kContentTypeJsonUtf8);
  httpd_resp_sendstr(req, R"({"status":"ok"})");

  return ESP_OK;
}
/*
esp_err_t WebServer::handler_get_api_files_contents(httpd_req_t* req) {
  ESP_LOGI(TAG, "GET %s", req->uri);
  auto parsed_query = parse_query_string(req, {kKeyPath});
  if (parsed_query.empty()) return ESP_FAIL;

  const auto path = parsed_query[kKeyPath];
  const auto target_file = std::filesystem::path{std::string(kKeySDCardRoot) + path};

  if (!exists(target_file) || is_directory(target_file)) {
    ESP_LOGE(TAG, "File does not exist or is a directory: %s", target_file.c_str());
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
    return ESP_FAIL;
  }

  FILE* fd = fopen(target_file.string().c_str(), "rb");
  if (fd == nullptr) {
    ESP_LOGE(TAG, "Failed to open file: %s", target_file.string().c_str());
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open file");
    return ESP_FAIL;
  }
  setvbuf(fd, nullptr, _IOFBF, kFileIOBufferSize);

  enable_tcp_nodelay(req);

  const auto file_size = std::filesystem::file_size(target_file);
  const std::string response_header = std::string(kHttpRawStatusOk) + kHttpRawContentTypeOctetStream + kHttpRawContentDispositionPrefix + target_file.filename().string() +
                                      kHttpRawQuoteCRLF + kHttpRawContentLengthPrefix + std::to_string(file_size) + kHttpRawCRLF + kHttpRawConnectionClose + kHttpRawCRLF;

  if (httpd_send(req, response_header.data(), response_header.length()) < 0) {
    ESP_LOGE(TAG, "Failed to send HTTP headers");
    fclose(fd);
    return ESP_FAIL;
  }

  // 링버퍼 + 백그라운드 SD 읽기 태스크로 비동기 파이프라인 구성
  RingbufHandle_t ringbuf = xRingbufferCreate(kDownloadRingBufferSize, RINGBUF_TYPE_BYTEBUF);
  SemaphoreHandle_t done_sem = xSemaphoreCreateBinary();
  if (ringbuf == nullptr || done_sem == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate memory resources for async download");
    fclose(fd);
    if (ringbuf) vRingbufferDelete(ringbuf);
    if (done_sem) vSemaphoreDelete(done_sem);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    return ESP_FAIL;
  }

  AsyncIOContext context;
  context.ringbuf = ringbuf;
  context.fd = fd;
  context.is_done = false;
  context.has_error = false;
  context.done_sem = done_sem;

  BaseType_t task_ret = xTaskCreatePinnedToCore(sd_read_task, "sd_read_task", kSDTaskStackSize, &context, kSDTaskPriority, nullptr, kSDTaskCore);

  if (task_ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create background reading task");
    fclose(fd);
    vRingbufferDelete(ringbuf);
    vSemaphoreDelete(done_sem);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create background task");
    return ESP_FAIL;
  }

  // 메인스레드: 링버퍼에서 꺼내서 네트워크 전송
  esp_err_t ret_status = ESP_OK;
  while (true) {
    size_t item_size = 0;
    char* item = static_cast<char*>(xRingbufferReceiveUpTo(context.ringbuf, &item_size, kRingbufferRecvTimeout, kSendBlockSize));
    if (item != nullptr) {
      int send_ret = httpd_send(req, item, item_size);
      vRingbufferReturnItem(context.ringbuf, item);
      if (send_ret < 0) {
        ESP_LOGE(TAG, "Failed to send file data");
        context.has_error = true;
        ret_status = ESP_FAIL;
        break;
      }
    } else if (context.is_done) {
      break;
    }
  }

  xSemaphoreTake(context.done_sem, portMAX_DELAY);

  fclose(fd);
  vRingbufferDelete(ringbuf);
  vSemaphoreDelete(done_sem);

  if (ret_status != ESP_OK || context.has_error) {
    ESP_LOGE(TAG, "Download failed during streaming process");
    return ESP_FAIL;
  }

  return ESP_OK;
}
*/
esp_err_t WebServer::handler_get_api_files_contents(httpd_req_t* req) {
  ESP_LOGI(TAG, "GET %s", req->uri);
  auto parsed_query = parse_query_string(req, {kKeyPath});
  if (parsed_query.empty()) return ESP_FAIL;

  const auto path = parsed_query[kKeyPath];
  const auto target_file = std::filesystem::path{std::string(kKeySDCardRoot) + path};

  if (!exists(target_file) || is_directory(target_file)) {
    ESP_LOGE(TAG, "File does not exist or is a directory: %s", target_file.c_str());
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
    return ESP_FAIL;
  }

  FILE* fd = fopen(target_file.string().c_str(), "rb");
  if (fd == nullptr) {
    ESP_LOGE(TAG, "Failed to open file: %s", target_file.string().c_str());
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open file");
    return ESP_FAIL;
  }
  setvbuf(fd, nullptr, _IOFBF, kFileIOBufferSize);

  enable_tcp_nodelay(req);

  const auto file_size = std::filesystem::file_size(target_file);
  const std::string response_header = std::string(kHttpRawStatusOk) + kHttpRawContentTypeOctetStream + kHttpRawContentDispositionPrefix + target_file.filename().string() +
                                      kHttpRawQuoteCRLF + kHttpRawContentLengthPrefix + std::to_string(file_size) + kHttpRawCRLF + kHttpRawConnectionClose + kHttpRawCRLF;

  if (httpd_send(req, response_header.data(), response_header.length()) < 0) {
    ESP_LOGE(TAG, "Failed to send HTTP headers");
    fclose(fd);
    return ESP_FAIL;
  }

  // ─── 개선: 백그라운드 태스크 + 수신 루프 최적화 ───
  RingbufHandle_t ringbuf = xRingbufferCreate(kDownloadRingBufferSize, RINGBUF_TYPE_BYTEBUF);
  SemaphoreHandle_t done_sem = xSemaphoreCreateBinary();
  if (ringbuf == nullptr || done_sem == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate memory resources for async download");
    fclose(fd);
    if (ringbuf) vRingbufferDelete(ringbuf);
    if (done_sem) vSemaphoreDelete(done_sem);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    return ESP_FAIL;
  }

  AsyncIOContext context;
  context.ringbuf = ringbuf;
  context.fd = fd;
  context.is_done = false;
  context.has_error = false;
  context.done_sem = done_sem;

  BaseType_t task_ret = xTaskCreatePinnedToCore(sd_read_task, "sd_read_task", kSDTaskStackSize, &context, kSDTaskPriority, nullptr, kSDTaskCore);

  if (task_ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create background reading task");
    fclose(fd);
    vRingbufferDelete(ringbuf);
    vSemaphoreDelete(done_sem);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create background task");
    return ESP_FAIL;
  }

  // ─── 개선 1: 더 긴 타임아웃 사용 ───
  esp_err_t ret_status = ESP_OK;
  int consecutive_timeouts = 0;
  const int max_consecutive_timeouts = 10;

  while (true) {
    size_t item_size = 0;
    // ─── 개선 2: 수신 크기 제한 제거 (전체 링버퍼 크기까지 수신) ───
    char* item = static_cast<char*>(xRingbufferReceiveUpTo(context.ringbuf, &item_size,
                                                           pdMS_TO_TICKS(1000),     // 타임아웃 1초로 증가
                                                           kDownloadRingBufferSize  // ← 최대 크기로 변경 (16KB → 전체)
                                                           ));

    if (item != nullptr) {
      consecutive_timeouts = 0;  // 타임아웃 카운터 리셋
      int send_ret = httpd_send(req, item, item_size);
      vRingbufferReturnItem(context.ringbuf, item);
      if (send_ret < 0) {
        ESP_LOGE(TAG, "Failed to send file data");
        context.has_error = true;
        ret_status = ESP_FAIL;
        break;
      }
    } else {
      if (context.is_done) {
        break;  // 읽기 완료
      }
      // ─── 개선 3: 연속 타임아웃 감지 ───
      consecutive_timeouts++;
      if (consecutive_timeouts > max_consecutive_timeouts) {
        ESP_LOGE(TAG, "Too many consecutive ringbuffer timeouts");
        context.has_error = true;
        ret_status = ESP_FAIL;
        break;
      }
    }
  }

  xSemaphoreTake(context.done_sem, portMAX_DELAY);

  fclose(fd);
  vRingbufferDelete(ringbuf);
  vSemaphoreDelete(done_sem);

  if (ret_status != ESP_OK || context.has_error) {
    ESP_LOGE(TAG, "Download failed during streaming process");
    return ESP_FAIL;
  }

  return ESP_OK;
}

esp_err_t WebServer::handler_get_api_speedtest_download(httpd_req_t* req) {
  ESP_LOGI(TAG, "GET %s", req->uri);
  auto parsed_query = parse_query_string_optional(req, {kKeySize});

  size_t total_size = kSpeedtestDefaultSize;
  if (!parsed_query.empty() && !parsed_query[std::string(kKeySize)].empty()) {
    char* endptr = nullptr;
    unsigned long val = strtoul(parsed_query[std::string(kKeySize)].c_str(), &endptr, 10);
    if (endptr != parsed_query[std::string(kKeySize)].c_str() && val > 0) {
      total_size = static_cast<size_t>(val);
    }
  }

  enable_tcp_nodelay(req);

  const std::string response_header = std::string(kHttpRawStatusOk) + kHttpRawContentTypeOctetStream + kHttpRawContentDispositionPrefix + "speedtest.bin" + kHttpRawQuoteCRLF +
                                      kHttpRawContentLengthPrefix + std::to_string(total_size) + kHttpRawCRLF + kHttpRawConnectionClose + kHttpRawCRLF;

  if (httpd_send(req, response_header.data(), response_header.length()) < 0) {
    ESP_LOGE(TAG, "Failed to send HTTP headers");
    return ESP_FAIL;
  }

  size_t block_size = kSendBlockSize;
  std::vector<char> buf(block_size, 0);

  size_t sent_bytes = 0;
  while (sent_bytes < total_size) {
    size_t to_send = std::min(block_size, total_size - sent_bytes);
    if (httpd_send(req, buf.data(), to_send) < 0) {
      ESP_LOGE(TAG, "Failed to send speedtest download data");
      return ESP_FAIL;
    }
    sent_bytes += to_send;
  }

  return ESP_OK;
}

esp_err_t WebServer::handler_post_api_speedtest_upload(httpd_req_t* req) {
  ESP_LOGI(TAG, "POST %s", req->uri);

  size_t block_size = kRecvBlockSize;
  std::vector<char> buf(block_size);

  size_t remaining = req->content_len;
  size_t received_bytes = 0;

  while (remaining > 0) {
    size_t to_recv = std::min(block_size, remaining);
    int received = httpd_req_recv(req, buf.data(), to_recv);
    if (received <= 0) {
      if (received == HTTPD_SOCK_ERR_TIMEOUT) {
        continue;
      }
      ESP_LOGE(TAG, "Socket error during speedtest upload: %d", received);
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Socket error during speedtest upload");
      return ESP_FAIL;
    }
    remaining -= received;
    received_bytes += received;
  }

  std::string response_json = R"({"status":"ok","bytes_received":)" + std::to_string(received_bytes) + "}";
  httpd_resp_set_type(req, kContentTypeJsonUtf8);
  httpd_resp_sendstr(req, response_json.c_str());
  return ESP_OK;
}

std::map<std::string, std::string> WebServer::parse_query_string(httpd_req_t* request, std::initializer_list<std::string_view> query_keys) {
  return parse_query_string_impl(request, query_keys, true);
}

std::map<std::string, std::string> WebServer::parse_query_string_optional(httpd_req_t* request, std::initializer_list<std::string_view> query_keys) {
  return parse_query_string_impl(request, query_keys, false);
}