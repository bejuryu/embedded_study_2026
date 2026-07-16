#pragma once

#include <array>
#include <cstdint>

#include "sdkconfig.h"
#include "touchpad.h"  // HidReportInput1 을 사용하여 payload 크기 자동 파생

namespace Ble {

// ─── BLE 장치 상태 ───
enum class State : uint8_t {
  Uninitialized,  // NimBLE 미초기화
  Initialized,    // NimBLE 초기화 완료, sync 대기
  Advertising,    // 브로드캐스팅 중, 연결 대기
  Connected,      // 연결됨, 보안 미수립 (과도 상태 — 즉시 Securing 전환)
  Securing,       // SMP 핸드셰이크 진행 중
  Bonded          // 보안 채널 수립 완료, HID 전송 가능
};

// ─── 장치 식별 정보 ───
namespace DeviceInfo {
// PnP ID Vendor ID Source (Bluetooth GATT PnP ID Characteristic 스펙)
// 0x01 = Bluetooth SIG Assigned Company Identifier
// 0x02 = USB Implementer's Forum Assigned Vendor ID
//
// ⚠ 이전에는 Windows 정밀 터치패드 드라이버를 강제로 붙이려는 의도로
//   Microsoft의 실제 USB VID(0x045E, PID 0x0001 — 과거 실존 마우스 제품 조합)를
//   그대로 흉내 냈으나, Windows의 PTP 드라이버 바인딩은 VID/PID가 아니라
//   HID Usage(Digitizer/TouchPad) 구조로 결정된다. 오히려 실존 제품 ID와 겹쳐
//   드라이버/캐시 매칭이 꼬일 위험이 있어 Espressif에 실제로 할당된
//   USB-IF Vendor ID로 되돌린다.
constexpr uint8_t kPnpVendorSourceBluetooth = 0x01;

constexpr uint16_t kVendorId       = 0x02E5;  // Espressif 공식 VID
constexpr uint16_t kProductId      = 0x0002;  // 임의의 PID (Microsoft Mouse ID 오인 방지)
constexpr uint16_t kProductVersion = 0x0100;  // v1.0.0

constexpr auto kManufacturerName = "bejuryu";
constexpr auto kDeviceName       = "TAB5 PTP";
}  // namespace DeviceInfo

// ─── HID 사양 상수 ───
namespace Hid {
// HID Information: v1.11, 국가 코드 없음
constexpr uint16_t kBcdHid      = 0x0111;
constexpr uint8_t  kCountryCode = 0x00;

// HID Information Flags
constexpr uint8_t kFlagRemoteWakeup        = 0x01;
constexpr uint8_t kFlagNormallyConnectable = 0x02;

// Report ID — touchpad.h 매크로에서 파생 (touchpad.h는 수작업 관리)
constexpr uint8_t kReportIdTouch        = HID_REPORT_INPUT1_ID;    // 터치 데이터
constexpr uint8_t kReportIdConsumer     = HID_REPORT_INPUT2_ID;    // 미디어 컨트롤
constexpr uint8_t kReportIdKeyboard     = HID_REPORT_INPUT3_ID;    // 키보드
constexpr uint8_t kReportIdMouse        = HID_REPORT_INPUT8_ID;    // 마우스 (PTP 필수 호환 TLC)
constexpr uint8_t kReportIdTouchFeature = HID_REPORT_FEATURE9_ID;  // Contact Count Max + Pad Type
constexpr uint8_t kReportIdConfig       = HID_REPORT_FEATURE4_ID;  // Input(Device) Mode
constexpr uint8_t kReportIdCertBlob     = HID_REPORT_FEATURE5_ID;  // PTPHQA 인증 blob (256B)
constexpr uint8_t kReportIdLatency      = HID_REPORT_FEATURE6_ID;  // Latency Mode
constexpr uint8_t kReportIdSelective    = HID_REPORT_FEATURE7_ID;  // Selective Reporting
// 주의: kReportIdTouch(Input)와 kReportIdTouchFeature(Feature)는 같은 ID 값(=1)이지만
// Report Type(Input vs Feature)이 다르므로 GATT에서 별개의 Characteristic으로 등록됨.

// PTP Device Mode (Feature Report ID 4)
// HID Usage Tables 1.6:  0=Mouse, 1=Single Input, 2=Multi-Input
// Microsoft PTP 독자 확장: 3=Windows Precision Touchpad
constexpr uint8_t kInputModeMouse    = 0;  // HID 표준
constexpr uint8_t kInputModeTouchpad = 3;  // MS PTP 확장

// Pad Type (Feature Report ID 1 상위 4비트, MS PTP 스펙)
// 0=Depressible(클릭패드), 1=Pressure-pad, 2=Non-clickable(Discrete-pad)
// Input Report에 Button 1(통합 버튼)을 선언했으므로 0(Click-pad)이 일관됨.
// (2=Discrete-pad는 외부 버튼(Button 2/3) 구성을 의미 — 미선언 상태와 모순)
constexpr uint8_t kPadTypeClickPad = 0;

// PTPHQA 인증 blob 크기 (Feature Report ID 5)
// Windows는 usage 존재/응답 여부만 검사하고 내용은 검증하지 않음 (MS Learn 문서)
constexpr size_t kCertBlobSize = sizeof(HidReportFeature5) - 1;  // = 256

// Latency Mode (Feature Report ID 6): 0=Normal, 1=High Latency
constexpr uint8_t kLatencyModeNormal = 0;

// Selective Reporting (Feature Report ID 7)
constexpr uint8_t kSelectiveSurfaceSwitch = 0x01;  // bit0: 표면 접촉 보고
constexpr uint8_t kSelectiveButtonSwitch  = 0x02;  // bit1: 버튼 보고
constexpr uint8_t kSelectiveDefault       = kSelectiveSurfaceSwitch | kSelectiveButtonSwitch;

// HID Control Point (HOGP 1.0, Section 6.1)
// ⚠ Classic BT HID_CONTROL(Suspend=3, Exit=4)과 다름 — BLE HOGP에서는 0, 1만 사용
constexpr uint8_t kCtrlSuspend     = 0x00;  // HOGP 표준
constexpr uint8_t kCtrlExitSuspend = 0x01;  // HOGP 표준

// 최대 동시 터치 수 — menuconfig에서 변경 시 자동 반영
constexpr uint8_t kMaxFingers = CONFIG_ESP_LCD_TOUCH_MAX_POINTS;

// Touch Report Payload 크기 — touchpad.h 재생성 시 자동 반영
// Touch Report Payload 크기 (Report ID 제외, payload만)
// HidReportInput1 구조체 = ReportId(1B) + Payload[20] 이므로 sizeof - 1 = 20
// ⚠ Payload는 필드명이지 타입이 아님 — sizeof(struct HidReportInput1::Payload)은 사용 불가
constexpr size_t kTouchPayloadSize = sizeof(HidReportInput1) - 1;  // = 20

// ─── 터치패드 영역 물리적 제원 및 마우스 감도 스케일링 ───
// 터치패드 가로/세로 물리 크기 (단위: mm)
constexpr float kTouchpadPhysicalWidthMm  = 65.0f;
constexpr float kTouchpadPhysicalHeightMm = 50.2f;

// 터치패드 영역 원시 해상도 (720x580)
constexpr float kTouchpadResolutionX = 720.0f;
constexpr float kTouchpadResolutionY = 580.0f;  // 1280 - 700

// 사용자가 터치패드를 끝에서 끝까지 1회 쓸었을 때의 목표 마우스 포인터 픽셀 이동량 (가속 없을 때 기준)
constexpr float kTargetMouseTravelX = 960.0f;  // FHD 화면 가로의 1/2
constexpr float kTargetMouseTravelY = 540.0f;  // FHD 화면 세로의 1/2

// Windows OS 마우스 포인터 가속(Enhance Pointer Precision) 보정율
// 윈도우 마우스 가속도 왜곡을 감쇄하여 마우스 모드 속도를 안정화하기 위해 8.0f로 복구합니다.
constexpr float kOsAccelerationCorrection = 1.0f;

// 수학적으로 유도된 최종 마우스 감도 스케일 계수
constexpr float kMouseScaleX = (kTargetMouseTravelX / kTouchpadResolutionX) / kOsAccelerationCorrection;  // = 0.1666f
constexpr float kMouseScaleY = (kTargetMouseTravelY / kTouchpadResolutionY) / kOsAccelerationCorrection;  // = 0.1163f

// ─── PTP 논리 최대 좌표 사양 (touchpad.wara의 logicalValueRange [0, 4095]) ───
constexpr uint16_t kPtpLogicalMaxX = 4095;
constexpr uint16_t kPtpLogicalMaxY = 4095;
}  // namespace Hid

// ─── BLE Connection Parameter 상수 ───
namespace ConnParam {
// BLE 규격 기본 환산 단위
constexpr uint32_t kBleConnIntervalUnitUs       = 1250;  // 1.25ms (BLE Core Spec)
constexpr uint32_t kBleSupervisionTimeoutUnitMs = 10;    // 10ms   (BLE Core Spec)

// Connection Interval: 15ms 고정 (터치 반응성 최적화)
// ⚠ macOS/iOS는 Min==Max 설정을 거부할 수 있음 — 크로스 플랫폼 필요 시 Max를 20~30ms로 확장
constexpr uint32_t kIntervalMinUs = 15000;  // 15ms
constexpr uint32_t kIntervalMaxUs = 15000;  // 15ms

// Slave Latency: 0 (터치 입력 중 즉시 응답)
constexpr uint16_t kLatency = 0;

// Supervision Timeout: 5초 (일시적 패킷 손실 허용)
constexpr uint32_t kSupervisionTimeoutMs = 5000;

// 컴파일 타임 검증 — 상수명이 위 선언과 정확히 일치해야 합니다
static_assert(kIntervalMinUs % kBleConnIntervalUnitUs == 0, "Connection Interval Min must be a multiple of 1.25ms");
static_assert(kIntervalMaxUs % kBleConnIntervalUnitUs == 0, "Connection Interval Max must be a multiple of 1.25ms");
static_assert(kSupervisionTimeoutMs % kBleSupervisionTimeoutUnitMs == 0, "Supervision Timeout must be a multiple of 10ms");

// 외부 API 매핑용 최종 상수 (NimBLE API에 전달하는 단위로 변환)
constexpr uint16_t kIntervalMin        = kIntervalMinUs / kBleConnIntervalUnitUs;
constexpr uint16_t kIntervalMax        = kIntervalMaxUs / kBleConnIntervalUnitUs;
constexpr uint16_t kSupervisionTimeout = kSupervisionTimeoutMs / kBleSupervisionTimeoutUnitMs;

// Report Rate 제한: BLE Connection Interval보다 짧은 간격으로 보내면 mbuf 오버플로우 발생
constexpr int64_t kMinReportIntervalUs = 12000;  // 12ms

// 터치 폴링 간격 — rate limiter(kMinReportIntervalUs)와 동일하게 유지합니다.
// 폴링이 rate limiter보다 짧으면 제스처 리셋 count=0 프레임이 드롭될 수 있습니다.
constexpr uint32_t kTouchPollIntervalMs = static_cast<uint32_t>(kMinReportIntervalUs / 1000);  // = 12ms
}  // namespace ConnParam

// ─── 터치 추적 상수 ───
namespace Touch {
// Nearest-Neighbor 추적 최대 거리² (pixel²)
// 연속 프레임 간 같은 물리 손가락으로 판단하는 최대 이동 거리의 제곱.
// ST7123은 hardware track_id를 제공하지 않으므로 좌표 거리로 동일 손가락을 판별합니다.
// 200px 이상 이동하면 새 손가락으로 간주합니다. (해상도 720×1280 기준)
constexpr uint32_t kMaxTrackDistSq = 200u * 200u;  // = 40000
}  // namespace Touch

// ─── BLE Appearance (Bluetooth SIG Assigned Numbers) ───
// 0x03C5 = Digitizer/Touch Pad (MS PTP 공식 필수 규격)
constexpr uint16_t kAppearanceTouchpad = 0x03C5;

// 어디서나 사용할 수 있는 편의 상수
// BLE_HS_CONN_HANDLE_NONE (0xFFFF) 를 host/ble_hs.h 없이 표현하기 위해 정의
constexpr uint16_t kNoConnHandle = 0xFFFF;

// ─── 터치 좌표 자료형 ───
// send_touch_report()의 파라미터로 사용 — 함수 인터페이스를 명확히 함
struct FingerData {
  bool     tip_switch  = false;  // 표면 접촉 여부 (릴리즈 보고 시 false)
  bool     touch_valid = false;  // Confidence — 의도된 손가락 접촉 여부.
                                 // ⚠ 릴리즈 보고에서도 true 유지 (false = 팜 리젝션 의미)
  uint8_t  contact_id  = 0;      // finger 추적 ID (0~15) — HID: Contact Identifier
  uint16_t x           = 0;      // X 논리 좌표 (0~1008, 하드웨어 720px × 7/5 스케일)
  uint16_t y           = 0;      // Y 논리 좌표 (0~928, 터치패드 영역 580px × 8/5 스케일)
};

// ─── GATT 바이너리 구조체 (바이트 패킹 필수) ───
// GATT Read 시 구조체를 그대로 바이트 버퍼로 전송 → 컴파일러 패딩 삽입 방지
#pragma pack(push, 1)
struct __attribute__((packed)) HidInformation {
  uint16_t bcd_hid;       // HID 릴리즈 버전 (0x0111 = v1.11)
  uint8_t  country_code;  // 국가 코드 (0x00)
  uint8_t  flags;         // HID 장치 플래그
};

struct __attribute__((packed)) PnpId {
  uint8_t  vendor_id_source;  // VID 출처 (0x01 = Bluetooth SIG)
  uint16_t vendor_id;
  uint16_t product_id;
  uint16_t product_version;
};

struct __attribute__((packed)) ReportReference {
  uint8_t report_id;
  uint8_t report_type;  // Input(0x01) / Output(0x02) / Feature(0x03)
};
#pragma pack(pop)

static_assert(sizeof(PnpId) == 7, "PnpId struct size must be exactly 7 bytes");
static_assert(sizeof(HidInformation) == 4, "HidInformation struct size must be exactly 4 bytes");

}  // namespace Ble
