#include <thread>

#include "bsp/esp-bsp.h"
#include "device/rtc/rx8130ce.hpp"
#include "hal/i2c_bus.hpp"
#include "ui/custom/custom.h"
#include "ui/generated/gui_guider.h"

extern "C" void app_main(void) {
  lv_ui guider_ui;

  {
    hal::I2CBus i2c_bus;

    const device::rtc::RX8130CE rx8130ce{i2c_bus.get_bus_handle()};
    const auto i2c_time = rx8130ce.get_time();
    device::rtc::set_system_time(i2c_time->to_sys_seconds());
  }

  const auto display_handle = bsp_display_start();
  bsp_display_lock(0);
  bsp_display_rotate(display_handle, LV_DISPLAY_ROTATION_90);

  setup_ui(&guider_ui);
  custom_init(&guider_ui);
  bsp_display_unlock();

  bsp_display_brightness_set(25);

  while (true) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}
