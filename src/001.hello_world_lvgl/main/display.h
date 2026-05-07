#pragma once

#include <bsp/esp-bsp.h>

class DisplayLockGuard {
 public:
  DisplayLockGuard() { bsp_display_lock(0); }
  ~DisplayLockGuard() { bsp_display_unlock(); }

  DisplayLockGuard(const DisplayLockGuard&) = delete;
  DisplayLockGuard& operator=(const DisplayLockGuard&) = delete;
};

class Display {
 public:
  bool init();
  void uninit();
  void update();

 private:
  lv_display_t* display_ = nullptr;
  lv_obj_t* screen_ = nullptr;
  lv_indev_t* input_device_ = nullptr;
  lv_obj_t* image_background_ = nullptr;
  lv_obj_t* label_text_ = nullptr;
};
