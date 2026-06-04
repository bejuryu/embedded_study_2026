#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdlib.h>
#include <time.h>

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "system_init.h"
#include "wifi.h"

static const char* TAG = "MAIN";
static const char* WIFI_CONFIG_FILE_NAME = "wifi.json";

void app_main(void) {
  system_init();
  wifi_init_station();
  sdcard_sdmmc_init();

  const char* path_join_format = "%s/%s";
  const auto wifi_config_path_len =
      snprintf(nullptr, 0, path_join_format, CONFIG_BSP_SD_MOUNT_POINT, WIFI_CONFIG_FILE_NAME) + 1;
  char* wifi_config_path = malloc(wifi_config_path_len);
  snprintf(wifi_config_path, wifi_config_path_len, path_join_format, CONFIG_BSP_SD_MOUNT_POINT, WIFI_CONFIG_FILE_NAME);
  struct custom_wifi_info_t wifi_info = {};
  custom_wifi_info_init(&wifi_info);

  if (load_wifi_info(wifi_config_path, &wifi_info)) {
  }
  free(wifi_config_path);

  size_t scan_count = 0;
  auto scan_result = wifi_scan(&scan_count);
  const auto exist_ap = wifi_exists_ap(wifi_info.ssid, scan_result, scan_count);
  free(scan_result);
  if (exist_ap) {
    ESP_LOGI(TAG, "scan result[%d] / exist AP[%s]: %s", scan_count, wifi_info.ssid, exist_ap ? "true" : "false");
  } else {
    ESP_LOGI(TAG, "scan result[%d]", scan_count);
  }

  wifi_connect(wifi_info.ssid, wifi_info.password);
  int count = 0;
  bool is_sntp_synced = false;

  while (true) {
    constexpr int kCountReset = 10;
    vTaskDelay(pdMS_TO_TICKS(1'000));
    if (count++ >= kCountReset) {
      if (!is_sntp_synced) {
        is_sntp_synced = wifi_sntp_sync("pool.ntp.org");
      }
      count = 0;
    }

    time_t now = {};
    struct tm timeinfo = {};
    time(&now);
    localtime_r(&now, &timeinfo);

    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    ESP_LOGI("MAIN", "Current localized time: %s", strftime_buf);
  }
}
