#include "wifi.hpp"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

static auto* TAG = "WIFI";
static constexpr auto WIFI_CONNECTED_BIT = BIT0;
static constexpr auto WIFI_FAIL_BIT = BIT1;

bool Wifi::initialize_station() {
  s_wifi_event_group = xEventGroupCreate();

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  const auto station_network_interface = esp_netif_create_default_wifi_sta();
  assert(station_network_interface != nullptr);

  const wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));

  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler_wifi, this));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler_wifi, this));

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());
  return true;
}

void Wifi::finalize_station() {
  esp_wifi_stop();
}

void Wifi::connect(const std::string& ssid, const std::string& password) {
  if (ssid.empty()) {
    ESP_LOGI(TAG, "SSID is empty in config. Trying to connect using previously saved credentials in NVS...");
    esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to start connection with saved credentials: %s", esp_err_to_name(ret));
    }
  } else {
    wifi_config_t wifi_config = {};
    size_t ssid_len = ssid.copy((char*)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid) - 1);
    wifi_config.sta.ssid[ssid_len] = '\0';

    size_t pw_len = password.copy((char*)wifi_config.sta.password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.password[pw_len] = '\0';

    if (password.empty()) {
      wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    } else {
      wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA_PSK;
    }
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_LOGI(TAG, "New credentials provided. Saving and connecting to AP SSID: [%s]...", wifi_config.sta.ssid);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_connect());
  }
}

void Wifi::disconnect() {}

bool Wifi::is_connected() const {
  if (s_wifi_event_group == nullptr) return false;
  return (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT) != 0;
}

void Wifi::event_handler_wifi(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
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
        const wifi_event_sta_disconnected_t* disconnected = static_cast<wifi_event_sta_disconnected_t*>(event_data);
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
        if (arg != nullptr) {
          const auto* wifi_args = static_cast<Wifi*>(arg);
          xEventGroupClearBits(wifi_args->s_wifi_event_group, WIFI_CONNECTED_BIT);
          xEventGroupSetBits(wifi_args->s_wifi_event_group, WIFI_FAIL_BIT);
        } else {
          ESP_LOGE(TAG, "wifi args is null");
        }
        break;
      }
    }
  } else if (event_base == IP_EVENT) {
    switch (event_id) {
      case IP_EVENT_STA_GOT_IP: {
        const auto* got_ip = static_cast<ip_event_got_ip_t*>(event_data);
        ESP_LOGI(TAG, "WIFI GOT IP: " IPSTR, IP2STR(&got_ip->ip_info.ip));

        if (arg != nullptr) {
          const auto* wifi_args = static_cast<Wifi*>(arg);
          xEventGroupClearBits(wifi_args->s_wifi_event_group, WIFI_FAIL_BIT);
          xEventGroupSetBits(wifi_args->s_wifi_event_group, WIFI_CONNECTED_BIT);
        } else {
          ESP_LOGE(TAG, "wifi args is null");
        }
        break;
      }
      case IP_EVENT_STA_LOST_IP: {
        ESP_LOGI(TAG, "WIFI LOST IP");

        if (arg != nullptr) {
          const auto* wifi_args = static_cast<Wifi*>(arg);
          xEventGroupClearBits(wifi_args->s_wifi_event_group, WIFI_CONNECTED_BIT);
          xEventGroupSetBits(wifi_args->s_wifi_event_group, WIFI_FAIL_BIT);
        } else {
          ESP_LOGE(TAG, "wifi args is null");
        }

        break;
      }
      default: {
        ESP_LOGI(TAG, "WIFI IP Event: %d", event_id);
        break;
      }
    }
  }
}