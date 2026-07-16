#pragma once

#include "app_state.hpp"
#include "font_set.hpp"
#include "lvgl.h"
#include "media_control.hpp"
#include "media_remote.hpp"
#include "numpad.hpp"
#include "status_bar.hpp"

// esp_lcd_touch: BLE 터치 HID 태스크에서 직접 폴링하기 위해 사용
// (CMakeLists.txt REQUIRES: esp_lcd_touch)
#include "esp_lcd_touch.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace Display {

class Context {
 public:
  Context() = default;
  ~Context();
  bool initialize();

  void update_wifi_status(bool connected);
  void update_ble_status(bool connected);
  void update_battery(uint8_t percent);

  void set_mode(AppMode mode);

  void update_media_track(const char* title, const char* artist);
  void update_media_progress(uint32_t current_ms, uint32_t total_ms);
  void set_media_album_art(const void* img_src, uint32_t w, uint32_t h);
  void update_media_volume(uint8_t volume);
  void update_media_play_state(const std::string& state);

  void set_calculator_result(const char* result) { numpad_.set_result(result); }
  void set_calculator_history(const char* history) { numpad_.set_history(history); }

  void add_trackpad_point(AppMode mode, uint8_t finger_id, uint16_t x, uint16_t y) {
    if (mode == AppMode::MEDIA_CONTROL) {
      media_control_.add_trackpad_point(finger_id, x, y);
    } else if (mode == AppMode::MEDIA_REMOTE) {
      media_remote_.add_trackpad_point(finger_id, x, y);
    }
  }
  void clear_trackpad_trail(AppMode mode, uint8_t finger_id) {
    if (mode == AppMode::MEDIA_CONTROL) {
      media_control_.clear_trackpad_trail(finger_id);
    } else if (mode == AppMode::MEDIA_REMOTE) {
      media_remote_.clear_trackpad_trail(finger_id);
    }
  }

 private:
  lv_display_t* display_handle_                              = nullptr;
  lv_obj_t*     tileview_                                    = nullptr;
  lv_obj_t*     tiles_[static_cast<uint8_t>(AppMode::COUNT)] = {};

  StatusBar    status_bar_;
  MediaControl media_control_;
  MediaRemote  media_remote_;
  Numpad       numpad_;

  FontSet fonts_;

  void*  font_buffer_      = nullptr;
  size_t font_buffer_size_ = 0;

  // BLE 상태 폴링 타이머 — LVGL 태스크 컨텍스트에서 500ms마다 BLE 상태를 읽어
  // 상태바 아이콘을 갱신합니다.
  lv_timer_t* ble_status_timer_ = nullptr;

  // 현재 활성 모드 인덱스 (내비게이션 버튼에서 이전/다음 계산에 사용)
  uint8_t current_mode_index_ = 0;

  // ─── 터치 HID 태스크 ───
  // ST7123 터치 패널에서 직접 읽어 BLE HID PTP 보고서를 전송합니다.
  // LVGL 입력 드라이버와 별개로 동작하여 멀티터치를 지원합니다.
  esp_lcd_touch_handle_t touch_handle_          = nullptr;
  TaskHandle_t           touch_hid_task_handle_ = nullptr;
  static void            touch_hid_task(void* arg);

  void load_fonts();
  void init_ui_tree();

  static void tileview_value_changed_cb(lv_event_t* e);
  static void on_ble_status_timer(lv_timer_t* t);
};

}  // namespace Display
