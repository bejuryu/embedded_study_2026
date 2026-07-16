#include "context.hpp"

#include <algorithm>
#include <filesystem>
#include <string>

#include "ble/ble_gatt_services.hpp"  // Gatt::device_mode (PTP/Mouse 모드 분기)
#include "ble/ble_hid_device.hpp"     // BLE 상태 폰링 타이머
#include "ble/ble_hid_report.hpp"     // send_touch_report(), send_mouse_report()
#include "bsp/esp-bsp.h"
#include "constants.hpp"
#include "dlna/dlna_controller.hpp"
#include "esp_heap_caps.h"
#include "esp_lcd_touch.h"  // esp_lcd_touch_get_coordinates()
#include "esp_log.h"
#include "ui_icons.hpp"

namespace Display {

static const char* TAG = "DisplayContext";

namespace {
#if LV_USE_TINY_TTF
lv_font_t* load_font(const std::string& path, int32_t size) {
  if (path.empty()) {
    ESP_LOGW("DisplayContext", "Empty path provided, using internal unscii_8 fallback (size: %ld)", size);
    return const_cast<lv_font_t*>(&lv_font_unscii_8);
  }
  lv_font_t* f = lv_tiny_ttf_create_file(path.c_str(), size);
  if (f == nullptr) {
    ESP_LOGW("DisplayContext", "Failed to load tiny TTF from %s (size: %ld), using unscii_8 fallback", path.c_str(), size);
    return const_cast<lv_font_t*>(&lv_font_unscii_8);
  }
  ESP_LOGI("DisplayContext", "Loaded tiny TTF from %s (size: %ld)", path.c_str(), size);
  return f;
}
#else
lv_font_t* load_font(const std::string& path, int32_t size) {
  ESP_LOGW("DisplayContext", "LV_USE_TINY_TTF is disabled, using internal unscii_8 fallback (size: %ld)", size);
  return const_cast<lv_font_t*>(&lv_font_unscii_8);
}
#endif
}  // namespace

Context::~Context() {
  if (touch_hid_task_handle_) {
    vTaskDelete(touch_hid_task_handle_);
    touch_hid_task_handle_ = nullptr;
  }
  if (font_buffer_) {
    heap_caps_free(font_buffer_);
    font_buffer_ = nullptr;
  }
}

bool Context::initialize() {
  ESP_LOGI(TAG, "Initializing hardware display with config...");

  bsp_display_cfg_t display_cfg           = {.lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
                                             .buffer_size   = BSP_LCD_H_RES * CONFIG_BSP_LCD_DRAW_BUF_HEIGHT,
                                             .double_buffer = true,
                                             .flags         = {
                                                         .buff_dma    = true,
                                                         .buff_spiram = true,
                                                         .sw_rotate   = true,
                                   }};
  display_cfg.lvgl_port_cfg.task_affinity = PRO_CPU_NUM + 1;
  display_handle_                         = bsp_display_start_with_config(&display_cfg);

  if (display_handle_ == nullptr) {
    ESP_LOGE(TAG, "Failed to start hardware display via BSP");
    return false;
  }
  ESP_LOGI(TAG, "Hardware display successfully started");

  {
    bsp_display_lock(0);

    Icons::init_cache();
    load_fonts();
    init_ui_tree();

    bsp_display_unlock();
  }

  // DLNA 컨트롤러에 Display context 주소를 바인딩하고 초기화 기동
  Ble::Dlna::DlnaController::instance().set_display_context(this);
  if (!Ble::Dlna::DlnaController::instance().initialize()) {
    ESP_LOGE(TAG, "Failed to initialize DlnaController");
  }

  bsp_display_brightness_set(5);

  // ─── esp_lcd_touch 핸들 추출 ───
  // LVGL 포트가 저장한 lvgl_port_touch_ctx_t의 첫 번째 필드가 esp_lcd_touch_handle_t입니다.
  // 첫 번째 필드 = offset 0 보장(C 구조체), 안전하게 역참조합니다.
  {
    lv_indev_t* it = lv_indev_get_next(nullptr);
    while (it) {
      if (lv_indev_get_type(it) == LV_INDEV_TYPE_POINTER) {
        void* drv_data = lv_indev_get_driver_data(it);
        if (drv_data) {
          // lvgl_port_touch_ctx_t의 첫 필드가 esp_lcd_touch_handle_t
          touch_handle_ = *static_cast<esp_lcd_touch_handle_t*>(drv_data);
        }
        break;
      }
      it = lv_indev_get_next(it);
    }
  }

  if (touch_handle_) {
    // 터치 패널 직접 폰링 태스크 (12ms 주기, 우선순위 5 = LVGL 태스크보다 낙음)
    xTaskCreate(touch_hid_task, "ble_touch_hid", 4096, this, 5, &touch_hid_task_handle_);
    ESP_LOGI(TAG, "BLE 터치 HID 태스크 시작");
  } else {
    ESP_LOGW(TAG, "touch_handle 추출 실패 — 터치 HID 비활성화");
  }

  ESP_LOGI(TAG, "Display Context initialized successfully");
  return true;
}

void Context::load_fonts() {
  std::string       font_path;
  const std::string mount_point = CONFIG_BSP_SD_MOUNT_POINT;
  const std::string fonts_dir   = mount_point + "/fonts";

  namespace fs = std::filesystem;
  if (fs::exists(fonts_dir) && fs::is_directory(fonts_dir)) {
    for (const auto& entry : fs::directory_iterator(fonts_dir)) {
      if (entry.is_regular_file() && entry.path().extension() == ".ttf") {
        font_path = entry.path().string();
        ESP_LOGI(TAG, "Found target font file in SD card: %s", font_path.c_str());
        break;
      }
    }
  } else {
    ESP_LOGW(TAG, "Could not open SD card directory: %s", fonts_dir.c_str());
  }

  if (!font_path.empty()) {
    FILE* f = fopen(font_path.c_str(), "rb");
    if (f) {
      fseek(f, 0, SEEK_END);
      font_buffer_size_ = ftell(f);
      fseek(f, 0, SEEK_SET);

      font_buffer_ = heap_caps_malloc(font_buffer_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (font_buffer_ == nullptr) {
        ESP_LOGW(TAG, "Failed to allocate memory for font buffer in PSRAM, falling back to internal RAM");
        font_buffer_ = malloc(font_buffer_size_);
      }

      if (font_buffer_) {
        size_t read_bytes = fread(font_buffer_, 1, font_buffer_size_, f);
        fclose(f);
        ESP_LOGI(TAG, "Successfully read font file into memory buffer (%zu/%zu bytes)", read_bytes, font_buffer_size_);
      } else {
        fclose(f);
        ESP_LOGE(TAG, "Failed to allocate memory buffer for font of size %zu", font_buffer_size_);
      }
    } else {
      ESP_LOGE(TAG, "Failed to open font file directly from path: %s", font_path.c_str());
    }
  }

  if (font_buffer_ && font_buffer_size_ > 0) {
    ESP_LOGI(TAG, "Creating tiny TTF fonts from memory data...");
  } else {
    ESP_LOGW(TAG, "No font buffer loaded. Falling back to internal unscii_8 font.");
  }

  auto load_font_set = [&](lv_font_t*& font, int32_t size) {
    if (font_buffer_ && font_buffer_size_ > 0) {
      font = lv_tiny_ttf_create_data(font_buffer_, font_buffer_size_, size);
    } else {
      font = load_font({}, size);
    }
  };

  load_font_set(fonts_.font_16, 16);
  load_font_set(fonts_.font_18, 18);
  load_font_set(fonts_.font_24, 24);
  load_font_set(fonts_.font_32, 32);
  load_font_set(fonts_.font_40, 40);
  load_font_set(fonts_.font_48, 48);
  load_font_set(fonts_.font_80, 80);

  if (font_buffer_ && font_buffer_size_ > 0) {
    ESP_LOGI(TAG, "All fonts successfully loaded from memory buffer");
  }
}

void Context::init_ui_tree() {
  lv_obj_t* screen_handle = lv_disp_get_scr_act(display_handle_);
  lv_obj_set_style_bg_color(screen_handle, UI::Global::COLOR_BG, 0);
  lv_obj_set_style_bg_opa(screen_handle, LV_OPA_COVER, 0);

  // 내비게이션 콜백: ◀ ▶ 버튼이 눌렸을 때 이전/다음 모드로 전환
  // LVGL 이벤트 컨텍스트에서 호출되므로 bsp_display_lock 불필요
  auto nav_cb = [this](bool next) {
    const auto    count   = static_cast<uint8_t>(AppMode::COUNT);
    const uint8_t new_idx = next ? static_cast<uint8_t>((current_mode_index_ + 1) % count) : static_cast<uint8_t>((current_mode_index_ + count - 1) % count);
    set_mode(static_cast<AppMode>(new_idx));
  };

  status_bar_.create(screen_handle, 0, fonts_, std::move(nav_cb));

  tileview_ = lv_tileview_create(screen_handle);
  lv_obj_remove_style_all(tileview_);
  lv_obj_set_size(tileview_, UI::Global::SCREEN_W, UI::Global::SCREEN_H - UI::StatusBar::HEIGHT);
  lv_obj_set_pos(tileview_, 0, UI::StatusBar::HEIGHT);
  lv_obj_set_style_bg_color(tileview_, UI::Global::COLOR_BG, 0);
  lv_obj_set_style_bg_opa(tileview_, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(tileview_, 0, 0);
  lv_obj_set_style_pad_all(tileview_, 0, 0);
  // ⚠ 제스처 스와이프 비활성화 — 모드 전환은 상태바 ◀▶ 버튼으로만 수행
  // lv_tileview_set_tile_by_index()는 lv_obj_scroll_to()를 직접 호출하므로
  // SCROLLABLE 플래그 제거 후에도 프로그래밍 전환은 정상 동작합니다.
  lv_obj_remove_flag(tileview_, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_add_event_cb(tileview_, tileview_value_changed_cb, LV_EVENT_VALUE_CHANGED, this);

  for (uint8_t i = 0; i < static_cast<uint8_t>(AppMode::COUNT); ++i) {
    // LV_DIR_NONE: 제스처 스와이프로 타일 밖출 금지
    // 프로그래밍 전환(set_mode)은 여전히 동작함
    tiles_[i] = lv_tileview_add_tile(tileview_, i, 0, LV_DIR_NONE);
    lv_obj_remove_flag(tiles_[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(tiles_[i], LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(tiles_[i], 0, 0);
    lv_obj_set_style_pad_row(tiles_[i], 0, 0);
    lv_obj_set_style_clip_corner(tiles_[i], true, 0);
  }

  media_control_.initialize(tiles_[static_cast<uint8_t>(AppMode::MEDIA_CONTROL)], fonts_);
  media_remote_.initialize(tiles_[static_cast<uint8_t>(AppMode::MEDIA_REMOTE)], fonts_);
  numpad_.initialize(tiles_[static_cast<uint8_t>(AppMode::NUMPAD)], fonts_);

  // BLE 상태 폴링 타이머
  // LVGL 태스크 컨텍스트에서 실행되므로 bsp_display_lock 불필요.
  // State::Bonded 일 때만 아이콘 밝기, 그 외에는 흘리게 표시.
  ble_status_timer_ = lv_timer_create(on_ble_status_timer, 500, this);

  ESP_LOGI(TAG, "All mode components (MediaControl, MediaRemote, Numpad) initialized");
}

void Context::tileview_value_changed_cb(lv_event_t* e) {
  auto* self = static_cast<Context*>(lv_event_get_user_data(e));
  if (!self) return;

  lv_obj_t* active_tile = lv_tileview_get_tile_active(self->tileview_);
  if (!active_tile) return;

  uint8_t index = 0;
  for (uint8_t i = 0; i < static_cast<uint8_t>(AppMode::COUNT); ++i) {
    if (self->tiles_[i] == active_tile) {
      index = i;
      break;
    }
  }
  self->current_mode_index_ = index;
  self->status_bar_.update_mode_indicator(index);

  if (index != static_cast<uint8_t>(AppMode::MEDIA_REMOTE)) {
    self->media_remote_.close_dropdown();
  }
  ESP_LOGI(TAG, "Swiped active tile changed to Mode %d", index + 1);
}

void Context::update_wifi_status(bool connected) {
  bsp_display_lock(0);
  status_bar_.update_wifi_status(connected);
  bsp_display_unlock();
}

void Context::update_ble_status(bool connected) {
  bsp_display_lock(0);
  status_bar_.update_ble_status(connected);
  bsp_display_unlock();
}

void Context::update_battery(uint8_t percent) {
  bsp_display_lock(0);
  status_bar_.update_battery(percent);
  bsp_display_unlock();
}

static_assert(static_cast<uint8_t>(AppMode::COUNT) == 3, "AppMode count must match tiles array size!");

// ─── BLE 상태 폴링 타이머 콜백 ───
// LVGL 타이머는 LVGL 태스크 컨텍스트에서 호용되므로
// bsp_display_lock 없이 status_bar_를 직접 업데이트합니다.
void Context::on_ble_status_timer(lv_timer_t* t) {
  auto* self = static_cast<Context*>(lv_timer_get_user_data(t));
  if (!self) return;

  // Bonded 상태에서만 연결 표시 (HID 데이터 전송 가능 시점 = 실질적 연결 완료)
  const bool bonded = (Ble::HidDevice::instance().state() == Ble::State::Bonded);
  self->status_bar_.update_ble_status(bonded);
}

void Context::set_mode(AppMode mode) {
  bsp_display_lock(0);
  const auto index = static_cast<uint8_t>(mode);
  if (index < static_cast<uint8_t>(AppMode::COUNT)) {
    current_mode_index_ = index;
    lv_tileview_set_tile_by_index(tileview_, index, 0, LV_ANIM_ON);
    status_bar_.update_mode_indicator(index);
    if (mode != AppMode::MEDIA_REMOTE) {
      media_remote_.close_dropdown();
    }
    ESP_LOGI(TAG, "Mode set programmatically to AppMode: %d", static_cast<int>(mode));
  } else {
    ESP_LOGE(TAG, "Attempted to set invalid AppMode index: %d", index);
  }
  bsp_display_unlock();
}

void Context::update_media_track(const char* title, const char* artist) {
  bsp_display_lock(0);
  media_remote_.update_track_info(title, artist);
  bsp_display_unlock();
}

void Context::update_media_progress(uint32_t current_ms, uint32_t total_ms) {
  bsp_display_lock(0);
  media_remote_.update_progress(current_ms, total_ms);
  bsp_display_unlock();
}

void Context::set_media_album_art(const void* img_src, uint32_t w, uint32_t h) {
  bsp_display_lock(0);
  media_remote_.set_album_art(img_src, w, h);
  bsp_display_unlock();
}

void Context::update_media_volume(uint8_t volume) {
  bsp_display_lock(0);
  media_remote_.update_volume(volume);
  bsp_display_unlock();
}

void Context::update_media_play_state(const std::string& state) {
  bsp_display_lock(0);
  media_remote_.update_play_state(state);
  bsp_display_unlock();
}

}  // namespace Display

// ─── BLE 터치 HID 태스크 구현 ───
// Display namespace 외부에서 정의하면 include 충돌이 복잡해지므로
// namespace 닫기 직전에 배치합니다.
namespace Display {

/**
 * @brief ST7123 터치 패널을 직접 폴링하여 BLE HID 보고서를 전송합니다.
 *
 * 동작:
 *   1. 12ms 주기로 터치 좌표 폴링
 *   2. Y >= kTouchpadTopY 인 접점만 보고서로 변환
 *   3. Input Mode=3(PTP)이면 send_touch_report(), 그 외에는 send_mouse_report() 폴백
 *
 * PTP 병렬 모드 규약 (Windows/Linux 공통):
 *   - 호스트는 Contact Count 개수만큼 리포트 앞에서부터 Finger 엔트리를 파싱하고
 *     나머지는 무시한다 → 유효 접점은 반드시 fingers[0]부터 연속으로 채운다(앞채움).
 *     손가락의 정체성은 엔트리 위치가 아니라 Contact Identifier로 추적된다.
 *   - 떨어진 접점은 Tip Switch=0, Confidence(Touch Valid)=1 로 "1회" 보고하며,
 *     그 접점도 Contact Count에 포함해야 한다. (Confidence=0은 팜 리젝션 의미이므로
 *     릴리즈에 0을 쓰면 탭 제스처가 무효화된다)
 *
 * 터치패드 영역:
 *   StatusBar(120px) + Mode 상단(580px) = 700px 아래가 터치패드 영역
 *   → FingerData.y = screen_y - 700  (0~580)
 *
 * 모드별 동작:
 *   MEDIA_CONTROL / MEDIA_REMOTE    → HID 보고서 전송
 *   NUMPAD                          → 터치패드 아님, skip (활성 접점은 릴리즈 보고)
 */
void Context::touch_hid_task(void* arg) {
  auto* self = static_cast<Context*>(arg);

  // 터치패드 영역 상단 Y 좌표 (화면 절대값)
  constexpr int32_t kTouchpadTopY = UI::StatusBar::HEIGHT + UI::Touchpad::HEIGHT;  // 120 + 580 = 700

  constexpr TickType_t kPollInterval = pdMS_TO_TICKS(Ble::ConnParam::kTouchPollIntervalMs);  // = 12ms

  constexpr uint8_t kMaxPts = Ble::Hid::kMaxFingers;

  // 접점 추적 테이블 구조체
  struct TrackedContact {
    bool     active     = false;
    uint8_t  contact_id = 0;
    uint16_t x = 0, y = 0;  // 마지막 감지되었던 원시 물리 좌표
  };
  std::array<TrackedContact, kMaxPts> tracked{};
  uint8_t                             next_id = 0;

  // 상대 마우스 폴백 모사용 실시간 소수점 누적 버퍼 (Fractional Accumulator)
  float mouse_accum_dx = 0.0f;
  float mouse_accum_dy = 0.0f;

  // 원시 화면 Y좌표 → 터치패드 영역 상대 좌표 (0~580)
  auto to_pad_y = [](uint16_t raw_y) -> uint16_t { return static_cast<uint16_t>(raw_y > kTouchpadTopY ? raw_y - kTouchpadTopY : 0); };

  // 하드웨어 좌표 → HID 논리 좌표 스케일링
  // MS PTP는 300 DPI 이상 해상도를 강제하므로 원시 픽셀(720×580, 62.3×50.2mm,
  // 281~293 DPI)을 디스크립터 논리 범위(X 0~1008, Y 0~928)로 확장합니다.
  //   X: ×7/5 → 1008/65.0mm ≈ 392 DPI,  Y: ×8/5 → 928/50.2mm ≈ 469 DPI
  auto scale_x = [](uint16_t hw_x) -> uint16_t { return static_cast<uint16_t>(static_cast<uint32_t>(hw_x) * Ble::Hid::kPtpLogicalMaxX / 720); };
  auto scale_y = [](uint16_t pad_y) -> uint16_t { return static_cast<uint16_t>(static_cast<uint32_t>(pad_y) * Ble::Hid::kPtpLogicalMaxY / 580); };

  while (true) {
    vTaskDelay(kPollInterval);

    // ── 하드웨어 터치 데이터 읽기 ──────────────────────────────────────────
    esp_lcd_touch_read_data(self->touch_handle_);

    esp_lcd_touch_point_data_t pts[kMaxPts] = {};
    uint8_t                    raw_count    = 0;
    esp_lcd_touch_get_data(self->touch_handle_, pts, &raw_count, kMaxPts);

    // ── 터치패드 영역 필터링 (Y >= kTouchpadTopY) ──────────────────────────
    esp_lcd_touch_point_data_t valid_pts[kMaxPts] = {};
    uint8_t                    active             = 0;
    for (uint8_t i = 0; i < raw_count && i < kMaxPts; ++i) {
      if (static_cast<int32_t>(pts[i].y) < kTouchpadTopY) continue;
      valid_pts[active++] = pts[i];
    }

    // ── BLE 상태 및 Numpad 모드 확인 ─────────────────────────────────────
    const bool ble_ready = Ble::HidDevice::instance().is_report_ready();
    const bool is_numpad = self->current_mode_index_ >= static_cast<uint8_t>(AppMode::NUMPAD);

    if (!ble_ready || is_numpad) {
      if (active > 0 && !ble_ready) {
        ESP_LOGW(TAG, "[Touch] 터치 감지됨 (%d개 접점) - 그러나 BLE HID가 준비(페어링/본딩)되지 않음!", active);
      }

      // 눌려 있던 접점이 남아 있으면 릴리즈 리포트를 1회 송출하여
      // 호스트 측에 손가락이 눌린 채 남는(stuck) 문제를 방지합니다.
      bool any_tracked = false;
      for (const auto& t : tracked) any_tracked = any_tracked || t.active;

      if (any_tracked && ble_ready) {
        std::array<Ble::FingerData, kMaxPts> fingers{};
        uint8_t                              n = 0;
        for (const auto& t : tracked) {
          if (!t.active) continue;
          fingers[n++] = {
              .tip_switch  = false,
              .touch_valid = true,
              .contact_id  = t.contact_id,
              .x           = scale_x(t.x),
              .y           = scale_y(to_pad_y(t.y)),
          };
        }
        Ble::send_touch_report(fingers, n, true);
      }
      for (auto& t : tracked) t.active = false;
      continue;
    }

    // 프레임 시작 시점의 활성 스냅샷 (마우스 모드 delta 계산용)
    std::array<bool, kMaxPts> was_active{};
    for (uint8_t ti = 0; ti < kMaxPts; ++ti) was_active[ti] = tracked[ti].active;

    std::array<bool, kMaxPts> slot_updated_this_frame{};
    int32_t                   mouse_dx        = 0;
    int32_t                   mouse_dy        = 0;
    bool                      mouse_has_delta = false;

    if (active > 0) {
      ESP_LOGI(TAG, "[Touch] 하드웨어 터치 감지: %d개 (필터 전 원본: %d개)", active, raw_count);

      // 1. 기존 활성 접점 ↔ 신규 좌표 최적 매칭 (Global Nearest-Neighbor)
      //    하드웨어가 프레임마다 접점을 다른 순서로 보고할 수 있으므로,
      //    원시 순서(ci) 기준 탐욕적 매칭 대신 가능한 모든 (점,슬롯) 쌍을
      //    거리순으로 정렬한 뒤 가장 가까운 쌍부터 확정합니다.
      //    (순서 의존적 매칭은 2손가락 이상에서 Contact ID가 서로 뒤바뀌어
      //     제스처가 오인식되는 원인이 될 수 있습니다.)
      struct Candidate {
        uint32_t dist;
        uint8_t  ci;
        uint8_t  ti;
      };
      std::array<Candidate, kMaxPts * kMaxPts> candidates{};
      uint8_t                                  candidate_count = 0;

      for (uint8_t ci = 0; ci < active; ++ci) {
        for (uint8_t ti = 0; ti < kMaxPts; ++ti) {
          if (!tracked[ti].active) continue;

          int32_t  dx   = static_cast<int32_t>(valid_pts[ci].x) - tracked[ti].x;
          int32_t  dy   = static_cast<int32_t>(valid_pts[ci].y) - tracked[ti].y;
          uint32_t dist = static_cast<uint32_t>(dx * dx + dy * dy);

          if (dist <= Ble::Touch::kMaxTrackDistSq) {
            candidates[candidate_count++] = {dist, ci, ti};
          }
        }
      }

      std::sort(candidates.begin(), candidates.begin() + candidate_count, [](const Candidate& a, const Candidate& b) { return a.dist < b.dist; });

      std::array<bool, kMaxPts>   prev_matched{};
      std::array<bool, kMaxPts>   point_matched{};
      std::array<int8_t, kMaxPts> point_to_slot{};
      point_to_slot.fill(-1);

      for (uint8_t c = 0; c < candidate_count; ++c) {
        const auto& cand = candidates[c];
        if (point_matched[cand.ci] || prev_matched[cand.ti]) continue;

        point_to_slot[cand.ci] = static_cast<int8_t>(cand.ti);
        point_matched[cand.ci] = true;
        prev_matched[cand.ti]  = true;
      }

      // 2. 매칭 실패한 신규 터치는 비활성 슬롯에 신규 할당 및 ID 발급
      //    슬롯이 모두 점유되어 있으면 해당 접점은 이번 프레임에서 드롭됩니다.
      for (uint8_t ci = 0; ci < active; ++ci) {
        if (point_matched[ci]) continue;

        int8_t free_slot = -1;
        for (uint8_t ti = 0; ti < kMaxPts; ++ti) {
          if (!tracked[ti].active && !prev_matched[ti]) {
            free_slot = static_cast<int8_t>(ti);
            break;
          }
        }

        if (free_slot >= 0) {
          tracked[free_slot].active     = true;
          tracked[free_slot].contact_id = next_id;
          point_to_slot[ci]             = free_slot;
          prev_matched[free_slot]       = true;
          point_matched[ci]             = true;
          next_id                       = (next_id + 1) & 0x0F;
        } else {
          ESP_LOGW(TAG, "[Touch] 슬롯 부족으로 접점 드롭 - X=%d, Y=%d", valid_pts[ci].x, valid_pts[ci].y);
        }
      }

      // 3. tracked 테이블 좌표 최신화 (매칭/할당된 접점만 좌표 업데이트)
      //    첫 번째 기존-매칭 접점의 이동량은 마우스 모드 폴백의 delta로 사용
      for (uint8_t ci = 0; ci < active; ++ci) {
        int8_t slot = point_to_slot[ci];
        if (slot >= 0) {
          int32_t dx = static_cast<int32_t>(valid_pts[ci].x) - tracked[slot].x;
          int32_t dy = static_cast<int32_t>(valid_pts[ci].y) - tracked[slot].y;

          if (was_active[slot] && !mouse_has_delta) {
            mouse_dx        = dx;
            mouse_dy        = dy;
            mouse_has_delta = true;
          }

          tracked[slot].x               = valid_pts[ci].x;
          tracked[slot].y               = valid_pts[ci].y;
          slot_updated_this_frame[slot] = true;
        }
      }
    }

    // ── 리포트 엔트리 빌드 (PTP 병렬 모드 규약: 앞채움 패킹) ─────────────────
    //    - 이번 프레임에 갱신된 슬롯      → Tip=1, Confidence=1 (눌림 유지)
    //    - 활성이었으나 미갱신된 슬롯     → Tip=0, Confidence=1 (릴리즈 1회 보고)
    //    - Contact Count = 채워진 엔트리 수 (릴리즈 보고 포함!)
    std::array<Ble::FingerData, kMaxPts> fingers{};
    uint8_t                              entry_count = 0;
    bool                                 any_release = false;

    for (uint8_t ti = 0; ti < kMaxPts; ++ti) {
      if (!tracked[ti].active) continue;
      const bool down        = slot_updated_this_frame[ti];
      fingers[entry_count++] = {
          .tip_switch  = down,
          .touch_valid = true,  // Confidence — 릴리즈에도 1 유지 (0 = 팜 리젝션 의미)
          .contact_id  = tracked[ti].contact_id,
          .x           = scale_x(tracked[ti].x),
          .y           = scale_y(to_pad_y(tracked[ti].y)),
      };
      if (!down) any_release = true;
    }

    if (entry_count == 0) continue;  // 활성/릴리즈 접점 없음 — 보낼 것 없음

    // ── Mouse 모드 폴백 (Input Mode != 3) ──────────────────────────────────
    // 호스트가 PTP 핸드셰이크(Input Mode=3 Write)를 아직 수행하지 않았거나
    // PTP 미지원 호스트인 경우 상대 좌표 마우스로 동작합니다.
    if (Ble::Gatt::device_mode.load(std::memory_order_relaxed) != Ble::Hid::kInputModeTouchpad) {
      if (mouse_has_delta && (mouse_dx != 0 || mouse_dy != 0)) {
        // 소수점 델타 누적기에 스케일링된 float 값 누적
        mouse_accum_dx += static_cast<float>(mouse_dx) * Ble::Hid::kMouseScaleX;
        mouse_accum_dy += static_cast<float>(mouse_dy) * Ble::Hid::kMouseScaleY;

        // 정수 부분 추출
        int16_t tx = static_cast<int16_t>(mouse_accum_dx);
        int16_t ty = static_cast<int16_t>(mouse_accum_dy);

        // 누적기에서 방출된 정수 분 차감
        mouse_accum_dx -= tx;
        mouse_accum_dy -= ty;

        if (tx != 0 || ty != 0) {
          ESP_LOGI(TAG, "[Touch] Mouse 모드 폴백 전송 - dx: %d (scaled: %d, accum_rem: %.3f), dy: %d (scaled: %d, accum_rem: %.3f)", (int)mouse_dx, (int)tx, mouse_accum_dx,
                   (int)mouse_dy, (int)ty, mouse_accum_dy);
          Ble::send_mouse_report(0, tx, ty);
        }
      }
      // 마우스 모드에서는 릴리즈 리포트가 없으므로 떨어진 슬롯 즉시 비활성화
      bool any_still_active = false;
      for (uint8_t ti = 0; ti < kMaxPts; ++ti) {
        if (tracked[ti].active && !slot_updated_this_frame[ti]) {
          tracked[ti].active = false;
        }
        any_still_active = any_still_active || tracked[ti].active;
      }
      // 손가락이 완전히 떨어지면 복귀 지터 누적 전송 방지를 위해 누적기 리셋
      if (!any_still_active) {
        mouse_accum_dx = 0.0f;
        mouse_accum_dy = 0.0f;
      }
      continue;
    }

    // ── PTP 리포트 전송 ────────────────────────────────────────────────────
    const int64_t now_us = esp_timer_get_time();
    ESP_LOGI(TAG, "[Touch] PTP 모드 전송 - ScanTime: %u, 유효접점: %d개, 릴리즈감지: %d", static_cast<uint16_t>((now_us / 100) & 0xFFFF), entry_count, any_release);
    for (uint8_t i = 0; i < entry_count; ++i) {
      ESP_LOGI(TAG, "  -> Finger[%d]: ID=%d, TipSwitch=%d, Scaled(X=%d, Y=%d)", i, fingers[i].contact_id, fingers[i].tip_switch, fingers[i].x, fingers[i].y);
    }

    const bool ok = Ble::send_touch_report(fingers, entry_count, any_release);

    if (ok) {
      // 릴리즈 보고가 호스트에 전달됨 — 해당 슬롯 비활성화
      bool any_still_active = false;
      for (uint8_t ti = 0; ti < kMaxPts; ++ti) {
        if (tracked[ti].active && !slot_updated_this_frame[ti]) {
          ESP_LOGD(TAG, "[Touch] 슬롯 %d (ID %d) 릴리즈 완료", ti, tracked[ti].contact_id);
          tracked[ti].active = false;
        }
        any_still_active = any_still_active || tracked[ti].active;
      }
      if (!any_still_active) next_id = 0;
    }
    // 전송 실패(rate limit / CCCD 미활성 등) 시 슬롯 상태 유지 → 다음 프레임 재시도.
    // 릴리즈 프레임은 rate limiter를 우회하므로 릴리즈 유실은 발생하지 않습니다.
  }

  // 도달 불가 — FreeRTOS 태스크는 무한 루프
  vTaskDelete(nullptr);
}

}  // namespace Display
