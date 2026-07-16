#pragma once

#include "font_set.hpp"
#include "lvgl.h"
#include "trackpad.hpp"

namespace Display {

class MediaControl {
 public:
  void initialize(lv_obj_t* parent_tile, const FontSet& fonts);

  void add_trackpad_point(uint8_t finger_id, uint16_t x, uint16_t y) { trackpad_.add_trail_point(finger_id, x, y); }
  void clear_trackpad_trail(uint8_t finger_id) { trackpad_.clear_trail(finger_id); }

 private:
  lv_obj_t* top_container_ = nullptr;
  TrackPad  trackpad_;

  lv_obj_t* buttons_[6] = {};
  lv_obj_t* icons_[6]   = {};
  lv_obj_t* labels_[6]  = {};

  void      create_button_grid(lv_obj_t* parent, const FontSet& fonts);
  lv_obj_t* create_button(lv_obj_t* parent, const char* label_text, uint8_t index, bool is_play_btn, const FontSet& fonts);
};

}  // namespace Display
