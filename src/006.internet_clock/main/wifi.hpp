#pragma once
#include <string>
#include "esp_event.h"

class Wifi {
 public:
  bool initialize_station();
  void finalize_station();

  void connect(const std::string& ssid, const std::string& password);
  void disconnect();

  bool is_connected() const;

 private:
  EventGroupHandle_t s_wifi_event_group = nullptr;
  static void event_handler_wifi(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
};
