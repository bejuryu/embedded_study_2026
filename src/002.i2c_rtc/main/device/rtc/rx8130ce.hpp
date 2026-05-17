#pragma once
#include <expected>

#include "hal/i2c_device.hpp"
#include "rtc_common.hpp"

namespace device::rtc {

class RX8130CE {
 public:
  explicit RX8130CE(i2c_master_bus_handle_t master_bus);
  ~RX8130CE() = default;
  RX8130CE(const RX8130CE&) = delete;
  RX8130CE& operator=(const RX8130CE&) = delete;

  [[nodiscard]] std::expected<DateTime, esp_err_t> get_time() const;
  [[nodiscard]] std::expected<DateTime, esp_err_t> set_time(const DateTime& date_time) const;

 private:
  [[nodiscard]] std::expected<void, esp_err_t> set_stop(bool stop) const;
  [[nodiscard]] std::expected<uint8_t, esp_err_t> read_flag_reg() const;
  [[nodiscard]] std::expected<bool, esp_err_t> get_vlf_flag() const;
  [[nodiscard]] std::expected<void, esp_err_t> reset_vlf_flag() const;

  [[nodiscard]] std::expected<DateTime, esp_err_t> get_time_() const;
  [[nodiscard]] std::expected<void, esp_err_t> set_time_(const DateTime& date_time) const;

  hal::I2CDevice<uint8_t, 8, true> i2c_device_;
};

}  // namespace device::rtc