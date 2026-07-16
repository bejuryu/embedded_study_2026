# DESIGN

## 개발 사양

- hardware: M5Stack TAB5
- framework: idf 6.0.1
- language: C++
- display library: LVGL 9.x
- screen: 720 x 1280

## 1. 코딩 표준 및 아키텍처 (Coding Standards & Architecture)

### 1.1 Modern C++23 활용
- ESP-IDF v5.2+ 환경의 이점을 살려 **C++23 표준**을 철저히 준수합니다.
- 매크로(`#define`) 사용을 전면 금지하고, 모든 상수는 `constexpr` 또는 `consteval`로 선언하여 타입 안정성을 확보하고 런타임 오버헤드를 0으로 만듭니다.
- `std::span`, `std::array`, Range-based for loop 등 모던 C++ 문법을 적극 활용하여 포인터 연산의 위험성과 메모리 누수를 원천 차단합니다.

### 1.2 매직 넘버(Magic Number) 배제 및 상수 모듈화
- UI 그리기에 필요한 SVG 좌표(Vector Path Data), 색상 헥스코드, 위젯 크기 등 렌더링 수치 데이터를 함수 내부에 절대 하드코딩하지 않습니다.
- 특정 목적을 가진 상수들은 `namespace`나 `struct` 단위로 논리적으로 묶어서 관리합니다. (예: `UI::Theme::Color::SpotifyGreen`, `UI::Icon::Play_Path`)
- **데이터(좌표/디자인)**와 **로직(LVGL 함수 연산)**을 완벽히 분리하여, 추후 UI 디자인 수정 시 C++ 로직 코드를 손대지 않도록 구조화합니다.

### 1.3 간결함과 가독성 (Readability)
- 1함수 1기능 원칙을 준수하여 로직을 짧고 간결하게 유지합니다.
- "이 코드가 무엇(What)을 하는가"가 코드 흐름 자체로 읽히도록 가독성 높은 네이밍(Self-documenting Code)을 지향합니다.

## 2. 공통 기능 (Global Features)

- **모드 전환 기능:** 화면 스와이프 또는 상단 UI 버튼을 통한 Mode 1, Mode 2, Mode 3 상태 전환.
- **연결 상태 표시:** BLE 통신 상태 및 WiFi 연결 상태 아이콘 출력.

## 2. 기능 명세 (화면 구성)

### [Mode 1] PC 미디어 제어 + Touch Pad (세로 분할 1:1)
BLE HID 디바이스(Consumer Control 및 Mouse/Digitizer) 역할 수행.
- **상단 (PC 미디어 리모컨):** 터치 이벤트 발생 시 사전 정의된 BLE Consumer Report(Play/Pause, Volume, Prev/Next)를 호스트 기기(PC)로 전송.
- **하단 (정밀 터치패드):** GT911 터치 센서의 원시 좌표(X, Y) 및 상태를 스케일링하여 BLE PTP(Precision Touchpad) Report 데이터로 전송. 다중 터치 제스처 연산 지원.

### [Mode 2] Spotify 독립 제어 + Touch Pad (세로 분할 1:1)
WiFi 기반 REST API 통신 및 BLE HID 디바이스 역할 동시 수행.
- **상단 (Spotify 독립 제어):** ESP32 WiFi 모듈을 통한 Spotify Web API 통신. 트랙 정보(곡명, 가수, 앨범 아트) 파싱 및 디스플레이 출력. 재생 제어 명령을 API 서버로 HTTP 요청.
- **하단 (정밀 터치패드):** Mode 1과 동일하게 BLE PTP Report 데이터 전송.

### [Mode 3] 숫자 패드 (Numpad) 및 로컬 계산기 (전체 화면)
토글 버튼 이벤트에 따른 입력 데이터 라우팅 분기 처리.
- **A. Numpad 모드 (BLE HID Keyboard):** 화면 터치 시 BLE Keyboard Report(Numpad Keycodes)를 호스트 기기로 전송. 기기 로컬 디스플레이 갱신 없음.
- **B. 계산기 모드 (Local Processing):** 입력된 데이터를 로컬 메모리에서 산술 연산 후 디스플레이 라벨 갱신. BLE 통신 발생하지 않음.

## 3. 화면별 UI 구성요소 (UI Components)

### 🌐 [공통 UI 요소]
- **상태바 (Status Bar - 최상단):**
  - 좌/우측: 배터리 잔량, BLE 연결 상태, WiFi 연결 상태 표기용 아이콘 위젯.
  - 중앙: 현재 활성화된 모드 인덱스 표기(`● ○ ○`).
- **내비게이션:** 좌우 스와이프 제스처 이벤트에 바인딩된 모드 전환 로직.

### 🎮 [Mode 1] PC 미디어 제어 + Touch Pad
시각적 주의 분산을 최소화하기 위한 어두운 단색 배경 채택 및 블라인드 터치(Blind Touch) 조작성을 극대화한 대형 그리드 레이아웃 적용.
- **상단 (PC 미디어 제어 영역 - 3x2 매트릭스 레이아웃):**
  - **제어 위젯:** 빈 공간을 최소화한 6개의 대형 라운드 사각형 버튼 렌더링.
  - **버튼 맵핑 (1행):** 이전 곡(`Prev`), 재생/일시정지(`Play/Pause`), 다음 곡(`Next`).
  - **버튼 맵핑 (2행):** 볼륨 감소(`Vol -`), 음소거(`Mute`), 볼륨 증가(`Vol +`).
  - **인터랙션:** 터치 이벤트 발생 시 버튼 위젯의 명도를 일시적으로 높여 상태 피드백 렌더링. 슬라이더 미사용.
- **하단 (터치패드 영역):**
  - 단일 헥스 코드(Hex code) 기반의 어두운 배경 출력(예: #000000). 4개 모서리에 터치 바운더리를 명시하는 코너 라인 UI 렌더링.

### 🎧 [Mode 2] Spotify 컨트롤러 + Touch Pad
ESP32의 WiFi 기능을 활용한 Spotify Web API 연동 및 블루투스 터치 동시 동작 처리. 풀스크린 앨범 아트(Fullscreen Album Art) 기반의 몰입감 높은 프리미엄 UI 제공.
- **상단 (Spotify 제어 영역):**
  - **배경 처리:** Spotify API에서 수신한 `640x640` 해상도의 고화질 앨범 아트 이미지를 제어 영역(720x640)의 전체 배경(Background)으로 렌더링.
  - **가독성 확보:** 텍스트와 컨트롤러 버튼이 위치하는 하단부로 갈수록 짙어지는 검은색 그라데이션 오버레이(Gradient Overlay) 렌더링 적용.
  - **정보 위젯:** 하단부 밀착 배치된 현재 재생 곡 제목(Title) 및 아티스트(Artist) 정보 텍스트 렌더링.
  - **진행 바:** 곡 진행 상태를 나타내는 하단 Progress Bar 및 재생 시간 텍스트.
  - **제어 위젯:** Shuffle, Prev, Play/Pause, Next, Repeat 등 5개의 하단 미디어 컨트롤 제스처 위젯.
- **하단 (터치패드 영역):**
  - Mode 1의 하단 레이아웃 위젯 재사용.

### 🖩 [Mode 3] 숫자 패드 (Numpad) 및 계산기
- **상단 (디스플레이 영역 - 높이 30% 할당):**
  - 우측 정렬된 메인 연산 텍스트 라벨 및 보조 수식 히스토리 라벨.
  - 연산/Numpad 모드 상태 전환 및 현재 상태를 표기하는 토글 버튼 위젯.
- **하단 (버튼 매트릭스 영역 - 높이 70% 할당):**
  - 4x5 배열 규격의 Button Matrix 위젯 적용.
  - 맵핑 데이터: `[C]`, `[/]`, `[*]`, `[-]`, `[+]`, `[Enter]`, `[0-9]`, `[.]`.
  - Span 속성을 활용하여 `[Enter]` 버튼의 세로 2셀 단위 병합, `[0]` 버튼의 가로 2셀 단위 병합 처리.

## 4. 시스템 상태 및 예외 처리 로직 (System State & Exception Handling)

### 4.1 부팅 및 초기화 상태 (Boot & Initialization)
- **로직:** 하드웨어(Display, GT911, NVS, BLE, WiFi 모듈) 초기화 시퀀스 실행.
- **UI 렌더링:** 스플래시 이미지(로고) 및 부팅 진행률/상태 텍스트 라벨 출력. 메인 GUI 이벤트 루프 진입 전까지 홀드.

### 4.2 네트워크 통신 상태별 UI 분기 (Connection States)
활성화된 통신 인터페이스(BLE/WiFi)의 연결 상태에 따른 위젯 속성(Enabled/Disabled) 동적 제어.

- **상태 A: BLE 연결 실패 또는 대기 (BLE Disconnected)**
  - **공통:** 상태바 BLE 아이콘 투명도 저하 또는 점멸 처리. 터치 이벤트의 BLE 전송 라우팅 차단.
  - **Mode 1 & 2 하단 (터치패드):** 터치패드 영역 내에 `BLE Not Connected` 텍스트 오버레이 렌더링.
  - **Mode 1 상단 (PC 미디어):** 모든 제어 버튼 위젯 상태를 비활성화(Disabled) 처리하여 터치 이벤트 무시.
  - **Mode 3 (Numpad):** Numpad 모드 진입 제한. Local Calculator 모드로 강제 라우팅하여 단독 연산 수행.

- **상태 B: BLE 연결 완료, WiFi 미연결 (BLE Connected, WiFi Disconnected)**
  - **공통:** 상태바 BLE 아이콘 점등, WiFi 아이콘 비활성화.
  - **Mode 1 & 3:** 모든 기능 정상 로드 및 작동.
  - **Mode 2 상단 (Spotify):** API 서버 접속 불가에 따른 경고 표출 및 미디어 제어 버튼 비활성화. 하단의 터치패드(BLE PTP) 영역은 독립적으로 활성 상태 유지.

- **상태 C: 통신 세션 일시 단절 (Connection Lost / Exception)**
  - **로직:** 런타임 중 BLE/WiFi 소켓 단절 감지 시 즉각적으로 전송 큐(Queue) 데이터 푸시 중단(Crash 및 Overflow 방지).
  - **UI 렌더링:** 화면 최상단 레이어에 재연결 상태(`Reconnecting...`)를 나타내는 알림 배너 위젯 팝업.

### 4.3 모드 전환 인터랙션 (Mode Transition Logic)
- **전환 효과:** 스와이프 제스처 이벤트 발생 시 LVGL 내장 API(`LV_SCR_LOAD_ANIM_SLIDE_LEFT` / `RIGHT`)를 호출하여 스크린 전환 렌더링.
- **상태 데이터 보존:** 3개의 스크린 객체(Screen Objects)를 시스템 메모리 힙(Heap) 영역에 상주시켜(Persistent Allocation), 모드 간 전환 시 텍스트 라벨(예: 계산기 연산 히스토리)의 상태 데이터를 보존.
- **비동기 로딩 대기 상태 (Spotify Fetching):** Mode 2 진입 시 HTTP GET 요청에 의한 UI Blocking 스레드 지연을 방지. 서버 응답 수신 대기 시간 동안 앨범 아트 위젯 영역에 비동기 로딩 스피너(Loading Spinner) 위젯 표출.