// ble/ble_gatt_services.cpp
#include "ble/ble_gatt_services.hpp"

#include <cstring>

#include "ble/ble_hid_report.hpp"
#include "ble/constants.hpp"
#include "esp_log.h"
#include "host/ble_uuid.h"
#include "services/dis/ble_svc_dis.h"  // BLE_SVC_DIS_CHR_UUID16_* 상수
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "services/hid/ble_svc_hid.h"  // BLE_SVC_HID_*  상수 및 Report Type
#include "touchpad.h"                  // hidReportDescriptor (Waratah 자동 생성)

static const char* TAG = "BleGatt";

namespace Ble::Gatt {

// ─── BLE UUID 변수 (named lvalue) ───────────────────────────────────────────
//
// BLE_UUID16_DECLARE() 매크로는 C99 compound literal을 사용하므로 C++에서 rvalue.
// C++에서 ble_uuid_t* 포인터를 올바르게 얻으려면 이름 있는 정적 변수(lvalue)로
// 선언 후 .u 필드의 주소를 사용해야 한다.
//
//   ❌ C-only:   .uuid = BLE_UUID16_DECLARE(0x1812)
//   ✅ C++:      .uuid = &kUuid_HidSvc.u
//
// 값은 Bluetooth SIG Assigned Numbers 에서 가져온 표준 UUID 값.
// ────────────────────────────────────────────────────────────────────────────

// Device Information Service (DIS) UUIDs
static const ble_uuid16_t kUuid_DisSvc  = BLE_UUID16_INIT(BLE_SVC_DIS_UUID16);
static const ble_uuid16_t kUuid_MfrName = BLE_UUID16_INIT(BLE_SVC_DIS_CHR_UUID16_MANUFACTURER_NAME);
static const ble_uuid16_t kUuid_PnpId   = BLE_UUID16_INIT(BLE_SVC_DIS_CHR_UUID16_PNP_ID);

// Human Interface Device Service (HIDS) UUIDs
static const ble_uuid16_t kUuid_HidSvc    = BLE_UUID16_INIT(BLE_SVC_HID_UUID16);
static const ble_uuid16_t kUuid_HidInfo   = BLE_UUID16_INIT(BLE_SVC_HID_CHR_UUID16_HID_INFO);
static const ble_uuid16_t kUuid_RptMap    = BLE_UUID16_INIT(BLE_SVC_HID_CHR_UUID16_REPORT_MAP);
static const ble_uuid16_t kUuid_CtrlPt    = BLE_UUID16_INIT(BLE_SVC_HID_CHR_UUID16_HID_CTRL_PT);
static const ble_uuid16_t kUuid_ProtoMode = BLE_UUID16_INIT(BLE_SVC_HID_CHR_UUID16_PROTOCOL_MODE);
static const ble_uuid16_t kUuid_Rpt       = BLE_UUID16_INIT(BLE_SVC_HID_CHR_UUID16_RPT);
static const ble_uuid16_t kUuid_RptRef    = BLE_UUID16_INIT(BLE_SVC_HID_DSC_UUID16_RPT_REF);

// ─── GATT Characteristic Value Handle 변수 ───
uint16_t touch_report_handle             = 0;
uint16_t consumer_report_handle          = 0;
uint16_t keyboard_report_handle          = 0;
uint16_t mouse_report_handle             = 0;  // Mouse Input (Report ID 8)
uint16_t feature_report_handle           = 0;  // Touch Feature (Report ID 1)
uint16_t config_feature_report_handle    = 0;  // Config Feature (Report ID 4)
uint16_t cert_feature_report_handle      = 0;  // PTPHQA blob Feature (Report ID 5)
uint16_t latency_feature_report_handle   = 0;  // Latency Mode Feature (Report ID 6)
uint16_t selective_feature_report_handle = 0;  // Selective Reporting Feature (Report ID 7)

// ─── HID Suspend 상태 ───
// GATT 콜백(NimBLE 태스크)과 send_touch_report()(터치 태스크)가 서로 다른 태스크에서 접근.
// std::atomic<bool>으로 데이터 레이스 방지 (SIDE-002).
std::atomic<bool> is_suspended{false};

// ─── PTP Input(Device) Mode ───
// 초기값 0 = Mouse 모드로 부팅. MS PTP 스펙의 정상 핸드셰이크는
// "장치가 0으로 부팅 → 호스트가 Read로 확인 → 호스트가 3(PTP)으로 Write"이다.
// 터치 태스크(send_touch_report 게이팅)에서도 읽으므로 atomic.
std::atomic<uint8_t> device_mode{Hid::kInputModeMouse};

// ─── Selective Reporting (Feature Report 7) ───
// 기본값: Surface + Button 모두 보고
std::atomic<uint8_t> selective_reporting{Hid::kSelectiveDefault};

// ─── Protocol Mode (0x2A4E) ───
// 기본값: 1 (Report Mode). HOGP HID 필수 요소.
std::atomic<uint8_t> protocol_mode{1};

// ─── 정적 데이터 (GATT Read 시 반환할 바이너리) ───
static const HidInformation kHidInfo = {
    .bcd_hid = Hid::kBcdHid, .country_code = Hid::kCountryCode, .flags = static_cast<uint8_t>(Hid::kFlagRemoteWakeup | Hid::kFlagNormallyConnectable)};

static const PnpId kPnpId = {.vendor_id_source = DeviceInfo::kPnpVendorSourceBluetooth,
                             .vendor_id        = DeviceInfo::kVendorId,
                             .product_id       = DeviceInfo::kProductId,
                             .product_version  = DeviceInfo::kProductVersion};

// ─── Report Reference Descriptor 데이터 ───
// NimBLE의 `arg` 포인터로 전달하여 handle 오프셋 의존 없이 안전하게 식별.
static const ReportReference kRefTouchInput       = {Hid::kReportIdTouch, BLE_SVC_HID_RPT_TYPE_INPUT};
static const ReportReference kRefConsumerInput    = {Hid::kReportIdConsumer, BLE_SVC_HID_RPT_TYPE_INPUT};
static const ReportReference kRefKeyboardInput    = {Hid::kReportIdKeyboard, BLE_SVC_HID_RPT_TYPE_INPUT};
static const ReportReference kRefMouseInput       = {Hid::kReportIdMouse, BLE_SVC_HID_RPT_TYPE_INPUT};
static const ReportReference kRefFeature          = {Hid::kReportIdTouchFeature, BLE_SVC_HID_RPT_TYPE_FEATURE};
static const ReportReference kRefFeatureConfig    = {Hid::kReportIdConfig, BLE_SVC_HID_RPT_TYPE_FEATURE};
static const ReportReference kRefFeatureCert      = {Hid::kReportIdCertBlob, BLE_SVC_HID_RPT_TYPE_FEATURE};
static const ReportReference kRefFeatureLatency   = {Hid::kReportIdLatency, BLE_SVC_HID_RPT_TYPE_FEATURE};
static const ReportReference kRefFeatureSelective = {Hid::kReportIdSelective, BLE_SVC_HID_RPT_TYPE_FEATURE};

// ─── Feature Report 상태 ───
static uint8_t feature_contact_count_max = Hid::kMaxFingers;
static uint8_t feature_latency_mode      = Hid::kLatencyModeNormal;

// ─── PTPHQA 인증 blob (Feature Report 5, 256바이트) ───
// MS 공식 기본 blob — 인증받지 않은 장치는 이 기본값을 보고해야 한다.
// 출처: MS Learn "Windows Precision Touchpad Collection"
// (touchpad-windows-precision-touchpad-collection, Device Certification Status
//  Feature Report 섹션: "Prior to a device receiving a 256-byte blob indicating
//  its certification status, it should implement a default blob as follows")
static const uint8_t kCertBlob[Hid::kCertBlobSize] = {
    0xfc, 0x28, 0xfe, 0x84, 0x40, 0xcb, 0x9a, 0x87, 0x0d, 0xbe, 0x57, 0x3c, 0xb6, 0x70, 0x09, 0x88, 0x07, 0x97, 0x2d, 0x2b, 0xe3, 0x38, 0x34, 0xb6, 0x6c, 0xed, 0xb0, 0xf7, 0xe5,
    0x9c, 0xf6, 0xc2, 0x2e, 0x84, 0x1b, 0xe8, 0xb4, 0x51, 0x78, 0x43, 0x1f, 0x28, 0x4b, 0x7c, 0x2d, 0x53, 0xaf, 0xfc, 0x47, 0x70, 0x1b, 0x59, 0x6f, 0x74, 0x43, 0xc4, 0xf3, 0x47,
    0x18, 0x53, 0x1a, 0xa2, 0xa1, 0x71, 0xc7, 0x95, 0x0e, 0x31, 0x55, 0x21, 0xd3, 0xb5, 0x1e, 0xe9, 0x0c, 0xba, 0xec, 0xb8, 0x89, 0x19, 0x3e, 0xb3, 0xaf, 0x75, 0x81, 0x9d, 0x53,
    0xb9, 0x41, 0x57, 0xf4, 0x6d, 0x39, 0x25, 0x29, 0x7c, 0x87, 0xd9, 0xb4, 0x98, 0x45, 0x7d, 0xa7, 0x26, 0x9c, 0x65, 0x3b, 0x85, 0x68, 0x89, 0xd7, 0x3b, 0xbd, 0xff, 0x14, 0x67,
    0xf2, 0x2b, 0xf0, 0x2a, 0x41, 0x54, 0xf0, 0xfd, 0x2c, 0x66, 0x7c, 0xf8, 0xc0, 0x8f, 0x33, 0x13, 0x03, 0xf1, 0xd3, 0xc1, 0x0b, 0x89, 0xd9, 0x1b, 0x62, 0xcd, 0x51, 0xb7, 0x80,
    0xb8, 0xaf, 0x3a, 0x10, 0xc1, 0x8a, 0x5b, 0xe8, 0x8a, 0x56, 0xf0, 0x8c, 0xaa, 0xfa, 0x35, 0xe9, 0x42, 0xc4, 0xd8, 0x55, 0xc3, 0x38, 0xcc, 0x2b, 0x53, 0x5c, 0x69, 0x52, 0xd5,
    0xc8, 0x73, 0x02, 0x38, 0x7c, 0x73, 0xb6, 0x41, 0xe7, 0xff, 0x05, 0xd8, 0x2b, 0x79, 0x9a, 0xe2, 0x34, 0x60, 0x8f, 0xa3, 0x32, 0x1f, 0x09, 0x78, 0x62, 0xbc, 0x80, 0xe3, 0x0f,
    0xbd, 0x65, 0x20, 0x08, 0x13, 0xc1, 0xe2, 0xee, 0x53, 0x2d, 0x86, 0x7e, 0xa7, 0x5a, 0xc5, 0xd3, 0x7d, 0x98, 0xbe, 0x31, 0x48, 0x1f, 0xfb, 0xda, 0xaf, 0xa2, 0xa8, 0x6a, 0x89,
    0xd6, 0xbf, 0xf2, 0xd3, 0x32, 0x2a, 0x9a, 0xe4, 0xcf, 0x17, 0xb7, 0xb8, 0xf4, 0xe1, 0x33, 0x08, 0x24, 0x8b, 0xc4, 0x43, 0xa5, 0xe5, 0x24, 0xc2};
static_assert(sizeof(kCertBlob) == 256, "PTPHQA blob must be exactly 256 bytes");

// ─── Feature Write payload 파싱 헬퍼 ───
// HOGP 스펙상 GATT Write payload에는 Report ID가 포함되지 않지만,
// 일부 호스트 스택이 Report ID를 앞에 붙여 보내는 사례가 관찰되어
// (기존 Device Mode 핸들러에서 확인) 두 형식을 모두 수용한다.
static bool parse_feature_byte(struct os_mbuf* om, uint8_t report_id, uint8_t* out) {
  const uint16_t len = OS_MBUF_PKTLEN(om);
  if (len < 1) return false;

  uint8_t        buf[8]   = {};
  const uint16_t copy_len = len > sizeof(buf) ? sizeof(buf) : len;
  os_mbuf_copydata(om, 0, copy_len, buf);

  if (len >= 2 && buf[0] == report_id) {
    ESP_LOGW(TAG, "  -> [감지] Host가 Report ID(%d) 포함 전송 — [1]바이트 사용", report_id);
    *out = buf[1];
  } else {
    *out = buf[0];
  }
  return true;
}

// ─── GATT Read 콜백 (Device Information Service) ───
static int dis_access_cb(uint16_t /*conn_handle*/, uint16_t /*attr_handle*/, struct ble_gatt_access_ctxt* ctxt, void* /*arg*/) {
  const uint16_t uuid = ble_uuid_u16(ctxt->chr->uuid);

  if (uuid == BLE_SVC_DIS_CHR_UUID16_MANUFACTURER_NAME) {
    return os_mbuf_append(ctxt->om, DeviceInfo::kManufacturerName, std::strlen(DeviceInfo::kManufacturerName));
  }
  if (uuid == BLE_SVC_DIS_CHR_UUID16_PNP_ID) {
    return os_mbuf_append(ctxt->om, &kPnpId, sizeof(kPnpId));
  }
  return BLE_ATT_ERR_UNLIKELY;
}

// ─── GATT Read/Write 콜백 (HID Service) ───
static int hids_access_cb(uint16_t /*conn_handle*/, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg) {
  const uint16_t uuid = ble_uuid_u16(ctxt->chr->uuid);

  // ── Report Reference Descriptor 읽기 ──
  if (ctxt->op == BLE_GATT_ACCESS_OP_READ_DSC) {
    const uint16_t dsc_uuid = ble_uuid_u16(ctxt->dsc->uuid);
    if (dsc_uuid == BLE_SVC_HID_DSC_UUID16_RPT_REF) {
      const auto* ref = static_cast<const ReportReference*>(arg);
      if (ref != nullptr) {
        return os_mbuf_append(ctxt->om, ref, sizeof(ReportReference));
      }
    }
    return BLE_ATT_ERR_UNLIKELY;
  }

  // ── HID Information 읽기 ──
  if (uuid == BLE_SVC_HID_CHR_UUID16_HID_INFO) {
    return os_mbuf_append(ctxt->om, &kHidInfo, sizeof(kHidInfo));
  }

  // ── Report Map 읽기 (HID Descriptor 전체 바이트) ──
  if (uuid == BLE_SVC_HID_CHR_UUID16_REPORT_MAP) {
    return os_mbuf_append(ctxt->om, hidReportDescriptor, sizeof(hidReportDescriptor));
  }

  // ── HID Control Point 쓰기 (Suspend / Exit Suspend) ──
  if (uuid == BLE_SVC_HID_CHR_UUID16_HID_CTRL_PT) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
      const uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
      if (len >= 1) {
        uint8_t val = 0;
        os_mbuf_copydata(ctxt->om, 0, 1, &val);
        if (val == Hid::kCtrlSuspend) {
          is_suspended.store(true, std::memory_order_relaxed);
          ESP_LOGI(TAG, "HID Control Point: Suspend");
        } else if (val == Hid::kCtrlExitSuspend) {
          is_suspended.store(false, std::memory_order_relaxed);
          ESP_LOGI(TAG, "HID Control Point: Exit Suspend");
        }
      }
    }
    return 0;
  }

  // ── Protocol Mode 읽기/쓰기 ──
  if (uuid == BLE_SVC_HID_CHR_UUID16_PROTOCOL_MODE) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
      const uint8_t payload = protocol_mode.load(std::memory_order_relaxed);
      ESP_LOGI(TAG, "[GATT] Protocol Mode 읽기 요청 수신 - 응답 값: %d", payload);
      return os_mbuf_append(ctxt->om, &payload, 1);
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
      const uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
      if (len >= 1) {
        uint8_t val = 0;
        os_mbuf_copydata(ctxt->om, 0, 1, &val);
        protocol_mode.store(val, std::memory_order_relaxed);
        ESP_LOGI(TAG, "[GATT] Protocol Mode 쓰기 요청 수신 - 설정 값: %d (%s)", val, val == 1 ? "Report Mode" : "Boot Mode");
      }
    }
    return 0;
  }

  // ── Report Characteristic 읽기/쓰기 ──
  if (uuid == BLE_SVC_HID_CHR_UUID16_RPT) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
      // Feature Report 1: Contact Count Max(1B) + Pad Type(1B) = 2바이트
      if (attr_handle == feature_report_handle) {
        const uint8_t payload[2] = {static_cast<uint8_t>(feature_contact_count_max), static_cast<uint8_t>(Hid::kPadTypeClickPad)};
        ESP_LOGI(TAG, "[GATT] 장치 능력(Feature 1) 읽기 - CCM=%d, PadType=%d (2바이트 응답)", feature_contact_count_max, Hid::kPadTypeClickPad);
        return os_mbuf_append(ctxt->om, payload, 2);
      }
      // Feature Report 4: Input(Device) Mode
      if (attr_handle == config_feature_report_handle) {
        const uint8_t payload = device_mode.load(std::memory_order_relaxed);
        ESP_LOGI(TAG, "[GATT] Input Mode 읽기 요청 수신 - 응답 값: %d", payload);
        return os_mbuf_append(ctxt->om, &payload, 1);
      }
      // Feature Report 5: PTPHQA 인증 blob (256B, ATT long-read로 분할 전송됨)
      if (attr_handle == cert_feature_report_handle) {
        ESP_LOGI(TAG, "[GATT] PTPHQA blob 읽기 요청 수신 - %d바이트 응답", (int)sizeof(kCertBlob));
        return os_mbuf_append(ctxt->om, kCertBlob, sizeof(kCertBlob));
      }
      // Feature Report 6: Latency Mode
      if (attr_handle == latency_feature_report_handle) {
        const uint8_t payload = feature_latency_mode & 0x01;
        ESP_LOGI(TAG, "[GATT] Latency Mode 읽기 요청 수신 - 응답 값: %d", payload);
        return os_mbuf_append(ctxt->om, &payload, 1);
      }
      // Feature Report 7: Selective Reporting
      if (attr_handle == selective_feature_report_handle) {
        const uint8_t payload = selective_reporting.load(std::memory_order_relaxed);
        ESP_LOGI(TAG, "[GATT] Selective Reporting 읽기 요청 수신 - 응답 값: 0x%02X", payload);
        return os_mbuf_append(ctxt->om, &payload, 1);
      }
      // Input Report: 올바른 크기 반환 (SIDE-003)
      if (attr_handle == touch_report_handle) {
        std::array<uint8_t, Hid::kTouchPayloadSize> payload{};
        BitWriter                                   writer(payload.data(), payload.size());
        for (int i = 0; i < Hid::kMaxFingers; ++i) {
          writer.write(0, 1);   // Touch Valid
          writer.write(0, 1);   // Tip Switch
          writer.write(0, 6);   // Padding
          writer.write(i, 8);   // Contact ID (0, 1, 2, 3, 4)
          writer.write(0, 16);  // X
          writer.write(0, 16);  // Y
        }
        writer.write(0, 16);  // Scan Time
        writer.write(0, 8);   // Contact Count
        writer.write(0, 1);   // Button
        writer.write(0, 7);   // Padding

        ESP_LOGI(TAG, "[GATT] Touch Report 읽기 요청 수신 - 고유 Contact ID 포함 %d바이트 응답", (int)payload.size());
        return os_mbuf_append(ctxt->om, payload.data(), payload.size());
      }
      if (attr_handle == consumer_report_handle) {
        uint8_t zero[1] = {};
        return os_mbuf_append(ctxt->om, zero, 1);
      }
      if (attr_handle == keyboard_report_handle) {
        uint8_t zero[7] = {};
        return os_mbuf_append(ctxt->om, zero, 7);
      }
      if (attr_handle == mouse_report_handle) {
        uint8_t zero[5] = {};
        return os_mbuf_append(ctxt->om, zero, 5);
      }
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
      // Feature Report 4: Input(Device) Mode
      // MS PTP 핸드셰이크의 핵심 — 호스트가 3(Touchpad)을 Write하면 PTP 활성화
      if (attr_handle == config_feature_report_handle) {
        uint8_t value = 0;
        ESP_LOGI(TAG, "[GATT] Input Mode 쓰기 요청 수신 - 길이: %d", OS_MBUF_PKTLEN(ctxt->om));
        if (parse_feature_byte(ctxt->om, Hid::kReportIdConfig, &value)) {
          device_mode.store(value, std::memory_order_relaxed);
          ESP_LOGI(TAG, "  -> 최종 Input Mode 설정값: %d (%s)", value, value == Hid::kInputModeTouchpad ? "Touchpad (PTP)" : "Mouse");
        }
        return 0;
      }
      // Feature Report 6: Latency Mode
      if (attr_handle == latency_feature_report_handle) {
        uint8_t value = 0;
        if (parse_feature_byte(ctxt->om, Hid::kReportIdLatency, &value)) {
          feature_latency_mode = value & 0x01;
          ESP_LOGI(TAG, "[GATT] Latency Mode 쓰기: %d (%s)", feature_latency_mode, feature_latency_mode ? "High Latency" : "Normal");
        }
        return 0;
      }
      // Feature Report 7: Selective Reporting
      if (attr_handle == selective_feature_report_handle) {
        uint8_t value = 0;
        if (parse_feature_byte(ctxt->om, Hid::kReportIdSelective, &value)) {
          selective_reporting.store(value & 0x03, std::memory_order_relaxed);
          ESP_LOGI(TAG, "[GATT] Selective Reporting 쓰기: 0x%02X (surface=%d, button=%d)", value & 0x03, value & 0x01, (value >> 1) & 0x01);
        }
        return 0;
      }
    }
  }

  return 0;
}

// ─── GATT 서비스 테이블 ─────────────────────────────────────────────────────
//
// C API 구조체(ble_gatt_svc_def 등)에 지정 초기화자로 일부 필드만 설정하면
// 나머지는 C++ 표준(§11.9.1)에 의해 0 초기화됩니다.
// GCC의 -Wmissing-field-initializers는 이 경우에도 경고를 발생시키므로
// 해당 블록에 한해 국소적으로 억제합니다.
//
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

const struct ble_gatt_svc_def kServiceTable[] = {

    // ═══ 1. Device Information Service (DIS) ═══
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = &kUuid_DisSvc.u,
     .characteristics =
         (struct ble_gatt_chr_def[]){
             {
                 .uuid      = &kUuid_MfrName.u,
                 .access_cb = dis_access_cb,
                 .flags     = BLE_GATT_CHR_F_READ,
             },
             {
                 .uuid      = &kUuid_PnpId.u,
                 .access_cb = dis_access_cb,
                 .flags     = BLE_GATT_CHR_F_READ,
             },
             {0}  // 종단자
         }},

    // ═══ 2. Human Interface Device Service (HIDS) ═══
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = &kUuid_HidSvc.u,
     .characteristics =
         (struct ble_gatt_chr_def[]){

             // ── HID Information ──
             {
                 .uuid      = &kUuid_HidInfo.u,
                 .access_cb = hids_access_cb,
                 .flags     = BLE_GATT_CHR_F_READ,
             },
             // ── Report Map ──
             {
                 .uuid      = &kUuid_RptMap.u,
                 .access_cb = hids_access_cb,
                 .flags     = BLE_GATT_CHR_F_READ,
             },
             // ── HID Control Point ──
             {
                 .uuid      = &kUuid_CtrlPt.u,
                 .access_cb = hids_access_cb,
                 .flags     = BLE_GATT_CHR_F_WRITE_NO_RSP,
             },
             // ── Protocol Mode ──
             {
                 .uuid      = &kUuid_ProtoMode.u,
                 .access_cb = hids_access_cb,
                 .flags     = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP,
             },

             // ── Input Report 1: Touch (Report ID 1) ──
             {
                 .uuid        = &kUuid_Rpt.u,
                 .access_cb   = hids_access_cb,
                 .arg         = const_cast<ReportReference*>(&kRefTouchInput),
                 .descriptors = (struct ble_gatt_dsc_def[]){{
                                                                .uuid      = &kUuid_RptRef.u,
                                                                .att_flags = BLE_ATT_F_READ,
                                                                .access_cb = hids_access_cb,
                                                                .arg       = const_cast<ReportReference*>(&kRefTouchInput),
                                                            },
                                                            {0}},
                 .flags       = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ_ENC,
                 .val_handle  = &touch_report_handle,
             },

             // ── Input Report 2: Consumer Control (Report ID 2) ──
             {
                 .uuid        = &kUuid_Rpt.u,
                 .access_cb   = hids_access_cb,
                 .arg         = const_cast<ReportReference*>(&kRefConsumerInput),
                 .descriptors = (struct ble_gatt_dsc_def[]){{
                                                                .uuid      = &kUuid_RptRef.u,
                                                                .att_flags = BLE_ATT_F_READ,
                                                                .access_cb = hids_access_cb,
                                                                .arg       = const_cast<ReportReference*>(&kRefConsumerInput),
                                                            },
                                                            {0}},
                 .flags       = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ_ENC,
                 .val_handle  = &consumer_report_handle,
             },

             // ── Input Report 3: Keyboard (Report ID 3) ──
             {
                 .uuid        = &kUuid_Rpt.u,
                 .access_cb   = hids_access_cb,
                 .arg         = const_cast<ReportReference*>(&kRefKeyboardInput),
                 .descriptors = (struct ble_gatt_dsc_def[]){{
                                                                .uuid      = &kUuid_RptRef.u,
                                                                .att_flags = BLE_ATT_F_READ,
                                                                .access_cb = hids_access_cb,
                                                                .arg       = const_cast<ReportReference*>(&kRefKeyboardInput),
                                                            },
                                                            {0}},
                 .flags       = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ_ENC,
                 .val_handle  = &keyboard_report_handle,
             },

             // ── Input Report 8: Mouse (PTP 필수 호환 TLC, Input Mode=0에서 사용) ──
             {
                 .uuid        = &kUuid_Rpt.u,
                 .access_cb   = hids_access_cb,
                 .arg         = const_cast<ReportReference*>(&kRefMouseInput),
                 .descriptors = (struct ble_gatt_dsc_def[]){{
                                                                .uuid      = &kUuid_RptRef.u,
                                                                .att_flags = BLE_ATT_F_READ,
                                                                .access_cb = hids_access_cb,
                                                                .arg       = const_cast<ReportReference*>(&kRefMouseInput),
                                                            },
                                                            {0}},
                 .flags       = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ_ENC,
                 .val_handle  = &mouse_report_handle,
             },

             // ── Feature Report: Touch (Report ID 1, Type=Feature) ──
             {
                 .uuid        = &kUuid_Rpt.u,
                 .access_cb   = hids_access_cb,
                 .arg         = const_cast<ReportReference*>(&kRefFeature),
                 .descriptors = (struct ble_gatt_dsc_def[]){{
                                                                .uuid      = &kUuid_RptRef.u,
                                                                .att_flags = BLE_ATT_F_READ,
                                                                .access_cb = hids_access_cb,
                                                                .arg       = const_cast<ReportReference*>(&kRefFeature),
                                                            },
                                                            {0}},
                 .flags       = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC,
                 .val_handle  = &feature_report_handle,
             },

             // ── Feature Report: Config / Input Mode (Report ID 4, Type=Feature) ──
             {
                 .uuid        = &kUuid_Rpt.u,
                 .access_cb   = hids_access_cb,
                 .arg         = const_cast<ReportReference*>(&kRefFeatureConfig),
                 .descriptors = (struct ble_gatt_dsc_def[]){{
                                                                .uuid      = &kUuid_RptRef.u,
                                                                .att_flags = BLE_ATT_F_READ,
                                                                .access_cb = hids_access_cb,
                                                                .arg       = const_cast<ReportReference*>(&kRefFeatureConfig),
                                                            },
                                                            {0}},
                 .flags       = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_WRITE_ENC,
                 .val_handle  = &config_feature_report_handle,
             },

             // ── Feature Report: PTPHQA 인증 blob (Report ID 5, Type=Feature) ──
             // Windows PTP 필수 — 연결 초기화 중 GET_FEATURE로 읽어감
             {
                 .uuid        = &kUuid_Rpt.u,
                 .access_cb   = hids_access_cb,
                 .arg         = const_cast<ReportReference*>(&kRefFeatureCert),
                 .descriptors = (struct ble_gatt_dsc_def[]){{
                                                                .uuid      = &kUuid_RptRef.u,
                                                                .att_flags = BLE_ATT_F_READ,
                                                                .access_cb = hids_access_cb,
                                                                .arg       = const_cast<ReportReference*>(&kRefFeatureCert),
                                                            },
                                                            {0}},
                 .flags       = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC,
                 .val_handle  = &cert_feature_report_handle,
             },

             // ── Feature Report: Latency Mode (Report ID 6, Type=Feature) ──
             {
                 .uuid        = &kUuid_Rpt.u,
                 .access_cb   = hids_access_cb,
                 .arg         = const_cast<ReportReference*>(&kRefFeatureLatency),
                 .descriptors = (struct ble_gatt_dsc_def[]){{
                                                                .uuid      = &kUuid_RptRef.u,
                                                                .att_flags = BLE_ATT_F_READ,
                                                                .access_cb = hids_access_cb,
                                                                .arg       = const_cast<ReportReference*>(&kRefFeatureLatency),
                                                            },
                                                            {0}},
                 .flags       = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_WRITE_ENC,
                 .val_handle  = &latency_feature_report_handle,
             },

             // ── Feature Report: Selective Reporting (Report ID 7, Type=Feature) ──
             {
                 .uuid        = &kUuid_Rpt.u,
                 .access_cb   = hids_access_cb,
                 .arg         = const_cast<ReportReference*>(&kRefFeatureSelective),
                 .descriptors = (struct ble_gatt_dsc_def[]){{
                                                                .uuid      = &kUuid_RptRef.u,
                                                                .att_flags = BLE_ATT_F_READ,
                                                                .access_cb = hids_access_cb,
                                                                .arg       = const_cast<ReportReference*>(&kRefFeatureSelective),
                                                            },
                                                            {0}},
                 .flags       = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_WRITE_ENC,
                 .val_handle  = &selective_feature_report_handle,
             },

             {0}  // Characteristics 종단자
         }},

    {0}  // 서비스 테이블 종단자
};

#pragma GCC diagnostic pop

bool register_services() {
  ble_svc_gap_init();
  ble_svc_gatt_init();

  int rc = ble_gatts_count_cfg(kServiceTable);
  if (rc != 0) {
    ESP_LOGE(TAG, "GATT count_cfg 실패 (rc=%d)", rc);
    return false;
  }

  rc = ble_gatts_add_svcs(kServiceTable);
  if (rc != 0) {
    ESP_LOGE(TAG, "GATT add_svcs 실패 (rc=%d)", rc);
    return false;
  }

  ESP_LOGI(TAG, "GATT 서비스 등록 완료");
  return true;
}

}  // namespace Ble::Gatt
