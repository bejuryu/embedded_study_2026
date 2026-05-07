#include <thread>

#include "display.h"

extern "C" void app_main(void) {
  Display display_;
  if (!display_.init()) {
    printf("Display init failed!\n");
    return;
  }

  while (1) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    display_.update();
  }

  display_.uninit();
}
