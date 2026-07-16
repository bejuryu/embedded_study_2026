# M5Stack Tab5 Display & Input Architecture Design

이 문서는 ESP-IDF v6.0.1 및 LVGL 9.x 기반 `009.touchpad` 프로젝트의 UI 및 터치 입력 처리에 대한 상세 아키텍처 명세서입니다.
`prototype.html`의 모든 시각적 요소를 LVGL 위젯으로 pixel-perfect 재현하기 위한 구현 가이드를 포함합니다.

---

## 1. 아키텍처 철학 (Architecture Philosophy)

1. **객체지향 컴포넌트 모델 (Composition/Has-A):**
   God-Class를 지양하고, 각 UI 모드별로 캡슐화된 독립적인 C++ 클래스로 분리합니다.
2. **엄격한 메모리 포인터 추적 (Pointer Tracking):**
   LVGL 엔진의 자동 해제에만 의존하지 않고, 생성된 모든 동적 위젯(`lv_obj_t*`)을 클래스 멤버로 명시적으로 저장하여 완벽하게 라이프사이클을 추적합니다.
3. **입력/출력 이원화 (Two-Track Routing):**
   하드웨어 터치(GT911)의 빠른 폴링 레이트는 살리면서 UI 렌더링 부하를 줄이기 위해, HID 전송(100Hz)과 화면 렌더링(30Hz)을 완전히 병렬로 분리합니다.

---

## 2. 생성될 파일 명세 (File Structure)

모든 소스코드는 `main/` 디렉토리 아래에 역할별로 분할되어 생성됩니다.

* **전역 상태 (Global State)**
  * `app_state.hpp`: `AppState` 싱글톤 구조체 선언.
* **UI 상수 (UI Constants)**
  * `ui_constants.hpp`: prototype.html의 모든 레이아웃/색상/크기 값을 `constexpr`로 정의. 데이터와 로직의 완벽 분리.
  * `ui_icons.hpp`: SVG Vector Path Data를 LVGL vector path 변환용 `constexpr` 배열로 정의.
* **입력 계층 (Input Layer)**
  * `touch_hid_processor.hpp` / `.cpp`: GT911 원시 데이터를 가로채어 분석하고 BLE HID 패킷으로 변환 및 궤적 렌더링 데이터를 준비하는 전담 클래스.
* **UI 오케스트레이터 (Root Layer)**
  * `display.hpp` / `display.cpp`: 하드웨어 제어, 폰트 로딩, 최상위 타일뷰(`Tileview`)를 관장하는 메인 클래스. 공통 상태바 생성 함수 포함.
* **UI 하위 컴포넌트 (Component Layer)**
  * `display_status_bar.hpp` / `.cpp`: 3개 모드에서 공유되는 통합 상태바 컴포넌트.
  * `display_media_control.hpp` / `.cpp`: 미디어 제어 (Mode 1) 화면.
  * `display_spotify.hpp` / `.cpp`: 스포티파이 재생 (Mode 2) 화면.
  * `display_numpad.hpp` / `.cpp`: 숫자 키패드 (Mode 3) 화면.
  * `display_trackpad.hpp` / `.cpp`: 하단 블랙 영역 (Y >= 640)의 터치 잔상(Trail) 시각적 피드백 화면.

---

## 3. 이벤트 처리 및 병렬 라우팅 (Input Pipeline)

터치 데이터의 입력 주기(Polling Rate)와 해상도는 배터리 효율과 마우스 조작감을 결정합니다.

### 3.1 해상도 및 데이터 주기 (Resolution & Polling)
* **물리 해상도:** 720 x 1280 (X: 0~720, Y: 0~1280)
* **터치 IC 수집:** 범용 추상화 레이어 사용 (초당 최대 100Hz 목표)
* **LVGL 렌더링 타이머:** 절전형 30Hz (약 33ms 간격)

### 3.2 우회식 병렬 처리 (Two-Track Routing)
터치 IC (GT911 또는 ST7123) 드라이버에서 터치 좌표를 읽은 즉시, LVGL로 들어가기 전 **`TouchHIDProcessor`**가 범용 좌표 데이터를 가로챕니다.

* **Mode 1 (Media) / Mode 2 (Spotify) 일 때:**
  * **Y < 640:** 상단 UI 조작 영역이므로 터치를 그대로 LVGL 엔진으로 넘깁니다.
  * **Y >= 640:** 하단 블랙 트랙패드 영역이므로 이벤트를 두 갈래로 나눕니다.
    * **Track 1 (BLE HID 송신 - 100Hz):** 좌표 보정(`HID_X = Raw_X`, `HID_Y = Raw_Y - 640`)을 거쳐 Report ID 1 (마우스) 패킷을 즉각 BLE 송신 큐에 넣습니다. (지연 없는 쫀득한 커서 조작감)
    * **Track 2 (LVGL 시각 효과 - 30Hz):** `DisplayTrackPad` 안에는 손가락 개수만큼(최대 5개)의 굵은 `lv_line` 위젯이 준비되어 있습니다. 100Hz 좌표 데이터들을 베지어 곡선(Bezier Curve)으로 보간하여 `lv_line` 배열에 밀어 넣습니다. 
    * **삭제 로직:** 복잡한 애니메이션 없이 화면에 라인을 남겨두되, 손을 뗀 후 특정 시간(예: 0.3초)이 지나는 타이머가 발동하거나 **새로운 터치가 시작되는 즉시** 기존 라인을 싹 비우고(Clear) 다시 그립니다. (오버헤드 0%, 최고급 곡선 보장)

* **Mode 3 (Numpad) 일 때 (패스스루):**
  * `TouchHIDProcessor`는 좌표 조작 없이 **화면 전체(0~1280)** 터치를 LVGL 엔진으로 넘깁니다.
  * 숫자 버튼은 실제 위젯이므로 LVGL의 `LV_EVENT_CLICKED` 콜백을 통해 시각적 눌림 효과를 주며, `DisplayNumpad` 내부에서 Report ID 3 (키보드) 패킷을 전송합니다.

---

## 4. 리소스 라이프사이클 (Resource Management)

* **PSRAM 폰트 로드:** `Display::initialize()`에서 SD 카드의 `Inter.ttf`를 읽어 **PSRAM**에 버퍼를 할당합니다. 이 폰트 파일은 무거운 아이콘을 배제하고 오직 '텍스트와 숫자' 렌더링에만 사용되어 용량 낭비를 극도로 줄입니다. (상세 폰트 크기 명세는 섹션 14 참조)
* **아이콘 캐싱 (Boot-time Vector Caching):** 무거운 아이콘 전용 폰트나 이미지 파일을 쓰는 대신, 부팅 시 LVGL 9.x의 `lv_vector_dsc_t` API를 이용해 수학적 벡터(SVG Path)로 아이콘(▶, ❚❚ 등)을 한 번만 그려서 PSRAM 비트맵으로 캐싱해 둡니다. 런타임에는 연산 없이 이를 이미지로 즉시 재사용합니다. (상세 벡터 드로잉 방법은 섹션 7.5 및 섹션 10 참조)
* **상태 동기화 (Thread-Safe):** FreeRTOS 백그라운드 태스크(WiFi/BLE)가 화면을 갱신할 때는 반드시 `Display` 클래스의 갱신 함수를 호출하며, 이 함수들은 내부적으로 `bsp_display_lock(0)` 뮤텍스를 획득하여 메모리 충돌을 막습니다.

---

## 5. 특수 UX 설계

* **스텔스 미니멀리즘:** True Black(#000000) 배경을 사용하며, Faux 볼드/이탤릭 없이 폰트 크기와 색상(투명도)만으로 계층을 구분합니다.
* **BLE 수동 페어링 강제 진입:** 눈에 보이는 20x20 아이콘 대신, 우측 상단의 상태 아이콘 전체를 감싸는 **최소 100x60 크기의 거대한 투명 Hit-box**를 배치합니다. 2초 이상 Long Press 시 루트 `Display`가 NVS 본딩을 삭제하고 페어링 대기 상태로 전향합니다.

---

## 6. 핵심 클래스 명세 (Class Signatures)

### 6.1 전역 상태 및 입력 제어
```cpp
// app_state.hpp
enum class AppMode { MEDIA_CONTROL, SPOTIFY_PLAYER, NUMPAD };

struct AppState {
    AppMode current_mode = AppMode::MEDIA_CONTROL;
    bool is_wifi_connected = false;
    bool is_ble_connected = false;
    bool is_spotify_authenticated = false;
};

// touch_hid_processor.hpp
struct RawTouchData {
    bool is_pressed;
    uint16_t x;
    uint16_t y;
    uint8_t finger_id;
};

class TouchHIDProcessor {
public:
    void process_raw_touch(const RawTouchData& touch_data, AppState* state);
private:
    void process_as_trackpad(const RawTouchData& touch_data);
    void send_hid_report(uint8_t report_id, uint8_t* data, size_t len);
};
```

### 6.2 하위 컴포넌트 및 루트 
```cpp
// display_status_bar.hpp (공통 상태바 컴포넌트)
class DisplayStatusBar {
public:
    void create(lv_obj_t* parent, uint8_t active_mode_index);
    void update_battery(uint8_t percent);
    void update_ble_status(bool connected);
    void update_wifi_status(bool connected);
    void update_mode_indicator(uint8_t active_mode_index);
private:
    lv_obj_t* container_ = nullptr;
    lv_obj_t* battery_icon_ = nullptr;
    lv_obj_t* battery_label_ = nullptr;
    lv_obj_t* wifi_icon_ = nullptr;
    lv_obj_t* ble_icon_ = nullptr;
    lv_obj_t* indicators_[3] = {};  // ● ○ ○
};

// display_trackpad.hpp (시각 효과 컴포넌트)
class DisplayTrackPad {
public:
    void initialize(lv_obj_t* parent_tile);
    void add_trail_point(uint8_t finger_id, uint16_t x, uint16_t y);
    void clear_trail(uint8_t finger_id);
private:
    lv_obj_t* lines[5]; // 최대 5개 손가락의 궤적 라인
    // 타이머 및 베지어 곡선 보간 로직 포함
};

// display.hpp
class Display {
public:
    bool initialize(); 
    void update_wifi_status(bool connected);
    void update_mode(AppMode mode);

private:
    lv_display_t* display_handle_ = nullptr;
    lv_obj_t* tileview_ = nullptr;
    
    // 서브 컴포넌트 인스턴스 (Composition)
    DisplayStatusBar    status_bars_[3];   // 각 모드별 상태바 인스턴스
    DisplayMediaControl media_control_;
    DisplaySpotify      spotify_player_;
    DisplayNumpad       numpad_;
    DisplayTrackPad     trackpad_;

    void* psram_font_buffer_ = nullptr; 
};
```

---

## 7. 하드웨어 한계 극복 및 최적화 전략 (Hardware Optimization Strategy)

### 7.1 하드웨어 파편화 (GT911 vs ST7123) 대응
* M5Stack Tab5는 제조 배치에 따라 GT911 또는 ST7123 터치 IC가 혼용되어 사용됩니다. (I2C Auto-probe 작동)
* 특정 칩셋의 주사율 레지스터(예: GT911의 0x8047)를 직접 수정하여 100Hz를 강제하는 하드코딩은 다른 칩셋 탑재 기기에서 시스템 충돌을 유발하므로 절대 금지합니다.
* 오직 ESP-IDF의 추상화된 터치 핸들(`esp_lcd_touch_handle_t`)에서 제공하는 범용 구조체를 통해서만 터치 데이터를 읽어옵니다.

### 7.2 주사율 저하 보상 로직 (Software Speed Fallback)
* 제조사 펌웨어 설정으로 인해 터치 폴링 레이트가 60Hz(약 16ms) 등으로 하향 고정되어 있을 수 있습니다.
* `TouchHIDProcessor`는 이전 프레임과의 실제 시간차(`dt`, `esp_timer_get_time()` 활용)를 측정합니다.
* `dt`가 이상적인 목표 주기(10ms)보다 길어질 경우, 계산된 마우스 이동량(Delta X, Y)에 `(실제 dt) / (목표 dt)` 기반의 배율을 곱해서 전송합니다. 이를 통해 하드웨어 센서가 느리더라도 PC 마우스 커서의 물리적 이동 속도는 100Hz 스펙과 동일하게 쾌적함을 유지합니다.

### 7.3 멀티터치 고스팅 방지 (Outlier Rejection)
* 여러 손가락이 교차할 때 터치 센서의 한계로 좌표가 엉뚱한 곳으로 튀는(Ghosting) 에러 현상이 발생합니다.
* 이를 방지하기 위해 **관성 기반 속도 필터(Inertia-based Filter)**를 적용합니다.
* 이전 좌표와 현재 좌표 사이의 이동 속도(Distance / dt)가 인간이 물리적으로 낼 수 없는 한계치(예: 10ms당 300px 이상 이동)를 초과하면 해당 좌표를 노이즈로 간주하고 무시(`return;`)하여, 베지어 곡선이 화면 밖으로 스파이크(Spike)를 치는 것을 원천 차단합니다.

### 7.4 PSRAM 대역폭 및 `lv_line` 렌더링 최적화
* 느린 외부 PSRAM(프레임 버퍼) 위에서 굵은 안티앨리어싱 라인 5개를 실시간으로 렌더링하면 극심한 병목과 프레임 저하(FPS Drop)가 발생합니다.
* `DisplayTrackPad`는 이 연산 부하를 최소화하기 위해 다음 최적화를 강제합니다:
  1. 화면 절반 크기의 전체 영역 갱신(Invalidate)을 금지하고, 방금 선이 길어진 **바운딩 박스(Bounding Box)에 대해서만 `lv_obj_invalidate_area()`를 호출하여 부분 렌더링(Partial Invalidation)을 극대화**합니다.
  2. 트랙패드 컨테이너에 `Clip` 속성을 적용하여, PPA(하드웨어 2D 가속기)의 DMA 전송이 상단 미디어 UI 영역을 절대 침범하지 않고 720x640 내부에서만 제한적으로 이루어지도록 합니다.
  3. 퍼포먼스가 여전히 30fps 방어가 안 될 경우, `lv_obj_set_style_line_rounded()` 속성을 비활성화하여 가장 무거운 연산인 둥근 끝단(Rounded Cap) 렌더링을 끕니다.

### 7.5 벡터 아이콘 래스터라이징 캐싱 (Boot-time Vector Rasterization)
* 뚱뚱한 아이콘 폰트(FontAwesome 등)나 다수의 PNG 이미지 파일은 플래시 용량과 SD 카드 로딩 속도를 갉아먹는 주범입니다. 
* 또한, 런타임에 매 프레임마다 벡터 API로 아이콘의 곡선을 연산하는 것은 소프트웨어 렌더링 오버헤드를 발생시킵니다.
* **해결 및 최적화 로직:** 기기 부팅 시(초기화 단계) 단 1회에 한하여 LVGL 9.x의 **`lv_vector_dsc_t`** API를 사용하여 `lv_canvas` 레이어 위에 벡터 패스를 그립니다. `lv_vector_path_t`에 SVG의 `move_to`, `line_to`, `cubic_to` 커맨드를 직접 매핑하여 prototype.html의 SVG 아이콘과 수학적으로 동일한 곡선을 생성합니다. 다 그려진 캔버스 메모리는 `lv_image_dsc_t` 구조체로 캐스팅하여 PSRAM에 **비트맵 이미지로 캐싱(Rasterization)**해 둡니다.
* **`lv_conf.h` 필수 설정:** `LV_USE_VECTOR_GRAPHIC 1` 활성화 필요.
* **SVG Arc(`a`) 커맨드 변환:** LVGL vector API는 SVG의 `arc` 커맨드를 직접 지원하지 않습니다. WiFi, Heart, Repeat 등 아이콘에 포함된 SVG arc는 **중심점 매개변수화(Center Parameterization) → 3차 베지어 근사(Cubic Bezier Approximation)** 알고리즘으로 변환하여 `lv_vector_path_cubic_to()`로 그립니다.
* **결과:** 외부 아이콘 파일 의존성은 0%로 완벽히 제거되며, 런타임 그래픽 렌더링 부하 역시 0%가 됩니다. 해상도가 변경되어도 부팅 시 그리는 수학적 비율만 수정하면 픽셀 깨짐 없이 영구적으로 깨끗한 아이콘을 유지할 수 있는 궁극의 최적화 기법입니다.
* **아이콘 크기 효율화:** 동일 벡터 패스에서 크기만 다른 아이콘(예: Prev 48x48 / 36x36)은 큰 사이즈 1벌만 래스터라이징 후 `lv_image_set_scale()`로 축소하여 PSRAM 절약. 48x48 → 36x36 변환 시 스케일 팩터 = `256 * 36 / 48 = 192`.

---

## 8. UI 레이아웃 상수 명세 (Pixel-Perfect Constants)

> prototype.html의 모든 CSS 수치를 추출하여 LVGL `constexpr` 상수로 정의합니다.
> 이 섹션의 값을 `ui_constants.hpp`에 그대로 옮겨 하드코딩을 원천 차단합니다.

### 8.1 전역 상수 (Global)

| 상수명 | 값 | CSS 원본 | LVGL API |
|:---|:---|:---|:---|
| `SCREEN_W` | `720` | `.screen { width: 720px }` | `lv_obj_set_width()` |
| `SCREEN_H` | `1280` | `.screen { height: 1280px }` | `lv_obj_set_height()` |
| `COLOR_SCREEN_BG` | `0x000000` | `background-color: #000000` | `lv_obj_set_style_bg_color()` |
| `COLOR_TEXT_DEFAULT` | `0xFFFFFF` | `color: white` | `lv_obj_set_style_text_color()` |
| `SPLIT_Y` | `640` | 상/하단 분할 기준 | 터치 라우팅 분기점 |

### 8.2 상태바 (Status Bar — 공통)

| 상수명 | 값 | CSS 원본 |
|:---|:---|:---|
| `STATUSBAR_H` | `30` | `height: 30px` |
| `STATUSBAR_PAD_H` | `20` | `padding: 0 20px` |
| `STATUSBAR_FONT_SIZE` | `16` | `font-size: 16px` |
| `STATUSBAR_FONT_WEIGHT` | `600` | `font-weight: 600` |
| `STATUSBAR_TEXT_OPA` | `230` (0.9 × 255) | `color: rgba(255,255,255,0.9)` |
| `STATUSBAR_LEFT_W` | `120` | `.status-left { width: 120px }` |
| `STATUSBAR_LEFT_GAP` | `8` | `gap: 8px` |
| `STATUSBAR_RIGHT_W` | `120` | `.status-right { width: 120px }` |
| `STATUSBAR_RIGHT_GAP` | `12` | `gap: 12px` |
| `STATUSBAR_CENTER_LETTER_SPACE` | `8` | `letter-spacing: 8px` |
| `STATUSBAR_BATTERY_ICON_SIZE` | `24` | `width="24" height="24"` |
| `STATUSBAR_ICON_SIZE` | `20` | `width="20" height="20"` (WiFi, BLE) |

### 8.3 터치패드 영역 (Touchpad Area — Mode 1/2 공통)

| 상수명 | 값 | CSS 원본 |
|:---|:---|:---|
| `TOUCHPAD_H` | `640` | `height: 640px` |
| `TOUCHPAD_BG` | `0x000000` | `background-color: #000000` |
| `CORNER_SIZE` | `40` | `width: 40px; height: 40px` |
| `CORNER_BORDER_W` | `2` | `border: 2px solid` |
| `CORNER_BORDER_COLOR` | `0x222222` | `#222222` |
| `CORNER_OFFSET` | `20` | `top/bottom/left/right: 20px` |
| `TOUCH_HINT_TEXT` | `"TOUCHPAD"` | 중앙 힌트 텍스트 |
| `TOUCH_HINT_COLOR` | `0x222222` | `color: #222222` |
| `TOUCH_HINT_FONT_SIZE` | `24` | `font-size: 24px` |
| `TOUCH_HINT_FONT_WEIGHT` | `600` | `font-weight: 600` |
| `TOUCH_HINT_LETTER_SPACE` | `4` | `letter-spacing: 4px` |

**코너 L자 구현:** LVGL에서 각 코너는 `lv_obj_t` 40x40 위젯에 `lv_obj_set_style_border_side()`로 2면만 보이도록 설정합니다:
```cpp
// 좌상단: 상+좌 테두리만 표시
lv_obj_set_style_border_side(corner_tl, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT, 0);
// 우상단: 상+우 테두리만 표시
lv_obj_set_style_border_side(corner_tr, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_RIGHT, 0);
// 좌하단: 하+좌 테두리만 표시
lv_obj_set_style_border_side(corner_bl, LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_LEFT, 0);
// 우하단: 하+우 테두리만 표시
lv_obj_set_style_border_side(corner_br, LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_RIGHT, 0);
```

### 8.4 Mode 1 — PC 미디어 제어

| 상수명 | 값 | CSS 원본 |
|:---|:---|:---|
| `M1_TOP_H` | `640` | `height: 640px` |
| `M1_TOP_BG` | `0x000000` | `background-color: #000000` |
| `M1_TOP_PAD_TOP` | `10` | `padding: 10px 20px 20px 20px` |
| `M1_TOP_PAD_SIDE` | `20` | 좌/우 패딩 |
| `M1_TOP_PAD_BOTTOM` | `20` | 하단 패딩 |
| `M1_GRID_GAP` | `15` | `gap: 15px` |
| `M1_GRID_MARGIN_TOP` | `15` | `margin-top: 15px` |
| `M1_GRID_COLS` | `3` | `grid-template-columns: repeat(3, 1fr)` |
| `M1_GRID_ROWS` | `2` | `grid-template-rows: repeat(2, 1fr)` |
| `M1_BTN_BG` | `0x121212` | `.m1-btn { background-color: #121212 }` |
| `M1_BTN_RADIUS` | `20` | `border-radius: 20px` |
| `M1_BTN_BORDER_W` | `2` | `border: 2px solid` |
| `M1_BTN_BORDER_COLOR` | `0x1A1A1A` | `#1a1a1a` |
| `M1_BTN_ICON_COLOR` | `0x888888` | `color: #888` |
| `M1_BTN_ICON_MARGIN_B` | `12` | `margin-bottom: 12px` |
| `M1_BTN_ICON_SIZE` | `48` | `width="48" height="48"` |
| `M1_BTN_LABEL_SIZE` | `16` | `font-size: 16px` |
| `M1_BTN_LABEL_WEIGHT` | `600` | `font-weight: 600` |
| `M1_BTN_LABEL_LETTER_SPACE` | `2` | `letter-spacing: 2px` |
| `M1_BTN_LABEL_COLOR` | `0x555555` | `color: #555` |
| **Play/Pause 강조** | | |
| `M1_PLAY_BTN_BG` | `0x1A1A1A` | `.m1-play-btn { background-color: #1a1a1a }` |
| `M1_PLAY_BTN_BORDER` | `0x333333` | `border-color: #333` |
| `M1_PLAY_BTN_ICON_COLOR` | `0xFFFFFF` | `color: #fff` |
| `M1_PLAY_BTN_LABEL_COLOR` | `0x888888` | `color: #888` |

**Mode 1 버튼 매트릭스 레이아웃 (1행 → 2행, 좌 → 우):**

| 위치 | 아이콘 | 라벨 | 스타일 |
|:---|:---|:---|:---|
| (0,0) | Prev 48×48 | `PREV` | 기본 |
| (0,1) | Play/Pause 48×48 | `PLAY/PAUSE` | **강조** (밝은 배경) |
| (0,2) | Next 48×48 | `NEXT` | 기본 |
| (1,0) | Vol Down 48×48 | `VOL -` | 기본 |
| (1,1) | Mute 48×48 | `MUTE` | 기본 |
| (1,2) | Vol Up 48×48 | `VOL +` | 기본 |

### 8.5 Mode 2 — Spotify 컨트롤러

| 상수명 | 값 | CSS 원본 |
|:---|:---|:---|
| `M2_TOP_H` | `640` | `height: 640px` |
| `M2_TOP_PAD_TOP` | `10` | `padding: 10px 40px 30px 40px` |
| `M2_TOP_PAD_SIDE` | `40` | 좌/우 패딩 |
| `M2_TOP_PAD_BOTTOM` | `30` | 하단 패딩 |
| **곡 정보 (Info)** | | |
| `M2_INFO_MARGIN_B` | `25` | `margin-bottom: 25px` |
| `M2_TITLE_FONT_SIZE` | `40` | `font-size: 40px` |
| `M2_TITLE_FONT_WEIGHT` | `700` | `font-weight: 700` |
| `M2_TITLE_MARGIN_B` | `4` | `margin-bottom: 4px` |
| `M2_TITLE_LINE_HEIGHT` | `1.2` | `line-height: 1.2` (= 48px) |
| `M2_ARTIST_FONT_SIZE` | `24` | `font-size: 24px` |
| `M2_ARTIST_COLOR` | `0xD0D0D0` | `color: #d0d0d0` |
| `M2_HEART_COLOR` | `0x1DB954` | `color: #1DB954` (Spotify Green) |
| `M2_HEART_SIZE` | `36` | `width="36" height="36"` |
| **진행 바 (Progress)** | | |
| `M2_PROG_GAP` | `10` | `gap: 10px` |
| `M2_PROG_MARGIN_B` | `30` | `margin-bottom: 30px` |
| `M2_PROG_BAR_H` | `6` | `height: 6px` |
| `M2_PROG_BAR_RADIUS` | `3` | `border-radius: 3px` |
| `M2_PROG_BAR_BG_COLOR` | `0xFFFFFF` | `rgba(255,255,255,0.3)` |
| `M2_PROG_BAR_BG_OPA` | `77` | (0.3 × 255) |
| `M2_PROG_FILL_COLOR` | `0xFFFFFF` | `background: white` |
| `M2_PROG_TIME_FONT_SIZE` | `16` | `font-size: 16px` |
| `M2_PROG_TIME_COLOR` | `0xCCCCCC` | `color: #ccc` |
| **컨트롤 버튼 (Controls)** | | |
| `M2_CTRL_PAD_SIDE` | `10` | `padding: 0 10px` |
| `M2_BTN_HITBOX_PAD` | `20` | `padding: 20px` (투명 터치 확장 영역) |
| `M2_BTN_DIM_COLOR` | `0xB3B3B3` | `.m2-btn.dim { color: #b3b3b3 }` |
| `M2_PLAY_BTN_SIZE` | `80` | `width: 80px; height: 80px` |
| `M2_PLAY_BTN_BG` | `0xFFFFFF` | `background: white` |
| `M2_PLAY_BTN_RADIUS` | `LV_RADIUS_CIRCLE` | `border-radius: 50%` |
| `M2_PLAY_BTN_ICON_COLOR` | `0x000000` | `color: black` |
| `M2_PLAY_BTN_SHADOW_W` | `15` | `box-shadow: 0 5px 15px` |
| `M2_PLAY_BTN_SHADOW_OFS_Y` | `5` | |
| `M2_PLAY_BTN_SHADOW_OPA` | `128` | `rgba(0,0,0,0.5)` |
| `M2_SHUFFLE_REPEAT_SIZE` | `32` | `width="32" height="32"` |
| `M2_PREV_NEXT_SIZE` | `36` | `width="36" height="36"` |
| `M2_PLAY_ICON_SIZE` | `36` | Play/Pause 아이콘 내부 크기 |

**Mode 2 컨트롤 버튼 배치 (좌 → 우):**

| 위치 | 아이콘 | 크기 | 색상 | 스타일 |
|:---|:---|:---|:---|:---|
| 1 | Shuffle | 32×32 | `#b3b3b3` (dim) | 비활성 톤 |
| 2 | Prev | 36×36 | `White` | 기본 |
| 3 | Pause (원형 버튼 안) | 36×36 | `Black` on White | 원형 80×80 흰색 배경 |
| 4 | Next | 36×36 | `White` | 기본 |
| 5 | Repeat | 32×32 | `#b3b3b3` (dim) | 비활성 톤 |

### 8.6 Mode 3 — 숫자 패드 / 계산기

| 상수명 | 값 | CSS 원본 |
|:---|:---|:---|
| **상단 (디스플레이)** | | |
| `M3_TOP_H` | `384` | `height: 384px` |
| `M3_TOP_BG` | `0x121212` | `background-color: #121212` |
| `M3_TOP_PAD_TOP` | `10` | `padding: 10px 40px 20px 40px` |
| `M3_TOP_PAD_SIDE` | `40` | |
| `M3_TOP_PAD_BOTTOM` | `20` | |
| **토글 버튼** | | |
| `M3_TOGGLE_BG` | `0x2C2C2C` | `background: #2C2C2C` |
| `M3_TOGGLE_PAD_V` | `16` | `padding: 16px 24px` |
| `M3_TOGGLE_PAD_H` | `24` | |
| `M3_TOGGLE_RADIUS` | `30` | `border-radius: 30px` |
| `M3_TOGGLE_FONT_SIZE` | `18` | `font-size: 18px` |
| `M3_TOGGLE_COLOR` | `0x1DB954` | `color: #1DB954` |
| `M3_TOGGLE_BORDER_W` | `1` | `border: 1px solid` |
| `M3_TOGGLE_BORDER_COLOR` | `0x1DB954` | `#1DB954` |
| `M3_TOGGLE_FONT_WEIGHT` | `600` | `font-weight: 600` |
| **결과 표시** | | |
| `M3_HISTORY_FONT_SIZE` | `32` | `font-size: 32px` |
| `M3_HISTORY_COLOR` | `0x888888` | `color: #888` |
| `M3_HISTORY_MARGIN_B` | `10` | `margin-bottom: 10px` |
| `M3_RESULT_FONT_SIZE` | `80` | `font-size: 80px` |
| `M3_RESULT_FONT_WEIGHT` | `300` | `font-weight: 300` |
| `M3_RESULT_LETTER_SPACE` | `2` | `letter-spacing: 2px` |
| **하단 (버튼 매트릭스)** | | |
| `M3_MATRIX_H` | `896` | `height: 896px` |
| `M3_MATRIX_BG` | `0x121212` | `background-color: #121212` |
| `M3_MATRIX_PAD` | `10` | `padding: 10px 10px` |
| `M3_MATRIX_GAP` | `4` | `gap: 4px` |
| `M3_MATRIX_COLS` | `4` | `grid-template-columns: repeat(4, 1fr)` |
| `M3_MATRIX_ROWS` | `5` | `grid-template-rows: repeat(5, 1fr)` |
| `M3_BTN_BG` | `0x2C2C2C` | `background-color: #2C2C2C` |
| `M3_BTN_RADIUS` | `12` | `border-radius: 12px` |
| `M3_BTN_FONT_SIZE` | `48` | `font-size: 48px` |
| `M3_BTN_FONT_WEIGHT` | `400` | `font-weight: 400` |
| `M3_BTN_COLOR` | `0xFFFFFF` | `color: white` |
| `M3_BTN_OP_BG` | `0xFF9F0A` | `.btn-op { background-color: #FF9F0A }` |
| `M3_BTN_OP_WEIGHT` | `600` | `font-weight: 600` |
| `M3_BTN_FN_BG` | `0xA5A5A5` | `.btn-fn { background-color: #A5A5A5 }` |
| `M3_BTN_FN_COLOR` | `0x000000` | `color: black` |
| `M3_BTN_FN_WEIGHT` | `600` | `font-weight: 600` |
| `M3_BACKSPACE_ICON_SIZE` | `36` | `width: 36px; height: 36px` |

**Mode 3 버튼 그리드 레이아웃 (5행 × 4열):**

| 행 | Col 0 | Col 1 | Col 2 | Col 3 |
|:---|:---|:---|:---|:---|
| **Row 0** | C (fn) | / (op) | * (op) | ⌫ Backspace (op) |
| **Row 1** | 7 | 8 | 9 | − (op) |
| **Row 2** | 4 | 5 | 6 | + (op) |
| **Row 3** | 1 | 2 | 3 | ↵ Enter (op, **row-span 2**) |
| **Row 4** | 0 (**col-span 2**) | . | ↵ (row 3에서 연장) |

---

## 9. 상태바 통합 구현 (Status Bar Unification)

3개 모드 모두 **동일한 구조의 상태바**를 사용합니다. `DisplayStatusBar` 클래스 하나로 생성하되, 부모 위젯과 포지셔닝만 모드별로 다르게 적용합니다.

### 9.1 상태바 내부 구조 (3분할 Flex Row)

```
[좌측 120px]          [중앙 flex-grow]          [우측 120px]
Battery(24×24) 100%   ● ○ ○                    WiFi(20×20) BLE(20×20)
gap: 8px              letter-spacing: 8px       gap: 12px
```

### 9.2 모드별 배치 차이

| 모드 | 부모 | 포지셔닝 | 배경 |
|:---|:---|:---|:---|
| **Mode 1** | `.m1-top` 컨테이너의 첫 번째 flex 자식 | 일반 flex flow (자연 배치) | 투명 (부모 #000000 노출) |
| **Mode 2** | `.m2-top` 앨범아트 컨테이너 | `lv_obj_set_pos(bar, 0, 10)` + `lv_obj_move_foreground(bar)` | 투명 (그라데이션 위에 떠있음) |
| **Mode 3** | `.m3-top` 컨테이너의 첫 번째 flex 자식 | 일반 flex flow (자연 배치) | 투명 (부모 #121212 노출) |

Mode 2에서 상태바가 `position: absolute; top: 10px`로 앨범아트 위에 떠있는 것과 동일하게, LVGL에서는:
```cpp
// Mode 2: 상태바를 앨범아트 컨테이너 위에 떠있게 처리
lv_obj_remove_flag(status_bar, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK); // flex에서 제외
lv_obj_set_pos(status_bar, 0, 10);    // absolute position
lv_obj_move_foreground(status_bar);    // z-order 최상위
```

### 9.3 모드 인디케이터 (● ○ ○)

폰트/벡터 의존성 없이 LVGL 네이티브 위젯으로 구현:
```cpp
constexpr int32_t INDICATOR_SIZE = 8;      // 직경
constexpr int32_t INDICATOR_GAP = 8;       // 간격 (letter-spacing: 8px 대응)
constexpr lv_color_t INDICATOR_ACTIVE = lv_color_white();
constexpr lv_color_t INDICATOR_INACTIVE_BORDER = lv_color_white();

// 활성: 채워진 원 (bg_opa = COVER, radius = CIRCLE)
// 비활성: 빈 원 (bg_opa = TRANSP, border_opa = COVER, border_width = 1)
```

---

## 10. 아이콘 벡터 렌더링 명세 (Icon Rendering Specification)

### 10.1 부팅 시 캐싱할 15종 벡터 아이콘

> 각 아이콘의 **렌더링 방식**(Fill/Stroke)을 명시합니다. prototype.html의 SVG `<symbol>` 속성에서 정확히 추출한 값입니다.
> LVGL `lv_vector_dsc_t`에서 `lv_vector_dsc_set_fill_color()` 또는 `lv_vector_dsc_set_stroke_color()` + `lv_vector_dsc_set_stroke_width()`를 사용할 때 이 구분이 필수적입니다.

| 아이콘 | 용도 | 사이즈 | 색상 | 렌더링 | SVG Path Data (요소별 fill/stroke 구분) |
|:---|:---|:---|:---|:---|:---|
| **Battery** | 상태바 | 24×24 | `White(0.9)` | **Stroke** | `rect(stroke, x=2,y=7,w=16,h=10,r=2)`, `line(stroke w=2, 22,11→22,13)` |
| **WiFi** | 상태바 | 20×20 | `White(0.9)` | **Stroke** | `path(stroke, "M5 12.55a11 11 0 0 1 14.08 0")`, `path(stroke, "M1.42 9a16 16 0 0 1 21.16 0")`, `path(stroke, "M8.53 16.11a6 6 0 0 1 6.95 0")`, `line(stroke w=2, 12,20→12.01,20)` |
| **BLE** | 상태바 | 20×20 | `White(0.9)` | **Stroke** | `polyline(stroke, "6.5,6.5 17.5,17.5 12,23 12,1 17.5,6.5 6.5,17.5")` |
| **Prev** | Mode1/2 | 48/36 | `#888`/`White` | **Fill+Stroke** | `polygon(fill, "19,20 9,12 19,4")`, `line(stroke w=2, 5,19→5,5)` |
| **Next** | Mode1/2 | 48/36 | `#888`/`White` | **Fill+Stroke** | `polygon(fill, "5,4 15,12 5,20")`, `line(stroke w=2, 19,5→19,19)` |
| **Play** | Mode1/2 | 48/36 | `#888`/`Black` | **Fill** | `polygon(fill, "5,3 19,12 5,21")` |
| **Pause** | Mode 2 | 36 | `Black` | **Fill** | `rect(fill, x=6,y=4,w=4,h=16)`, `rect(fill, x=14,y=4,w=4,h=16)` |
| **Play/Pause** | Mode 1 | 48 | `#888` | **Fill** | `polygon(fill, "3,4 12,12 3,20")`, `rect(fill, x=15,y=4,w=3,h=16)`, `rect(fill, x=20,y=4,w=3,h=16)` |
| **Vol Down** | Mode 1 | 48 | `#888` | **Stroke** | `polygon(stroke, "11,5 6,9 2,9 2,15 6,15 11,19")`, `path(stroke, "M15.54 8.46a5 5 0 0 1 0 7.07")` |
| **Vol Up** | Mode 1 | 48 | `#888` | **Stroke** | `polygon(stroke, "11,5 6,9 2,9 2,15 6,15 11,19")`, `path(stroke, "M19.07 4.93a10 10 0 0 1 0 14.14 M15.54 8.46a5 5 0 0 1 0 7.07")` |
| **Mute** | Mode 1 | 48 | `#888` | **Stroke** | `polygon(stroke, "11,5 6,9 2,9 2,15 6,15 11,19")`, `line(stroke, 23,9→17,15)`, `line(stroke, 17,9→23,15)` |
| **Shuffle** | Mode 2 | 32 | `White`/`#b3b3b3` | **Stroke** | `polyline(stroke, "16,3 21,3 21,8")`, `line(stroke, 4,20→21,3)`, `polyline(stroke, "21,16 21,21 16,21")`, `line(stroke, 15,15→21,21)`, `line(stroke, 4,4→9,9)` |
| **Repeat** | Mode 2 | 32 | `White`/`#b3b3b3` | **Stroke** | `polyline(stroke, "17,1 21,5 17,9")`, `path(stroke, "M3 11V9a4 4 0 0 1 4-4h14")`, `polyline(stroke, "7,23 3,19 7,15")`, `path(stroke, "M21 13v2a4 4 0 0 1-4 4H3")` |
| **Heart** | Mode 2 | 36 | `#1DB954` | **Fill** | `path(fill, "M20.84 4.61a5.5 5.5 0 0 0-7.78 0L12 5.67l-1.06-1.06a5.5 5.5 0 0 0-7.78 7.78l1.06 1.06L12 21.23l7.78-7.78 1.06-1.06a5.5 5.5 0 0 0 0-7.78z")` |
| **Backspace** | Mode 3 | 36 | `White` | **Stroke** | `path(stroke, "M21 4H8l-7 8 7 8h13a2 2 0 0 0 2-2V6a2 2 0 0 0-2-2z")`, `line(stroke, 18,9→12,15)`, `line(stroke, 12,9→18,15)` |
| **Enter** | Mode 3 | 36 | `White` | **Fill+Stroke** | `polyline(stroke, "15,5 15,15 5,15")`, `polygon(fill, "5,11 1,15 5,19")` |

> **참고:** `stroke` 렌더링 시 모든 아이콘의 공통 속성은 `stroke-width: 2`, `stroke-linecap: round`, `stroke-linejoin: round` 입니다 (ViewBox 24×24 기준). LVGL에서는 `lv_vector_dsc_set_stroke_width()` 및 `LV_VECTOR_STROKE_CAP_ROUND`, `LV_VECTOR_STROKE_JOIN_ROUND`로 설정합니다.

### 10.2 SVG Path → LVGL Vector Path 변환 규칙

| SVG 커맨드 | LVGL API | 비고 |
|:---|:---|:---|
| `M x y` | `lv_vector_path_move_to(path, x, y)` | 시작점 이동 |
| `L x y` | `lv_vector_path_line_to(path, x, y)` | 직선 |
| `H x` | `lv_vector_path_line_to(path, x, current_y)` | 수평선 (y 유지) |
| `V y` | `lv_vector_path_line_to(path, current_x, y)` | 수직선 (x 유지) |
| `C x1 y1 x2 y2 x y` | `lv_vector_path_cubic_to(path, cx1,cy1, cx2,cy2, x,y)` | 3차 베지어 |
| `Q x1 y1 x y` | `lv_vector_path_quad_to(path, cx,cy, x,y)` | 2차 베지어 |
| `A rx ry rot large sweep x y` | **직접 지원 안 함** → 베지어 근사 변환 필요 | 아래 10.3 참조 |
| `Z` | `lv_vector_path_close(path)` | 경로 닫기 |

> **좌표 스케일링:** ViewBox 24×24 기준 패스를 실제 렌더 사이즈(예: 48×48)로 캐싱할 때, 모든 좌표에 `scale = target_size / 24.0f` 배율을 곱합니다.

### 10.3 SVG Arc → Cubic Bezier 변환 알고리즘

WiFi, Heart, Repeat, Vol Down/Up 아이콘에 포함된 SVG `a`(arc) 커맨드를 LVGL로 변환하는 절차:

1. **Endpoint → Center 변환:** SVG arc의 endpoint 매개변수(`rx, ry, x-rotation, large-arc-flag, sweep-flag, x, y`)를 중심점(`cx, cy`) + 시작/끝 각도(`θ1, θ2`)로 변환합니다. (W3C SVG Implementation Notes 표준 알고리즘)
2. **Arc → Cubic Bezier 분할:** 하나의 arc 세그먼트가 90°를 초과하면 여러 개의 sub-arc로 분할합니다 (각 sub-arc ≤ 90°).
3. **각 sub-arc를 3차 베지어로 근사:** `α = 4.0 * tan(dθ/4) / 3.0` 공식으로 제어점을 계산하여 `lv_vector_path_cubic_to()`를 호출합니다.

```cpp
// 유틸리티 함수 시그니처 (ui_icons.hpp에 구현)
void svg_arc_to_bezier(
    lv_vector_path_t* path,
    float cx, float cy,           // 현재 위치
    float rx, float ry,           // 반지름
    float x_rotation,             // X축 회전 (라디안)
    bool large_arc, bool sweep,   // 플래그
    float ex, float ey,           // 끝점
    float scale                   // 스케일 팩터
);
```

### 10.4 상단 모드 인디케이터 (● ○ ○)

폰트나 벡터 아이콘이 아닌, LVGL 기본 빈 위젯(`lv_obj_t`)에 `radius = LV_RADIUS_CIRCLE` 속성을 주어 폰트/벡터 의존성 및 메모리 점유율 0%로 네이티브 렌더링합니다.

---

## 11. Mode 2 그라데이션 오버레이 구현

### 11.1 문제 정의

prototype.html의 앨범아트 위 그라데이션은 **4개 정지점(Color Stop)**을 가집니다:

```css
linear-gradient(180deg,
    rgba(0,0,0,0.4)   0%,     /* 상단: 약간 어둡게 */
    rgba(0,0,0,0.1)  30%,     /* 중단: 거의 투명 (앨범아트 최대 노출) */
    rgba(0,0,0,0.8)  70%,     /* 하단부 시작: 짙게 */
    rgba(0,0,0,1.0) 100%      /* 최하단: 완전 검정 (터치패드와 자연 연결) */
);
```

LVGL 9.x의 `lv_grad_dsc_t`는 `LV_GRADIENT_MAX_STOPS` 개수만큼 정지점을 지원합니다. 기본값은 2개이므로, 4-stop gradient를 사용하려면 **`lv_conf.h`에서 `LV_GRADIENT_MAX_STOPS`를 4 이상으로 설정**해야 합니다.

### 11.2 구현 방법

```cpp
// lv_conf.h 설정
#define LV_GRADIENT_MAX_STOPS 4

// 구현 코드
lv_obj_t* gradient_overlay = lv_obj_create(m2_top_container);
lv_obj_set_size(gradient_overlay, 720, 640);
lv_obj_set_pos(gradient_overlay, 0, 0);

lv_grad_dsc_t grad;
lv_gradient_init_stops(&grad, 4);
lv_gradient_set_stops(&grad, (lv_gradient_stop_t[]){
    {.color = lv_color_black(), .opa = LV_OPA_40,    .frac = 0},    // 0%
    {.color = lv_color_black(), .opa = LV_OPA_10,    .frac = 77},   // 30% (255*0.3)
    {.color = lv_color_black(), .opa = LV_OPA_80,    .frac = 179},  // 70% (255*0.7)
    {.color = lv_color_black(), .opa = LV_OPA_COVER, .frac = 255},  // 100%
});
grad.dir = LV_GRAD_DIR_VER;

lv_obj_set_style_bg_grad(gradient_overlay, &grad, 0);
lv_obj_move_foreground(gradient_overlay); // 앨범아트 위, 컨트롤 아래
```

> **`LV_GRADIENT_MAX_STOPS` 변경이 불가능한 경우 대안:** 2개의 gradient 레이어를 겹침.
> - 레이어 1 (상반부 0~50%): `rgba(0,0,0,0.4)` → `rgba(0,0,0,0.1)` (height: 320px)
> - 레이어 2 (하반부 50~100%): `rgba(0,0,0,0.1)` → `rgba(0,0,0,1.0)` (height: 320px)

### 11.3 텍스트 그림자 (Text Shadow) 대응

prototype.html에서 `text-shadow: 0 2px 10px rgba(0,0,0,0.8)`이 적용된 텍스트(제목, 아티스트, 시간)가 있습니다. LVGL는 텍스트 그림자를 네이티브 지원하지 않으므로:

**대안: Shadow Label 이중 렌더링**
```cpp
// 그림자용 라벨 (먼저 생성 → z-order 하위)
lv_obj_t* shadow_label = lv_label_create(parent);
lv_label_set_text(shadow_label, title_text);
lv_obj_set_style_text_color(shadow_label, lv_color_black(), 0);
lv_obj_set_style_text_opa(shadow_label, LV_OPA_80, 0);  // 0.8
lv_obj_set_pos(shadow_label, x_pos, y_pos + 2);          // y 오프셋 2px

// 본 라벨 (나중 생성 → z-order 상위)
lv_obj_t* main_label = lv_label_create(parent);
lv_label_set_text(main_label, title_text);
lv_obj_set_pos(main_label, x_pos, y_pos);
```

> blur 효과(10px)는 LVGL에서 직접 구현 불가. 그라데이션 오버레이가 이미 가독성을 확보하므로, y 오프셋 그림자만으로 시각적 동등성을 충분히 달성할 수 있습니다.

---

## 12. Mode 2 프로그레스 바 / 슬라이더 구현

### 12.1 설계 선택: `lv_slider` (Seek 기능 대비)

prototype.html의 프로그레스 바는 표시 전용이지만, Spotify Web API의 seek 기능을 활용하기 위해 **`lv_slider`**를 사용합니다. 프로토타입과 동일한 외형을 유지하면서도 터치 드래그로 seek 명령을 보낼 수 있습니다.

### 12.2 커스텀 스타일링

```cpp
lv_obj_t* slider = lv_slider_create(prog_container);
lv_obj_set_width(slider, lv_pct(100));  // 컨테이너 폭에 맞춤
lv_obj_set_height(slider, 6);           // M2_PROG_BAR_H

// Track (배경 바)
lv_obj_set_style_bg_color(slider, lv_color_white(), LV_PART_MAIN);
lv_obj_set_style_bg_opa(slider, 77, LV_PART_MAIN);       // rgba(255,255,255,0.3)
lv_obj_set_style_radius(slider, 3, LV_PART_MAIN);         // border-radius: 3px

// Indicator (채워진 부분)
lv_obj_set_style_bg_color(slider, lv_color_white(), LV_PART_INDICATOR);
lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
lv_obj_set_style_radius(slider, 3, LV_PART_INDICATOR);

// Knob (프로토타입에 노브 없음 → 완전 투명 처리)
lv_obj_set_style_bg_opa(slider, LV_OPA_TRANSP, LV_PART_KNOB);
lv_obj_set_style_pad_all(slider, 0, LV_PART_KNOB);
lv_obj_set_style_width(slider, 0, LV_PART_KNOB);
lv_obj_set_style_height(slider, 0, LV_PART_KNOB);
```

### 12.3 시간 텍스트 배치

```
[2:14]                                               [5:55]
  ↑ lv_obj_align(LV_ALIGN_TOP_LEFT)    lv_obj_align(LV_ALIGN_TOP_RIGHT) ↑
```

- `font-variant-numeric: tabular-nums` 대응: Inter 폰트의 Tabular Figures OpenType 기능을 활용. LVGL의 `lv_freetype`에서 OpenType feature 설정이 불가한 경우, 시간 라벨을 고정 너비로 설정(`lv_obj_set_width(time_label, fixed_width)` + `LV_TEXT_ALIGN_LEFT/RIGHT`)하여 시각적 일관성을 확보합니다.

---

## 13. Mode 3 커스텀 그리드 구현 (Enter Row-Span 해결)

### 13.1 문제 정의

prototype.html에서 Enter(↵) 버튼은 `grid-row: span 2`로 세로 2셀을 차지합니다.
**LVGL 9.x의 `lv_btnmatrix`는 column-span만 지원하고 row-span은 불가능합니다.**

### 13.2 해결: 개별 `lv_obj` 버튼 + Grid Layout

`lv_btnmatrix`를 사용하지 않고, **20개의 개별 `lv_obj_t` 버튼**을 LVGL의 Grid Layout에 배치합니다.

```cpp
// 그리드 컨테이너 생성
lv_obj_t* grid = lv_obj_create(mode3_tile);
lv_obj_set_size(grid, 720, 896);
lv_obj_set_style_bg_color(grid, lv_color_hex(0x121212), 0);
lv_obj_set_style_pad_all(grid, 10, 0);
lv_obj_set_style_pad_gap(grid, 4, 0);

// 4열 × 5행 그리드 정의
static const int32_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
static const int32_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);

// 일반 버튼 배치 예시 (col, row 지정)
lv_obj_set_grid_cell(btn_7, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
//                                                  col, col_span, row_align, row, row_span

// Enter 버튼: row-span 2 적용
lv_obj_set_grid_cell(btn_enter, LV_GRID_ALIGN_STRETCH, 3, 1, LV_GRID_ALIGN_STRETCH, 3, 2);
//                                                     col=3, col_span=1, row=3, row_span=2

// 0 버튼: col-span 2 적용
lv_obj_set_grid_cell(btn_zero, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 4, 1);
//                                                    col=0, col_span=2, row=4, row_span=1
```

### 13.3 개별 버튼의 장점

- Row-span, Col-span 모두 자유롭게 적용 가능
- 각 버튼의 배경색, 폰트, radius를 개별 스타일링
- `LV_EVENT_CLICKED` 콜백을 버튼별로 직접 등록
- 시각적 눌림 효과(Pressed state)를 `LV_STATE_PRESSED` 스타일로 세밀 제어

---

## 14. 폰트 및 텍스트 명세 (Font Specification)

### 14.1 폰트 파일

- **파일:** `Inter.ttf` (Google Fonts) → SD 카드 `/sdcard/fonts/Inter.ttf`
- **로딩:** `lv_freetype` 또는 `lv_tiny_ttf`를 사용하여 SD 카드에서 PSRAM으로 로드
- **범위:** ASCII (0x20~0x7E) + 쉼표(,) + 점(.) — 한글 미포함 (UI 텍스트는 모두 영문/숫자)

### 14.2 필요 크기 및 weight 목록

prototype.html에서 사용되는 모든 `font-size` × `font-weight` 조합:

| 크기 (px) | Weight | 용도 | LVGL Font 인스턴스명 |
|:---|:---|:---|:---|
| `16` | `600` (SemiBold) | 상태바 텍스트, Mode 1 버튼 라벨, Mode 2 시간 텍스트 | `font_inter_16_semibold` |
| `18` | `600` (SemiBold) | Mode 3 토글 버튼 텍스트 | `font_inter_18_semibold` |
| `24` | `400` (Regular) | Mode 2 아티스트명, 터치패드 힌트 | `font_inter_24_regular` |
| `32` | `400` (Regular) | Mode 3 연산 히스토리 | `font_inter_32_regular` |
| `40` | `700` (Bold) | Mode 2 곡 제목 | `font_inter_40_bold` |
| `48` | `400` (Regular) | Mode 3 숫자 버튼 | `font_inter_48_regular` |
| `48` | `600` (SemiBold) | Mode 3 연산자 버튼 | `font_inter_48_semibold` |
| `80` | `300` (Light) | Mode 3 결과 표시 | `font_inter_80_light` |

> **최적화:** `lv_freetype`는 동일 TTF에서 다양한 크기를 런타임에 생성 가능. 8개 인스턴스를 부팅 시 한꺼번에 생성하여 `Display` 클래스 멤버로 보관합니다.

### 14.3 폰트 Weight 근사 전략

`lv_tiny_ttf`나 `lv_freetype`에서 TTF의 `weight` 축 지정이 제한적인 경우:
- Inter TTF는 Variable Font를 지원하므로, `Inter-VariableFont_wght.ttf`를 사용하면 weight 축을 직접 제어 가능
- Variable Font 미지원 시: `Inter-Light.ttf`(300), `Inter-Regular.ttf`(400), `Inter-SemiBold.ttf`(600), `Inter-Bold.ttf`(700) 4개 파일을 SD 카드에 배치하고 개별 로드

---

## 15. 초기화 순서 (Initialization Sequence)

SD 카드에서 폰트를 로드해야 하므로, **SD 카드 마운트가 디스플레이 초기화보다 반드시 먼저** 수행되어야 합니다.

```
1. bsp_feature_enable(BSP_FEATURE_WIFI, true)
2. vTaskDelay(2000ms)
3. nvs_flash_init()
4. WiFi::initialize_station()
5. sdcard_sdmmc_init()          ← SD 카드 마운트 (폰트 파일 접근 필요)
6. Display::initialize()        ← 폰트 로드 + 벡터 아이콘 캐싱 + UI 생성
   6.1 bsp_display_start_with_config()  — 하드웨어 디스플레이 초기화
   6.2 SD 카드에서 Inter.ttf 로드 → PSRAM 폰트 인스턴스 8개 생성
   6.3 lv_canvas 기반 벡터 아이콘 15종 래스터라이징 → PSRAM 비트맵 캐싱
   6.4 Tileview 생성 (3개 타일: Mode 1, 2, 3)
   6.5 각 모드별 UI 위젯 트리 구축 (상태바 포함)
   6.6 스플래시 화면 → 메인 화면 전환
7. BLE HID 초기화
8. 메인 루프 진입
```

> **LVGL 9.x API 참고:** 기존 코드의 `lv_disp_get_scr_act(display_handle_)`는 LVGL 9.x에서 **`lv_display_get_screen_active(display_handle_)`**로 변경되었습니다. 마찬가지로 `lv_img_dsc_t`는 `lv_image_dsc_t`로 변경되었습니다.

---

## 16. prototype.html 완전 재현 체크리스트

아래 항목을 모두 충족하면 prototype.html과 시각적으로 동일한 LVGL UI가 완성됩니다.

- [ ] **색상 체계:** 섹션 8의 모든 `COLOR_*` / hex 상수가 코드에 반영
- [ ] **레이아웃 치수:** 모든 padding, gap, margin, radius, border-width가 px 단위로 일치
- [ ] **상태바:** 3개 모드 동일 구조, Mode 2만 absolute 포지셔닝 (섹션 9)
- [ ] **Mode 1 그리드:** 3×2 버튼, Play/Pause 강조 스타일, 아이콘+라벨 구성
- [ ] **Mode 2 그라데이션:** 4-stop linear gradient (섹션 11)
- [ ] **Mode 2 슬라이더:** 노브 숨김, 6px height, radius 3px (섹션 12)
- [ ] **Mode 2 컨트롤:** Shuffle/Repeat dim 톤, 원형 Play 버튼, shadow
- [ ] **Mode 3 그리드:** 개별 버튼 Grid Layout, Enter row-span 2, 0 col-span 2 (섹션 13)
- [ ] **Mode 3 색상:** C=#A5A5A5(검정글씨), 연산자=#FF9F0A(흰글씨), 숫자=#2C2C2C(흰글씨)
- [ ] **아이콘:** 15종 벡터 아이콘 fill/stroke 정확 구분 (섹션 10)
- [ ] **폰트:** Inter 8개 인스턴스, 크기/weight 정확 (섹션 14)
- [ ] **터치패드:** 코너 L자 2면 border, 중앙 "TOUCHPAD" 힌트 텍스트
- [ ] **스와이프:** Tileview 좌우 스와이프 전환, 인디케이터 동기화
- [ ] **텍스트 그림자:** Mode 2 제목/아티스트 이중 라벨 기법 (섹션 11.3)
- [ ] **초기화 순서:** SD 마운트 → Display 초기화 (섹션 15)
