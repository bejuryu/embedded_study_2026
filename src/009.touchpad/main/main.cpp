#include "ble/ble_hid_device.hpp"
#include "bsp/esp-bsp.h"
#include "display/context.hpp"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "nvs_flash.h"
#include "wifi.hpp"

static const char* TAG = "Main";

struct MachineData {
  Display::Context display;
  WiFi             wifi;
};

void system_init(MachineData& machine_data);
bool sdcard_sdmmc_init();

extern "C" void app_main() {
  esp_rom_delay_us(500000);  // 500ms hardware stabilization delay
  static MachineData machine_data;
  system_init(machine_data);
}

void system_init(MachineData& machine_data) {
  // 1. 코프로세서(ESP32-C6) 전원 활성화
  //    ⚠ bsp_feature_enable 이후 최소 2초 대기 필수.
  //    이 대기 없이 nimble_port_init()을 호출하면 HCI sync가 실패합니다.
  bsp_feature_enable(BSP_FEATURE_WIFI, true);
  vTaskDelay(pdMS_TO_TICKS(2000));

  // 2. NVS 초기화 (BLE 본딩 키 저장소)
  auto esp_ret = nvs_flash_init();
  if (esp_ret == ESP_ERR_NVS_NO_FREE_PAGES || esp_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    esp_ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(esp_ret);

  // 3. WiFi 초기화 (ESP-Hosted 네트워크 채널)
  machine_data.wifi.initialize_station();

  // 4. SD 카드 마운트 (폰트 파일 접근)
  sdcard_sdmmc_init();

  // 4.5. SD 카드 내 http_server.json 기반 다중 AP 오토스캔 및 자동 로밍 접속
  machine_data.wifi.connect_from_json_list("/sdcard/http_server.json");

  // 5. Display 초기화 (LVGL UI 트리 구축)
  machine_data.display.initialize();

  // 6. BLE HID 초기화
  //    esp-hosted VHCI 채널(ESP32-P4 ↔ ESP32-C6)이 HCI 데이터를 전달할
  //    준비를 완료하는 데 Transport active 이후 추가 시간이 필요합니다.
  //    Display 초기화(~1.5초)를 마쳤으나 VHCI BLE 채널이 아직 준비 중일 수 있으므로
  //    1초 추가 대기합니다. 타임아웃이 계속되면 이 값을 늘립니다.
  vTaskDelay(pdMS_TO_TICKS(1000));

  if (!Ble::HidDevice::instance().initialize()) {
    ESP_LOGE(TAG, "BLE HID 초기화 실패 — BLE 없이 계속 실행");
  }
}

bool sdcard_sdmmc_init() {
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.slot         = SDMMC_HOST_SLOT_0;
  host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
  host.init         = []() { return ESP_OK; };
  host.deinit       = []() { return ESP_OK; };

  sdmmc_slot_config_t slot = {};
  bsp_sdcard_sdmmc_get_slot(SDMMC_HOST_SLOT_0, &slot);

  bsp_sdcard_cfg_t sd_config{};
  sd_config.host = &host;
  sd_config.slot = {.sdmmc = &slot};

  const auto ret = bsp_sdcard_sdmmc_mount(&sd_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SD card mount failed");
    return false;
  }
  return true;
}
