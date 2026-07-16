#include "ble/ble_hid_device.hpp"

#include <cstring>

#include "ble/ble_gatt_services.hpp"
extern "C" {
#include "esp_hosted_misc.h"  // esp_hosted_bt_controller_init/enable (C 헤더, extern "C" 필수)
}
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"  // ble_store_util_status_rr
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/hid/ble_svc_hid.h"  // BLE_SVC_HID_UUID16 (Advertising Data용)
#include "store/config/ble_store_config.h"

static const char* TAG = "BleHid";

namespace Ble {

// ─── 싱글톤 인스턴스 ───
HidDevice& HidDevice::instance() {
  static HidDevice dev;
  return dev;
}

// ─── 초기화 ───
bool HidDevice::initialize() {
  ESP_LOGI(TAG, "BLE HID 장치 초기화 시작...");

  // 1. ESP32-C6 BLE 컨트롤러 초기화 및 활성화
  //    ⚠ 이 두 호출은 nimble_port_init() 이전에 반드시 수행해야 합니다.
  //    esp-hosted 구조에서 BLE 컨트롤러는 슬레이브(ESP32-C6)에 있습니다.
  //    이 호출 없이는 슬레이브의 BLE 컨트롤러가 HCI 명령에 응답하지 않아
  //    BLE_HS_ETIMEOUT_HCI (HCI_Reset 20초 타임아웃) 가 발생합니다.
  if (esp_hosted_bt_controller_init() != ESP_OK) {
    ESP_LOGE(TAG, "esp_hosted_bt_controller_init 실패");
    return false;
  }
  if (esp_hosted_bt_controller_enable() != ESP_OK) {
    ESP_LOGE(TAG, "esp_hosted_bt_controller_enable 실패");
    return false;
  }

  // 2. NimBLE 포트 초기화
  //    ⚠ nimble_port_init()은 BT 컨트롤러 초기화 이후, 그리고
  //    ble_svc_gap_init() 등 GATT API 이전에 반드시 호출해야 합니다.
  nimble_port_init();

  // 3. NimBLE 호스트 콜백 설정
  ble_hs_cfg.sync_cb  = on_stack_sync;
  ble_hs_cfg.reset_cb = on_stack_reset;

  // 4. 보안 매개변수 설정 (HOGP 규격 준수)
  configure_security();

  // 5. GATT 서비스 등록
  if (!Gatt::register_services()) {
    ESP_LOGE(TAG, "GATT 서비스 등록 실패");
    return false;
  }

  // 6. GAP 장치 이름 + Appearance 설정
  // ble_svc_gap_device_appearance_set(): GATT GAP 서비스 Characteristic(UUID 0x2A01)을 설정합니다.
  // 광고 패킷의 fields.appearance와 별개로, Linux BlueZ는 연결 후 이 GATT값을 읽어
  // 장치 타입을 결정합니다. 미설정 시 "Unknown"으로 표시됩니다.
  ble_svc_gap_device_name_set(DeviceInfo::kDeviceName);
  ble_svc_gap_device_appearance_set(kAppearanceTouchpad);  // 0x03C9 = Touchpad

  // 7. NimBLE FreeRTOS 이벤트 루프 태스크 시작
  nimble_port_freertos_init([](void*) {
    nimble_port_run();  // NimBLE 이벤트 루프 (블로킹)
    nimble_port_freertos_deinit();
  });

  state_ = State::Initialized;
  ESP_LOGI(TAG, "BLE HID 장치 초기화 완료, NimBLE sync 대기...");
  return true;
}

// ─── 보안 설정 (HOGP 규격) ───
void HidDevice::configure_security() {
  ble_hs_cfg.sm_io_cap         = BLE_HS_IO_NO_INPUT_OUTPUT;  // Just Works (페어링 PIN 없음)
  ble_hs_cfg.sm_bonding        = 1;                          // 본딩 키를 NVS에 저장 (재연결 시 PIN 없이 복원)
  ble_hs_cfg.sm_mitm           = 0;                          // MITM 보호 비활성
  ble_hs_cfg.sm_sc             = 0;                          // Legacy Pairing 사용
                                                             // ⚠ LE Secure Connections(sm_sc=1)는 esp-hosted VHCI 환경에서
                                                             //   DHKey check failure (SM error 0x0B, status=1035)를 일으킵니다.
                                                             //   Legacy Pairing도 HOGP HID 장치에 충분한 보안 수준입니다.
  ble_hs_cfg.sm_our_key_dist   = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  // 본드 저장소가 가득 찼을 때 가장 오래된 항목을 삭제하고 재시도
  ble_hs_cfg.store_status_cb   = ble_store_util_status_rr;
}

// ─── NimBLE 스택 동기화 완료 콜백 ───
// NimBLE가 코프로세서(ESP32-C6)와 HCI sync를 완료하면 호출됩니다.
// 이 시점에서야 BLE 기능을 사용할 수 있습니다.
void HidDevice::on_stack_sync() {
  ESP_LOGI(TAG, "NimBLE 스택 sync 완료 — Advertising 시작");
  instance().start_advertising();
}

// ─── NimBLE 스택 리셋 콜백 ───
void HidDevice::on_stack_reset(int reason) {
  ESP_LOGW(TAG, "NimBLE 스택 리셋 (reason=%d)", reason);
  instance().state_ = State::Initialized;
}

// ─── Advertising 시작 ───
void HidDevice::start_advertising() {
  struct ble_hs_adv_fields fields = {};

  // Flags: General Discoverable + BR/EDR 미지원
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

  // Appearance: Touchpad (0x03C9)
  fields.appearance            = kAppearanceTouchpad;
  fields.appearance_is_present = 1;

  // HOGP 스펙 요구사항: HID 서비스 UUID(0x1812)를 Advertising Data에 포함
  // (호스트가 연결 전 단계에서 HID 장치임을 인식하는 데 사용)
  static const ble_uuid16_t kAdvUuidHid = BLE_UUID16_INIT(BLE_SVC_HID_UUID16);
  fields.uuids16                        = &kAdvUuidHid;
  fields.num_uuids16                    = 1;
  fields.uuids16_is_complete            = 1;

  // 장치 이름
  const char* name        = ble_svc_gap_device_name();
  fields.name             = reinterpret_cast<const uint8_t*>(name);
  fields.name_len         = static_cast<uint8_t>(std::strlen(name));
  fields.name_is_complete = 1;

  int rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0) {
    ESP_LOGE(TAG, "Advertising 데이터 설정 실패 (rc=%d)", rc);
    return;
  }

  struct ble_gap_adv_params adv_params = {};
  adv_params.conn_mode                 = BLE_GAP_CONN_MODE_UND;  // Undirected Connectable
  adv_params.disc_mode                 = BLE_GAP_DISC_MODE_GEN;  // General Discoverable

  rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, nullptr, BLE_HS_FOREVER, &adv_params, on_gap_event, nullptr);
  if (rc != 0) {
    ESP_LOGE(TAG, "Advertising 시작 실패 (rc=%d)", rc);
    return;
  }

  state_ = State::Advertising;
  ESP_LOGI(TAG, "BLE Advertising 시작 — '%s' 으로 검색됩니다.", name);
}

void HidDevice::stop_advertising() {
  ble_gap_adv_stop();
  if (state_ == State::Advertising) {
    state_ = State::Initialized;
  }
}

// ─── 언페어링 ───
// ⚠ Race Condition 대응: ble_gap_terminate()는 비동기이므로
//   실제 키 삭제와 재광고는 BLE_GAP_EVENT_DISCONNECT 콜백의 is_unpairing_
//   플래그 확인 이후 수행합니다.
void HidDevice::unpair_all() {
  ESP_LOGW(TAG, "모든 본딩 키 삭제 요청");
  is_unpairing_ = true;

  if (conn_handle_ != kNoConnHandle) {
    // 연결 종료 → Disconnect 이벤트에서 ble_store_clear() + start_advertising()
    ble_gap_terminate(conn_handle_, BLE_ERR_REM_USER_CONN_TERM);
  } else {
    // 이미 연결 없음 → 즉시 키 삭제 후 재광고
    is_unpairing_ = false;
    ble_store_clear();
    start_advertising();
  }
}

// ─── GAP 이벤트 디스패처 ───
int HidDevice::on_gap_event(struct ble_gap_event* event, void* /*arg*/) {
  auto& self = instance();
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      self.handle_connect(event);
      break;
    case BLE_GAP_EVENT_DISCONNECT:
      self.handle_disconnect(event);
      break;
    case BLE_GAP_EVENT_ENC_CHANGE:
      self.handle_encryption_change(event);
      break;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
      self.handle_repeat_pairing(event);
      return BLE_GAP_REPEAT_PAIRING_RETRY;
    case BLE_GAP_EVENT_SUBSCRIBE:
      self.handle_subscribe(event);
      break;
    case BLE_GAP_EVENT_CONN_UPDATE:
      self.handle_conn_update(event);
      break;
    default:
      break;
  }
  return 0;
}

// ─── 연결 이벤트 처리 ───
void HidDevice::handle_connect(const struct ble_gap_event* event) {
  if (event->connect.status != 0) {
    ESP_LOGW(TAG, "연결 실패 (status=%d), 재광고", event->connect.status);
    start_advertising();
    return;
  }

  conn_handle_ = event->connect.conn_handle;
  state_       = State::Connected;
  ESP_LOGI(TAG, "호스트 PC 연결 성공 (handle=%d)", conn_handle_);

  // Connection Parameter Update 요청 (터치 반응성: 15ms CI)
  // 호스트 OS가 거부할 수 있으므로 실패해도 계속 진행합니다.
  struct ble_gap_upd_params params = {
      .itvl_min            = ConnParam::kIntervalMin,
      .itvl_max            = ConnParam::kIntervalMax,
      .latency             = ConnParam::kLatency,
      .supervision_timeout = ConnParam::kSupervisionTimeout,
      .min_ce_len          = 0,
      .max_ce_len          = 0,
  };
  int rc = ble_gap_update_params(conn_handle_, &params);
  if (rc != 0) {
    ESP_LOGW(TAG, "Connection Parameter Update 요청 실패 (rc=%d) — 기본값 사용", rc);
  }

  // ⚠ Peripheral은 보안을 직접 개시하지 않습니다.
  //   HOGP 규격에 따라 Central(Windows)이 GATT 서비스 탐색 후
  //   'Insufficient Authentication' 응답을 받으면 스스로 페어링을 시작합니다.
  //   Peripheral이 ble_gap_security_initiate()를 호출하면 SM 상태가 오염되어
  //   DHKey check failure (SM error 0x0B)가 발생합니다.
  state_ = State::Securing;
  ESP_LOGI(TAG, "보안 협상 대기 — Windows 측 페어링 개시를 기다립니다.");
}

// ─── 연결 해제 이벤트 처리 ───
void HidDevice::handle_disconnect(const struct ble_gap_event* event) {
  conn_handle_ = kNoConnHandle;

  // is_unpairing_ 플래그로 unpair_all()의 비동기 race condition 해결
  if (is_unpairing_) {
    is_unpairing_ = false;
    ble_store_clear();  // 연결 완전 종료 후 키 삭제
    ESP_LOGW(TAG, "언페어링 완료 — 모든 본딩 키 삭제됨");
  }

  state_ = State::Advertising;
  ESP_LOGW(TAG, "연결 종료 (reason=0x%02x), 재광고", event->disconnect.reason);
  start_advertising();
}

// ─── 암호화 변경 이벤트 처리 ───
void HidDevice::handle_encryption_change(const struct ble_gap_event* event) {
  if (event->enc_change.status == 0) {
    state_ = State::Bonded;
    ESP_LOGI(TAG, "보안 채널 수립 완료 — HID Report 전송 가능");
  } else {
    ESP_LOGE(TAG, "암호화 변경 실패 (status=%d)", event->enc_change.status);
    // 보안 실패 시 연결을 유지하나 HID Report 전송은 차단됨 (is_report_ready = false)
  }
}

// ─── 페어링 키 불일치 이벤트 처리 ───
void HidDevice::handle_repeat_pairing(const struct ble_gap_event* event) {
  // ESP32 측의 구 키를 삭제하고 재시도
  struct ble_gap_conn_desc desc;
  const int                rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
  if (rc == 0) {
    ble_store_util_delete_peer(&desc.peer_id_addr);
    ESP_LOGW(TAG, "페어링 키 불일치 — 기존 키 삭제 후 재시도");
  } else {
    ESP_LOGE(TAG, "repeat_pairing: conn_find 실패 (rc=%d)", rc);
  }
  // BLE_GAP_REPEAT_PAIRING_RETRY는 on_gap_event에서 반환
}

// ─── CCCD Subscribe 이벤트 처리 (IMPROVE-002) ───
// 호스트가 Notification을 Enable/Disable할 때 발생합니다.
void HidDevice::handle_subscribe(const struct ble_gap_event* event) {
  const uint16_t handle = event->subscribe.attr_handle;

  // ⚠ NimBLE의 subscribe 이벤트 attr_handle은 CCCD 핸들이 아니라
  //   해당 Characteristic의 "Value Handle"입니다 (bleprph 예제와 동일).
  //   이전 코드는 val_handle+1과 비교하여 항상 "Unknown"으로 로깅되던 버그가 있었음.
  const char* name = "Unknown";
  if (handle == Gatt::touch_report_handle) {
    name = "Touch";
  } else if (handle == Gatt::consumer_report_handle) {
    name = "Consumer";
  } else if (handle == Gatt::keyboard_report_handle) {
    name = "Keyboard";
  } else if (handle == Gatt::mouse_report_handle) {
    name = "Mouse";
  }

  ESP_LOGI(TAG, "[GATT] 구독(Subscribe) 변경 - %s (핸들: %d), 알림설정(Notify): %d, 이전설정: %d", name, handle, event->subscribe.cur_notify, event->subscribe.prev_notify);
  // 필요 시 attr_handle별로 is_notify_enabled_ 플래그를 관리할 수 있습니다.
  // 현재는 Bonded 상태 확인(is_report_ready())으로 대부분의 경우를 처리합니다.
}

// ─── Connection Parameter Update 결과 이벤트 처리 (IMPROVE-002) ───
void HidDevice::handle_conn_update(const struct ble_gap_event* event) {
  if (event->conn_update.status == 0) {
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(conn_handle_, &desc) == 0) {
      ESP_LOGI(TAG, "Connection Parameter Update 완료: itvl=%d×1.25ms, latency=%d, supervision=%d×10ms", desc.conn_itvl, desc.conn_latency, desc.supervision_timeout);
    }
  } else {
    ESP_LOGW(TAG, "Connection Parameter Update 실패 (status=%d) — OS 기본값 사용", event->conn_update.status);
  }
}

}  // namespace Ble
