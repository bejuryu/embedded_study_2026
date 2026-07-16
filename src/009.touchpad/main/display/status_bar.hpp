#pragma once

#include <functional>

#include "font_set.hpp"
#include "lvgl.h"

namespace Display {

class StatusBar {
 public:
  // 모드 전환 콜백: true = 다음, false = 이전
  using NavCallback = std::function<void(bool next)>;

  void create(lv_obj_t* parent, uint8_t active_mode_index, const FontSet& fonts, NavCallback nav_cb = nullptr);

  void update_battery(uint8_t percent) const;
  void update_ble_status(bool connected) const;
  void update_wifi_status(bool connected) const;
  void update_mode_indicator(uint8_t active_mode_index);

 private:
  lv_obj_t* container_ = nullptr;

  lv_obj_t* battery_icon_  = nullptr;
  lv_obj_t* battery_label_ = nullptr;

  lv_obj_t* indicators_[3] = {};

  lv_obj_t* wifi_icon_ = nullptr;
  lv_obj_t* ble_icon_  = nullptr;

  // 내비게이션 버튼 (◀ ▶)
  lv_obj_t*   nav_btn_prev_ = nullptr;
  lv_obj_t*   nav_btn_next_ = nullptr;
  NavCallback nav_callback_;

  void create_left_section(lv_obj_t* parent, const FontSet& fonts);
  void create_center_section(lv_obj_t* parent, uint8_t active_mode_index, const FontSet& fonts);
  void create_right_section(lv_obj_t* parent);
  void update_indicator_style(uint8_t i, bool active) const;

  // LVGL 이벤트 콜백 (static)
  static void on_nav_event(lv_event_t* e);
};

}  // namespace Display
