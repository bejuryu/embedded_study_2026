#pragma once

#include <string>

class SystemConfig {
  class Wifi {
   public:
    std::string ssid;
    std::string password;
  };
  class GeoLocation {
   public:
    std::string location;
    float latitude = 0;
    float longitude = 0;
  };

 public:
  bool valid = false;
  Wifi wifi;
  std::string ntp_server;
  std::string timezone;
  GeoLocation geo_location;

  explicit operator bool() const { return valid; }
  static SystemConfig get_default();
};

class Utility {
 public:
  static SystemConfig load_system_config(const std::string &path);
  static bool system_init();

  /**
   * @brief Initializes the SD card using the SDMMC interface.
   * 반드시 BSP 초기화 된 다음 호출
   * @return true if the initialization is successful, false otherwise.
   */
  static bool sdcard_sdmmc_init();
  static bool sntp_sync(const std::string &ntp_server);
};
