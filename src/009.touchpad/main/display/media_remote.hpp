#pragma once

#include <atomic>
#include <string>

#include "font_set.hpp"
#include "lvgl.h"
#include "trackpad.hpp"

namespace Display {

class MediaRemote {
 public:
  ~MediaRemote();
  void initialize(lv_obj_t* parent_tile, const FontSet& fonts);

  void update_track_info(const char* title, const char* artist) const;
  void update_progress(uint32_t current_ms, uint32_t total_ms) const;
  void set_album_art(const void* img_src, uint32_t w, uint32_t h) const;
  void update_volume(uint8_t volume) const;
  void update_play_state(const std::string& state) const;
  void close_dropdown();

  void add_trackpad_point(uint8_t finger_id, uint16_t x, uint16_t y) { trackpad_.add_trail_point(finger_id, x, y); }
  void clear_trackpad_trail(uint8_t finger_id) { trackpad_.clear_trail(finger_id); }

 private:
  lv_obj_t* top_container_ = nullptr;
  lv_obj_t* album_art_img_ = nullptr;
  TrackPad  trackpad_;

  lv_obj_t* title_shadow_  = nullptr;
  lv_obj_t* title_label_   = nullptr;
  lv_obj_t* artist_shadow_ = nullptr;
  lv_obj_t* artist_label_  = nullptr;
  lv_obj_t* heart_icon_    = nullptr;

  lv_obj_t* prog_slider_  = nullptr;
  lv_obj_t* time_current_ = nullptr;
  lv_obj_t* time_total_   = nullptr;

  lv_obj_t* btn_shuffle_ = nullptr;
  lv_obj_t* btn_prev_    = nullptr;
  lv_obj_t* btn_play_    = nullptr;
  lv_obj_t* btn_next_    = nullptr;

  // repeat 버튼은 DLNA 기기 탐색/캐스팅 드롭다운 팝업 버튼으로 대체
  lv_obj_t* btn_renderer_     = nullptr;
  lv_obj_t* speaker_dropdown_ = nullptr;

  // 볼륨 제어 위젯
  lv_obj_t* volume_slider_ = nullptr;
  lv_obj_t* volume_icon_   = nullptr;

  // 비동기 동시성 제어 락
  mutable bool        volume_lock_       = false;
  mutable bool        play_lock_         = false;
  mutable lv_timer_t* volume_lock_timer_ = nullptr;
  mutable lv_timer_t* play_lock_timer_   = nullptr;

  // 재생 상태 및 로딩 표시 제어
  mutable std::string last_play_state_   = "STOPPED";
  mutable lv_obj_t*   buffering_spinner_ = nullptr;
  mutable lv_obj_t*   scan_spinner_      = nullptr;
  mutable lv_obj_t*   scan_label_        = nullptr;
  mutable lv_timer_t* scan_delay_timer_  = nullptr;

  mutable bool shuffle_enabled_ = false;
  mutable bool repeat_enabled_  = false;

  mutable void*          current_art_buffer_ = nullptr;
  mutable lv_image_dsc_t album_art_dsc_{};

  // 플레이타임 추적 및 가상 타이머
  mutable lv_timer_t* progress_timer_      = nullptr;
  mutable uint32_t    current_progress_ms_ = 0;
  mutable uint32_t    total_duration_ms_   = 0;

  void create_gradient_overlay(lv_obj_t* parent);
  void create_track_info(lv_obj_t* parent, const FontSet& fonts);
  void create_progress_bar(lv_obj_t* parent, const FontSet& fonts);
  void create_controls(lv_obj_t* parent);
  void create_volume_slider(lv_obj_t* parent);

  static void volume_slider_event_cb(lv_event_t* e);
  static void control_button_event_cb(lv_event_t* e);
  static void renderer_button_event_cb(lv_event_t* e);
  static void dropdown_event_cb(lv_event_t* e);

  static void volume_lock_timeout_cb(lv_timer_t* t);
  static void play_lock_timeout_cb(lv_timer_t* t);
  static void progress_timer_cb(lv_timer_t* t);
  static void scan_delay_timer_cb(lv_timer_t* t);

  const FontSet* fonts_ = nullptr;
};

}  // namespace Display
