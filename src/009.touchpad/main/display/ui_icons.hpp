#pragma once

#include "lvgl.h"

namespace Display::Icons {

struct Cache {
  lv_draw_buf_t* battery    = nullptr;
  lv_draw_buf_t* wifi       = nullptr;
  lv_draw_buf_t* ble        = nullptr;
  lv_draw_buf_t* prev       = nullptr;
  lv_draw_buf_t* next       = nullptr;
  lv_draw_buf_t* play       = nullptr;
  lv_draw_buf_t* pause      = nullptr;
  lv_draw_buf_t* play_pause = nullptr;
  lv_draw_buf_t* vol_down   = nullptr;
  lv_draw_buf_t* vol_up     = nullptr;
  lv_draw_buf_t* mute       = nullptr;
  lv_draw_buf_t* shuffle    = nullptr;
  lv_draw_buf_t* repeat     = nullptr;
  lv_draw_buf_t* heart      = nullptr;
  lv_draw_buf_t* backspace  = nullptr;
  lv_draw_buf_t* enter      = nullptr;
};

extern Cache cache;

void init_cache();

}  // namespace Display::Icons
