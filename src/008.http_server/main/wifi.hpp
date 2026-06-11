#pragma once

#include <string>
#include <vector>

#include "esp_event.h"
#include "esp_wifi.h"

class WiFi {
 public:
  bool initialize_station();
  void finalize_station();

  void connect(const std::string& ssid, const std::string& password);
  void disconnect();

  [[nodiscard]] bool is_connected() const;
  [[nodiscard]] bool is_connecting() const;

  [[nodiscard]] std::string get_ip_v4_address() const;
  [[nodiscard]] std::string get_connected_ssid() const;
  std::vector<wifi_ap_record_t> scan();
  static bool exist_ssid(const std::string& ssid, const std::vector<wifi_ap_record_t>& scan_results);

 private:
  EventGroupHandle_t s_wifi_event_group = nullptr;
  bool is_connecting_ = false;
  std::string connected_ssid_;
  static void event_handler_wifi(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
};