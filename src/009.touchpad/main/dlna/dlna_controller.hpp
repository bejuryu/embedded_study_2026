#pragma once
#include <mutex>
#include <string>
#include <vector>

#include "dlna_constants.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace Ble::Dlna {

enum class DlnaState : uint8_t { UNINITIALIZED, IDLE, SEARCHING, CONNECTING, SUBSCRIBED, ERROR_DISCONNECTED };

struct DlnaTrack {
  uint32_t    id = 0;
  std::string title;
  std::string artist;
  std::string art_url;
};

struct DlnaDevice {
  std::string friendly_name;
  std::string udn;
  std::string control_url;
  std::string event_sub_url;
  std::string rendering_ctrl_url;
  std::string ip_address;
  bool        has_oh_playlist = false;
  std::string oh_playlist_url;
};

class DlnaController {
 public:
  static DlnaController& instance();

  bool initialize();
  void start_search();
  void select_target(const std::string& udn);

  void send_media_control(const char* action);  // "Play", "Pause", "Next", "Previous"
  void set_volume(uint8_t volume);

  DlnaState                      get_state() const { return state_; }
  const std::vector<DlnaDevice>& get_devices() const { return devices_; }
  std::string                    get_active_device_name() const { return active_target_.friendly_name; }

  QueueHandle_t get_command_queue() const { return command_queue_; }
  QueueHandle_t get_event_queue() const { return event_queue_; }

  // Display Context 등록용 wrapper (의존성 최소화)
  void  set_display_context(void* ctx) { display_ctx_ = ctx; }
  void* get_display_context() const { return display_ctx_; }
  void  set_subscription_sid(const std::string& sid) { subscription_sid_ = sid; }
  void  set_subscription_timeout(uint32_t timeout) { subscription_timeout_ = timeout; }

  // 오픈홈 플레이리스트 대기열 목록 조회
  std::vector<DlnaTrack> get_playlist();
  bool                   update_openhome_playlist();

 private:
  DlnaController()  = default;
  ~DlnaController() = default;

  DlnaState               state_ = DlnaState::UNINITIALIZED;
  std::vector<DlnaDevice> devices_;
  DlnaDevice              active_target_;
  std::string             subscription_sid_;
  uint32_t                subscription_timeout_ = 1800;

  std::vector<DlnaTrack> playlist_;
  uint32_t               last_playlist_token_ = 0xFFFFFFFF;
  std::mutex             playlist_mutex_;

  std::mutex    device_mutex_;
  QueueHandle_t command_queue_    = nullptr;
  QueueHandle_t event_queue_      = nullptr;
  TaskHandle_t  dlna_task_handle_ = nullptr;

  void*       display_ctx_ = nullptr;
  std::string current_title_;
  int         ping_retry_count_ = 0;

  void handle_ssdp_packet(const char* packet, size_t len, const std::string& sender_ip);
  void send_ssdp_msearch();

  bool send_soap_request(const std::string& url, const std::string& service_type, const std::string& action, const std::string& body, std::string* out_response = nullptr);

  bool subscribe_events();
  bool renew_subscription();
  void unsubscribe_events();

  bool check_renderer_alive();

  static void dlna_task(void* arg);
};

}  // namespace Ble::Dlna
