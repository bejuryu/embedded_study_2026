#include "system_init.h"

#include <stdlib.h>

#include "bsp/esp-bsp.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

static const char* TAG = "SYSTEM_INIT";

static esp_err_t sdmmc_host_init_noop(void) { return ESP_OK; }
static esp_err_t sdmmc_host_deinit_noop(void) { return ESP_OK; }

bool system_init() {
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

bool sdcard_sdmmc_init() {
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.slot = SDMMC_HOST_SLOT_0;
  host.init = &sdmmc_host_init_noop;
  host.deinit = &sdmmc_host_deinit_noop;

  sdmmc_slot_config_t slot = {};
  bsp_sdcard_sdmmc_get_slot(SDMMC_HOST_SLOT_0, &slot);

  bsp_sdcard_cfg_t sd_cfg = {
      .host = &host,
      .slot = {.sdmmc = &slot},
  };

  const auto esp_ret = bsp_sdcard_sdmmc_mount(&sd_cfg);
  if (esp_ret != ESP_OK) {
    ESP_LOGE(TAG, "SD card mount failed");
    return false;
  }
  return true;
}