#include "status_bar.hpp"

#include <cstdio>

#include "constants.hpp"
#include "ui_icons.hpp"

namespace Display {

void StatusBar::create(lv_obj_t* parent, uint8_t active_mode_index, const FontSet& fonts, NavCallback nav_cb) {
  namespace SB = UI::StatusBar;

  nav_callback_ = std::move(nav_cb);

  container_ = UI::create_clean_obj(parent);
  lv_obj_set_size(container_, UI::Global::SCREEN_W, SB::HEIGHT);
  lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_hor(container_, SB::PAD_H, 0);
  lv_obj_set_style_border_width(container_, 0, 0);

  lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(container_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  create_left_section(container_, fonts);
  create_center_section(container_, active_mode_index, fonts);
  create_right_section(container_);
}

void StatusBar::create_left_section(lv_obj_t* parent, const FontSet& fonts) {
  namespace SB = UI::StatusBar;

  lv_obj_t* left = UI::create_clean_obj(parent);
  lv_obj_set_size(left, SB::LEFT_W, SB::HEIGHT);
  lv_obj_set_flex_flow(left, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(left, SB::LEFT_GAP, 0);

  battery_icon_ = UI::create_icon_or_fallback(left, Icons::cache.battery, LV_SYMBOL_BATTERY_FULL, SB::BATTERY_ICON_SIZE, UI::Global::COLOR_WHITE);
  if (!Icons::cache.battery) {
    lv_obj_set_style_text_opa(battery_icon_, SB::TEXT_OPA, 0);
  }

  battery_label_ = lv_label_create(left);
  lv_label_set_text(battery_label_, "100%");
  lv_obj_set_style_text_color(battery_label_, UI::Global::COLOR_WHITE, 0);
  lv_obj_set_style_text_opa(battery_label_, SB::TEXT_OPA, 0);
  // font_32 사용 (FONT_SIZE = 32, 2.8mm @ 294PPI)
  set_obj_font_if_exists(battery_label_, fonts.font_32);
}

void StatusBar::create_center_section(lv_obj_t* parent, uint8_t active_mode_index, const FontSet& fonts) {
  namespace SB = UI::StatusBar;

  // 중앙 컨테이너: flex row, 가운데 정렬
  lv_obj_t* center = UI::create_clean_obj(parent);
  lv_obj_set_flex_grow(center, 1);
  lv_obj_set_height(center, SB::HEIGHT);
  lv_obj_set_flex_flow(center, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(center, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(center, SB::INDICATOR_GAP * 2, 0);

  // ─── ◀ 이전 버튼 ───
  // 터치 타겟: NAV_BTN_W(80px) × HEIGHT(120px) = 6.9mm × 10.3mm
  nav_btn_prev_ = lv_btn_create(center);
  lv_obj_remove_style_all(nav_btn_prev_);
  lv_obj_set_size(nav_btn_prev_, SB::NAV_BTN_W, SB::HEIGHT);
  lv_obj_set_style_bg_opa(nav_btn_prev_, LV_OPA_TRANSP, 0);
  {
    lv_obj_t* lbl = lv_label_create(nav_btn_prev_);
    lv_label_set_text(lbl, "\xE2\x97\x80");  // U+25C0 ◀
    lv_obj_set_style_text_color(lbl, UI::Global::COLOR_WHITE, 0);
    lv_obj_set_style_text_opa(lbl, UI::StatusBar::TEXT_OPA, 0);
    set_obj_font_if_exists(lbl, fonts.font_32);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
  }
  lv_obj_add_event_cb(nav_btn_prev_, on_nav_event, LV_EVENT_CLICKED, this);

  // ─── 인디케이터 (● ○ ○) ───
  lv_obj_t* indicators_box = UI::create_clean_obj(center);
  lv_obj_set_height(indicators_box, SB::HEIGHT);
  lv_obj_set_width(indicators_box, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(indicators_box, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(indicators_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(indicators_box, SB::INDICATOR_GAP, 0);

  for (uint8_t i = 0; i < 3; ++i) {
    indicators_[i] = UI::create_clean_obj(indicators_box);
    lv_obj_set_size(indicators_[i], SB::INDICATOR_SIZE, SB::INDICATOR_SIZE);
    lv_obj_set_style_radius(indicators_[i], LV_RADIUS_CIRCLE, 0);
    update_indicator_style(i, i == active_mode_index);
  }

  // ─── ▶ 다음 버튼 ───
  nav_btn_next_ = lv_btn_create(center);
  lv_obj_remove_style_all(nav_btn_next_);
  lv_obj_set_size(nav_btn_next_, SB::NAV_BTN_W, SB::HEIGHT);
  lv_obj_set_style_bg_opa(nav_btn_next_, LV_OPA_TRANSP, 0);
  {
    lv_obj_t* lbl = lv_label_create(nav_btn_next_);
    lv_label_set_text(lbl, "\xE2\x96\xB6");  // U+25B6 ▶
    lv_obj_set_style_text_color(lbl, UI::Global::COLOR_WHITE, 0);
    lv_obj_set_style_text_opa(lbl, UI::StatusBar::TEXT_OPA, 0);
    set_obj_font_if_exists(lbl, fonts.font_32);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
  }
  lv_obj_add_event_cb(nav_btn_next_, on_nav_event, LV_EVENT_CLICKED, this);
}

void StatusBar::create_right_section(lv_obj_t* parent) {
  namespace SB = UI::StatusBar;

  lv_obj_t* right = UI::create_clean_obj(parent);
  lv_obj_set_size(right, SB::RIGHT_W, SB::HEIGHT);
  lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(right, SB::RIGHT_GAP, 0);

  wifi_icon_ = UI::create_icon_or_fallback(right, Icons::cache.wifi, LV_SYMBOL_WIFI, SB::ICON_SIZE, UI::Global::COLOR_WHITE);
  if (!Icons::cache.wifi) {
    lv_obj_set_style_text_opa(wifi_icon_, SB::TEXT_OPA, 0);
  }

  ble_icon_ = UI::create_icon_or_fallback(right, Icons::cache.ble, LV_SYMBOL_BLUETOOTH, SB::ICON_SIZE, UI::Global::COLOR_WHITE);
  if (!Icons::cache.ble) {
    lv_obj_set_style_text_opa(ble_icon_, SB::TEXT_OPA, 0);
  }

  lv_obj_set_style_opa(wifi_icon_, LV_OPA_80, 0);
  lv_obj_set_style_opa(ble_icon_, LV_OPA_80, 0);
}

// ─── 내비게이션 버튼 이벤트 핸들러 ───
void StatusBar::on_nav_event(lv_event_t* e) {
  auto* self = static_cast<StatusBar*>(lv_event_get_user_data(e));
  if (!self || !self->nav_callback_) return;
  lv_obj_t*  btn     = lv_event_get_target_obj(e);
  const bool is_next = (btn == self->nav_btn_next_);
  self->nav_callback_(is_next);
}

void StatusBar::update_battery(uint8_t percent) const {
  if (battery_label_ == nullptr) return;
  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", percent);
  lv_label_set_text(battery_label_, buf);
}

void StatusBar::update_ble_status(bool connected) const {
  if (ble_icon_ == nullptr) return;
  lv_obj_set_style_opa(ble_icon_, connected ? LV_OPA_COVER : LV_OPA_80, 0);
}

void StatusBar::update_wifi_status(bool connected) const {
  if (wifi_icon_ == nullptr) return;
  lv_obj_set_style_opa(wifi_icon_, connected ? LV_OPA_COVER : LV_OPA_80, 0);
}

void StatusBar::update_mode_indicator(uint8_t active_mode_index) {
  for (uint8_t i = 0; i < 3; ++i) {
    update_indicator_style(i, i == active_mode_index);
  }
}

void StatusBar::update_indicator_style(uint8_t i, bool active) const {
  if (indicators_[i] == nullptr) return;
  if (active) {
    lv_obj_set_style_bg_color(indicators_[i], UI::Global::COLOR_WHITE, 0);
    lv_obj_set_style_bg_opa(indicators_[i], LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(indicators_[i], 0, 0);
  } else {
    lv_obj_set_style_bg_opa(indicators_[i], LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(indicators_[i], 1, 0);
    lv_obj_set_style_border_color(indicators_[i], UI::Global::COLOR_WHITE, 0);
    lv_obj_set_style_border_opa(indicators_[i], LV_OPA_COVER, 0);
  }
}

}  // namespace Display
