#include "utility.hpp"

#include <ctime>
#include <filesystem>

#include "bsp/esp-bsp.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "nvs_flash.h"

static const char* TAG = "Utility";

SystemConfig SystemConfig::get_default() {
  return {
      .valid = true,
      .wifi = {.ssid = "", .password = ""},
      .ntp_server = "pool.ntp.org",
      .timezone = "Asia/Seoul",
      .geo_location = {.location = "BUSAN", .latitude = 35.1796, .longitude = 129.0756},
  };
}

SystemConfig Utility::load_system_config(const std::string& path) {
  static const auto kKeyWifi = "wifi";
  static const auto kKeyNtpServer = "ntp";
  static const auto kKeyTimezone = "timezone";
  static const auto kKeyGeoLocation = "geo_location";
  static const auto kKeySsid = "ssid";
  static const auto kKeyPassword = "password";
  static const auto kKeyLocation = "location";
  static const auto kKeyLatitude = "latitude";
  static const auto kKeyLongitude = "longitude";

  if (path.empty() || std::filesystem::exists(path) == false) {
    ESP_LOGW(TAG, "Configuration file not found at path: %s", path.c_str());
    return {};
  }
  FILE* file = fopen(path.c_str(), "r");
  if (file == nullptr) {
    ESP_LOGE(TAG, "Failed to open configuration file: %s", path.c_str());
    return {};
  }
  const auto file_size = std::filesystem::file_size(std::filesystem::path(path));
  auto* buffer = new char[file_size + 1]{};
  fread(buffer, 1, file_size, file);
  fclose(file);

  ESP_LOGI(TAG, "Raw file buffer read from %s (size: %u):\n%s", path.c_str(), (unsigned int)file_size, buffer);

  auto* root = cJSON_Parse(buffer);
  if (root == nullptr) {
    ESP_LOGE(TAG, "JSON syntax error in file: %s (Check encoding, must be UTF-8 without BOM)", path.c_str());
    delete[] buffer;
    return {};
  }

  SystemConfig config{};

  bool do_continue = true;

  auto* wifi = cJSON_GetObjectItem(root, kKeyWifi);
  auto* ntp_server = cJSON_GetObjectItem(root, kKeyNtpServer);
  auto* timezone = cJSON_GetObjectItem(root, kKeyTimezone);
  auto* geo_location = cJSON_GetObjectItem(root, kKeyGeoLocation);

  // wifi
  if (wifi != nullptr && cJSON_IsObject(wifi)) {
    auto* ssid = cJSON_GetObjectItem(wifi, kKeySsid);
    auto* password = cJSON_GetObjectItem(wifi, kKeyPassword);
    if (ssid != nullptr && password != nullptr && ssid->valuestring != nullptr && password->valuestring != nullptr) {
      config.wifi = {.ssid = ssid->valuestring, .password = password->valuestring};
    } else {
      ESP_LOGE(TAG, "Missing or invalid 'wifi.ssid' or 'wifi.password' string fields in system.json");
      do_continue = false;
    }
  } else {
    ESP_LOGE(TAG, "Missing or invalid 'wifi' object block in system.json");
    do_continue = false;
  }

  // ntp server
  if (do_continue) {
    if (ntp_server != nullptr && ntp_server->valuestring != nullptr) {
      config.ntp_server = ntp_server->valuestring;
    } else {
      ESP_LOGE(TAG, "Missing or invalid 'ntp' string field in system.json");
      do_continue = false;
    }
  }

  // timezone
  if (do_continue) {
    if (timezone != nullptr && timezone->valuestring != nullptr) {
      config.timezone = timezone->valuestring;
    } else {
      ESP_LOGE(TAG, "Missing or invalid 'timezone' string field in system.json");
      do_continue = false;
    }
  }

  // geo location
  if (do_continue) {
    if (geo_location != nullptr && cJSON_IsObject(geo_location)) {
      auto* location = cJSON_GetObjectItem(geo_location, kKeyLocation);
      auto* latitude = cJSON_GetObjectItem(geo_location, kKeyLatitude);
      auto* longitude = cJSON_GetObjectItem(geo_location, kKeyLongitude);
      if (location != nullptr && latitude != nullptr && longitude != nullptr && cJSON_IsString(location) &&
          cJSON_IsNumber(latitude) && cJSON_IsNumber(longitude)) {
        config.geo_location = {.location = location->valuestring,
                               .latitude = static_cast<float>(latitude->valuedouble),
                               .longitude = static_cast<float>(longitude->valuedouble)};
      } else {
        ESP_LOGE(TAG, "Missing or invalid fields inside 'geo_location' (location: string, latitude: num, longitude: num) in system.json");
        do_continue = false;
      }
    } else {
      ESP_LOGE(TAG, "Missing or invalid 'geo_location' object block in system.json");
      do_continue = false;
    }
  }

  delete[] buffer;
  cJSON_Delete(root);

  if (do_continue) {
    config.valid = true;
    setenv("TZ", config.timezone.c_str(), 1);
    tzset();
    ESP_LOGI(TAG, "System configuration loaded successfully from system.json");
    return config;
  } else {
    ESP_LOGE(TAG, "Failed to load system config due to parsing errors. Fallback to default/empty configuration.");
    return {};
  }
}

bool Utility::system_init() {
  bsp_feature_enable(BSP_FEATURE_WIFI, true);
  vTaskDelay(pdMS_TO_TICKS(2000));

  auto esp_ret = nvs_flash_init();
  if (esp_ret == ESP_ERR_NVS_NO_FREE_PAGES || esp_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    esp_ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(esp_ret);
  return true;
}

bool Utility::sdcard_sdmmc_init() {
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.slot = SDMMC_HOST_SLOT_0;
  host.init = []() { return ESP_OK; };
  host.deinit = []() { return ESP_OK; };

  sdmmc_slot_config_t slot = {};
  bsp_sdcard_sdmmc_get_slot(SDMMC_HOST_SLOT_0, &slot);

  bsp_sdcard_cfg_t sd_config{};
  sd_config.host = &host;
  sd_config.slot = {.sdmmc = &slot};

  const auto esp_ret = bsp_sdcard_sdmmc_mount(&sd_config);
  if (esp_ret != ESP_OK) {
    ESP_LOGE(TAG, "SD card mount failed");
    return false;
  }
  return true;
}

bool Utility::sntp_sync(const std::string& ntp_server) {
  if (ntp_server.empty()) {
    ESP_LOGW(TAG, "SNTP server is empty");
    return false;
  }
  if (esp_sntp_enabled()) {
    esp_netif_sntp_deinit();
  }

  esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(ntp_server.c_str());
  config.smooth_sync = false;
  auto ret = esp_netif_sntp_init(&config);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "SNTP initialization failed: %s", esp_err_to_name(ret));
    return false;
  }
  ESP_LOGI(TAG, "wait for SNTP Synchronized...");
  ret = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10'000));
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "SNTP synchronized successfully");
  } else {
    ESP_LOGE(TAG, "SNTP synchronization failed: %s", esp_err_to_name(ret));
    return false;
  }

  return true;
}