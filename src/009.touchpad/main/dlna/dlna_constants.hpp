#pragma once
#include <cstdint>
#include <string_view>

namespace Ble::Dlna::Config {
// SSDP 멀티캐스트 정보
constexpr std::string_view kSsdpMulticastIp    = "239.255.255.250";
constexpr uint16_t         kSsdpMulticastPort  = 1900;
constexpr std::string_view kSsdpTargetDmr      = "urn:schemas-upnp-org:device:MediaRenderer:1";
constexpr uint32_t         kSsdpSearchTimeoutS = 3;

// GENA 수신 HTTP 서버 정보
constexpr uint16_t kGenaListenerPort       = 8080;
constexpr float    kGenaRenewalMarginRatio = 0.90f;  // 구독 유효시간의 90% 경과 시 리뉴얼

// 동작 주기 튜닝
constexpr uint32_t kProgressSyncIntervalMs = 5000;   // 5초 재생바 시간 오차 보정 폴링
constexpr uint32_t kSpeakerPingIntervalMs  = 10000;  // 10초 스피커 TCP 생존 확인
constexpr uint32_t kVolumeThrottlingMs     = 150;    // 볼륨 갱신 제어 스로틀
constexpr uint32_t kVolumeMaxClamped       = 100;

// XML 파싱 및 통신 규격 상수
constexpr std::string_view kGenaCallbackUrlFormat     = "http://%s:%d/callback";
constexpr std::string_view kSoapActionGetPositionInfo = "GetPositionInfo";

// 시간 계산 상수
constexpr uint32_t kSecondsPerHour        = 3600;
constexpr uint32_t kSecondsPerMinute      = 60;
constexpr uint32_t kMillisecondsPerSecond = 1000;
}  // namespace Ble::Dlna::Config

namespace Image::Config {
constexpr size_t   kMaxDownloadBytes   = 1024 * 1024;  // 이미지 다운로드 최대 1MB 한계
constexpr uint32_t kMaxResolutionLimit = 1024;         // 최대 해상도 1024px 한계 (P4 32MB PSRAM 활용)
constexpr uint32_t kDrawDeadlineMs     = 2500;         // 최대 이미지 렌더링 데드라인 시간 (2.5초)
}  // namespace Image::Config

namespace Display::UI::Mode_MediaRemote {
constexpr int32_t  kVolumeMaxLevel      = 100;
constexpr int32_t  kVolumeMinLevel      = 0;
constexpr int32_t  kVolumeStep          = 5;
constexpr uint32_t kPlayLockTimeoutMs   = 1000;  // 재생 상태 락 타임아웃
constexpr uint32_t kVolumeLockTimeoutMs = 500;   // 볼륨 상태 락 타임아웃
}  // namespace Display::UI::Mode_MediaRemote

namespace Ble::Dlna {

enum class DlnaCmdType : uint8_t { START_SEARCH, SELECT_TARGET, PLAY, PAUSE, NEXT, PREVIOUS, SET_VOLUME, SHUFFLE_ON, SHUFFLE_OFF, REPEAT_ON, REPEAT_OFF };

struct DlnaCommand {
  DlnaCmdType type;
  uint8_t     value;
  char        target_udn[64];
};

}  // namespace Ble::Dlna
