#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace Ble::Dlna {

class DlnaEventListener {
 public:
  DlnaEventListener(QueueHandle_t event_queue);
  ~DlnaEventListener();

  bool start();
  void stop();

 private:
  QueueHandle_t event_queue_          = nullptr;
  TaskHandle_t  listener_task_handle_ = nullptr;
  int           server_fd_            = -1;
  bool          is_running_           = false;

  static void listener_thread(void* arg);
};

}  // namespace Ble::Dlna
