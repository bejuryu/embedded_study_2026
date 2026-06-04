
#include <chrono>
#include <filesystem>

#include "display.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "utility.hpp"
#include "weather_info.hpp"
#include "wifi.hpp"

static const char* TAG = "main";
static const auto system_config_file_name = "system.json";

using namespace std::chrono_literals;
using steady_clock_t = std::chrono::steady_clock;

struct WeatherTaskParams {
  Wifi* wifi;
  Display* display;
  SystemConfig* config;
};

static void weather_sync_task(void* pvParameters) {
  auto* params = static_cast<WeatherTaskParams*>(pvParameters);
  WeatherInfo weather;
  bool was_connected = false;

  auto last_weather = steady_clock_t::now() - 30min;
  auto last_ntp = steady_clock_t::now() - 1h;

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    auto now = steady_clock_t::now();

    bool connected = params->wifi->is_connected();

    // WiFi 연결 → 미연결 전환 시: NO WIFI 표시
    if (was_connected && !connected) {
      params->display->update_weather("", "NO WIFI", 0, true);
    }

    // WiFi 미연결 → 연결 전환 시: 즉시 날씨/NTP 실행
    if (!was_connected && connected) {
      last_weather = steady_clock_t::now() - 30min;
      last_ntp = steady_clock_t::now() - 1h;
    }

    was_connected = connected;

    if (!connected) continue;

    // 30분 주기: 날씨 갱신
    if (now - last_weather >= 30min) {
      last_weather = now;
      auto data = weather.fetch(params->config->geo_location.latitude, params->config->geo_location.longitude);
      if (data.valid) {
        params->display->update_weather(params->config->geo_location.location.c_str(), data.condition, data.temperature,
                                       true);  // lock=true (외부 태스크)
      }
    }

    // 1시간 주기: NTP 재동기화
    if (now - last_ntp >= 1h) {
      last_ntp = now;
      Utility::sntp_sync(params->config->ntp_server);
    }
  }
}

extern "C" void app_main() {
  ESP_LOGI(TAG, "Free DMA heap: %lu", heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
  ESP_LOGI(TAG, "Largest DMA block: %lu", heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));

  static Wifi wifi;
  static Display display;
  static SystemConfig system_config;

  Utility::system_init();
  wifi.initialize_station();
  Utility::sdcard_sdmmc_init();

  // step 1. display init
  ESP_LOGI(TAG, "Initializing display");
  display.initialize();

  // step 2. load system.json
  ESP_LOGI(TAG, "Loading system config");
  const auto config_path = std::filesystem::path(CONFIG_BSP_SD_MOUNT_POINT) / system_config_file_name;
  system_config = Utility::load_system_config(config_path);
  if (!system_config) system_config = SystemConfig::get_default();
  display.set_system_config(system_config);

  // step 3. wifi station connection
  ESP_LOGI(TAG, "WiFi Connecting");
  wifi.connect(system_config.wifi.ssid, system_config.wifi.password);

  // step 4. create weather and NTP sync task
  ESP_LOGI(TAG, "Creating Weather/NTP sync task");
  static WeatherTaskParams task_params;
  task_params.wifi = &wifi;
  task_params.display = &display;
  task_params.config = &system_config;

  xTaskCreatePinnedToCore(
      weather_sync_task,
      "weather_sync_task",
      8192,
      &task_params,
      5,
      nullptr,
      0
  );

  ESP_LOGI(TAG, "Main loop started");

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
