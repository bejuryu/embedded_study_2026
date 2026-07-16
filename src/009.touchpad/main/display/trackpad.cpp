#include "trackpad.hpp"

#include "constants.hpp"

namespace Display {

void TrackPad::initialize(lv_obj_t* parent_tile, const FontSet& fonts) {
  namespace TP = UI::Touchpad;

  container_ = UI::create_clean_obj(parent_tile);
  lv_obj_set_size(container_, UI::Global::SCREEN_W, TP::HEIGHT);
  lv_obj_set_style_bg_color(container_, TP::BG, 0);
  lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(container_, 0, 0);
  lv_obj_add_flag(container_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_set_style_clip_corner(container_, true, 0);

  for (uint8_t i = 0; i < 4; ++i) {
    create_corner(container_, i);
  }

  hint_label_ = lv_label_create(container_);
  lv_label_set_text(hint_label_, "TOUCHPAD");
  lv_obj_set_style_text_color(hint_label_, TP::HINT_COLOR, 0);
  lv_obj_set_style_text_letter_space(hint_label_, TP::HINT_LETTER_SPACE, 0);
  set_obj_font_if_exists(hint_label_, fonts.font_24);
  lv_obj_align(hint_label_, LV_ALIGN_CENTER, 0, 0);

  for (uint8_t i = 0; i < 5; ++i) {
    lines_[i] = lv_line_create(container_);
    lv_obj_add_flag(lines_[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_line_color(lines_[i], UI::Global::COLOR_WHITE, 0);
    lv_obj_set_style_line_width(lines_[i], 4, 0);
    lv_obj_set_style_line_rounded(lines_[i], true, 0);
    point_counts_[i] = 0;
  }
}

namespace {
struct CornerDef {
  lv_align_t       align;
  lv_border_side_t side;
  int16_t          offset_x;
  int16_t          offset_y;
};
}  // namespace

void TrackPad::create_corner(lv_obj_t* parent, uint8_t index) {
  namespace TP = UI::Touchpad;
  if (index >= 4) return;

  static constexpr CornerDef kCorners[] = {
      {LV_ALIGN_TOP_LEFT, static_cast<lv_border_side_t>(LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT), TP::CORNER_OFFSET, TP::CORNER_OFFSET},
      {LV_ALIGN_TOP_RIGHT, static_cast<lv_border_side_t>(LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_RIGHT), -TP::CORNER_OFFSET, TP::CORNER_OFFSET},
      {LV_ALIGN_BOTTOM_LEFT, static_cast<lv_border_side_t>(LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_LEFT), TP::CORNER_OFFSET, -TP::CORNER_OFFSET},
      {LV_ALIGN_BOTTOM_RIGHT, static_cast<lv_border_side_t>(LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_RIGHT), -TP::CORNER_OFFSET, -TP::CORNER_OFFSET}};

  const auto& def = kCorners[index];
  corners_[index] = UI::create_clean_obj(parent);
  lv_obj_set_size(corners_[index], TP::CORNER_SIZE, TP::CORNER_SIZE);
  lv_obj_set_style_bg_opa(corners_[index], LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(corners_[index], TP::CORNER_BORDER_W, 0);
  lv_obj_set_style_border_color(corners_[index], TP::CORNER_BORDER_COLOR, 0);

  lv_obj_set_style_border_side(corners_[index], def.side, 0);
  lv_obj_align(corners_[index], def.align, def.offset_x, def.offset_y);
}

void TrackPad::add_trail_point(uint8_t finger_id, uint16_t x, uint16_t y) {
  if (finger_id >= 5) return;

  uint8_t& count = point_counts_[finger_id];

  if (count < MAX_POINTS) {
    points_[finger_id][count].x = x;
    points_[finger_id][count].y = y;
    count++;
  } else {
    for (int i = 1; i < MAX_POINTS; ++i) {
      points_[finger_id][i - 1] = points_[finger_id][i];
    }
    points_[finger_id][MAX_POINTS - 1].x = x;
    points_[finger_id][MAX_POINTS - 1].y = y;
  }

  if (count >= 2) {
    lv_obj_remove_flag(lines_[finger_id], LV_OBJ_FLAG_HIDDEN);
    lv_line_set_points(lines_[finger_id], points_[finger_id], count);
  }
}

void TrackPad::clear_trail(uint8_t finger_id) {
  if (finger_id >= 5) return;

  lv_obj_add_flag(lines_[finger_id], LV_OBJ_FLAG_HIDDEN);
  point_counts_[finger_id] = 0;
}

}  // namespace Display
