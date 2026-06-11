#pragma once
#include <vector>

#include "esp_wifi.h"
#include "esp_wifi_types_generic.h"
#include "lvgl.h"

class Display {
 public:
  bool initialize();
  void update_status_message(const char* message) const;
  void update_wifi_scan_result(const std::vector<wifi_ap_record_t>& scan_results) const;

 private:
  lv_display_t* display_handle_ = nullptr;
  lv_obj_t* screen_handle_ = nullptr;
  lv_obj_t* label_status_handle_ = nullptr;
  lv_obj_t* table_wifi_handle_ = nullptr;
};