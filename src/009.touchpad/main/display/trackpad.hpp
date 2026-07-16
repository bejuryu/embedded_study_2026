#pragma once

#include "font_set.hpp"
#include "lvgl.h"

namespace Display {

class TrackPad {
 public:
  void initialize(lv_obj_t* parent_tile, const FontSet& fonts);
  void add_trail_point(uint8_t finger_id, uint16_t x, uint16_t y);
  void clear_trail(uint8_t finger_id);

 private:
  lv_obj_t* container_  = nullptr;
  lv_obj_t* hint_label_ = nullptr;
  lv_obj_t* corners_[4] = {};  // TL, TR, BL, BR
  lv_obj_t* lines_[5]   = {};  // 5 fingers

  static constexpr int MAX_POINTS             = 10;
  lv_point_precise_t   points_[5][MAX_POINTS] = {};
  uint8_t              point_counts_[5]       = {};

  void create_corner(lv_obj_t* parent, uint8_t index);
};

}  // namespace Display
