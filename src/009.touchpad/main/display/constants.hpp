#pragma once

#include <cstdint>

#include "lvgl.h"

// =============================================================================
// prototype.html CSS → LVGL constexpr 상수 매핑
// 이 파일의 모든 수치는 prototype.html에서 추출한 pixel-perfect 값입니다.
// 함수 내부에 매직 넘버를 절대 하드코딩하지 않고 이 상수만 참조합니다.
// =============================================================================

namespace Display::UI {

// -----------------------------------------------------------------------------
// 전역 (Global)
// -----------------------------------------------------------------------------
namespace Global {
constexpr int32_t SCREEN_W = 720;
constexpr int32_t SCREEN_H = 1280;
constexpr int32_t SPLIT_Y  = 640;  // 상/하단 분할 기준선

constexpr lv_color_t COLOR_BG    = {.blue = 0x00, .green = 0x00, .red = 0x00};  // #000000
constexpr lv_color_t COLOR_WHITE = {.blue = 0xFF, .green = 0xFF, .red = 0xFF};
}  // namespace Global

// -----------------------------------------------------------------------------
// 상태바 (Status Bar — 공통)
// -----------------------------------------------------------------------------
// 물리 크기 기준 (5인치, 294 PPI 스크린)
//   1px = 25.4mm / 294 = 0.0864mm
//   터치 최소 타겟: 9~10mm = 104~116px → HEIGHT = 120px (10.3mm) 채택
//   아이콘/폰트: 기준 160DPI 대비 스케일 = 294/160 = 1.84 적용
// ─────────────────────────────────────────────────────────────────
namespace StatusBar {
constexpr int32_t  HEIGHT              = 120;  // 30 → 120px (10.3mm, 손가락 터치 최적)
constexpr int32_t  PAD_H               = 20;   // 좌/우 패딩
constexpr int32_t  FONT_SIZE           = 32;   // 16 → 32px (×1.84 스케일, font_32 사용)
constexpr int32_t  FONT_WEIGHT         = 600;
constexpr lv_opa_t TEXT_OPA            = static_cast<lv_opa_t>(0.9f * 255);
constexpr int32_t  LEFT_W              = 200;  // 120 → 200px (44px 아이콘 + "100%" 32px 폰트)
constexpr int32_t  LEFT_GAP            = 12;   // 8 → 12px
constexpr int32_t  RIGHT_W             = 110;  // 120 → 110px (아이콘 2개 37px + 갭)
constexpr int32_t  RIGHT_GAP           = 15;   // 12 → 15px
constexpr int32_t  CENTER_LETTER_SPACE = 8;
constexpr int32_t  BATTERY_ICON_SIZE   = 44;  // 24 → 44px (×1.84)
constexpr int32_t  ICON_SIZE           = 37;  // 20 → 37px (×1.84, WiFi·BLE)

// 모드 내비게이션 버튼 (◀ ▶)
constexpr int32_t NAV_BTN_W = 80;  // 터치 타겟 80×120px (6.9mm × 10.3mm)

// 모드 인디케이터 (● ○ ○)
constexpr int32_t INDICATOR_SIZE = 15;  // 8 → 15px (×1.84)
constexpr int32_t INDICATOR_GAP  = 15;  // 8 → 15px
}  // namespace StatusBar

// -----------------------------------------------------------------------------
// 터치패드 영역 (Touchpad Area — Mode 1/2 공통)
// -----------------------------------------------------------------------------
// 타일 높이 = SCREEN_H(1280) - StatusBar::HEIGHT(120) = 1160px
// 각 모드 콘텐츠 높이 합계가 1160px이 되도록 조정합니다.
namespace Touchpad {
constexpr int32_t    HEIGHT = 580;  // 640 → 580 (타일 1160px의 50%)
constexpr lv_color_t BG     = {.blue = 0x00, .green = 0x00, .red = 0x00};

// 코너 L자 마커
constexpr int32_t    CORNER_SIZE         = 40;
constexpr int32_t    CORNER_BORDER_W     = 2;
constexpr lv_color_t CORNER_BORDER_COLOR = {.blue = 0x22, .green = 0x22, .red = 0x22};
constexpr int32_t    CORNER_OFFSET       = 20;

// 중앙 힌트 텍스트
constexpr int32_t    HINT_FONT_SIZE    = 24;
constexpr int32_t    HINT_FONT_WEIGHT  = 600;
constexpr lv_color_t HINT_COLOR        = {.blue = 0x22, .green = 0x22, .red = 0x22};
constexpr int32_t    HINT_LETTER_SPACE = 4;
}  // namespace Touchpad

// -----------------------------------------------------------------------------
// Mode 1 — PC 미디어 제어
// -----------------------------------------------------------------------------
namespace Mode_MediaControl {
constexpr int32_t    TOP_H          = 580;  // 640 → 580 (타일 1160px의 50%)
constexpr lv_color_t TOP_BG         = {.blue = 0x00, .green = 0x00, .red = 0x00};
constexpr int32_t    TOP_PAD_TOP    = 10;
constexpr int32_t    TOP_PAD_SIDE   = 20;
constexpr int32_t    TOP_PAD_BOTTOM = 20;

// 3×2 그리드
constexpr int32_t GRID_GAP        = 15;
constexpr int32_t GRID_MARGIN_TOP = 15;
constexpr int32_t GRID_COLS       = 3;
constexpr int32_t GRID_ROWS       = 2;

// 일반 버튼
constexpr lv_color_t BTN_BG                 = {.blue = 0x12, .green = 0x12, .red = 0x12};
constexpr int32_t    BTN_RADIUS             = 20;
constexpr int32_t    BTN_BORDER_W           = 2;
constexpr lv_color_t BTN_BORDER_COLOR       = {.blue = 0x1A, .green = 0x1A, .red = 0x1A};
constexpr lv_color_t BTN_ICON_COLOR         = {.blue = 0x88, .green = 0x88, .red = 0x88};
constexpr int32_t    BTN_ICON_MARGIN_B      = 12;
constexpr int32_t    BTN_ICON_SIZE          = 48;
constexpr int32_t    BTN_LABEL_FONT_SIZE    = 16;
constexpr int32_t    BTN_LABEL_LETTER_SPACE = 2;
constexpr lv_color_t BTN_LABEL_COLOR        = {.blue = 0x55, .green = 0x55, .red = 0x55};

// Play/Pause 강조 버튼
constexpr lv_color_t PLAY_BTN_BG          = {.blue = 0x1A, .green = 0x1A, .red = 0x1A};
constexpr lv_color_t PLAY_BTN_BORDER      = {.blue = 0x33, .green = 0x33, .red = 0x33};
constexpr lv_color_t PLAY_BTN_ICON_COLOR  = {.blue = 0xFF, .green = 0xFF, .red = 0xFF};
constexpr lv_color_t PLAY_BTN_LABEL_COLOR = {.blue = 0x88, .green = 0x88, .red = 0x88};
}  // namespace Mode_MediaControl

// -----------------------------------------------------------------------------
// Mode 2 — 범용 미디어 리모컨
// -----------------------------------------------------------------------------
namespace Mode_MediaRemote {
constexpr int32_t TOP_H          = 580;  // 640 → 580 (타일 1160px의 50%)
constexpr int32_t TOP_PAD_TOP    = 10;
constexpr int32_t TOP_PAD_SIDE   = 40;
constexpr int32_t TOP_PAD_BOTTOM = 30;

// 그라데이션 오버레이 4-stop (0%, 30%, 70%, 100%)
constexpr lv_opa_t GRAD_STOP_0_OPA = LV_OPA_TRANSP;                      // 0 (was 102 / 40%)
constexpr lv_opa_t GRAD_STOP_1_OPA = static_cast<lv_opa_t>(0.1f * 255);  // 26
constexpr lv_opa_t GRAD_STOP_2_OPA = static_cast<lv_opa_t>(0.8f * 255);  // 204
constexpr lv_opa_t GRAD_STOP_3_OPA = LV_OPA_COVER;                       // 255
constexpr uint8_t  GRAD_FRAC_0     = 0;
constexpr uint8_t  GRAD_FRAC_1     = static_cast<uint8_t>(0.30f * 255);  // 77
constexpr uint8_t  GRAD_FRAC_2     = static_cast<uint8_t>(0.70f * 255);  // 179
constexpr uint8_t  GRAD_FRAC_3     = 255;

// 곡 정보
constexpr int32_t    INFO_MARGIN_B    = 25;
constexpr int32_t    TITLE_FONT_SIZE  = 40;
constexpr int32_t    TITLE_MARGIN_B   = 4;
constexpr int32_t    ARTIST_FONT_SIZE = 24;
constexpr lv_color_t ARTIST_COLOR     = {.blue = 0xD0, .green = 0xD0, .red = 0xD0};
constexpr lv_color_t HEART_COLOR      = {.blue = 0x54, .green = 0xB9, .red = 0x1D};  // #1DB954
constexpr int32_t    HEART_SIZE       = 36;

// 프로그레스 바
constexpr int32_t    PROG_GAP            = 10;
constexpr int32_t    PROG_MARGIN_B       = 30;
constexpr int32_t    PROG_BAR_H          = 6;
constexpr int32_t    PROG_BAR_RADIUS     = 3;
constexpr lv_opa_t   PROG_BAR_BG_OPA     = static_cast<lv_opa_t>(0.3f * 255);  // 77
constexpr int32_t    PROG_TIME_FONT_SIZE = 16;
constexpr lv_color_t PROG_TIME_COLOR     = {.blue = 0xCC, .green = 0xCC, .red = 0xCC};

// 컨트롤 버튼
constexpr int32_t    CTRL_PAD_SIDE         = 10;
constexpr int32_t    BTN_HITBOX_PAD        = 20;
constexpr lv_color_t BTN_DIM_COLOR         = {.blue = 0xB3, .green = 0xB3, .red = 0xB3};
constexpr int32_t    PLAY_BTN_SIZE         = 80;
constexpr int32_t    PLAY_BTN_SHADOW_W     = 15;
constexpr int32_t    PLAY_BTN_SHADOW_OFS_Y = 5;
constexpr lv_opa_t   PLAY_BTN_SHADOW_OPA   = static_cast<lv_opa_t>(0.5f * 255);  // 128

// 아이콘 크기
constexpr int32_t SHUFFLE_REPEAT_SIZE = 32;
constexpr int32_t PREV_NEXT_SIZE      = 36;
constexpr int32_t PLAY_ICON_SIZE      = 36;

// 텍스트 그림자 오프셋
constexpr int32_t  TEXT_SHADOW_OFS_Y = 2;
constexpr lv_opa_t TEXT_SHADOW_OPA   = static_cast<lv_opa_t>(0.8f * 255);  // 204
}  // namespace Mode_MediaRemote

// -----------------------------------------------------------------------------
// Mode 3 — 숫자 패드 / 계산기
// -----------------------------------------------------------------------------
namespace Mode_Numpad {
// 상단 디스플레이 (30%) + 하단 버튼 매트릭스 (70%) = 1160px
constexpr int32_t    TOP_H          = 348;  // 384 → 348 (1160 × 30% = 348)
constexpr lv_color_t TOP_BG         = {.blue = 0x12, .green = 0x12, .red = 0x12};
constexpr int32_t    TOP_PAD_TOP    = 10;
constexpr int32_t    TOP_PAD_SIDE   = 40;
constexpr int32_t    TOP_PAD_BOTTOM = 20;

// 토글 버튼
constexpr lv_color_t TOGGLE_BG        = {.blue = 0x2C, .green = 0x2C, .red = 0x2C};
constexpr int32_t    TOGGLE_PAD_V     = 16;
constexpr int32_t    TOGGLE_PAD_H     = 24;
constexpr int32_t    TOGGLE_RADIUS    = 30;
constexpr int32_t    TOGGLE_FONT_SIZE = 18;
constexpr lv_color_t TOGGLE_COLOR     = {.blue = 0x54, .green = 0xB9, .red = 0x1D};  // #1DB954
constexpr int32_t    TOGGLE_BORDER_W  = 1;

// 결과 표시
constexpr int32_t    HISTORY_FONT_SIZE   = 32;
constexpr lv_color_t HISTORY_COLOR       = {.blue = 0x88, .green = 0x88, .red = 0x88};
constexpr int32_t    HISTORY_MARGIN_B    = 10;
constexpr int32_t    RESULT_FONT_SIZE    = 80;
constexpr int32_t    RESULT_LETTER_SPACE = 2;

// 하단 버튼 매트릭스 (70%)
constexpr int32_t    MATRIX_H    = 812;  // 896 → 812 (1160 × 70% = 812)
constexpr lv_color_t MATRIX_BG   = {.blue = 0x12, .green = 0x12, .red = 0x12};
constexpr int32_t    MATRIX_PAD  = 10;
constexpr int32_t    MATRIX_GAP  = 4;
constexpr int32_t    MATRIX_COLS = 4;
constexpr int32_t    MATRIX_ROWS = 5;

// 버튼 스타일
constexpr lv_color_t BTN_BG        = {.blue = 0x2C, .green = 0x2C, .red = 0x2C};
constexpr int32_t    BTN_RADIUS    = 12;
constexpr int32_t    BTN_FONT_SIZE = 48;

// 연산자 버튼 (#FF9F0A 오렌지)
constexpr lv_color_t BTN_OP_BG = {.blue = 0x0A, .green = 0x9F, .red = 0xFF};

// 기능 버튼 (C — #A5A5A5 회색, 검정 글씨)
constexpr lv_color_t BTN_FN_BG    = {.blue = 0xA5, .green = 0xA5, .red = 0xA5};
constexpr lv_color_t BTN_FN_COLOR = {.blue = 0x00, .green = 0x00, .red = 0x00};

constexpr int32_t BACKSPACE_ICON_SIZE = 36;
}  // namespace Mode_Numpad

inline lv_obj_t* create_clean_obj(lv_obj_t* parent) {
  lv_obj_t* obj = lv_obj_create(parent);
  if (obj) {
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  }
  return obj;
}

inline lv_obj_t* create_icon_or_fallback(lv_obj_t* parent, lv_draw_buf_t* icon_cache, const char* fallback_symbol, int32_t size, lv_color_t color,
                                         lv_font_t* fallback_font = nullptr) {
  if (icon_cache) {
    lv_obj_t* img = lv_image_create(parent);
    lv_image_set_src(img, icon_cache);
    lv_obj_set_size(img, size, size);
    lv_obj_set_style_image_recolor(img, color, 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
    return img;
  } else {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, fallback_symbol);
    lv_obj_set_style_text_color(label, color, 0);
    if (fallback_font) {
      lv_obj_set_style_text_font(label, fallback_font, 0);
    }
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    return label;
  }
}

}  // namespace Display::UI
