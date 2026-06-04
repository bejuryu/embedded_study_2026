
#include "wifi.h"

#include <stdio.h>
#include <stdlib.h>

#include "bsp/esp-bsp.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "esp_wifi.h"

static const char* TAG = "WIFI";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static EventGroupHandle_t s_wifi_event_group;

static inline void auto_fclose(FILE** fp) {
  if (fp && *fp) {
    fclose(*fp);
    *fp = nullptr;
  }
}

static inline void auto_free(void* ptr) {
  auto real_ptr = (void**)ptr;
  if (real_ptr && *real_ptr) {
    free(*real_ptr);
    *real_ptr = nullptr;
  }
}

static void event_handler_wifi(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

bool custom_wifi_info_init(struct custom_wifi_info_t* wifi_info) {
  if (wifi_info == nullptr) return false;
  wifi_info->ssid = nullptr;
  wifi_info->password = nullptr;
  return true;
}

void custom_wifi_info_free(struct custom_wifi_info_t* wifi_info) {
  if (wifi_info == nullptr) return;
  if (wifi_info->ssid != nullptr) {
    free(wifi_info->ssid);
    wifi_info->ssid = nullptr;
  }
  if (wifi_info->password != nullptr) {
    free(wifi_info->password);
    wifi_info->password = nullptr;
  }
}

void wifi_init_station() {
  s_wifi_event_group = xEventGroupCreate();

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  const auto station_network_interface = esp_netif_create_default_wifi_sta();
  assert(station_network_interface != nullptr);

  const wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));

  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler_wifi, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler_wifi, nullptr));

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());
}

wifi_ap_record_t* wifi_scan(size_t* results_count) {
  const wifi_scan_config_t wifi_scan_config = {.show_hidden = true};
  auto err = esp_wifi_scan_start(&wifi_scan_config, true);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start scan for WiFi network scan: %s", esp_err_to_name(err));
    return nullptr;
  }
  uint16_t ap_count = 0;
  err = esp_wifi_scan_get_ap_num(&ap_count);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "%s", esp_err_to_name(err));
    return nullptr;
  }
  if (ap_count == 0) {
    ESP_LOGI(TAG, "can not search wifi ap");
    return nullptr;
  }

  wifi_ap_record_t* ap_result = calloc(ap_count, sizeof(*ap_result));
  if (ap_result == nullptr) {
    ESP_LOGE(TAG, "cannot allocated memory");
    return nullptr;
  }

  ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_result));

  for (int i = 0; i < ap_count; i++) {
    const auto ap_record = ap_result[i];
    ESP_LOGI(TAG, "SSID: %-16s | RSSI: %d dBm | Channel: %d", ap_record.ssid, ap_record.rssi, ap_record.primary);
  }

  *results_count = ap_count;
  return ap_result;
}

bool wifi_exists_ap(const char* ssid, const wifi_ap_record_t* scan_result, const size_t scan_count) {
  if (ssid == nullptr || scan_result == nullptr || scan_count == 0) return false;

  for (size_t i = 0; i < scan_count; i++) {
    if (strcmp(ssid, (const char*)scan_result[i].ssid) == 0) return true;
  }
  return false;
}

bool load_wifi_info(const char* load_path, struct custom_wifi_info_t* wifi_info) {
  static constexpr long kWifiInfoMaxFileSize = 1024;
  static const char* kKeySSID = "ssid";
  static const char* kKeyPassword = "password";

  if (load_path == nullptr || wifi_info == nullptr) return false;
  [[gnu::cleanup(auto_fclose)]] FILE* file = fopen(load_path, "r");
  if (file == nullptr) {
    ESP_LOGE(TAG, "Failed to open %s", load_path);
    return false;
  }
  fseek(file, 0, SEEK_END);
  auto file_size = ftell(file);
  if (file_size > kWifiInfoMaxFileSize) {
    ESP_LOGE(TAG, "File size is too large: %ld", file_size);
    return false;
  }
  fseek(file, 0, SEEK_SET);
  [[gnu::cleanup(auto_free)]] char* buffer = malloc(file_size);
  if (buffer == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate memory for file");
    return false;
  }
  fread(buffer, 1, file_size, file);
  cJSON* json = cJSON_Parse(buffer);
  if (json == nullptr) {
    const char* error_ptr = cJSON_GetErrorPtr();
    if (error_ptr) {
      ESP_LOGE(TAG, "JSON parse error: %s", error_ptr);
    } else {
      ESP_LOGE(TAG, "Failed to allocate memory for json");
    }
    return false;
  }
  const cJSON* ssid = cJSON_GetObjectItemCaseSensitive(json, kKeySSID);
  const cJSON* password = cJSON_GetObjectItemCaseSensitive(json, kKeyPassword);
  if (ssid != nullptr && password != nullptr) {
    wifi_info->ssid = strdup(ssid->valuestring);
    wifi_info->password = strdup(password->valuestring);
  }

  cJSON_Delete(json);
  return true;
}

static void event_handler_wifi(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
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
        const wifi_event_sta_disconnected_t* disconnected = (wifi_event_sta_disconnected_t*)event_data;
        const uint8_t reason = disconnected->reason;
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

        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        break;
      }
    }
  } else if (event_base == IP_EVENT) {
    switch (event_id) {
      case IP_EVENT_STA_GOT_IP: {
        const auto got_ip = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "WIFI GOT IP: " IPSTR, IP2STR(&got_ip->ip_info.ip));

        xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        break;
      }
      case IP_EVENT_STA_LOST_IP: {
        ESP_LOGI(TAG, "WIFI LOST IP");

        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);

        break;
      }
      default: {
        ESP_LOGI(TAG, "WIFI IP Event: %d", event_id);
        break;
      }
    }
  }
}

bool wifi_connect(const char* ssid, const char* password) {
  wifi_config_t wifi_config = {};

  if (ssid == nullptr || password == nullptr) {
    wifi_config_t wifi_last_config = {};
    ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &wifi_last_config));

    strlcpy((char*)wifi_config.sta.ssid, (char*)wifi_last_config.sta.ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char*)wifi_config.sta.password, (char*)wifi_last_config.sta.password, sizeof(wifi_config.sta.password));
  } else {
    strlcpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
  }

  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
  ESP_LOGI(TAG, "Connecting to AP: %s", ssid);
  ESP_ERROR_CHECK(esp_wifi_connect());

  const EventBits_t connected_bits =
      xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFAIL, pdFAIL, portMAX_DELAY);

  bool wifi_connected = false;
  if (connected_bits & WIFI_CONNECTED_BIT) {
    ESP_LOGI(TAG, "Connected to AP: %s", ssid);
    wifi_connected = true;
  } else if (connected_bits & WIFI_FAIL_BIT) {
    ESP_LOGE(TAG, "Failed to connect to AP");
  }

  return wifi_connected;
}

bool wifi_sntp_sync(const char* sntp_server) {
  if (sntp_server == nullptr) {
    ESP_LOGW(TAG, "SNTP server is empty");
    return false;
  }
  if (esp_sntp_enabled()) {
    esp_netif_sntp_deinit();
  }

  esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
  config.smooth_sync = true;
  auto ret = esp_netif_sntp_init(&config);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "SNTP initialization failed: %s", esp_err_to_name(ret));
    return false;
  }
  ESP_LOGI(TAG, "wait for SNTP Synchronized...");
  ret = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10'000));
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "SNTP synchronized successfully");
    setenv("TZ", "KST-9", 1);
    tzset();
  } else {
    ESP_LOGE(TAG, "SNTP synchronization failed: %s", esp_err_to_name(ret));
    return false;
  }

  return true;
}