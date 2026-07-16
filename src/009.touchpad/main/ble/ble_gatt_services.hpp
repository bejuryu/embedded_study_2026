#pragma once

#include <atomic>
#include <cstdint>

#include "host/ble_hs.h"

namespace Ble::Gatt {

// GATT 서비스 테이블 (NimBLE C API에 전달)
extern const struct ble_gatt_svc_def kServiceTable[];

// ─── 각 Report Characteristic의 Value Handle ───
// NimBLE가 서비스 등록 시 자동으로 할당합니다.
extern uint16_t touch_report_handle;              // Input Report 1  (터치 데이터)
extern uint16_t consumer_report_handle;           // Input Report 2  (미디어 컨트롤)
extern uint16_t keyboard_report_handle;           // Input Report 3  (키보드)
extern uint16_t mouse_report_handle;              // Input Report 8  (마우스 — PTP 호환 TLC)
extern uint16_t feature_report_handle;            // Feature Report 1 (Contact Count Max + Pad Type)
extern uint16_t config_feature_report_handle;     // Feature Report 4 (Input Mode)
extern uint16_t cert_feature_report_handle;       // Feature Report 5 (PTPHQA blob)
extern uint16_t latency_feature_report_handle;    // Feature Report 6 (Latency Mode)
extern uint16_t selective_feature_report_handle;  // Feature Report 7 (Selective Reporting)

// ─── HID Suspend 상태 ───
// GATT 콜백(NimBLE 태스크)에서 쓰고, send_touch_report()(터치 태스크)에서 읽음.
// 서로 다른 FreeRTOS 태스크 간 접근이므로 std::atomic 사용.
extern std::atomic<bool> is_suspended;

// ─── PTP Input(Device) Mode (Feature Report 4) ───
// 부팅 시 0(Mouse). 호스트(Windows PTP 드라이버 / Linux hid-multitouch)가
// 연결 초기화 중 3(Touchpad)을 Write하면 PTP 리포트 전송이 활성화됩니다.
// GATT 콜백(NimBLE 태스크)에서 쓰고 터치 태스크에서 읽으므로 atomic.
extern std::atomic<uint8_t> device_mode;

// ─── Selective Reporting (Feature Report 7) ───
// bit0=Surface Switch(표면 접촉 보고), bit1=Button Switch(버튼 보고)
extern std::atomic<uint8_t> selective_reporting;

// ─── GATT 서비스 초기화 ───
// ble_gatts_count_cfg + ble_gatts_add_svcs를 호출합니다.
// NimBLE 포트 초기화(nimble_port_init) 이전에 호출해야 합니다.
bool register_services();

}  // namespace Ble::Gatt
