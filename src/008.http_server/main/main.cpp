#include <filesystem>

#include "app_config.hpp"
#include "bsp/esp-bsp.h"
#include "display.hpp"
#include "esp_log.h"
#include "nvs_flash.h"
#include "predefine.h"
#include "sdkconfig.h"
#include "utility.hpp"
#include "web_server.hpp"
#include "wifi.hpp"

static const char* TAG = "Main";
static const char* application_config_file_name = "http_server.json";

struct MachineData {
  WiFi wifi;
  WebServer web_server;
  Display display;
};

void system_init(MachineData& machine_data);
bool sdcard_sdmmc_init();
bool reconnect_wifi(MachineData& machine_data, const AppConfig& app_config);

extern "C" void app_main(void) {
  static MachineData s_machine_data;
  system_init(s_machine_data);

  const auto config_path = std::filesystem::path(CONFIG_BSP_SD_MOUNT_POINT) / application_config_file_name;
  auto app_config = AppConfig::load(config_path);
  if (!app_config) {
    ESP_LOGI(TAG, "Failed to load configuration file: %s, using default setting", application_config_file_name);
    app_config = AppConfig::get_default();
  }

  reconnect_wifi(s_machine_data, app_config);

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (s_machine_data.wifi.is_connecting()) continue;

    if (s_machine_data.wifi.is_connected()) {
      if (!s_machine_data.web_server.is_started()) {
        s_machine_data.web_server.start(app_config);

        std::string hostname = get_sanitized_hostname(HOST_NAME);
        start_mdns_service(hostname, app_config.http.port, app_config.https.port);

        const auto connected_ssid = s_machine_data.wifi.get_connected_ssid();
        const auto connected_ip = s_machine_data.wifi.get_ip_v4_address();
        std::string message = std::string("Connected to ") + connected_ssid + "\n";
        message += "IP: " + connected_ip + "\n";
        message += "HTTP port: " + std::to_string(app_config.http.port) + "\n" + "HTTPS port: " + std::to_string(app_config.https.port);
        s_machine_data.display.update_status_message(message.c_str());
      }
    } else {
      if (s_machine_data.web_server.is_started()) s_machine_data.web_server.stop();
      reconnect_wifi(s_machine_data, app_config);
    }
  }
}

void system_init(MachineData& machine_data) {
  bsp_feature_enable(BSP_FEATURE_WIFI, true);
  vTaskDelay(pdMS_TO_TICKS(2000));
  auto esp_ret = nvs_flash_init();
  if (esp_ret == ESP_ERR_NVS_NO_FREE_PAGES || esp_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    esp_ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(esp_ret);
  machine_data.wifi.initialize_station();
  machine_data.display.initialize();
  sdcard_sdmmc_init();
}

bool sdcard_sdmmc_init() {
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.slot = SDMMC_HOST_SLOT_0;
  host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
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

bool reconnect_wifi(MachineData& machine_data, const AppConfig& app_config) {
  const auto wifi_scan_result = machine_data.wifi.scan();
  if (wifi_scan_result.empty()) {
    ESP_LOGE(TAG, "Failed to scan wifi");
    return false;
  }

  machine_data.display.update_wifi_scan_result(wifi_scan_result);

  for (const auto& wifi_item : app_config.wifi_list) {
    if (WiFi::exist_ssid(wifi_item.ssid, wifi_scan_result)) {
      machine_data.wifi.connect(wifi_item.ssid, wifi_item.password);
      return true;
    }
  }
  return false;
}