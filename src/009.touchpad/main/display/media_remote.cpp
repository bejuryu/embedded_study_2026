#include "media_remote.hpp"

#include <cstdio>
#include <initializer_list>

#include "constants.hpp"
#include "dlna/dlna_controller.hpp"
#include "esp_log.h"
#include "ui_icons.hpp"

extern "C" void lv_image_cache_drop(const void* src);

namespace Display {

static const char* TAG = "MediaRemoteUI";

MediaRemote::~MediaRemote() {
  if (volume_lock_timer_) {
    lv_timer_delete(volume_lock_timer_);
  }
  if (play_lock_timer_) {
    lv_timer_delete(play_lock_timer_);
  }
  if (progress_timer_) {
    lv_timer_delete(progress_timer_);
  }
  if (scan_delay_timer_) {
    lv_timer_delete(scan_delay_timer_);
  }
  if (current_art_buffer_) {
    free(current_art_buffer_);
  }
}

void MediaRemote::initialize(lv_obj_t* parent_tile, const FontSet& fonts) {
  namespace UI_Mode = ::Display::UI::Mode_MediaRemote;
  fonts_            = &fonts;

  top_container_ = UI::create_clean_obj(parent_tile);
  lv_obj_set_size(top_container_, UI::Global::SCREEN_W, UI_Mode::TOP_H);
  lv_obj_set_style_bg_color(top_container_, UI::Global::COLOR_BG, 0);
  lv_obj_set_style_bg_opa(top_container_, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_top(top_container_, UI_Mode::TOP_PAD_TOP, 0);
  lv_obj_set_style_pad_left(top_container_, UI_Mode::TOP_PAD_SIDE, 0);
  lv_obj_set_style_pad_right(top_container_, UI_Mode::TOP_PAD_SIDE, 0);
  lv_obj_set_style_pad_bottom(top_container_, UI_Mode::TOP_PAD_BOTTOM, 0);
  lv_obj_set_flex_flow(top_container_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(top_container_, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_border_width(top_container_, 0, 0);
  lv_obj_add_flag(top_container_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

  album_art_img_ = lv_image_create(top_container_);
  lv_obj_set_size(album_art_img_, UI::Global::SCREEN_W, UI_Mode::TOP_H);
  lv_obj_add_flag(album_art_img_, LV_OBJ_FLAG_FLOATING);
  lv_obj_set_pos(album_art_img_, -UI_Mode::TOP_PAD_SIDE, -UI_Mode::TOP_PAD_TOP);  // padding 상쇄

  // [5.4절 사양: 앨범아트 종횡비 왜곡 차단 Center Crop]
  lv_image_set_inner_align(album_art_img_, LV_IMAGE_ALIGN_CENTER);
  lv_obj_move_to_index(album_art_img_, 0);  // 최하위 z-order

  create_track_info(top_container_, fonts);
  create_progress_bar(top_container_, fonts);
  create_controls(top_container_);
  create_volume_slider(top_container_);

  trackpad_.initialize(parent_tile, fonts);
}

void MediaRemote::create_track_info(lv_obj_t* parent, const FontSet& fonts) {
  namespace UI_Mode = ::Display::UI::Mode_MediaRemote;

  lv_obj_t* info_row = UI::create_clean_obj(parent);
  lv_obj_set_width(info_row, lv_pct(100));
  lv_obj_set_height(info_row, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_bottom(info_row, UI_Mode::INFO_MARGIN_B, 0);
  lv_obj_set_flex_flow(info_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(info_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);

  lv_obj_t* text_col = UI::create_clean_obj(info_row);
  lv_obj_set_flex_grow(text_col, 1);
  lv_obj_set_height(text_col, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(text_col, LV_FLEX_FLOW_COLUMN);

  auto create_shadow_label = [&](lv_obj_t*& shadow, lv_obj_t* shadow_parent, lv_font_t* font) {
    shadow = lv_label_create(shadow_parent);
    lv_obj_set_width(shadow, lv_pct(85));
    lv_label_set_text(shadow, "");
    lv_obj_set_style_text_color(shadow, UI::Global::COLOR_BG, 0);
    lv_obj_set_style_text_opa(shadow, UI_Mode::TEXT_SHADOW_OPA, 0);
    lv_obj_set_pos(shadow, 0, UI_Mode::TEXT_SHADOW_OFS_Y);
    set_obj_font_if_exists(shadow, font);
    lv_obj_add_flag(shadow, LV_OBJ_FLAG_HIDDEN);
  };

  // --- Title Box (Non-flex wrapper for title + shadow) ---
  lv_obj_t* title_box = UI::create_clean_obj(text_col);
  lv_obj_set_width(title_box, lv_pct(100));
  lv_obj_set_height(title_box, LV_SIZE_CONTENT);

  create_shadow_label(title_shadow_, title_box, fonts.font_40);

  title_label_ = lv_label_create(title_box);
  lv_obj_set_width(title_label_, lv_pct(85));
  lv_label_set_text(title_label_, "DLNA MediaRemote");
  lv_obj_set_style_text_color(title_label_, UI::Global::COLOR_WHITE, 0);
  lv_obj_set_style_pad_bottom(title_label_, UI_Mode::TITLE_MARGIN_B, 0);
  set_obj_font_if_exists(title_label_, fonts.font_40);

  // --- Artist Box (Non-flex wrapper for artist + shadow) ---
  lv_obj_t* artist_box = UI::create_clean_obj(text_col);
  lv_obj_set_width(artist_box, lv_pct(100));
  lv_obj_set_height(artist_box, LV_SIZE_CONTENT);

  create_shadow_label(artist_shadow_, artist_box, fonts.font_24);

  artist_label_ = lv_label_create(artist_box);
  lv_obj_set_width(artist_label_, lv_pct(85));
  lv_label_set_text(artist_label_, "Select Speaker...");
  lv_obj_set_style_text_color(artist_label_, UI_Mode::ARTIST_COLOR, 0);
  set_obj_font_if_exists(artist_label_, fonts.font_24);

  heart_icon_ = UI::create_clean_obj(info_row);
  lv_obj_set_size(heart_icon_, UI_Mode::HEART_SIZE, UI_Mode::HEART_SIZE);
  UI::create_icon_or_fallback(heart_icon_, Icons::cache.vol_up, "S", UI_Mode::HEART_SIZE, UI_Mode::HEART_COLOR);
  lv_obj_add_event_cb(heart_icon_, renderer_button_event_cb, LV_EVENT_CLICKED, this);
}

void MediaRemote::create_progress_bar(lv_obj_t* parent, const FontSet& fonts) {
  namespace UI_Mode = ::Display::UI::Mode_MediaRemote;

  lv_obj_t* prog_container = UI::create_clean_obj(parent);
  lv_obj_set_width(prog_container, lv_pct(100));
  lv_obj_set_height(prog_container, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_bottom(prog_container, UI_Mode::PROG_MARGIN_B, 0);
  lv_obj_set_flex_flow(prog_container, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(prog_container, UI_Mode::PROG_GAP, 0);

  prog_slider_ = lv_slider_create(prog_container);
  lv_obj_set_width(prog_slider_, lv_pct(100));
  lv_obj_set_height(prog_slider_, UI_Mode::PROG_BAR_H);
  lv_slider_set_range(prog_slider_, 0, 1000);
  lv_slider_set_value(prog_slider_, 0, LV_ANIM_OFF);

  for (lv_part_t part : {LV_PART_MAIN, LV_PART_INDICATOR}) {
    lv_obj_set_style_bg_color(prog_slider_, UI::Global::COLOR_WHITE, part);
    lv_obj_set_style_radius(prog_slider_, UI_Mode::PROG_BAR_RADIUS, part);
  }
  lv_obj_set_style_bg_opa(prog_slider_, UI_Mode::PROG_BAR_BG_OPA, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(prog_slider_, LV_OPA_COVER, LV_PART_INDICATOR);

  // Knob 숨김
  lv_obj_set_style_bg_opa(prog_slider_, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_set_style_pad_all(prog_slider_, 0, LV_PART_KNOB);
  lv_obj_set_style_width(prog_slider_, 0, LV_PART_KNOB);
  lv_obj_set_style_height(prog_slider_, 0, LV_PART_KNOB);

  lv_obj_t* time_row = UI::create_clean_obj(prog_container);
  lv_obj_set_width(time_row, lv_pct(100));
  lv_obj_set_height(time_row, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(time_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(time_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  auto setup_time_label = [&](lv_obj_t*& label) {
    label = lv_label_create(time_row);
    lv_label_set_text(label, "0:00");
    lv_obj_set_style_text_color(label, UI_Mode::PROG_TIME_COLOR, 0);
    set_obj_font_if_exists(label, fonts.font_16);
  };

  setup_time_label(time_current_);
  setup_time_label(time_total_);
}

void MediaRemote::create_controls(lv_obj_t* parent) {
  namespace UI_Mode = ::Display::UI::Mode_MediaRemote;

  lv_obj_t* ctrl_row = UI::create_clean_obj(parent);
  lv_obj_set_width(ctrl_row, lv_pct(100));
  lv_obj_set_height(ctrl_row, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_left(ctrl_row, UI_Mode::CTRL_PAD_SIDE, 0);
  lv_obj_set_style_pad_right(ctrl_row, UI_Mode::CTRL_PAD_SIDE, 0);
  lv_obj_set_flex_flow(ctrl_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(ctrl_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  btn_shuffle_ = UI::create_clean_obj(ctrl_row);
  lv_obj_set_size(btn_shuffle_, UI_Mode::SHUFFLE_REPEAT_SIZE + UI_Mode::BTN_HITBOX_PAD * 2, UI_Mode::SHUFFLE_REPEAT_SIZE + UI_Mode::BTN_HITBOX_PAD * 2);
  lv_obj_set_style_pad_all(btn_shuffle_, UI_Mode::BTN_HITBOX_PAD, 0);
  UI::create_icon_or_fallback(btn_shuffle_, Icons::cache.shuffle, "S", UI_Mode::SHUFFLE_REPEAT_SIZE, UI_Mode::BTN_DIM_COLOR);
  lv_obj_add_event_cb(btn_shuffle_, control_button_event_cb, LV_EVENT_CLICKED, this);

  btn_prev_ = UI::create_clean_obj(ctrl_row);
  lv_obj_set_size(btn_prev_, UI_Mode::PREV_NEXT_SIZE + UI_Mode::BTN_HITBOX_PAD * 2, UI_Mode::PREV_NEXT_SIZE + UI_Mode::BTN_HITBOX_PAD * 2);
  lv_obj_set_style_pad_all(btn_prev_, UI_Mode::BTN_HITBOX_PAD, 0);
  if (Icons::cache.prev) {
    lv_obj_t* img = lv_image_create(btn_prev_);
    lv_image_set_src(img, Icons::cache.prev);
    lv_obj_set_size(img, UI_Mode::PREV_NEXT_SIZE, UI_Mode::PREV_NEXT_SIZE);
    lv_image_set_scale(img, 192);  // 48px -> 36px (256 * 36 / 48 = 192)
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
  } else {
    UI::create_icon_or_fallback(btn_prev_, nullptr, LV_SYMBOL_PREV, UI_Mode::PREV_NEXT_SIZE, UI::Global::COLOR_WHITE);
  }
  lv_obj_add_event_cb(btn_prev_, control_button_event_cb, LV_EVENT_CLICKED, this);

  btn_play_ = UI::create_clean_obj(ctrl_row);
  lv_obj_set_size(btn_play_, UI_Mode::PLAY_BTN_SIZE, UI_Mode::PLAY_BTN_SIZE);
  lv_obj_set_style_bg_color(btn_play_, UI::Global::COLOR_WHITE, 0);
  lv_obj_set_style_bg_opa(btn_play_, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(btn_play_, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_shadow_width(btn_play_, UI_Mode::PLAY_BTN_SHADOW_W, 0);
  lv_obj_set_style_shadow_ofs_y(btn_play_, UI_Mode::PLAY_BTN_SHADOW_OFS_Y, 0);
  lv_obj_set_style_shadow_color(btn_play_, UI::Global::COLOR_BG, 0);
  lv_obj_set_style_shadow_opa(btn_play_, UI_Mode::PLAY_BTN_SHADOW_OPA, 0);
  lv_obj_set_flex_flow(btn_play_, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btn_play_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  UI::create_icon_or_fallback(btn_play_, Icons::cache.play, LV_SYMBOL_PLAY, UI_Mode::PLAY_ICON_SIZE, UI::Global::COLOR_BG);
  lv_obj_add_event_cb(btn_play_, control_button_event_cb, LV_EVENT_CLICKED, this);

  btn_next_ = UI::create_clean_obj(ctrl_row);
  lv_obj_set_size(btn_next_, UI_Mode::PREV_NEXT_SIZE + UI_Mode::BTN_HITBOX_PAD * 2, UI_Mode::PREV_NEXT_SIZE + UI_Mode::BTN_HITBOX_PAD * 2);
  lv_obj_set_style_pad_all(btn_next_, UI_Mode::BTN_HITBOX_PAD, 0);
  if (Icons::cache.next) {
    lv_obj_t* img = lv_image_create(btn_next_);
    lv_image_set_src(img, Icons::cache.next);
    lv_obj_set_size(img, UI_Mode::PREV_NEXT_SIZE, UI_Mode::PREV_NEXT_SIZE);
    lv_image_set_scale(img, 192);  // 48px -> 36px
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
  } else {
    UI::create_icon_or_fallback(btn_next_, nullptr, LV_SYMBOL_NEXT, UI_Mode::PREV_NEXT_SIZE, UI::Global::COLOR_WHITE);
  }
  lv_obj_add_event_cb(btn_next_, control_button_event_cb, LV_EVENT_CLICKED, this);

  btn_renderer_ = UI::create_clean_obj(ctrl_row);
  lv_obj_set_size(btn_renderer_, UI_Mode::SHUFFLE_REPEAT_SIZE + UI_Mode::BTN_HITBOX_PAD * 2, UI_Mode::SHUFFLE_REPEAT_SIZE + UI_Mode::BTN_HITBOX_PAD * 2);
  lv_obj_set_style_pad_all(btn_renderer_, UI_Mode::BTN_HITBOX_PAD, 0);
  UI::create_icon_or_fallback(btn_renderer_, Icons::cache.repeat, "R", UI_Mode::SHUFFLE_REPEAT_SIZE, UI_Mode::BTN_DIM_COLOR);
  lv_obj_add_event_cb(btn_renderer_, control_button_event_cb, LV_EVENT_CLICKED, this);
}

void MediaRemote::create_volume_slider(lv_obj_t* parent) {
  namespace UI_Mode = ::Display::UI::Mode_MediaRemote;

  lv_obj_t* vol_container = UI::create_clean_obj(parent);
  lv_obj_set_width(vol_container, lv_pct(100));
  lv_obj_set_height(vol_container, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_top(vol_container, 25, 0);
  lv_obj_set_flex_flow(vol_container, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(vol_container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  volume_icon_ = UI::create_clean_obj(vol_container);
  lv_obj_set_size(volume_icon_, 36, 36);
  UI::create_icon_or_fallback(volume_icon_, Icons::cache.vol_down, "V", 36, UI_Mode::BTN_DIM_COLOR);

  volume_slider_ = lv_slider_create(vol_container);
  lv_obj_set_flex_grow(volume_slider_, 1);
  lv_obj_set_style_pad_left(volume_slider_, 15, 0);
  lv_obj_set_height(volume_slider_, 12);
  lv_slider_set_range(volume_slider_, UI_Mode::kVolumeMinLevel, UI_Mode::kVolumeMaxLevel);
  lv_slider_set_value(volume_slider_, 50, LV_ANIM_OFF);

  for (lv_part_t part : {LV_PART_MAIN, LV_PART_INDICATOR}) {
    lv_obj_set_style_bg_color(volume_slider_, UI::Global::COLOR_WHITE, part);
    lv_obj_set_style_radius(volume_slider_, 6, part);
  }
  lv_obj_set_style_bg_opa(volume_slider_, LV_OPA_30, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(volume_slider_, LV_OPA_COVER, LV_PART_INDICATOR);

  lv_obj_set_style_bg_opa(volume_slider_, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_set_style_pad_all(volume_slider_, 0, LV_PART_KNOB);
  lv_obj_set_style_width(volume_slider_, 0, LV_PART_KNOB);
  lv_obj_set_style_height(volume_slider_, 0, LV_PART_KNOB);

  lv_obj_add_event_cb(volume_slider_, volume_slider_event_cb, LV_EVENT_ALL, this);
}

void MediaRemote::volume_slider_event_cb(lv_event_t* e) {
  namespace UI_Mode    = ::Display::UI::Mode_MediaRemote;
  auto*           self = static_cast<MediaRemote*>(lv_event_get_user_data(e));
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_PRESSING) {
    self->volume_lock_ = true;

    if (self->volume_lock_timer_) {
      lv_timer_reset(self->volume_lock_timer_);
    } else {
      self->volume_lock_timer_ = lv_timer_create(volume_lock_timeout_cb, UI_Mode::kVolumeLockTimeoutMs, self);
      lv_timer_set_repeat_count(self->volume_lock_timer_, 1);
    }

    int32_t val = lv_slider_get_value(self->volume_slider_);
    if (val == 0) {
      UI::create_icon_or_fallback(self->volume_icon_, Icons::cache.mute, "M", 36, UI_Mode::BTN_DIM_COLOR);
    } else if (val < 50) {
      UI::create_icon_or_fallback(self->volume_icon_, Icons::cache.vol_down, "V", 36, UI_Mode::BTN_DIM_COLOR);
    } else {
      UI::create_icon_or_fallback(self->volume_icon_, Icons::cache.vol_up, "V", 36, UI_Mode::BTN_DIM_COLOR);
    }
  } else if (code == LV_EVENT_RELEASED) {
    int32_t val = lv_slider_get_value(self->volume_slider_);
    Ble::Dlna::DlnaController::instance().set_volume(static_cast<uint8_t>(val));
  }
}

void MediaRemote::volume_lock_timeout_cb(lv_timer_t* t) {
  auto* self               = static_cast<MediaRemote*>(lv_timer_get_user_data(t));
  self->volume_lock_       = false;
  self->volume_lock_timer_ = nullptr;
  ESP_LOGD(TAG, "Volume lock auto-released.");
}

void MediaRemote::control_button_event_cb(lv_event_t* e) {
  namespace UI_Mode = ::Display::UI::Mode_MediaRemote;
  auto* self        = static_cast<MediaRemote*>(lv_event_get_user_data(e));
  auto* target      = static_cast<lv_obj_t*>(lv_event_get_target(e));

  if (self->play_lock_) return;

  if (target == self->btn_play_) {
    self->play_lock_ = true;

    if (self->play_lock_timer_) {
      lv_timer_reset(self->play_lock_timer_);
    } else {
      self->play_lock_timer_ = lv_timer_create(play_lock_timeout_cb, UI_Mode::kPlayLockTimeoutMs, self);
      lv_timer_set_repeat_count(self->play_lock_timer_, 1);
    }

    if (self->last_play_state_ == "PLAYING") {
      Ble::Dlna::DlnaController::instance().send_media_control("Pause");
    } else {
      Ble::Dlna::DlnaController::instance().send_media_control("Play");
    }
  } else if (target == self->btn_prev_) {
    Ble::Dlna::DlnaController::instance().send_media_control("Previous");
  } else if (target == self->btn_next_) {
    Ble::Dlna::DlnaController::instance().send_media_control("Next");
  } else if (target == self->btn_shuffle_) {
    self->shuffle_enabled_ = !self->shuffle_enabled_;
    if (self->shuffle_enabled_) {
      UI::create_icon_or_fallback(self->btn_shuffle_, Icons::cache.shuffle, "S", UI_Mode::SHUFFLE_REPEAT_SIZE, UI::Global::COLOR_WHITE);
      Ble::Dlna::DlnaController::instance().send_media_control("Shuffle_On");
    } else {
      UI::create_icon_or_fallback(self->btn_shuffle_, Icons::cache.shuffle, "S", UI_Mode::SHUFFLE_REPEAT_SIZE, UI_Mode::BTN_DIM_COLOR);
      Ble::Dlna::DlnaController::instance().send_media_control("Shuffle_Off");
    }
  } else if (target == self->btn_renderer_) {
    self->repeat_enabled_ = !self->repeat_enabled_;
    if (self->repeat_enabled_) {
      UI::create_icon_or_fallback(self->btn_renderer_, Icons::cache.repeat, "R", UI_Mode::SHUFFLE_REPEAT_SIZE, UI::Global::COLOR_WHITE);
      Ble::Dlna::DlnaController::instance().send_media_control("Repeat_On");
    } else {
      UI::create_icon_or_fallback(self->btn_renderer_, Icons::cache.repeat, "R", UI_Mode::SHUFFLE_REPEAT_SIZE, UI_Mode::BTN_DIM_COLOR);
      Ble::Dlna::DlnaController::instance().send_media_control("Repeat_Off");
    }
  }
}

void MediaRemote::play_lock_timeout_cb(lv_timer_t* t) {
  auto* self             = static_cast<MediaRemote*>(lv_timer_get_user_data(t));
  self->play_lock_       = false;
  self->play_lock_timer_ = nullptr;
  ESP_LOGD(TAG, "Play control lock auto-released.");
}

void MediaRemote::renderer_button_event_cb(lv_event_t* e) {
  namespace UI_Mode = ::Display::UI::Mode_MediaRemote;
  auto* self        = static_cast<MediaRemote*>(lv_event_get_user_data(e));

  // 만약 이미 스피너가 돌고 있거나 타이머가 작동 중이면, 캔슬하고 리턴 (이중 방어)
  if (self->scan_delay_timer_) {
    lv_timer_delete(self->scan_delay_timer_);
    self->scan_delay_timer_ = nullptr;
    if (self->scan_spinner_) {
      lv_obj_delete(self->scan_spinner_);
      self->scan_spinner_ = nullptr;
    }
    if (self->scan_label_) {
      lv_obj_delete(self->scan_label_);
      self->scan_label_ = nullptr;
    }
    return;
  }

  if (self->speaker_dropdown_) {
    lv_obj_delete(self->speaker_dropdown_);
    self->speaker_dropdown_ = nullptr;
    return;
  }

  // 1. 비동기 M-SEARCH 즉시 발송
  Ble::Dlna::DlnaController::instance().start_search();

  // 2. 화면 중앙에 탐색 스피너 및 텍스트 렌더링
  self->scan_spinner_ = lv_spinner_create(self->top_container_);
  lv_obj_add_flag(self->scan_spinner_, LV_OBJ_FLAG_FLOATING);
  lv_obj_set_size(self->scan_spinner_, 80, 80);
  lv_obj_align(self->scan_spinner_, LV_ALIGN_CENTER, 0, -40);
  lv_obj_set_style_arc_width(self->scan_spinner_, 8, LV_PART_MAIN);
  lv_obj_set_style_arc_width(self->scan_spinner_, 8, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(self->scan_spinner_, UI::Global::COLOR_WHITE, LV_PART_INDICATOR);

  self->scan_label_ = lv_label_create(self->top_container_);
  lv_obj_add_flag(self->scan_label_, LV_OBJ_FLAG_FLOATING);
  lv_label_set_text(self->scan_label_, "Searching Speakers...");
  if (self->fonts_) {
    lv_obj_set_style_text_font(self->scan_label_, self->fonts_->font_40, 0);
  }
  lv_obj_set_style_text_color(self->scan_label_, UI::Global::COLOR_WHITE, 0);
  lv_obj_align_to(self->scan_label_, self->scan_spinner_, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);

  // 3. 백그라운드 탐색 완료(SEARCHING 상태 탈출)를 300ms 주기로 감시하는 반복 타이머 기동
  self->scan_delay_timer_ = lv_timer_create(scan_delay_timer_cb, 300, self);
}

void MediaRemote::scan_delay_timer_cb(lv_timer_t* t) {
  namespace UI_Mode = ::Display::UI::Mode_MediaRemote;
  auto* self        = static_cast<MediaRemote*>(lv_timer_get_user_data(t));

  // 백그라운드 스레드에서 SSDP 수집이 진행 중이라면 스피너 유지 후 리턴
  if (Ble::Dlna::DlnaController::instance().get_state() == Ble::Dlna::DlnaState::SEARCHING) {
    return;
  }

  // 스캔이 완료되었으므로 타이머 파괴 (자가 종료)
  lv_timer_delete(t);
  self->scan_delay_timer_ = nullptr;

  // 스피너 및 탐색 문구 제거
  if (self->scan_spinner_) {
    lv_obj_delete(self->scan_spinner_);
    self->scan_spinner_ = nullptr;
  }
  if (self->scan_label_) {
    lv_obj_delete(self->scan_label_);
    self->scan_label_ = nullptr;
  }

  // 최신 검색 결과 획득
  const auto& devices = Ble::Dlna::DlnaController::instance().get_devices();

  // 드롭다운 목록 박스 열기
  self->speaker_dropdown_ = lv_dropdown_create(self->top_container_);
  lv_obj_add_flag(self->speaker_dropdown_, LV_OBJ_FLAG_FLOATING);
  lv_obj_set_width(self->speaker_dropdown_, lv_pct(70));
  lv_obj_align(self->speaker_dropdown_, LV_ALIGN_TOP_MID, 0, 10);

  if (self->fonts_) {
    lv_obj_set_style_text_font(self->speaker_dropdown_, self->fonts_->font_40, 0);
  }
  lv_obj_set_style_text_color(self->speaker_dropdown_, UI::Global::COLOR_WHITE, 0);

  lv_obj_t* list = lv_dropdown_get_list(self->speaker_dropdown_);
  if (list) {
    if (self->fonts_) {
      lv_obj_set_style_text_font(list, self->fonts_->font_40, 0);
      lv_obj_set_style_pad_ver(list, 15, 0);
    }
    lv_obj_set_style_text_color(list, UI::Global::COLOR_WHITE, 0);
    lv_obj_set_style_bg_color(list, UI::Global::COLOR_BG, 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(list, UI::Global::COLOR_WHITE, 0);
    lv_obj_set_style_border_width(list, 1, 0);
  }

  lv_dropdown_clear_options(self->speaker_dropdown_);

  if (devices.empty()) {
    lv_dropdown_add_option(self->speaker_dropdown_, "No Speaker Found", 0);
  } else {
    for (const auto& dev : devices) {
      lv_dropdown_add_option(self->speaker_dropdown_, dev.friendly_name.c_str(), LV_DROPDOWN_POS_LAST);
    }
  }

  lv_obj_add_event_cb(self->speaker_dropdown_, dropdown_event_cb, LV_EVENT_VALUE_CHANGED, self);
  lv_dropdown_open(self->speaker_dropdown_);
}

void MediaRemote::dropdown_event_cb(lv_event_t* e) {
  auto* self     = static_cast<MediaRemote*>(lv_event_get_user_data(e));
  auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(e));

  uint16_t    selected = lv_dropdown_get_selected(dropdown);
  const auto& devices  = Ble::Dlna::DlnaController::instance().get_devices();

  if (selected < devices.size()) {
    const auto& target_device = devices[selected];
    Ble::Dlna::DlnaController::instance().select_target(target_device.udn);
    lv_label_set_text(self->artist_label_, "Connecting...");
  }

  if (self->speaker_dropdown_) {
    lv_obj_delete(self->speaker_dropdown_);
    self->speaker_dropdown_ = nullptr;
  }
}

void MediaRemote::update_track_info(const char* title, const char* artist) const {
  auto update_info = [](lv_obj_t* label, lv_obj_t* shadow, const char* text) {
    if (label) {
      lv_label_set_text(label, text);
      if (shadow) {
        lv_label_set_text(shadow, text);
        lv_obj_remove_flag(shadow, LV_OBJ_FLAG_HIDDEN);
      }
    }
  };

  update_info(title_label_, title_shadow_, title);
  update_info(artist_label_, artist_shadow_, artist);
}

void MediaRemote::update_progress(uint32_t current_ms, uint32_t total_ms) const {
  current_progress_ms_ = current_ms;
  total_duration_ms_   = total_ms;

  if (prog_slider_ && total_ms > 0) {
    const auto value = static_cast<int32_t>((static_cast<uint64_t>(current_ms) * 1000) / total_ms);
    lv_slider_set_value(prog_slider_, value, LV_ANIM_ON);
  }

  auto format_time = [](lv_obj_t* label, uint32_t ms) {
    if (!label) return;
    const uint32_t total_sec = ms / 1000;
    const uint32_t min       = total_sec / 60;
    const uint32_t sec       = total_sec % 60;
    char           buf[16];
    snprintf(buf, sizeof(buf), "%lu:%02lu", static_cast<unsigned long>(min), static_cast<unsigned long>(sec));
    lv_label_set_text(label, buf);
  };

  format_time(time_current_, current_ms);
  format_time(time_total_, total_ms);
}

void MediaRemote::set_album_art(const void* img_src, uint32_t w, uint32_t h) const {
  if (album_art_img_) {
    if (img_src) {
      if (current_art_buffer_ && current_art_buffer_ != img_src) {
        free(current_art_buffer_);
      }
      current_art_buffer_ = const_cast<void*>(img_src);

      // 구버전 크기 정보 캐시 제거 (Out-of-bounds overread 방지)
      lv_image_cache_drop(&album_art_dsc_);

      // LVGL 정렬된 Stride 획득
      uint32_t aligned_stride = lv_draw_buf_width_to_stride(w, LV_COLOR_FORMAT_RGB565);

      album_art_dsc_.header.cf     = LV_COLOR_FORMAT_RGB565;
      album_art_dsc_.header.w      = w;
      album_art_dsc_.header.h      = h;
      album_art_dsc_.header.stride = aligned_stride;
      album_art_dsc_.header.magic  = LV_IMAGE_HEADER_MAGIC;
      album_art_dsc_.data          = static_cast<const uint8_t*>(img_src);
      album_art_dsc_.data_size     = aligned_stride * h;

      ESP_LOGI(TAG, "Setting album art UI: pointer=%p, w=%lu, h=%lu, stride=%u, size=%lu", img_src, (unsigned long)w, (unsigned long)h, album_art_dsc_.header.stride,
               (unsigned long)album_art_dsc_.data_size);

      lv_image_set_src(album_art_img_, &album_art_dsc_);
      lv_obj_invalidate(album_art_img_);
    } else {
      // 디코딩 실패 시 기존 이미지 캐시 소멸 및 플레이스홀더(빈 이미지) 클리어 렌더링
      if (current_art_buffer_) {
        free(current_art_buffer_);
        current_art_buffer_ = nullptr;
      }
      lv_image_cache_drop(&album_art_dsc_);
      lv_image_set_src(album_art_img_, nullptr);
    }
  }
}

void MediaRemote::progress_timer_cb(lv_timer_t* t) {
  auto* self = static_cast<MediaRemote*>(lv_timer_get_user_data(t));
  if (self->last_play_state_ == "PLAYING" && self->total_duration_ms_ > 0) {
    self->current_progress_ms_ += 1000;
    if (self->current_progress_ms_ > self->total_duration_ms_) {
      self->current_progress_ms_ = self->total_duration_ms_;
    }
    self->update_progress(self->current_progress_ms_, self->total_duration_ms_);
  }
}

void MediaRemote::update_volume(uint8_t volume) const {
  if (volume_slider_ && !volume_lock_) {
    lv_slider_set_value(volume_slider_, volume, LV_ANIM_ON);

    if (volume == 0) {
      UI::create_icon_or_fallback(volume_icon_, Icons::cache.mute, "M", 36, UI::Mode_MediaRemote::BTN_DIM_COLOR);
    } else if (volume < 50) {
      UI::create_icon_or_fallback(volume_icon_, Icons::cache.vol_down, "V", 36, UI::Mode_MediaRemote::BTN_DIM_COLOR);
    } else {
      UI::create_icon_or_fallback(volume_icon_, Icons::cache.vol_up, "V", 36, UI::Mode_MediaRemote::BTN_DIM_COLOR);
    }
  }
}

void MediaRemote::update_play_state(const std::string& state) const {
  last_play_state_ = state;

  if (buffering_spinner_) {
    lv_obj_delete(buffering_spinner_);
    buffering_spinner_ = nullptr;
    lv_obj_clear_state(btn_play_, LV_STATE_DISABLED);
  }

  // 재생 상태 전환 시 이전 자식 아이콘 오브젝트들을 모두 클린업 (오버랩 방지)
  lv_obj_clean(btn_play_);

  if (state == "PLAYING") {
    UI::create_icon_or_fallback(btn_play_, Icons::cache.pause, LV_SYMBOL_PAUSE, UI::Mode_MediaRemote::PLAY_ICON_SIZE, UI::Global::COLOR_BG);

    for (auto* lbl : {title_label_, title_shadow_}) {
      if (lbl) {
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
      }
    }

    if (!progress_timer_) {
      progress_timer_ = lv_timer_create(progress_timer_cb, 1000, const_cast<MediaRemote*>(this));
    } else {
      lv_timer_resume(progress_timer_);
    }
  } else if (state == "TRANSITIONING") {
    lv_obj_add_state(btn_play_, LV_STATE_DISABLED);
    buffering_spinner_ = lv_spinner_create(btn_play_);
    lv_obj_set_size(buffering_spinner_, 48, 48);
    lv_obj_align(buffering_spinner_, LV_ALIGN_CENTER, 0, 0);

    if (progress_timer_) {
      lv_timer_pause(progress_timer_);
    }
  } else {
    UI::create_icon_or_fallback(btn_play_, Icons::cache.play, LV_SYMBOL_PLAY, UI::Mode_MediaRemote::PLAY_ICON_SIZE, UI::Global::COLOR_BG);

    for (auto* lbl : {title_label_, title_shadow_}) {
      if (lbl) {
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
      }
    }

    if (progress_timer_) {
      lv_timer_pause(progress_timer_);
    }
  }
}

void MediaRemote::close_dropdown() {
  if (speaker_dropdown_) {
    lv_obj_delete(speaker_dropdown_);
    speaker_dropdown_ = nullptr;
  }
}

}  // namespace Display
