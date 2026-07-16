#pragma once

#include <array>
#include <cstdint>
#include <cstring>

#include "ble/constants.hpp"

namespace Ble {

/**
 * @brief 바이트 배열에 임의 비트 폭의 값을 순차적으로 패킹하는 유틸리티.
 *
 * HID Report는 비트 단위로 필드가 정의되며, 바이트 경계를 걸치는 경우가 빈번합니다.
 * 이 클래스를 사용하면 비트 오프셋 계산 실수를 원천 차단할 수 있습니다.
 *
 * LSB-first 패킹: value의 bit 0이 먼저 기록됩니다. HID Report Descriptor의
 * 비트 패킹 순서(섹션 4.3 비트 매핑 테이블)와 정확히 일치합니다.
 *
 * 사용법:
 *   uint8_t buffer[19]{};
 *   BitWriter writer(buffer, sizeof(buffer));
 *   writer.write(0, 1);      // Button 1: 1비트
 *   writer.write(count, 3);  // Contact Count: 3비트
 *   assert(!writer.overflowed());
 */
class BitWriter {
 public:
  BitWriter(uint8_t* buffer, size_t buffer_size) : buffer_(buffer), buffer_size_(buffer_size) { std::memset(buffer_, 0, buffer_size_); }

  /**
   * @brief 현재 비트 위치에 지정된 비트 수만큼 값을 기록합니다.
   * @param value     기록할 값 (하위 bit_count 비트만 사용됨)
   * @param bit_count 기록할 비트 수 (1~16)
   *
   * 버퍼를 초과하면 overflowed_ 플래그가 set되고 나머지 비트는 무시됩니다.
   * 전송 전 overflowed() 로 반드시 확인하십시오.
   */
  void write(uint16_t value, uint8_t bit_count) {
    for (uint8_t i = 0; i < bit_count; ++i) {
      if (bit_pos_ >= buffer_size_ * 8) {
        overflowed_ = true;
        return;
      }
      const size_t  byte_idx = bit_pos_ / 8;
      const uint8_t bit_idx  = static_cast<uint8_t>(bit_pos_ % 8);
      if (value & (1u << i)) {
        buffer_[byte_idx] |= static_cast<uint8_t>(1u << bit_idx);
      }
      ++bit_pos_;
    }
  }

  /** @brief 지금까지 기록된 총 비트 수 */
  [[nodiscard]] size_t bits_written() const { return bit_pos_; }

  /**
   * @brief 버퍼 크기를 초과하는 write 호출이 있었는지 여부.
   * true 이면 payload가 잘려 있으므로 전송하지 않아야 합니다.
   */
  [[nodiscard]] bool overflowed() const { return overflowed_; }

 private:
  uint8_t* buffer_;
  size_t   buffer_size_;
  size_t   bit_pos_    = 0;
  bool     overflowed_ = false;
};

// ─── Report 전송 API ───

/**
 * @brief 5-finger 터치 리포트를 BLE로 전송합니다. (Report ID 1)
 *
 * @param fingers       5개 finger 슬롯 배열 (비활성 슬롯은 기본값 유지)
 * @param contact_count 현재 활성 터치 수 (0~5)
 *
 * - Scan Time은 함수 내부에서 esp_timer 기반으로 자동 생성됩니다.
 * - Bonded 상태가 아니거나 HID Suspend 중이면 조용히 무시합니다.
 * - Input Mode가 3(Touchpad)이 아니면 전송하지 않습니다 (MS PTP 핸드셰이크).
 * - Rate Limiter: kMinReportIntervalUs(12ms)보다 짧은 간격은 드롭합니다.
 *
 * ⚠ PTP 병렬 모드 규약 (호출자 책임):
 *   - 유효 접점은 fingers[0]부터 연속으로 채워야 합니다 (호스트는
 *     contact_count 개수만큼 앞에서부터 파싱하고 나머지는 무시).
 *   - 떨어진 접점은 tip_switch=false, touch_valid=true로 1회 보고하고,
 *     그 접점도 contact_count에 포함해야 합니다.
 */
bool send_touch_report(const std::array<FingerData, Hid::kMaxFingers>& fingers, uint8_t contact_count, bool is_release = false);

/**
 * @brief 마우스 리포트를 전송합니다. (Report ID 8, PTP 호환 TLC)
 *
 * Input Mode=0(Mouse)일 때 사용 — 호스트가 PTP 핸드셰이크(Input Mode=3 Write)를
 * 수행하기 전이거나 PTP 미지원 호스트에서의 폴백 동작입니다.
 *
 * @param buttons bit0=좌클릭, bit1=우클릭
 * @param dx      X 상대 이동량 (-32767~32767)
 * @param dy      Y 상대 이동량 (-32767~32767)
 */
bool send_mouse_report(uint8_t buttons, int16_t dx, int16_t dy);

/**
 * @brief 미디어 제어 리포트를 전송합니다. (Report ID 2)
 *
 * @param media_mask 비트마스크 — Bit0:Play/Pause, Bit1:Next, Bit2:Prev,
 *                   Bit3:VolUp, Bit4:VolDown, Bit5:Mute
 *
 * ⚠ 키를 눌렀으면 반드시 0x00을 추가 호출하여 해제해야 합니다.
 *   send_consumer_key()를 사용하면 자동 해제됩니다.
 */
void send_consumer_report(uint8_t media_mask);

/**
 * @brief 미디어 버튼을 누르고 자동으로 해제하는 one-shot 래퍼.
 *
 * @param media_mask 비트마스크 (send_consumer_report와 동일)
 * @param hold_ms    키를 누르고 있을 시간 (기본값: 50ms)
 *
 * 호출자가 해제 보고를 누락하면 PC에서 키가 고착되므로,
 * 단발성 버튼 이벤트에는 이 함수를 사용하는 것을 권장합니다.
 */
void send_consumer_key(uint8_t media_mask, uint32_t hold_ms = 50);

/**
 * @brief 키보드 입력 리포트를 전송합니다. (Report ID 3)
 *
 * @param modifiers  Modifier 키 비트마스크 (Ctrl, Shift, Alt, GUI)
 * @param key_codes  동시 눌린 최대 6개 키의 스캔 코드
 */
void send_keyboard_report(uint8_t modifiers, const std::array<uint8_t, 6>& key_codes);

/**
 * @brief 키보드 키코드를 전송하고 즉시 해제합니다. (Key Stuck 방지)
 *
 * @param modifiers  Modifier 키 비트마스크 (Ctrl, Shift, Alt, GUI)
 * @param key_code   전송할 USB HID 키보드 스캔 코드
 */
void send_keyboard_key(uint8_t modifiers, uint8_t key_code);

}  // namespace Ble
