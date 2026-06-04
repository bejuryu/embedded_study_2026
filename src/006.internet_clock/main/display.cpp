
#include "display.hpp"

#include <ctime>

#include "bsp/esp-bsp.h"
#include "soc/soc.h"

extern "C" {
LV_IMAGE_DECLARE(IMAGE_BACKGROUND);
LV_FONT_DECLARE(antonio_290);
LV_FONT_DECLARE(inter_22);
}

namespace {
// ── Font Aliases ─────────────────────────────────────────
const auto& kFontDigit = antonio_290;  // 시간 digit (Antonio 290px)
const auto& kFontFooter = inter_22;    // 푸터 텍스트 (Inter 22px)

// ── Layout Constants (DESIGN.md §2, §4) ─────────────────
constexpr int32_t kScreenWidth = 1280;
constexpr int32_t kScreenHeight = 720;

// 플립 윈도우 공통 치수
constexpr int32_t kWindowWidth = 300;
constexpr int32_t kWindowHeight = 520;
constexpr int32_t kWindowY = 100;
constexpr int32_t kDigitY =
    210;  // kWindowY + (kWindowHeight - 255)/2 - 22 (Antonio 폰트 세로 중앙 정렬 및 오프셋 보정)

// 각 윈도우 X 시작점
constexpr int32_t kHourX = 100;
constexpr int32_t kMinuteX = 480;
constexpr int32_t kSecondX = 860;

// 푸터 레이아웃
constexpr int32_t kFooterY = 664;
constexpr int32_t kFooterMarginSide = 110;

// 요일 배지 (Pill)
constexpr int32_t kPillPadV = 2;
constexpr int32_t kPillPadH = 12;
constexpr int32_t kPillRadius = 50;

// ── Color Palette (DESIGN.md §5) ────────────────────────
constexpr lv_color_t kColorScreenBg = {.blue = 0x08, .green = 0x07, .red = 0x07};  // #070708
constexpr lv_color_t kColorInk = {.blue = 0xC5, .green = 0xD3, .red = 0xD8};       // #D8D3C5
constexpr lv_color_t kColorAccent = {.blue = 0x86, .green = 0xCF, .red = 0xF0};    // #F0CF86
constexpr lv_color_t kColorPillInk = {.blue = 0x0D, .green = 0x14, .red = 0x16};   // #16140D
constexpr lv_color_t kColorTextDim = {.blue = 0x95, .green = 0xA0, .red = 0xA4};   // #A4A095
constexpr lv_color_t kColorTextSep = {.blue = 0x4D, .green = 0x55, .red = 0x55};   // #55554D
}  // namespace

// ── LVGL 타이머 콜백 (Core 1 LVGL 태스크에서 실행) ────────────
static void clock_timer_cb(lv_timer_t* timer) {
  auto* self = static_cast<Display*>(lv_timer_get_user_data(timer));

  time_t now = time(nullptr);
  struct tm ti;
  localtime_r(&now, &ti);

  self->update_time(ti.tm_hour, ti.tm_min, ti.tm_sec);
  self->update_date(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday, ti.tm_wday);
}

// ── Display ──────────────────────────────────────────────────

Display::Display() {}

bool Display::initialize() {
  bsp_display_cfg_t display_cfg = {.lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
                                   .buffer_size = BSP_LCD_V_RES * BSP_LCD_H_RES,
                                   .double_buffer = true,
                                   .flags = {
                                       .buff_dma = true,
                                       .buff_spiram = true,
                                       .sw_rotate = true,
                                   }};
  display_cfg.lvgl_port_cfg.task_affinity = PRO_CPU_NUM + 1;
  display_handle_ = bsp_display_start_with_config(&display_cfg);

  bsp_display_brightness_set(0);  // 백라이트 OFF (하얀 화면 방지)

  bsp_display_lock(0);

  bsp_display_rotate(display_handle_, LV_DISPLAY_ROTATION_90);

  screen_obj_ = lv_disp_get_scr_act(display_handle_);
  lv_obj_set_style_bg_color(screen_obj_, kColorScreenBg, 0);  // 스크린 배경 검정

  // ── 배경 이미지 ──
  background_image_ = lv_image_create(screen_obj_);
  lv_image_set_src(background_image_, &IMAGE_BACKGROUND);
  lv_obj_set_size(background_image_, BSP_LCD_V_RES, BSP_LCD_H_RES);
  lv_obj_center(background_image_);

  // ── 시간 digit 라벨 (시, 분, 초 - 각 자리수 개별 정렬) ──
  const auto create_digit = [](lv_obj_t* parent, int32_t x_start, const lv_font_t* font, lv_color_t color,
                               lv_obj_t*& label_tens, lv_obj_t*& label_ones) {
    // 1. 투명 컨테이너 생성 (300x520px)
    lv_obj_t* win = lv_obj_create(parent);
    lv_obj_remove_style_all(win);
    lv_obj_set_pos(win, x_start, kWindowY);
    lv_obj_set_size(win, kWindowWidth, kWindowHeight);

    // 2. 10의 자리 라벨 (좌측 150px 영역 내 중앙 정렬)
    label_tens = lv_label_create(win);
    lv_obj_set_size(label_tens, 150, LV_SIZE_CONTENT);
    lv_obj_align(label_tens, LV_ALIGN_LEFT_MID, 0, -22);
    lv_obj_set_style_text_align(label_tens, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label_tens, font, 0);
    lv_obj_set_style_text_color(label_tens, color, 0);
    lv_label_set_text(label_tens, "0");

    // 3. 1의 자리 라벨 (우측 150px 영역 내 중앙 정렬)
    label_ones = lv_label_create(win);
    lv_obj_set_size(label_ones, 150, LV_SIZE_CONTENT);
    lv_obj_align(label_ones, LV_ALIGN_RIGHT_MID, 0, -22);
    lv_obj_set_style_text_align(label_ones, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label_ones, font, 0);
    lv_obj_set_style_text_color(label_ones, color, 0);
    lv_label_set_text(label_ones, "0");
  };

  create_digit(screen_obj_, kHourX, &kFontDigit, kColorInk, label_hour_tens_, label_hour_ones_);
  create_digit(screen_obj_, kMinuteX, &kFontDigit, kColorInk, label_minute_tens_, label_minute_ones_);
  create_digit(screen_obj_, kSecondX, &kFontDigit, kColorInk, label_second_tens_, label_second_ones_);

  // ── 푸터 컨테이너 ──
  footer_container_ = lv_obj_create(screen_obj_);
  lv_obj_remove_style_all(footer_container_);
  lv_obj_set_pos(footer_container_, kFooterMarginSide, kFooterY);
  lv_obj_set_size(footer_container_, kScreenWidth - 2 * kFooterMarginSide, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(footer_container_, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(footer_container_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // ── 푸터 좌측 그룹 ──
  lv_obj_t* footer_left = lv_obj_create(footer_container_);
  lv_obj_remove_style_all(footer_left);
  lv_obj_set_size(footer_left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(footer_left, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(footer_left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(footer_left, 12, 0);

  // 날짜 라벨
  label_date_ = lv_label_create(footer_left);
  lv_obj_set_style_text_font(label_date_, &kFontFooter, 0);
  lv_obj_set_style_text_color(label_date_, kColorInk, 0);
  lv_obj_set_style_text_letter_space(label_date_, 1, 0);
  lv_label_set_text(label_date_, "----.--.--");

  // 요일 배지 (Pill)
  pill_weekday_ = lv_obj_create(footer_left);
  lv_obj_remove_style_all(pill_weekday_);
  lv_obj_set_size(pill_weekday_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(pill_weekday_, kColorAccent, 0);
  lv_obj_set_style_bg_opa(pill_weekday_, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(pill_weekday_, kPillRadius, 0);
  lv_obj_set_style_pad_ver(pill_weekday_, kPillPadV, 0);
  lv_obj_set_style_pad_hor(pill_weekday_, kPillPadH, 0);

  label_weekday_ = lv_label_create(pill_weekday_);
  lv_obj_set_style_text_font(label_weekday_, &kFontFooter, 0);
  lv_obj_set_style_text_color(label_weekday_, kColorPillInk, 0);
  lv_obj_set_style_text_letter_space(label_weekday_, 1, 0);
  lv_obj_center(label_weekday_);
  lv_label_set_text(label_weekday_, "---");

  // ── 푸터 우측 (recolor 라벨) ──
  label_footer_right_ = lv_label_create(footer_container_);
  lv_obj_set_style_text_font(label_footer_right_, &kFontFooter, 0);
  lv_obj_set_style_text_color(label_footer_right_, kColorInk, 0);
  lv_obj_set_style_text_letter_space(label_footer_right_, 1, 0);
  lv_label_set_recolor(label_footer_right_, true);
  lv_label_set_text(label_footer_right_, "#A4A095 NO WIFI#");

  // ── LVGL 타이머 등록 (1초 주기, Core 1에서 실행) ──
  clock_timer_ = lv_timer_create(clock_timer_cb, 1000, this);

  bsp_display_unlock();

  // LVGL 태스크가 첫 프레임을 렌더링할 시간 확보 (갱신 주기 33ms)
  vTaskDelay(pdMS_TO_TICKS(1000));

  bsp_display_brightness_set(25);  // 콘텐츠 준비 후 백라이트 ON

  return true;
}

void Display::set_system_config(const SystemConfig& config) { system_config_ = config; }

// ── 업데이트 메서드 ──────────────────────────────────────────

void Display::update_time(int hour, int minute, int second, bool lock) {
  if (lock) bsp_display_lock(0);

  char buf[2];

  // 시 업데이트
  buf[0] = '0' + (hour / 10);
  buf[1] = '\0';
  lv_label_set_text(label_hour_tens_, buf);
  buf[0] = '0' + (hour % 10);
  buf[1] = '\0';
  lv_label_set_text(label_hour_ones_, buf);

  // 분 업데이트
  buf[0] = '0' + (minute / 10);
  buf[1] = '\0';
  lv_label_set_text(label_minute_tens_, buf);
  buf[0] = '0' + (minute % 10);
  buf[1] = '\0';
  lv_label_set_text(label_minute_ones_, buf);

  // 초 업데이트
  buf[0] = '0' + (second / 10);
  buf[1] = '\0';
  lv_label_set_text(label_second_tens_, buf);
  buf[0] = '0' + (second % 10);
  buf[1] = '\0';
  lv_label_set_text(label_second_ones_, buf);

  if (lock) bsp_display_unlock();
}

void Display::update_date(int year, int month, int day, int weekday, bool lock) {
  if (lock) bsp_display_lock(0);

  char buf[16];
  snprintf(buf, sizeof(buf), "%04d.%02d.%02d", year, month, day);
  lv_label_set_text(label_date_, buf);

  static const char* days[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  lv_label_set_text(label_weekday_, days[weekday % 7]);

  if (lock) bsp_display_unlock();
}

void Display::update_weather(const char* city, const char* condition, int temp_c, bool lock) {
  if (lock) bsp_display_lock(0);

  char buf[128];
  if (city[0] == '\0') {
    // WiFi 미연결 등 특수 상태 — condition만 dim 색상으로 표시
    snprintf(buf, sizeof(buf), "#A4A095 %s#", condition);
  } else {
    // 정상 날씨 표시
    snprintf(buf, sizeof(buf), "#D8D3C5 %s#  #55554D ·#  #A4A095 %s#  #55554D ·#  #D8D3C5 %d°C#", city, condition,
             temp_c);
  }
  lv_label_set_text(label_footer_right_, buf);

  if (lock) bsp_display_unlock();
}