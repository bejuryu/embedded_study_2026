#include "rx8130ce.hpp"

#include "common/util/bcd.hpp"
#include "esp_log.h"

namespace device::rtc {

namespace {
constexpr uint16_t kI2cAddress = 0x32;
constexpr uint32_t kI2cFrequency = 400'000;  // Fast-Mode
constexpr int kBaseYear = 2000;
constexpr uint8_t kWeekDefault = 0;

enum Register : uint8_t {
  SEC = 0x10,
  EXTENSION = 0x1C,
  FLAG = 0x1D,
};

enum TimeOffset : uint8_t {
  OffsetSec = 0,
  OffsetMin,
  OffsetHour,
  OffsetWeek,
  OffsetDay,
  OffsetMonth,
  OffsetYear,
  TimeRegCount
};

enum TimeMask : uint8_t {
  MaskMonth = 0b0001'1111,
  MaskDay = 0b0011'1111,
  MaskHour = 0b0011'1111,
  MaskMin = 0b0111'1111,
  MaskSec = 0b0111'1111
};

enum BitOffset : uint8_t { EXTENSION_STOP = (0x01 << 3), FLAG_VLF = (0x01 << 1) };
}  // namespace

RX8130CE::RX8130CE(i2c_master_bus_handle_t master_bus) : i2c_device_(master_bus, kI2cAddress, kI2cFrequency) {}

std::expected<DateTime, esp_err_t> RX8130CE::get_time() const {
  return get_vlf_flag().and_then([this](const bool vlf_bad) -> std::expected<DateTime, esp_err_t> {
    if (vlf_bad) {
      ESP_LOGE("RTC", "VLF detected! RTC data is invalid. Need to set time.");
      return std::unexpected(ESP_ERR_INVALID_STATE);
    }
    return get_time_();
  });
}

std::expected<DateTime, esp_err_t> RX8130CE::set_time(const DateTime& date_time) const {
  const auto update_time = set_stop(true)
                               .and_then([&]() { return set_time_(date_time); })
                               .and_then([&]() { return set_stop(false); })
                               .and_then([&]() { return reset_vlf_flag(); });
  if (update_time) {
    return date_time;
  }
  return std::unexpected(update_time.error());
}

std::expected<DateTime, esp_err_t> RX8130CE::get_time_() const {
  std::array<uint8_t, TimeRegCount> buffer{};

  const auto result = i2c_device_.read(SEC, buffer);
  if (!result.has_value()) {
    return std::unexpected(result.error());
  }

  using namespace std::chrono;
  using namespace common::util;

  const year y{bcd2dec(buffer[OffsetYear]) + kBaseYear};
  const month m{bcd2dec(buffer[OffsetMonth] & MaskMonth)};
  const day d{bcd2dec(buffer[OffsetDay] & MaskDay)};

  const hours hh{bcd2dec(buffer[OffsetHour] & MaskHour)};
  const minutes mm{bcd2dec(buffer[OffsetMin] & MaskMin)};
  const seconds ss{bcd2dec(buffer[OffsetSec] & MaskSec)};

  return DateTime{.date = year_month_day{y, m, d}, .time = hh_mm_ss<seconds>{hh + mm + ss}};
}

std::expected<void, esp_err_t> RX8130CE::set_time_(const DateTime& date_time) const {
  using namespace common::util;

  std::array<uint8_t, TimeRegCount> buffer{};

  buffer[OffsetSec] = dec2bcd(date_time.time.seconds().count());
  buffer[OffsetMin] = dec2bcd(date_time.time.minutes().count());
  buffer[OffsetHour] = dec2bcd(date_time.time.hours().count());
  buffer[OffsetWeek] = kWeekDefault;
  buffer[OffsetDay] = dec2bcd(static_cast<unsigned>(date_time.date.day()));
  buffer[OffsetMonth] = dec2bcd(static_cast<unsigned>(date_time.date.month()));
  buffer[OffsetYear] = dec2bcd(static_cast<int>(date_time.date.year()) - kBaseYear);

  return i2c_device_.write(SEC, buffer);
}

std::expected<void, esp_err_t> RX8130CE::set_stop(const bool stop) const {
  uint8_t reg = 0;
  return i2c_device_.read(EXTENSION, std::span{&reg, 1}).and_then([&]() -> std::expected<void, esp_err_t> {
    if (stop)
      reg |= EXTENSION_STOP;
    else
      reg &= ~EXTENSION_STOP;
    return i2c_device_.write(EXTENSION, std::span{&reg, 1});
  });
}

std::expected<uint8_t, esp_err_t> RX8130CE::read_flag_reg() const {
  uint8_t reg = 0;
  return i2c_device_.read(FLAG, std::span{&reg, 1}).transform([&]() { return reg; });
}

std::expected<bool, esp_err_t> RX8130CE::get_vlf_flag() const {
  return read_flag_reg().transform([](const uint8_t reg) { return (reg & FLAG_VLF) != 0; });
}

std::expected<void, esp_err_t> RX8130CE::reset_vlf_flag() const {
  return read_flag_reg().and_then([this](uint8_t reg) {
    reg &= ~FLAG_VLF;
    return i2c_device_.write(FLAG, std::span{&reg, 1});
  });
}

}  // namespace device::rtc