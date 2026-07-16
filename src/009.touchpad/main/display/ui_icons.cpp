#include "ui_icons.hpp"

#include <cstdio>
#include <cstring>
#include <optional>
#include <string>

#include "draw/lv_draw_vector.h"
#include "esp_log.h"
#include "nanosvg.h"

static const char* TAG = "ui_icons";

namespace Display::Icons {

Cache cache;

#if LV_USE_VECTOR_GRAPHIC

namespace {

// ==========================================
// 1. Rendering Configuration Constants
// ==========================================
static constexpr float SVG_DEFAULT_DPI = 96.0f;

// --- Target Render Sizes (0 = auto extract from SVG source) ---
static constexpr int SIZE_AUTO       = 0;
static constexpr int SIZE_PREV       = 48;
static constexpr int SIZE_NEXT       = 48;
static constexpr int SIZE_PLAY       = 36;
static constexpr int SIZE_PAUSE      = 36;
static constexpr int SIZE_PLAY_PAUSE = 48;
static constexpr int SIZE_VOL_DOWN   = 48;
static constexpr int SIZE_VOL_UP     = 48;
static constexpr int SIZE_MUTE       = 48;
static constexpr int SIZE_SHUFFLE    = 32;
static constexpr int SIZE_REPEAT     = 32;
static constexpr int SIZE_HEART      = 36;
static constexpr int SIZE_BACKSPACE  = 36;
static constexpr int SIZE_ENTER      = 36;

// --- Specialized Overlay Colors ---
// Theme green color (0x1DB954)
static const lv_color_t COLOR_THEME_GREEN = lv_color_make(29, 185, 84);

// C23 #embed SVG resources with null-termination
static const char SVG_BATTERY[] = {
#embed "icons/battery.svg"

    , 0};
static const char SVG_WIFI[] = {
#embed "icons/wifi.svg"

    , 0};
static const char SVG_BLE[] = {
#embed "icons/ble.svg"

    , 0};
static const char SVG_PREV[] = {
#embed "icons/prev.svg"

    , 0};
static const char SVG_NEXT[] = {
#embed "icons/next.svg"

    , 0};
static const char SVG_PLAY[] = {
#embed "icons/play.svg"

    , 0};
static const char SVG_PAUSE[] = {
#embed "icons/pause.svg"

    , 0};
static const char SVG_PLAY_PAUSE[] = {
#embed "icons/play_pause.svg"

    , 0};
static const char SVG_VOL_DOWN[] = {
#embed "icons/vol_down.svg"

    , 0};
static const char SVG_VOL_UP[] = {
#embed "icons/vol_up.svg"

    , 0};
static const char SVG_MUTE[] = {
#embed "icons/mute.svg"

    , 0};
static const char SVG_SHUFFLE[] = {
#embed "icons/shuffle.svg"

    , 0};
static const char SVG_REPEAT[] = {
#embed "icons/repeat.svg"

    , 0};
static const char SVG_HEART[] = {
#embed "icons/heart.svg"

    , 0};
static const char SVG_BACKSPACE[] = {
#embed "icons/backspace.svg"

    , 0};
static const char SVG_ENTER[] = {
#embed "icons/enter.svg"

    , 0};

void build_scaled_path(lv_vector_path_t* lv_path, NSVGpath* nsvg_path, float scale_x, float scale_y) {
  lv_fpoint_t p0 = {nsvg_path->pts[0] * scale_x, nsvg_path->pts[1] * scale_y};
  lv_vector_path_move_to(lv_path, &p0);

  for (int i = 1; i < nsvg_path->npts; i += 3) {
    float* p = &nsvg_path->pts[i * 2];
    // Bounds checking to prevent out-of-bounds array access
    if (i * 2 + 5 < nsvg_path->npts * 2) {
      lv_fpoint_t cp1   = {p[0] * scale_x, p[1] * scale_y};
      lv_fpoint_t cp2   = {p[2] * scale_x, p[3] * scale_y};
      lv_fpoint_t p_end = {p[4] * scale_x, p[5] * scale_y};
      lv_vector_path_cubic_to(lv_path, &cp1, &cp2, &p_end);
    }
  }

  if (nsvg_path->closed) {
    lv_vector_path_close(lv_path);
  }
}

void render_svg_internal(lv_draw_buf_t* draw_buf, NSVGimage* image, int w, int h, std::optional<lv_color_t> custom_color) {
  lv_obj_t* canvas = lv_canvas_create(lv_screen_active());
  if (canvas == nullptr) return;
  lv_canvas_set_draw_buf(canvas, draw_buf);
  lv_canvas_fill_bg(canvas, lv_color_hex(0xFFFFFF), LV_OPA_TRANSP);

  lv_layer_t layer;
  lv_canvas_init_layer(canvas, &layer);
  lv_draw_vector_dsc_t* dsc = lv_draw_vector_dsc_create(&layer);

  float scale_x = static_cast<float>(w) / image->width;
  float scale_y = static_cast<float>(h) / image->height;

  for (NSVGshape* shape = image->shapes; shape != nullptr; shape = shape->next) {
    lv_vector_path_t* lv_path = lv_vector_path_create(LV_VECTOR_PATH_QUALITY_MEDIUM);
    if (lv_path == nullptr) continue;

    for (NSVGpath* path = shape->paths; path != nullptr; path = path->next) {
      build_scaled_path(lv_path, path, scale_x, scale_y);
    }

    // Process Fill using Ternary Operator for simple conditional logic
    if (shape->fill.type == NSVG_PAINT_COLOR) {
      lv_color_t color = custom_color.has_value() ? custom_color.value() : lv_color_hex(shape->fill.color & 0xFFFFFF);
      uint8_t    opa   = custom_color.has_value() ? static_cast<uint8_t>(LV_OPA_COVER) : static_cast<uint8_t>((shape->fill.color >> 24) & 0xFF);
      lv_draw_vector_dsc_set_fill_color(dsc, color);
      lv_draw_vector_dsc_set_fill_opa(dsc, opa);
    } else {
      lv_draw_vector_dsc_set_fill_opa(dsc, LV_OPA_TRANSP);
    }

    // Process Stroke using Ternary Operator for simple conditional logic
    if (shape->stroke.type == NSVG_PAINT_COLOR) {
      lv_color_t color = custom_color.value_or(lv_color_hex(shape->stroke.color & 0xFFFFFF));
      uint8_t    opa   = custom_color.has_value() ? static_cast<uint8_t>(LV_OPA_COVER) : static_cast<uint8_t>((shape->stroke.color >> 24) & 0xFF);
      lv_draw_vector_dsc_set_stroke_color(dsc, color);
      lv_draw_vector_dsc_set_stroke_opa(dsc, opa);
      lv_draw_vector_dsc_set_stroke_width(dsc, shape->strokeWidth * scale_x);
    } else {
      lv_draw_vector_dsc_set_stroke_width(dsc, 0.0f);
    }

    lv_draw_vector_dsc_add_path(dsc, lv_path);
    lv_vector_path_delete(lv_path);
  }

  lv_draw_vector(dsc);
  lv_draw_vector_dsc_delete(dsc);
  lv_canvas_finish_layer(canvas, &layer);
  lv_obj_delete(canvas);
}

lv_draw_buf_t* create_and_render_icon(const char* svg_src, const char* name, int target_size = 0, std::optional<lv_color_t> custom_color = std::nullopt) {
  // Writable copy in RAM via C++ std::string to prevent 'Store access fault' crash on read-only Flash
  std::string temp_src(svg_src);

  NSVGimage* image = nsvgParse(temp_src.data(), "px", SVG_DEFAULT_DPI);

  if (image == nullptr) {
    ESP_LOGE(TAG, "[%s] Failed to parse SVG XML data!", name);
    return nullptr;
  }

  int final_size = (target_size > 0) ? target_size : static_cast<int>(image->width);

  lv_draw_buf_t* buf = lv_draw_buf_create(final_size, final_size, LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO);
  if (buf) {
    lv_draw_buf_clear(buf, nullptr);
    render_svg_internal(buf, image, final_size, final_size, custom_color);
  }

  nsvgDelete(image);
  return buf;
}

}  // namespace

#endif  // LV_USE_VECTOR_GRAPHIC

void init_cache() {
#if LV_USE_VECTOR_GRAPHIC
  ESP_LOGI(TAG, "Starting init_cache under LV_USE_VECTOR_GRAPHIC (Nanosvg)");

  struct IconLoadDef {
    lv_draw_buf_t* Cache::*   member;
    const char*               svg_src;
    const char*               name;
    int                       size;
    std::optional<lv_color_t> color = std::nullopt;
  };

  const IconLoadDef defs[] = {{&Cache::battery, SVG_BATTERY, "battery", SIZE_AUTO},
                              {&Cache::wifi, SVG_WIFI, "wifi", SIZE_AUTO},
                              {&Cache::ble, SVG_BLE, "ble", SIZE_AUTO},
                              {&Cache::prev, SVG_PREV, "prev", SIZE_PREV},
                              {&Cache::next, SVG_NEXT, "next", SIZE_NEXT},
                              {&Cache::play, SVG_PLAY, "play", SIZE_PLAY},
                              {&Cache::pause, SVG_PAUSE, "pause", SIZE_PAUSE},
                              {&Cache::play_pause, SVG_PLAY_PAUSE, "play_pause", SIZE_PLAY_PAUSE},
                              {&Cache::vol_down, SVG_VOL_DOWN, "vol_down", SIZE_VOL_DOWN},
                              {&Cache::vol_up, SVG_VOL_UP, "vol_up", SIZE_VOL_UP},
                              {&Cache::mute, SVG_MUTE, "mute", SIZE_MUTE},
                              {&Cache::shuffle, SVG_SHUFFLE, "shuffle", SIZE_SHUFFLE},
                              {&Cache::repeat, SVG_REPEAT, "repeat", SIZE_REPEAT},
                              {&Cache::heart, SVG_HEART, "heart", SIZE_HEART, COLOR_THEME_GREEN},
                              {&Cache::backspace, SVG_BACKSPACE, "backspace", SIZE_BACKSPACE},
                              {&Cache::enter, SVG_ENTER, "enter", SIZE_ENTER}};

  for (const auto& def : defs) {
    auto* buf = create_and_render_icon(def.svg_src, def.name, def.size, def.color);
    if (buf == nullptr) {
      ESP_LOGE(TAG, "Failed to render SVG icon: %s", def.name);
    }
    cache.*(def.member) = buf;
  }

  ESP_LOGI(TAG, "init_cache completed");
#else
  ESP_LOGW(TAG, "LV_USE_VECTOR_GRAPHIC is not enabled, skipping init_cache");
#endif
}

}  // namespace Display::Icons
// Force re-compile trigger for embed assets update
