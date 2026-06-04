#include "weather_info.hpp"

#include <cmath>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

static const char* TAG = "WeatherInfo";

// ── WMO Weather Interpretation Codes (WMO 4677) ─────────

const char* WeatherInfo::wmo_to_text(int code) {
  if (code == 0) return "CLEAR";
  if (code <= 3) return "CLOUDY";
  if (code == 45 || code == 48) return "FOG";
  if (code >= 51 && code <= 57) return "DRIZZLE";
  if (code >= 61 && code <= 65) return "RAIN";
  if (code >= 66 && code <= 67) return "SLEET";
  if (code >= 71 && code <= 77) return "SNOW";
  if (code >= 80 && code <= 82) return "SHOWERS";
  if (code >= 85 && code <= 86) return "SNOW";
  if (code >= 95 && code <= 99) return "STORM";
  return "UNKNOWN";
}

// ── Open-Meteo API 호출 ──────────────────────────────────

WeatherData WeatherInfo::fetch(float latitude, float longitude) {
  WeatherData result{};

  // URL 조립
  char url[256];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?"
           "latitude=%.4f&longitude=%.4f&current=temperature_2m,weather_code",
           latitude, longitude);

  ESP_LOGI(TAG, "Fetching weather: %s", url);

  // HTTP 클라이언트 설정
  esp_http_client_config_t config{};
  config.url = url;
  config.method = HTTP_METHOD_GET;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.timeout_ms = 10000;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    ESP_LOGE(TAG, "Failed to init HTTP client");
    return result;
  }

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return result;
  }

  int content_length = esp_http_client_fetch_headers(client);
  int status_code = esp_http_client_get_status_code(client);

  if (status_code != 200) {
    ESP_LOGE(TAG, "HTTP status %d", status_code);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return result;
  }

  // 응답 읽기
  char buffer[1024] = {};
  int read_len = esp_http_client_read(client, buffer, sizeof(buffer) - 1);
  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  if (read_len <= 0) {
    ESP_LOGE(TAG, "HTTP read failed (len=%d)", read_len);
    return result;
  }
  buffer[read_len] = '\0';

  // JSON 파싱
  cJSON* root = cJSON_Parse(buffer);
  if (root == nullptr) {
    ESP_LOGE(TAG, "JSON parse failed");
    return result;
  }

  cJSON* current = cJSON_GetObjectItem(root, "current");
  if (current == nullptr) {
    ESP_LOGE(TAG, "Missing 'current' field");
    cJSON_Delete(root);
    return result;
  }

  cJSON* temp = cJSON_GetObjectItem(current, "temperature_2m");
  cJSON* code = cJSON_GetObjectItem(current, "weather_code");

  if (temp == nullptr || code == nullptr || !cJSON_IsNumber(temp) || !cJSON_IsNumber(code)) {
    ESP_LOGE(TAG, "Missing or invalid temperature/weather_code");
    cJSON_Delete(root);
    return result;
  }

  result.temperature = static_cast<int>(lroundf(static_cast<float>(temp->valuedouble)));
  result.weather_code = code->valueint;
  result.condition = wmo_to_text(result.weather_code);
  result.valid = true;

  ESP_LOGI(TAG, "Weather: %d°C, code=%d (%s)", result.temperature, result.weather_code,
           result.condition);

  cJSON_Delete(root);
  return result;
}
