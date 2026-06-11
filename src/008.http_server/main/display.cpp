#include "display.hpp"

#include <string>

#include "bsp/esp-bsp.h"

namespace {
constexpr lv_color_t kColorScreenBg = {.blue = 0x08, .green = 0x07, .red = 0x07};  // #070708
constexpr lv_color_t kColorText = {.blue = 0xE0, .green = 0xE0, .red = 0xE0};      // #E0E0E0
constexpr auto* kTextFont = &lv_font_montserrat_40;
}  // namespace

bool Display::initialize() {
  bsp_display_cfg_t display_cfg = {.lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
                                   .buffer_size = BSP_LCD_H_RES * CONFIG_BSP_LCD_DRAW_BUF_HEIGHT,
                                   .double_buffer = true,
                                   .flags = {
                                       .buff_dma = true,
                                       .buff_spiram = true,
                                       .sw_rotate = true,
                                   }};
  display_cfg.lvgl_port_cfg.task_affinity = PRO_CPU_NUM + 1;
  display_handle_ = bsp_display_start_with_config(&display_cfg);

  {
    bsp_display_lock(0);
    screen_handle_ = lv_disp_get_scr_act(display_handle_);
    lv_obj_set_style_bg_color(screen_handle_, kColorScreenBg, 0);

    label_status_handle_ = lv_label_create(screen_handle_);
    lv_obj_set_width(label_status_handle_, LV_PCT(100));
    lv_obj_align(label_status_handle_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_align(label_status_handle_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label_status_handle_, kColorText, 0);
    lv_obj_set_style_bg_opa(screen_handle_, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(label_status_handle_, kTextFont, 0);
    lv_label_set_text(label_status_handle_, "Ready");

    // 와이파이 목록 표시용 테이블 생성 및 초기 설정 (숨김 상태)
    table_wifi_handle_ = lv_table_create(screen_handle_);
    lv_obj_set_width(table_wifi_handle_, LV_PCT(100));
    lv_obj_align(table_wifi_handle_, LV_ALIGN_TOP_MID, 0, 10);

    // 테이블 셀 스타일 설정 (글씨 색상 및 폰트 설정)
    lv_obj_set_style_text_color(table_wifi_handle_, kColorText, LV_PART_ITEMS);
    lv_obj_set_style_text_font(table_wifi_handle_, &lv_font_montserrat_40, LV_PART_ITEMS);

    // 구분선 제거 (테두리 두께 0) 및 배경을 투명하게 설정하여 화면 배경 투과
    lv_obj_set_style_border_width(table_wifi_handle_, 0, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(table_wifi_handle_, LV_OPA_TRANSP, LV_PART_ITEMS);

    // 19개 행(18개 AP + 1개 헤더)이 1280px 화면에 모두 알맞게 들어오도록 셀 패딩 설정
    lv_obj_set_style_pad_top(table_wifi_handle_, 6, LV_PART_ITEMS);
    lv_obj_set_style_pad_bottom(table_wifi_handle_, 6, LV_PART_ITEMS);
    lv_obj_set_style_pad_left(table_wifi_handle_, 10, LV_PART_ITEMS);
    lv_obj_set_style_pad_right(table_wifi_handle_, 10, LV_PART_ITEMS);

    lv_obj_add_flag(table_wifi_handle_, LV_OBJ_FLAG_HIDDEN);

    bsp_display_unlock();
  }
  bsp_display_brightness_set(25);

  return true;
}

void Display::update_status_message(const char* message) const {
  if (message == nullptr || label_status_handle_ == nullptr) return;

  bsp_display_lock(0);
  if (table_wifi_handle_) {
    lv_obj_add_flag(table_wifi_handle_, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_remove_flag(label_status_handle_, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(label_status_handle_, message);
  bsp_display_unlock();
}

void Display::update_wifi_scan_result(const std::vector<wifi_ap_record_t>& scan_results) const {
  if (table_wifi_handle_ == nullptr) return;

  bsp_display_lock(0);

  // 1. 라벨 숨기기
  if (label_status_handle_) {
    lv_obj_add_flag(label_status_handle_, LV_OBJ_FLAG_HIDDEN);
  }

  // 2. 테이블 표시
  lv_obj_remove_flag(table_wifi_handle_, LV_OBJ_FLAG_HIDDEN);

  // 3. 테이블 열 수 및 너비 설정 (화면 가로 해상도 720px에 맞춰 균형있게 분배)
  lv_table_set_col_cnt(table_wifi_handle_, 3);
  lv_table_set_col_width(table_wifi_handle_, 0, 320);  // SSID (최대 12글자 제한에 맞춰 너비를 조절하여 간격을 분배)
  lv_table_set_col_width(table_wifi_handle_, 1, 220);  // RSSI (SSID와 너무 붙지 않도록 간격 확보)
  lv_table_set_col_width(table_wifi_handle_, 2, 180);  // Channel (우측 정렬 및 간격 확보)

  // 4. 테이블 동적 행 개수 설정 (데이터 개수 + 헤더 1행, 최대 18개 AP 표출 = 19행)
  uint32_t total_rows = scan_results.size() + 1;
  if (total_rows > 19) total_rows = 19;
  lv_table_set_row_cnt(table_wifi_handle_, total_rows);

  // 5. 헤더 출력
  lv_table_set_cell_value(table_wifi_handle_, 0, 0, "SSID");
  lv_table_set_cell_value(table_wifi_handle_, 0, 1, "RSSI");
  lv_table_set_cell_value(table_wifi_handle_, 0, 2, "Ch");

  // 6. 데이터 출력
  for (size_t i = 0; i < (total_rows - 1); ++i) {
    const auto& ap = scan_results[i];
    uint32_t row = i + 1;

    std::string ssid_str = reinterpret_cast<const char*>(ap.ssid);
    if (ssid_str.length() > 12) {
      ssid_str = ssid_str.substr(0, 9) + "...";
    }
    lv_table_set_cell_value(table_wifi_handle_, row, 0, ssid_str.c_str());

    char rssi_str[16];
    snprintf(rssi_str, sizeof(rssi_str), "%d dBm", ap.rssi);
    lv_table_set_cell_value(table_wifi_handle_, row, 1, rssi_str);

    char ch_str[16];
    snprintf(ch_str, sizeof(ch_str), "%d", ap.primary);
    lv_table_set_cell_value(table_wifi_handle_, row, 2, ch_str);
  }

  bsp_display_unlock();
}