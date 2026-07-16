#pragma once

#include "lvgl.h"

namespace Display {

struct FontSet {
  lv_font_t* font_16 = nullptr;
  lv_font_t* font_18 = nullptr;
  lv_font_t* font_24 = nullptr;
  lv_font_t* font_32 = nullptr;
  lv_font_t* font_40 = nullptr;
  lv_font_t* font_48 = nullptr;
  lv_font_t* font_80 = nullptr;
};

inline void set_obj_font_if_exists(lv_obj_t* obj, lv_font_t* font) {
  if (obj && font) {
    lv_obj_set_style_text_font(obj, font, 0);
  }
}

}  // namespace Display
