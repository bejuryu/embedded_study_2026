#pragma once

#include <string>

#include "font_set.hpp"
#include "lvgl.h"

namespace Display {

enum class ButtonType : uint8_t { NUMBER = 0, OPERATOR, FUNCTION, BACKSPACE, ENTER };

struct ButtonDef {
  const char* text;
  uint8_t     col, col_span, row, row_span;
  ButtonType  type;
  uint8_t     keycode;
  uint8_t     modifiers;
};

class Numpad {
 public:
  void initialize(lv_obj_t* parent_tile, const FontSet& fonts);

  void set_result(const char* result_text);
  void set_history(const char* history_text);

 private:
  lv_obj_t*                top_container_          = nullptr;
  lv_obj_t*                toggle_btn_             = nullptr;
  lv_obj_t*                history_label_          = nullptr;
  lv_obj_t*                result_label_           = nullptr;
  lv_obj_t*                grid_container_         = nullptr;
  static constexpr uint8_t TOTAL_BUTTONS           = 19;
  lv_obj_t*                buttons_[TOTAL_BUTTONS] = {};

  void      create_top_section(lv_obj_t* parent, const FontSet& fonts);
  void      create_button_grid(lv_obj_t* parent, const FontSet& fonts);
  lv_obj_t* create_grid_button(lv_obj_t* grid, const char* text, uint8_t col, uint8_t col_span, uint8_t row, uint8_t row_span, lv_color_t bg, lv_color_t text_color,
                               int32_t font_weight, ButtonType type, const FontSet& fonts);

  // 계산기 상태 변수
  char   input_buffer_[32] = "";     // 현재 텍스트 입력창 버퍼
  double stored_value_     = 0.0;    // 연산용 누적값
  char   current_op_       = '\0';   // 대기 중인 연산자 ('+', '-', '*', '/')
  bool   is_new_input_     = true;   // 다음 입력 시 버퍼를 새로 덮어쓸지 여부
  bool   has_error_        = false;  // 에러 상태(0으로 나누기, NaN 등) 플래그

  static void button_click_cb(lv_event_t* e);
  void        handle_button_press(const ButtonDef* def);

  void handle_number(const char* text);
  void handle_operator(const char* op_text);
  void handle_function(const char* func_text);
  void handle_backspace();
  void handle_enter();

  void calculate_pending();
  void reset_calculator();
  void update_display();

  // 정밀도 보정을 위한 동적 유효 자릿수 계산 헬퍼 (ESP32-P4 하드웨어 최적화)
  double      round_to_significant_figures(double val, int n = 14);
  std::string format_double(double val);
};

}  // namespace Display
