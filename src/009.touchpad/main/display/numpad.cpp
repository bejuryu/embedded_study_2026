#include "numpad.hpp"

#include <algorithm>
#include <cmath>

#include "ble/ble_hid_device.hpp"
#include "ble/ble_hid_report.hpp"
#include "constants.hpp"
#include "ui_icons.hpp"

namespace Display {

namespace {

constexpr ButtonDef kButtons[] = {
    {"C", 0, 1, 0, 1, ButtonType::FUNCTION, 0x29, 0},  // ESC
    {"/", 1, 1, 0, 1, ButtonType::OPERATOR, 0x54, 0},  // Keypad /
    {"*", 2, 1, 0, 1, ButtonType::OPERATOR, 0x55, 0},  // Keypad *
    {"", 3, 1, 0, 1, ButtonType::BACKSPACE, 0x2A, 0},  // Backspace

    {"7", 0, 1, 1, 1, ButtonType::NUMBER, 0x5F, 0},    // Keypad 7
    {"8", 1, 1, 1, 1, ButtonType::NUMBER, 0x60, 0},    // Keypad 8
    {"9", 2, 1, 1, 1, ButtonType::NUMBER, 0x61, 0},    // Keypad 9
    {"-", 3, 1, 1, 1, ButtonType::OPERATOR, 0x56, 0},  // Keypad -

    {"4", 0, 1, 2, 1, ButtonType::NUMBER, 0x5C, 0},    // Keypad 4
    {"5", 1, 1, 2, 1, ButtonType::NUMBER, 0x5D, 0},    // Keypad 5
    {"6", 2, 1, 2, 1, ButtonType::NUMBER, 0x5E, 0},    // Keypad 6
    {"+", 3, 1, 2, 1, ButtonType::OPERATOR, 0x57, 0},  // Keypad +

    {"1", 0, 1, 3, 1, ButtonType::NUMBER, 0x59, 0},  // Keypad 1
    {"2", 1, 1, 3, 1, ButtonType::NUMBER, 0x5A, 0},  // Keypad 2
    {"3", 2, 1, 3, 1, ButtonType::NUMBER, 0x5B, 0},  // Keypad 3
    {"", 3, 1, 3, 2, ButtonType::ENTER, 0x58, 0},    // Keypad Enter

    {"0", 0, 2, 4, 1, ButtonType::NUMBER, 0x62, 0},  // Keypad 0
    {".", 2, 1, 4, 1, ButtonType::NUMBER, 0x63, 0},  // Keypad .
};
constexpr uint8_t kButtonCount = sizeof(kButtons) / sizeof(kButtons[0]);
}  // namespace

void Numpad::initialize(lv_obj_t* parent_tile, const FontSet& fonts) {
  create_top_section(parent_tile, fonts);
  create_button_grid(parent_tile, fonts);
  reset_calculator();
}

void Numpad::create_top_section(lv_obj_t* parent, const FontSet& fonts) {
  namespace UI_Mode = UI::Mode_Numpad;

  top_container_ = UI::create_clean_obj(parent);
  lv_obj_set_size(top_container_, UI::Global::SCREEN_W, UI_Mode::TOP_H);
  lv_obj_set_style_bg_color(top_container_, UI_Mode::TOP_BG, 0);
  lv_obj_set_style_bg_opa(top_container_, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_top(top_container_, UI_Mode::TOP_PAD_TOP, 0);
  lv_obj_set_style_pad_hor(top_container_, UI_Mode::TOP_PAD_SIDE, 0);
  lv_obj_set_style_pad_bottom(top_container_, UI_Mode::TOP_PAD_BOTTOM, 0);
  lv_obj_set_flex_flow(top_container_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(top_container_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_border_width(top_container_, 0, 0);

  toggle_btn_ = UI::create_clean_obj(top_container_);
  lv_obj_set_size(toggle_btn_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(toggle_btn_, UI_Mode::TOGGLE_BG, 0);
  lv_obj_set_style_bg_opa(toggle_btn_, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_ver(toggle_btn_, UI_Mode::TOGGLE_PAD_V, 0);
  lv_obj_set_style_pad_hor(toggle_btn_, UI_Mode::TOGGLE_PAD_H, 0);
  lv_obj_set_style_radius(toggle_btn_, UI_Mode::TOGGLE_RADIUS, 0);
  lv_obj_set_style_border_width(toggle_btn_, UI_Mode::TOGGLE_BORDER_W, 0);
  lv_obj_set_style_border_color(toggle_btn_, UI_Mode::TOGGLE_COLOR, 0);

  lv_obj_t* toggle_label = lv_label_create(toggle_btn_);
  lv_label_set_text(toggle_label, "Numpad");
  lv_obj_set_style_text_color(toggle_label, UI_Mode::TOGGLE_COLOR, 0);
  set_obj_font_if_exists(toggle_label, fonts.font_18);

  lv_obj_t* result_area = UI::create_clean_obj(top_container_);
  lv_obj_set_width(result_area, lv_pct(100));
  lv_obj_set_height(result_area, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(result_area, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(result_area, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);

  history_label_ = lv_label_create(result_area);
  lv_label_set_text(history_label_, "");
  lv_obj_set_style_text_color(history_label_, UI_Mode::HISTORY_COLOR, 0);
  lv_obj_set_style_pad_bottom(history_label_, UI_Mode::HISTORY_MARGIN_B, 0);
  set_obj_font_if_exists(history_label_, fonts.font_32);

  result_label_ = lv_label_create(result_area);
  lv_label_set_text(result_label_, "0");
  lv_obj_set_style_text_color(result_label_, UI::Global::COLOR_WHITE, 0);
  lv_obj_set_style_text_letter_space(result_label_, UI_Mode::RESULT_LETTER_SPACE, 0);
  set_obj_font_if_exists(result_label_, fonts.font_80);
}

void Numpad::create_button_grid(lv_obj_t* parent, const FontSet& fonts) {
  namespace UI_Mode = UI::Mode_Numpad;

  grid_container_ = UI::create_clean_obj(parent);
  lv_obj_set_size(grid_container_, UI::Global::SCREEN_W, UI_Mode::MATRIX_H);
  lv_obj_set_style_bg_color(grid_container_, UI_Mode::MATRIX_BG, 0);
  lv_obj_set_style_bg_opa(grid_container_, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(grid_container_, UI_Mode::MATRIX_PAD, 0);
  lv_obj_set_style_pad_gap(grid_container_, UI_Mode::MATRIX_GAP, 0);
  lv_obj_set_style_border_width(grid_container_, 0, 0);

  // 4열 × 5행 Grid
  static const int32_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static const int32_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(grid_container_, col_dsc, row_dsc);
  lv_obj_set_layout(grid_container_, LV_LAYOUT_GRID);

  for (uint8_t i = 0; i < kButtonCount; ++i) {
    const auto& def = kButtons[i];

    lv_color_t bg, text_color;
    int32_t    font_weight = 400;

    switch (def.type) {
      case ButtonType::OPERATOR:
      case ButtonType::ENTER:
      case ButtonType::BACKSPACE:
        bg          = UI_Mode::BTN_OP_BG;
        text_color  = UI::Global::COLOR_WHITE;
        font_weight = 600;
        break;
      case ButtonType::FUNCTION:
        bg          = UI_Mode::BTN_FN_BG;
        text_color  = UI_Mode::BTN_FN_COLOR;
        font_weight = 600;
        break;
      case ButtonType::NUMBER:
      default:
        bg         = UI_Mode::BTN_BG;
        text_color = UI::Global::COLOR_WHITE;
        break;
    }

    buttons_[i] = create_grid_button(grid_container_, def.text, def.col, def.col_span, def.row, def.row_span, bg, text_color, font_weight, def.type, fonts);
    lv_obj_set_user_data(buttons_[i], const_cast<ButtonDef*>(&kButtons[i]));
    lv_obj_add_event_cb(buttons_[i], button_click_cb, LV_EVENT_CLICKED, this);
  }
}

lv_obj_t* Numpad::create_grid_button(lv_obj_t* grid, const char* text, uint8_t col, uint8_t col_span, uint8_t row, uint8_t row_span, lv_color_t bg, lv_color_t text_color,
                                     int32_t font_weight, ButtonType type, const FontSet& fonts) {
  namespace UI_Mode = UI::Mode_Numpad;

  lv_obj_t* btn = UI::create_clean_obj(grid);
  lv_obj_set_style_bg_color(btn, bg, 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(btn, UI_Mode::BTN_RADIUS, 0);
  lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_set_grid_cell(btn, LV_GRID_ALIGN_STRETCH, col, col_span, LV_GRID_ALIGN_STRETCH, row, row_span);

  switch (type) {
    case ButtonType::BACKSPACE:
    case ButtonType::ENTER: {
      lv_draw_buf_t* icon         = (type == ButtonType::BACKSPACE) ? Icons::cache.backspace : Icons::cache.enter;
      const char*    fallback_sym = (type == ButtonType::BACKSPACE) ? LV_SYMBOL_BACKSPACE : "Enter";
      UI::create_icon_or_fallback(btn, icon, fallback_sym, UI_Mode::BACKSPACE_ICON_SIZE, text_color, fonts.font_24);
      break;
    }
    case ButtonType::NUMBER:
    case ButtonType::OPERATOR:
    case ButtonType::FUNCTION:
    default: {
      lv_obj_t* label = lv_label_create(btn);
      lv_label_set_text(label, text);
      lv_obj_set_style_text_color(label, text_color, 0);
      set_obj_font_if_exists(label, fonts.font_48);
      break;
    }
  }

  lv_obj_set_style_bg_opa(btn, LV_OPA_80, (lv_style_selector_t)LV_PART_MAIN | (lv_style_selector_t)LV_STATE_PRESSED);

  return btn;
}

void Numpad::set_result(const char* result_text) {
  if (result_label_) lv_label_set_text(result_label_, result_text);
}

void Numpad::set_history(const char* history_text) {
  if (history_label_) lv_label_set_text(history_label_, history_text);
}

void Numpad::button_click_cb(lv_event_t* e) {
  Numpad*          self = static_cast<Numpad*>(lv_event_get_user_data(e));
  lv_obj_t*        btn  = static_cast<lv_obj_t*>(lv_event_get_target(e));
  const ButtonDef* def  = static_cast<const ButtonDef*>(lv_obj_get_user_data(btn));
  if (self && def) {
    self->handle_button_press(def);
  }
}

void Numpad::handle_button_press(const ButtonDef* def) {
  if (def->keycode != 0 && Ble::HidDevice::instance().is_report_ready()) {
    Ble::send_keyboard_key(def->modifiers, def->keycode);
  }

  switch (def->type) {
    case ButtonType::NUMBER:
      handle_number(def->text);
      break;
    case ButtonType::OPERATOR:
      handle_operator(def->text);
      break;
    case ButtonType::FUNCTION:
      handle_function(def->text);
      break;
    case ButtonType::BACKSPACE:
      handle_backspace();
      break;
    case ButtonType::ENTER:
      handle_enter();
      break;
  }
}

void Numpad::handle_number(const char* text) {
  if (has_error_) {
    reset_calculator();
    is_new_input_ = false;
  } else if (is_new_input_) {
    input_buffer_[0] = '\0';
    is_new_input_    = false;
  }

  // 소수점 중복 검증
  if (text[0] == '.') {
    if (strchr(input_buffer_, '.') != nullptr) {
      return;
    }
  }

  size_t len = strlen(input_buffer_);

  // 자릿수 (글자 수) 14자 제한
  if (len >= 14) {
    return;
  }

  // "0" 단독 중복 입력 제한
  if (text[0] == '0' && len == 1 && input_buffer_[0] == '0') {
    return;
  }

  // 앞자리에 불필요한 0 소거 (예: "0" 상태에서 "5" 입력 시 "5"가 되도록 함)
  if (len == 1 && input_buffer_[0] == '0' && text[0] != '.') {
    input_buffer_[0] = '\0';
    len              = 0;
  }

  // 소수점 입력 시 특수 보완
  if (text[0] == '.') {
    if (len == 0) {
      strcpy(input_buffer_, "0.");
    } else if (len == 1 && input_buffer_[0] == '-') {
      strcpy(input_buffer_, "-0.");
    } else {
      strcat(input_buffer_, text);
    }
  } else {
    strcat(input_buffer_, text);
  }

  update_display();
}

void Numpad::handle_operator(const char* op_text) {
  if (has_error_) return;

  char next_op = op_text[0];

  if (input_buffer_[0] != '\0' && !is_new_input_) {
    if (current_op_ != '\0') {
      calculate_pending();
    } else {
      char* endptr  = nullptr;
      stored_value_ = std::strtod(input_buffer_, &endptr);
      if (endptr == input_buffer_) {
        stored_value_ = 0.0;
      }
    }
  }

  current_op_      = next_op;
  is_new_input_    = true;
  input_buffer_[0] = '\0';
  update_display();
}

void Numpad::handle_function(const char* func_text) {
  if (func_text[0] == 'C') {
    reset_calculator();
  }
}

void Numpad::handle_backspace() {
  if (has_error_) return;

  // 최종 결과 출력 완료 상태에서의 백스페이스 처리
  if (is_new_input_) {
    if (current_op_ == '\0') {
      std::string formatted = format_double(stored_value_);
      if (formatted.length() < sizeof(input_buffer_)) {
        strcpy(input_buffer_, formatted.c_str());
        is_new_input_ = false;
      } else {
        return;  // 버퍼 초과 방지
      }
    } else {
      return;  // 연산자 입력 직후는 여전히 무시
    }
  }

  size_t len = strlen(input_buffer_);
  if (len > 0) {
    input_buffer_[len - 1] = '\0';
  }

  if (input_buffer_[0] == '\0' || (input_buffer_[0] == '-' && input_buffer_[1] == '\0')) {
    input_buffer_[0] = '\0';
  }

  update_display();
}

void Numpad::handle_enter() {
  if (has_error_) return;

  if (current_op_ != '\0') {
    char*  endptr = nullptr;
    double val    = std::strtod(input_buffer_, &endptr);
    if (endptr == input_buffer_) {
      val = 0.0;
    }

    double prev_stored = stored_value_;
    char   op          = current_op_;

    calculate_pending();

    if (has_error_) {
      update_display();
      return;
    }

    std::string hist = format_double(prev_stored) + " " + op + " " + format_double(val) + " =";
    set_history(hist.c_str());
    set_result(format_double(stored_value_).c_str());

    current_op_      = '\0';
    is_new_input_    = true;
    input_buffer_[0] = '\0';
  }
}

void Numpad::calculate_pending() {
  if (has_error_) return;

  char*  endptr = nullptr;
  double val    = std::strtod(input_buffer_, &endptr);
  if (endptr == input_buffer_) {
    val = 0.0;
  }

  if (current_op_ == '+') {
    stored_value_ += val;
  } else if (current_op_ == '-') {
    stored_value_ -= val;
  } else if (current_op_ == '*') {
    stored_value_ *= val;
  } else if (current_op_ == '/') {
    if (val == 0.0) {
      has_error_ = true;
    } else {
      stored_value_ /= val;
    }
  }

  if (std::isnan(stored_value_) || std::isinf(stored_value_)) {
    has_error_ = true;
  }

  if (!has_error_) {
    stored_value_ = round_to_significant_figures(stored_value_, 14);
  }
}

void Numpad::reset_calculator() {
  input_buffer_[0] = '\0';
  stored_value_    = 0.0;
  current_op_      = '\0';
  is_new_input_    = true;
  has_error_       = false;
  update_display();
}

void Numpad::update_display() {
  if (has_error_) {
    set_result("Error");
    set_history("");
    return;
  }

  if (is_new_input_ && current_op_ != '\0') {
    set_result(format_double(stored_value_).c_str());
  } else {
    if (input_buffer_[0] == '\0') {
      set_result("0");
    } else {
      set_result(input_buffer_);
    }
  }

  if (current_op_ != '\0') {
    std::string hist = format_double(stored_value_) + " " + current_op_;
    set_history(hist.c_str());
  } else {
    // Enter를 친 직후에는 handle_enter()에서 이미 history를 완성했으므로 건드리지 않음
    if (is_new_input_ && input_buffer_[0] == '\0') {
      // 아무것도 하지 않음 (기존 결과 화면 유지)
    } else {
      set_history("");
    }
  }
}

double Numpad::round_to_significant_figures(double val, int n) {
  if (val == 0.0) return 0.0;

  double abs_val = std::abs(val);
  if (abs_val <= 1e-300) return 0.0;

  int    L        = std::floor(std::log10(abs_val));
  int    exponent = std::clamp(n - 1 - L, -15, 15);
  double scale    = std::pow(10.0, exponent);

  return std::round(val * scale) / scale;
}

std::string Numpad::format_double(double val) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%.14g", val);
  std::string s(buf);

  // 화면 크기에 맞추어 최대 11글자로 동적 축소 수행 (소수점이 있을 때만)
  constexpr size_t MAX_VISIBLE_CHARS = 11;
  if (s.length() > MAX_VISIBLE_CHARS && s.find('.') != std::string::npos) {
    size_t dot_pos      = s.find('.');
    size_t int_part_len = dot_pos;

    if (int_part_len >= MAX_VISIBLE_CHARS - 1) {
      snprintf(buf, sizeof(buf), "%.0f", val);
      return std::string(buf);
    } else {
      int  dec_places = MAX_VISIBLE_CHARS - 1 - int_part_len;
      char format_str[16];
      snprintf(format_str, sizeof(format_str), "%%.%df", dec_places);
      snprintf(buf, sizeof(buf), format_str, val);

      std::string res(buf);
      if (res.find('.') != std::string::npos) {
        while (!res.empty() && res.back() == '0') {
          res.pop_back();
        }
        if (!res.empty() && res.back() == '.') {
          res.pop_back();
        }
      }
      return res;
    }
  }
  return s;
}

}  // namespace Display
