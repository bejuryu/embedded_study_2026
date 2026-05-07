#include "display.h"

LV_IMG_DECLARE(IMAGE_BACKGROUND)
LV_FONT_DECLARE(GmarketSans_36)

bool Display::init() {
  bsp_display_cfg_t cfg = {.lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
                           .buffer_size = BSP_LCD_H_RES * CONFIG_BSP_LCD_DRAW_BUF_HEIGHT,
                           .double_buffer = 1,
                           .flags = {
                               .buff_dma = true,
                               .buff_spiram = false,
                               .sw_rotate = true,
                           }};

  display_ = bsp_display_start_with_config(&cfg);
  if (display_ == nullptr) {
    return false;
  }

  {
    DisplayLockGuard lock;

    bsp_display_rotate(display_, LV_DISPLAY_ROTATION_90);

    screen_ = lv_disp_get_scr_act(display_);
    if (screen_ == nullptr) return false;

    input_device_ = bsp_display_get_input_dev();
    if (input_device_ == nullptr) return false;

    image_background_ = lv_img_create(screen_);
    if (image_background_ == nullptr) return false;

    lv_img_set_src(image_background_, &IMAGE_BACKGROUND);
    lv_obj_center(image_background_);

    label_text_ = lv_label_create(screen_);
    if (label_text_ == nullptr) return false;

    lv_obj_set_style_text_color(label_text_, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_text_, &GmarketSans_36, LV_PART_MAIN);

    if (bsp_display_backlight_on() != ESP_OK) return false;
  }

  update();
  return true;
}

void Display::uninit() {
  DisplayLockGuard lock;
  bsp_display_backlight_off();
}

void Display::update() {
  DisplayLockGuard lock;
  constexpr uint16_t kCounterMax = 100;
  static uint16_t counter = 0;
  if (counter > kCounterMax) counter = 0;

  lv_label_set_text_fmt(label_text_, "Hello, 안녕하세요 [%03u]", counter++);
}
