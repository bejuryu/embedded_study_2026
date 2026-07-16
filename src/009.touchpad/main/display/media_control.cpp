#include "media_control.hpp"

#include "ble/ble_hid_report.hpp"
#include "constants.hpp"
#include "ui_icons.hpp"

namespace Display {

namespace {

enum MediaButton : uint8_t { BTN_PREV, BTN_PLAY_PAUSE, BTN_NEXT, BTN_VOL_DOWN, BTN_MUTE, BTN_VOL_UP, BTN_COUNT };

constexpr const char* kButtonLabels[BTN_COUNT] = {"PREV", "PLAY/PAUSE", "NEXT", "VOL -", "MUTE", "VOL +"};

struct MediaButtonDef {
  lv_draw_buf_t* Icons::Cache::* member;
  const char*                    fallback;
};

const MediaButtonDef kMediaButtons[BTN_COUNT] = {{&Icons::Cache::prev, LV_SYMBOL_PREV}, {&Icons::Cache::play_pause, LV_SYMBOL_PLAY "/" LV_SYMBOL_PAUSE},
                                                 {&Icons::Cache::next, LV_SYMBOL_NEXT}, {&Icons::Cache::vol_down, LV_SYMBOL_VOLUME_MID},
                                                 {&Icons::Cache::mute, LV_SYMBOL_MUTE}, {&Icons::Cache::vol_up, LV_SYMBOL_VOLUME_MAX}};

lv_draw_buf_t* get_icon_buf(uint8_t index) {
  if (index >= BTN_COUNT) return nullptr;
  return Icons::cache.*(kMediaButtons[index].member);
}

const char* get_fallback_symbol(uint8_t index) {
  if (index >= BTN_COUNT) return "";
  return kMediaButtons[index].fallback;
}
// ─── 미디어 버튼 → BLE Consumer Key 매핑 ───
// send_consumer_report() 비트마스크: Bit0=Play/Pause Bit1=Next Bit2=Prev
//                                   Bit3=VolUp Bit4=VolDown Bit5=Mute
static constexpr uint8_t kMediaMasks[BTN_COUNT] = {
    0x04,  // BTN_PREV       → Bit2: Previous Track
    0x01,  // BTN_PLAY_PAUSE → Bit0: Play/Pause
    0x02,  // BTN_NEXT       → Bit1: Next Track
    0x10,  // BTN_VOL_DOWN   → Bit4: Volume Down
    0x20,  // BTN_MUTE       → Bit5: Mute
    0x08,  // BTN_VOL_UP     → Bit3: Volume Up
};

static void on_media_btn_clicked(lv_event_t* e) {
  // user_data: &kMediaMasks[index] (static constexpr → 영구 주소)
  const auto* mask = static_cast<const uint8_t*>(lv_event_get_user_data(e));
  if (!mask) return;
  // send_consumer_key: 버튼 누름(50ms) + 자동 해제 → key stuck 방지
  Ble::send_consumer_key(*mask);
}

}  // namespace

void MediaControl::initialize(lv_obj_t* parent_tile, const FontSet& fonts) {
  namespace UI_Mode = UI::Mode_MediaControl;

  top_container_ = UI::create_clean_obj(parent_tile);
  lv_obj_set_size(top_container_, UI::Global::SCREEN_W, UI_Mode::TOP_H);
  lv_obj_set_style_bg_color(top_container_, UI_Mode::TOP_BG, 0);
  lv_obj_set_style_bg_opa(top_container_, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_top(top_container_, UI_Mode::TOP_PAD_TOP, 0);
  lv_obj_set_style_pad_hor(top_container_, UI_Mode::TOP_PAD_SIDE, 0);
  lv_obj_set_style_pad_bottom(top_container_, UI_Mode::TOP_PAD_BOTTOM, 0);
  lv_obj_set_flex_flow(top_container_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_border_width(top_container_, 0, 0);

  create_button_grid(top_container_, fonts);
  trackpad_.initialize(parent_tile, fonts);
}

void MediaControl::create_button_grid(lv_obj_t* parent, const FontSet& fonts) {
  namespace UI_Mode = UI::Mode_MediaControl;

  lv_obj_t* grid = UI::create_clean_obj(parent);
  lv_obj_set_flex_grow(grid, 1);
  lv_obj_set_width(grid, lv_pct(100));
  lv_obj_set_style_pad_top(grid, UI_Mode::GRID_MARGIN_TOP, 0);
  lv_obj_set_style_border_width(grid, 0, 0);

  static const int32_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static const int32_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);
  lv_obj_set_style_pad_column(grid, UI_Mode::GRID_GAP, 0);
  lv_obj_set_style_pad_row(grid, UI_Mode::GRID_GAP, 0);
  lv_obj_set_layout(grid, LV_LAYOUT_GRID);

  for (uint8_t i = 0; i < 6; ++i) {
    const bool is_play = (i == 1);  // 2번째 버튼 = Play/Pause (강조)
    buttons_[i]        = create_button(grid, kButtonLabels[i], i, is_play, fonts);

    const uint8_t col = i % UI_Mode::GRID_COLS;
    const uint8_t row = i / UI_Mode::GRID_COLS;
    lv_obj_set_grid_cell(buttons_[i], LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH, row, 1);
  }
}

lv_obj_t* MediaControl::create_button(lv_obj_t* parent, const char* label_text, uint8_t index, bool is_play_btn, const FontSet& fonts) {
  namespace UI_Mode = UI::Mode_MediaControl;

  lv_obj_t* btn = UI::create_clean_obj(parent);

  const auto bg_color = is_play_btn ? UI_Mode::PLAY_BTN_BG : UI_Mode::BTN_BG;
  lv_obj_set_style_bg_color(btn, bg_color, 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);

  const auto border_color = is_play_btn ? UI_Mode::PLAY_BTN_BORDER : UI_Mode::BTN_BORDER_COLOR;
  lv_obj_set_style_border_width(btn, UI_Mode::BTN_BORDER_W, 0);
  lv_obj_set_style_border_color(btn, border_color, 0);

  lv_obj_set_style_radius(btn, UI_Mode::BTN_RADIUS, 0);

  lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_draw_buf_t*   icon_buf   = get_icon_buf(index);
  const lv_color_t tint_color = is_play_btn ? UI_Mode::PLAY_BTN_ICON_COLOR : UI_Mode::BTN_ICON_COLOR;

  lv_obj_t* icon_widget = UI::create_icon_or_fallback(btn, icon_buf, get_fallback_symbol(index), UI_Mode::BTN_ICON_SIZE, tint_color);
  lv_obj_set_style_pad_bottom(icon_widget, UI_Mode::BTN_ICON_MARGIN_B, 0);
  icons_[index] = icon_widget;

  labels_[index] = lv_label_create(btn);
  lv_label_set_text(labels_[index], label_text);
  const auto label_color = is_play_btn ? UI_Mode::PLAY_BTN_LABEL_COLOR : UI_Mode::BTN_LABEL_COLOR;
  lv_obj_set_style_text_color(labels_[index], label_color, 0);
  lv_obj_set_style_text_letter_space(labels_[index], UI_Mode::BTN_LABEL_LETTER_SPACE, 0);
  set_obj_font_if_exists(labels_[index], fonts.font_16);

  lv_obj_set_style_bg_color(btn, lv_color_hex(0x2a2a2a), (lv_style_selector_t)LV_PART_MAIN | (lv_style_selector_t)LV_STATE_PRESSED);

  // BLE Consumer Key 이벤트 등록
  // kMediaMasks는 static constexpr이므로 &kMediaMasks[index]는 프로그램 수명 내내 유효
  lv_obj_add_event_cb(btn, on_media_btn_clicked, LV_EVENT_CLICKED, const_cast<void*>(static_cast<const void*>(&kMediaMasks[index])));

  return btn;
}

}  // namespace Display
