#pragma once
#include <esp_err.h>

#include <chrono>
#include <concepts>
#include <expected>
#include <format>
#include <sstream>
#include <string>

namespace device::rtc {

class DateTime {
 public:
  std::chrono::year_month_day date{};
  std::chrono::hh_mm_ss<std::chrono::seconds> time{};

  [[nodiscard]] std::chrono::weekday weekday() const { return std::chrono::sys_days{date}; };

  [[nodiscard]] std::chrono::sys_seconds to_sys_seconds() const {
    return std::chrono::sys_days{date} + time.to_duration();
  }

  [[nodiscard]]
  static DateTime from_sys_seconds(const std::chrono::sys_seconds sys_seconds) {
    const auto days = std::chrono::floor<std::chrono::days>(sys_seconds);
    return {
        .date = std::chrono::year_month_day{days},
        .time = std::chrono::hh_mm_ss{sys_seconds - days},
    };
  }

  [[nodiscard]] std::string to_string() const noexcept { return std::format("{:%Y-%m-%d} {:%H:%M:%S}", date, time); }

  [[nodiscard]] static std::expected<DateTime, esp_err_t> from_string(std::string_view data) noexcept {
    std::chrono::sys_seconds tp;
    std::istringstream in{std::string{data}};

    if (in >> std::chrono::parse("%Y-%m-%d %T", tp)) {
      return from_sys_seconds(tp);
    }

    return std::unexpected(ESP_ERR_INVALID_ARG);
  }
};

template <typename T>
concept RtcDevice = requires(const T rtc, const DateTime& date_time) {
  { rtc.get_time() } -> std::same_as<std::expected<DateTime, esp_err_t>>;
  { rtc.set_time(date_time) } -> std::same_as<std::expected<DateTime, esp_err_t>>;
};

inline void set_system_time(const std::chrono::sys_seconds target_time) {
  const timeval tv{.tv_sec = target_time.time_since_epoch().count(), .tv_usec = 0};
  settimeofday(&tv, nullptr);
}

}  // namespace device::rtc