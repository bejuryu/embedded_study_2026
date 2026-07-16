// ble/ble_hid_report.cpp
#include "ble/ble_hid_report.hpp"

#include <array>
#include <atomic>

#include "ble/ble_gatt_services.hpp"
#include "ble/ble_hid_device.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_hs.h"

static const char* TAG = "BleReport";

namespace Ble {

// ─── Rate Limiter ───
// BLE Connection Interval(15ms)보다 짧은 간격으로 Report를 전송하면
// NimBLE의 mbuf가 고갈되어 BLE_HS_ENOMEM 오류가 발생합니다.
// kMinReportIntervalUs(12ms)보다 짧은 간격의 Report는 드롭합니다.
//
// ⚠ 스레드 안전성: NimBLE 태스크와 터치 이벤트 태스크가 동시에 접근.
//   std::atomic<int64_t>으로 데이터 레이스 방지 (SIDE-002 수정).
static std::atomic<int64_t> last_report_time_us{0};

// ─── Touch Report (Report ID 1) ───
bool send_touch_report(const std::array<FingerData, Hid::kMaxFingers>& fingers, uint8_t contact_count, bool is_release) {
  auto& device = HidDevice::instance();
  if (!device.is_report_ready()) return false;

  // HID Suspend 상태에서는 전송하지 않음
  // std::atomic<bool>이므로 memory_order_relaxed 로드
  if (Gatt::is_suspended.load(std::memory_order_relaxed)) return false;

  // MS PTP 스펙: 호스트가 Input Mode=3(Touchpad)을 Write하기 전에는
  // PTP 리포트를 보내지 않는다 (Mouse 모드에서는 Report ID 8 사용 — 호출자가 분기).
  if (Gatt::device_mode.load(std::memory_order_relaxed) != Hid::kInputModeTouchpad) {
    return false;
  }
  // Selective Reporting: 호스트가 Surface Switch를 껐으면 표면 접촉 보고 중단
  if (!(Gatt::selective_reporting.load(std::memory_order_relaxed) & Hid::kSelectiveSurfaceSwitch)) {
    return false;
  }

  // Rate Limiting: 최소 전송 간격 확인 (릴리즈 패킷은 레이트 리미팅 제외)
  const int64_t now_us = esp_timer_get_time();
  const int64_t prev   = last_report_time_us.load(std::memory_order_relaxed);
  if (!is_release && (now_us - prev) < ConnParam::kMinReportIntervalUs) {
    return false;  // 최소 간격 미달 — 드롭
  }
  last_report_time_us.store(now_us, std::memory_order_relaxed);

  // ── BitWriter로 24바이트 HID Report 패킹 ──
  // WaratahCmd가 자동 최적화한 비트 레이아웃에 맞춥니다:
  //   [32b × 5 Finger] → [16b ScanTime] → [3b ContactCount + 5b Pad]
  //   → [1b Button 1 + 7b Pad]
  //   = 160 + 16 + 8 + 8 = 192 bits = 24 bytes
  std::array<uint8_t, Hid::kTouchPayloadSize> payload{};
  BitWriter                                   writer(payload.data(), payload.size());

  // [32 bits × 5] Finger 1~5 = 20 bytes
  // 비활성 슬롯(tip_switch=false, touch_valid=false)은 기본값 0
  for (const auto& f : fingers) {
    writer.write(f.touch_valid ? 1u : 0u, 1);  // Confidence (Touch Valid)
    writer.write(f.tip_switch ? 1u : 0u, 1);   // Tip Switch
    writer.write(f.contact_id & 0x07, 3);      // Contact ID (3비트)
    writer.write(0, 3);                        // 3비트 패딩 (정렬용)
    writer.write(f.x & 0x0FFF, 12);            // X (논리 0~4095, 12비트)
    writer.write(f.y & 0x0FFF, 12);            // Y (논리 0~4095, 12비트)
  }

  // [16 bits] Scan Time (100µs 단위, 모노토닉, 랩어라운드 정상)
  const uint16_t scan_time = static_cast<uint16_t>((now_us / 100) & 0xFFFF);
  writer.write(scan_time, 16);

  // [3 bits] Contact Count + [5 bits] Padding
  writer.write(contact_count & 0x07, 3);
  writer.write(0, 5);

  // [1 bit] Button 1 + [7 bits] Padding
  writer.write(0, 1);
  writer.write(0, 7);

  // BitWriter overflow 감지 (IMPROVE-001)
  // 160비트(20바이트)가 정확히 채워져야 합니다.
  if (writer.overflowed()) {
    ESP_LOGE(TAG, "BitWriter overflow — Report Descriptor와 payload 크기 불일치");
    return false;
  }

  // BLE Notification 전송
  struct os_mbuf* om = ble_hs_mbuf_from_flat(payload.data(), payload.size());
  if (om != nullptr) {
    const int rc = ble_gatts_notify_custom(device.conn_handle(), Gatt::touch_report_handle, om);
    if (rc == 0) {
      return true;
    } else {
      // BLE_HS_EINVAL: CCCD Notification이 아직 Enable되지 않은 경우 등
      // Bonded 직후 CCCD 활성화 전 짧은 구간에서 발생할 수 있음 (정상)
      ESP_LOGW(TAG, "Touch Notify 실패 (rc=%d)", rc);
      return false;
    }
  }
  return false;
}

// ─── Mouse Report (Report ID 8) ───
// Input Mode=0(Mouse)일 때의 PTP 호환 동작 — 상대 좌표 이동.
// 호스트가 Input Mode=3을 Write하기 전(또는 PTP 미지원 호스트)에 사용됩니다.
bool send_mouse_report(uint8_t buttons, int16_t dx, int16_t dy) {
  auto& device = HidDevice::instance();
  if (!device.is_report_ready()) return false;
  if (Gatt::is_suspended.load(std::memory_order_relaxed)) return false;

  // Touchpad 모드에서는 Mouse 리포트를 보내지 않음 (호출자 분기 실수 방어)
  if (Gatt::device_mode.load(std::memory_order_relaxed) == Hid::kInputModeTouchpad) {
    return false;
  }

  // Rate Limiting (터치 리포트와 동일한 제한 공유)
  const int64_t now_us = esp_timer_get_time();
  const int64_t prev   = last_report_time_us.load(std::memory_order_relaxed);
  if ((now_us - prev) < ConnParam::kMinReportIntervalUs) {
    return false;
  }
  last_report_time_us.store(now_us, std::memory_order_relaxed);

  // Mouse Report: Buttons(1B) + X(2B) + Y(2B) = 5바이트 (16-bit Relative)
  uint8_t payload[5] = {
      static_cast<uint8_t>(buttons & 0x03), static_cast<uint8_t>(dx & 0xFF),        static_cast<uint8_t>((dx >> 8) & 0xFF),
      static_cast<uint8_t>(dy & 0xFF),      static_cast<uint8_t>((dy >> 8) & 0xFF),
  };

  struct os_mbuf* om = ble_hs_mbuf_from_flat(payload, sizeof(payload));
  if (om == nullptr) return false;

  const int rc = ble_gatts_notify_custom(device.conn_handle(), Gatt::mouse_report_handle, om);
  if (rc != 0) {
    ESP_LOGD(TAG, "Mouse Notify 실패 (rc=%d)", rc);
    return false;
  }
  return true;
}

// ─── Consumer Report (Report ID 2) ───
void send_consumer_report(uint8_t media_mask) {
  auto& device = HidDevice::instance();
  if (!device.is_report_ready()) return;

  // Consumer Report: 6비트 (6개 버튼) + 2비트 padding = 1바이트
  struct os_mbuf* om = ble_hs_mbuf_from_flat(&media_mask, 1);
  if (om != nullptr) {
    const int rc = ble_gatts_notify_custom(device.conn_handle(), Gatt::consumer_report_handle, om);
    if (rc != 0) {
      ESP_LOGD(TAG, "Consumer Notify 실패 (rc=%d)", rc);
    }
  }
}

// ─── Consumer Key One-Shot (IMPROVE-003) ───
// 키 누름과 해제를 함께 처리하여 key stuck 방지.
void send_consumer_key(uint8_t media_mask, uint32_t hold_ms) {
  send_consumer_report(media_mask);
  vTaskDelay(pdMS_TO_TICKS(hold_ms));
  send_consumer_report(0x00);  // 자동 해제
}

// ─── Keyboard Report (Report ID 3) ───
void send_keyboard_report(uint8_t modifiers, const std::array<uint8_t, 6>& key_codes) {
  auto& device = HidDevice::instance();
  if (!device.is_report_ready()) return;

  // Keyboard Report: Modifiers(1B) + Reserved(1B) + KeyCodes(6B) = 8바이트 (Descriptor 스펙 준수)
  std::array<uint8_t, 8> payload{};
  payload[0] = modifiers;
  payload[1] = 0x00;  // Reserved
  std::copy(key_codes.begin(), key_codes.end(), payload.begin() + 2);

  struct os_mbuf* om = ble_hs_mbuf_from_flat(payload.data(), payload.size());
  if (om != nullptr) {
    const int rc = ble_gatts_notify_custom(device.conn_handle(), Gatt::keyboard_report_handle, om);
    if (rc != 0) {
      ESP_LOGD(TAG, "Keyboard Notify 실패 (rc=%d)", rc);
    }
  }
}

void send_keyboard_key(uint8_t modifiers, uint8_t key_code) {
  auto& device = HidDevice::instance();
  if (!device.is_report_ready()) return;

  std::array<uint8_t, 6> keys{};
  keys[0] = key_code;
  send_keyboard_report(modifiers, keys);

  // 호스트 OS가 키 입력을 인식할 수 있도록 최소 15ms 물리적 딜레이 시뮬레이션
  vTaskDelay(pdMS_TO_TICKS(15));

  // 즉각적으로 릴리즈 리포트를 송신하여 키 고착 예방
  send_keyboard_report(0, {0, 0, 0, 0, 0, 0});
}

}  // namespace Ble
