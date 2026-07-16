#pragma once

#include <cstdint>

// host/ble_hs.h를 namespace Ble 선언 앞에 include해야
// ble_gap_event가 ::ble_gap_event 로 연결됨 (Ble::ble_gap_event forward decl 방지)
#include "ble/constants.hpp"
#include "host/ble_hs.h"

namespace Ble {

/**
 * @brief BLE HID 장치의 전체 라이프사이클을 관리하는 클래스.
 *
 * NimBLE 스택의 C 콜백을 래핑하여 상태 머신 기반으로 제어합니다.
 * NimBLE C API가 단일 인스턴스를 전제하므로 싱글톤으로 구현합니다.
 *
 * 상태 전이:
 *   Uninitialized → Initialized → Advertising → Connected
 *   → Securing → Bonded → Advertising (재광고)
 *
 * HID Report 전송은 Bonded 상태에서만 가능합니다.
 */
class HidDevice {
 public:
  static HidDevice& instance();

  // ─── 라이프사이클 ───

  /**
   * @brief NimBLE 초기화, GATT 등록, 보안 설정, FreeRTOS 태스크 생성.
   * @return true = 성공, false = 실패 (로그 확인)
   *
   * 호출 전 nvs_flash_init()이 완료되어 있어야 합니다.
   * sync_cb가 호출된 이후 Advertising이 자동 시작됩니다.
   */
  bool initialize();

  /** @brief Undirected Connectable Advertising 시작. */
  void start_advertising();

  /** @brief Advertising 중지. */
  void stop_advertising();

  // ─── 상태 조회 ───

  [[nodiscard]] State    state() const { return state_; }
  [[nodiscard]] bool     is_report_ready() const { return state_ == State::Bonded; }
  [[nodiscard]] uint16_t conn_handle() const { return conn_handle_; }

  // ─── 언페어링 ───

  /**
   * @brief 현재 연결을 종료하고 NVS의 모든 본딩 키를 삭제한 후 재광고합니다.
   *
   * ble_gap_terminate()는 비동기이므로, 실제 키 삭제와 재광고는
   * BLE_GAP_EVENT_DISCONNECT 콜백에서 is_unpairing_ 플래그를 확인한 후 수행됩니다.
   */
  void unpair_all();

 private:
  HidDevice()                            = default;
  ~HidDevice()                           = default;
  HidDevice(const HidDevice&)            = delete;
  HidDevice& operator=(const HidDevice&) = delete;

  // ─── NimBLE 콜백 (static → 싱글톤 위임) ───
  static void on_stack_sync();
  static void on_stack_reset(int reason);
  static int  on_gap_event(struct ble_gap_event* event, void* arg);

  // ─── GAP 이벤트 개별 처리 ───
  void handle_connect(const struct ble_gap_event* event);
  void handle_disconnect(const struct ble_gap_event* event);
  void handle_encryption_change(const struct ble_gap_event* event);
  void handle_repeat_pairing(const struct ble_gap_event* event);
  void handle_subscribe(const struct ble_gap_event* event);
  void handle_conn_update(const struct ble_gap_event* event);

  // ─── 보안 설정 ───
  void configure_security();

  // ─── 상태 ───
  State    state_        = State::Uninitialized;
  uint16_t conn_handle_  = kNoConnHandle;  // BLE_HS_CONN_HANDLE_NONE와 동일한 값(0xFFFF)
  bool     is_unpairing_ = false;          // unpair_all() race condition 방지 플래그
};

}  // namespace Ble
