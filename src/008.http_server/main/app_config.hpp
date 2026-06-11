#pragma once
#include <list>
#include <string>

class AppConfig {
 public:
  struct WiFiItem {
    std::string ssid;
    std::string password;
  };
  struct Http {
    uint16_t port;
  };
  std::list<WiFiItem> wifi_list;
  Http http;
  Http https;

  static AppConfig get_default();
  bool verified() const;
  static AppConfig load(const std::string& path);

  explicit operator bool() const { return is_verified; }

 private:
  bool is_verified = false;
};