

#include <iostream>
#include <print>
#include <thread>

#include "device/rtc/rtc_common.hpp"
#include "device/rtc/rx8130ce.hpp"
#include "esp_system.h"
#include "hal/i2c_bus.hpp"

void print_i2c_scan(hal::I2CBus& i2c_bus) noexcept {
  const auto i2c_scan_result = i2c_bus.scan();
  if (i2c_scan_result.empty()) {
    std::print("No I2C devices found\n");
  } else {
    std::print("I2C devices found at: ");
    for (const auto& port : i2c_scan_result) {
      std::print("0x{:02X} ", port);
    }
    std::print("\n");
  }
}

extern "C" void app_main(void) {
  hal::I2CBus i2c_bus;
  print_i2c_scan(i2c_bus);

  const device::rtc::RX8130CE rx8130ce{i2c_bus.get_bus_handle()};
  const auto i2c_time = rx8130ce.get_time();
  device::rtc::set_system_time(i2c_time->to_sys_seconds());

  std::jthread thread_input([&rx8130ce] {
    std::print("Input thread started\r\n>> ");

    std::string str_line;
    constexpr int kMaxLineLength = 128;

    while (true) {
      const auto c = std::fgetc(stdin);

      if (c == EOF) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        continue;
      }
      std::fputc(c, stdout);
      std::fflush(stdout);

      if (c == '\r' || c == '\n') {
        if (str_line == "Q" || str_line == "q") break;
        std::cout << "input text: " << str_line << std::endl;

        const auto updated_rtc = device::rtc::DateTime::from_string(str_line).and_then(
            [&rx8130ce](const auto& date_time) { return rx8130ce.set_time(date_time); });
        if (updated_rtc) {
          device::rtc::set_system_time(updated_rtc.value().to_sys_seconds());
          std::print("RTC Updated\r\n");
        }

        std::print("current date time: {}\r\n>> ", std::chrono::system_clock::now());
        str_line.clear();
      } else {
        str_line += static_cast<char>(c);
        if (str_line.length() > kMaxLineLength) {
          std::cout << "max size text" << std::endl;
          str_line.clear();
        }
      }
    }
    std::print("Input thread stopped");
  });

  thread_input.join();

  esp_restart();
}
