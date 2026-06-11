#pragma once

#include <esp_http_server.h>

#include <initializer_list>
#include <map>
#include <string>
#include <string_view>

#include "app_config.hpp"

class WebServer {
 public:
  bool start(const AppConfig& config);
  bool is_started() const;
  void stop();

  static constexpr size_t kNumUriHandlers = 8;
  const httpd_uri_t uri_handlers[kNumUriHandlers] = {
      {.uri = "/", .method = HTTP_GET, .handler = handler_get_root, .user_ctx = this},
      {.uri = "/api/status", .method = HTTP_GET, .handler = handler_get_api_status, .user_ctx = this},
      {.uri = "/api/files", .method = HTTP_GET, .handler = handler_get_api_files, .user_ctx = this},
      {.uri = "/api/files", .method = HTTP_POST, .handler = handler_post_api_files, .user_ctx = this},
      {.uri = "/api/files", .method = HTTP_DELETE, .handler = handler_delete_api_files, .user_ctx = this},
      {.uri = "/api/files/content", .method = HTTP_GET, .handler = handler_get_api_files_contents, .user_ctx = this},
      {.uri = "/api/speedtest/download", .method = HTTP_GET, .handler = handler_get_api_speedtest_download, .user_ctx = this},
      {.uri = "/api/speedtest/upload", .method = HTTP_POST, .handler = handler_post_api_speedtest_upload, .user_ctx = this},
  };

  static esp_err_t handler_get_root(httpd_req_t* req);
  static esp_err_t handler_get_api_status(httpd_req_t* req);
  static esp_err_t handler_get_api_files(httpd_req_t* req);
  static esp_err_t handler_post_api_files(httpd_req_t* req);
  static esp_err_t handler_delete_api_files(httpd_req_t* req);
  static esp_err_t handler_get_api_files_contents(httpd_req_t* req);
  static esp_err_t handler_get_api_speedtest_download(httpd_req_t* req);
  static esp_err_t handler_post_api_speedtest_upload(httpd_req_t* req);

  static std::map<std::string, std::string> parse_query_string(httpd_req_t* request, std::initializer_list<std::string_view> query_keys);
  static std::map<std::string, std::string> parse_query_string_optional(httpd_req_t* request, std::initializer_list<std::string_view> query_keys);

 private:
  static esp_err_t handler_post_api_files_mkdir(httpd_req_t* req);
  static esp_err_t handler_post_api_files_upload(httpd_req_t* req);

  httpd_handle_t http_server_handle_ = nullptr;
  httpd_handle_t https_server_handle_ = nullptr;
};