#pragma once

#include "lvgl.h"
#include "utility.hpp"

class Display {
 public:
  Display();
  bool initialize();

  void set_system_config(const SystemConfig& config);

  // 시간 갱신 (매초 타이머에서 호출 — 기본 lock=false)
  void update_time(int hour, int minute, int second, bool lock = false);

  // 날짜·요일 갱신
  void update_date(int year, int month, int day, int weekday, bool lock = false);

  // 날씨 정보 갱신
  void update_weather(const char* city, const char* condition, int temp_celsius, bool lock = false);

 private:
  lv_display_t* display_handle_ = nullptr;
  lv_obj_t* screen_obj_         = nullptr;
  lv_obj_t* background_image_   = nullptr;

  // 시간 digit 라벨 (각 자리수 분리)
  lv_obj_t* label_hour_tens_   = nullptr;
  lv_obj_t* label_hour_ones_   = nullptr;
  lv_obj_t* label_minute_tens_ = nullptr;
  lv_obj_t* label_minute_ones_ = nullptr;
  lv_obj_t* label_second_tens_ = nullptr;
  lv_obj_t* label_second_ones_ = nullptr;

  // 푸터 요소
  lv_obj_t* footer_container_   = nullptr;
  lv_obj_t* label_date_         = nullptr;
  lv_obj_t* pill_weekday_       = nullptr;
  lv_obj_t* label_weekday_      = nullptr;
  lv_obj_t* label_footer_right_ = nullptr;

  // LVGL 타이머
  lv_timer_t* clock_timer_ = nullptr;

  SystemConfig system_config_;
};