#pragma once

#include <cstdint>

enum class AppMode : uint8_t { MEDIA_CONTROL, MEDIA_REMOTE, NUMPAD, COUNT };

struct AppState {
  AppMode current_mode           = AppMode::MEDIA_CONTROL;
  bool    is_wifi_connected      = false;
  bool    is_ble_connected       = false;
  bool    is_media_remote_active = false;
};
