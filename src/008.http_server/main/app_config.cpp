#include "app_config.hpp"

#include <filesystem>

#include "cJSON.h"
#include "esp_log.h"

static const char* TAG = "AppConfig";

AppConfig AppConfig::get_default() {
  AppConfig config{};
  config.http = {.port = 80};
  config.https = {.port = 443};
  config.is_verified = true;
  return config;
}

bool AppConfig::verified() const { return (http.port > 0) || (https.port > 0); }

AppConfig AppConfig::load(const std::string& path) {
  static const auto kKeyWifi = "wifi";
  static const auto kKeySsid = "ssid";
  static const auto kKeyPassword = "password";
  static const auto kKeyHttp = "http";
  static const auto kKeyHttps = "https";
  static const auto kKeyPort = "port";

  if (path.empty()) {
    ESP_LOGE(TAG, "Invalid path provided: %s", path.c_str());
    return {};
  }
  if (!std::filesystem::exists(path)) {
    ESP_LOGE(TAG, "File does not exist: %s", path.c_str());
    return {};
  }
  ESP_LOGI(TAG, "Loading configuration from: %s", path.c_str());

  const auto file_size = std::filesystem::file_size(path);
  auto* buffer = new char[file_size + 1];

  bool do_continue = true;
  FILE* file = fopen(path.c_str(), "r");
  if (file == nullptr) {
    ESP_LOGE(TAG, "Failed to open file: %s", path.c_str());
    do_continue = false;
  }

  if (do_continue) {
    fread(buffer, 1, file_size, file);
    buffer[file_size] = '\0';
    fclose(file);
    ESP_LOGI(TAG, "load %s success: %s", path.c_str(), buffer);
  }

  AppConfig config{};

  cJSON* root = nullptr;
  if (do_continue) {
    root = cJSON_Parse(buffer);
    if (root == nullptr) {
      ESP_LOGE(TAG, "JSON syntax error in file: %s", path.c_str());
      do_continue = false;
    }
  }
  cJSON* wifi = nullptr;
  cJSON* http = nullptr;
  cJSON* https = nullptr;

  if (do_continue) {
    wifi = cJSON_GetObjectItem(root, kKeyWifi);
    http = cJSON_GetObjectItem(root, kKeyHttp);
    https = cJSON_GetObjectItem(root, kKeyHttps);
    do_continue = (wifi != nullptr) && cJSON_IsArray(wifi) && (http != nullptr) && cJSON_IsObject(http) && (https != nullptr) && cJSON_IsObject(https);
  }
  // wifi
  if (do_continue) {
    cJSON* wifi_item = nullptr;
    cJSON_ArrayForEach(wifi_item, wifi) {
      const auto* ssid = cJSON_GetObjectItem(wifi_item, kKeySsid);
      const auto* password = cJSON_GetObjectItem(wifi_item, kKeyPassword);
      if ((ssid != nullptr) && cJSON_IsString(ssid) && (password != nullptr) && cJSON_IsString(password)) {
        config.wifi_list.push_back(WiFiItem{.ssid = ssid->valuestring, .password = password->valuestring});
      } else
        do_continue = false;
    }
  }
  // http
  if (do_continue) {
    const auto* http_port = cJSON_GetObjectItem(http, kKeyPort);
    if ((http_port != nullptr) && cJSON_IsNumber(http_port)) {
      config.http = {.port = static_cast<uint16_t>(http_port->valueint)};
    } else
      do_continue = false;
  }
  // https
  if (do_continue) {
    const auto* https_port = cJSON_GetObjectItem(https, kKeyPort);
    if ((https_port != nullptr) && cJSON_IsNumber(https_port)) {
      config.https = {.port = static_cast<uint16_t>(https_port->valueint)};
    } else
      do_continue = false;
  }

  delete[] buffer;
  if (root != nullptr) {
    cJSON_Delete(root);
  }

  config.is_verified = do_continue && config.verified();
  return config;
}