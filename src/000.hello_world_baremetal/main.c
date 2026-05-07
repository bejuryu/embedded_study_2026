// =========================================================================
// 순수 베어메탈 제어 예제 (외부 프레임워크 미사용) - ESP32-P4
// =========================================================================

#include <stdint.h>

#include "soc_esp32p4_baremetal.h"

#define UINT32_MAX_DIGITS 10  // uint32_t 최대값(4,294,967,295)의 십진수 자릿수

// =========================================================================
// [GCC 내장 64비트 정수 나눗셈 기계어 변환 수동 구현 (__udivdi3)]
//
// * 컴파일러 ABI(Application Binary Interface) 내부 링킹(Linking) 규칙:
//   - ESP32-P4(RV32) 아키텍처는 64비트 전용 하드웨어 나눗셈 명령어(div)를 미지원함.
//   - C 코드의 64비트 나눗셈 연산자('/')는 컴파일러에 의해 아래 규격의 외부 함수 호출(call) 어셈블리로 치환됨:
//     • u   : Unsigned (부호 없는 선언)
//     • div : Division (나눗셈 연산)
//     • di  : Double Integer (64비트 정수형)
//     • 3   : 입력 파라미터 2개, 반환값 1개 구조
//   - 기본적으로 링커는 컴파일러 내장 라이브러리(libgcc.a)에서 해당 심볼을 탐색하여 바이너리를 구성함.
//   - 본 예제는 베어메탈(-nostdlib) 환경이므로 라이브러리 링킹 과정이 원천 차단됨.
//   - 따라서, 동일한 식별자(__udivdi3)의 비트 연산 함수를 소스 본문에 직접 정의하여 링커 에러를 방지함.
// =========================================================================
uint64_t __udivdi3(uint64_t num, uint64_t den) {
  if (den == 0) return ~(uint64_t)0;  // 제로 디바이드 처리

  uint64_t res = 0;
  // MSB(최상위 비트) 기준 단일 비트 시프트 기반 세로 나눗셈 알고리즘
  for (int i = 63; i >= 0; i--) {
    if ((num >> i) >= den) {
      res |= ((uint64_t)1 << i);
      num -= (den << i);
    }
  }
  return res;
}

// [GCC 내장 64비트 정수 나머지 연산 수동 구현 (__umoddi3)]
// __udivdi3와 동일한 ABI 규칙으로 '%' 연산자의 64비트 외부 심볼을 제공함
uint64_t __umoddi3(uint64_t num, uint64_t den) {
  if (den == 0) return 0;
  return num - __udivdi3(num, den) * den;
}

// =========================================================================
// [기본 유틸리티 함수]
// =========================================================================

// 문자열 길이를 연산하여 반환 (표준 구현체 strlen 대체)
uint32_t my_strlen(const char *s) {
  uint32_t len = 0;
  while (s[len]) len++;
  return len;
}

// =========================================================================
// [시간 관리 함수 (HAL)]
// =========================================================================

// 인터럽트 개입 없이 하드웨어 타이머(TIMG1) 레지스터를 직접 참조하여 밀리초(ms) 반환
uint32_t get_time_ms(void) {
  // 하드웨어 타이머 섀도우 레지스터 동기화 래치(Latch)
  TIMG1_T0UPDATE_REG = 1;
  while (TIMG1_T0UPDATE_REG == 1) { NOP(); }

  // 32비트 카운터 오버플로 방지를 위한 상/하 레지스터 병합 (64비트 타입 변환)
  const uint32_t lo = TIMG1_T0LO_REG;
  const uint32_t hi = TIMG1_T0HI_REG;
  const uint64_t full_timer_val = ((uint64_t)hi << 32) | lo;

  // 40MHz 스탠다드 클럭 기준 분주 연산 반환
  return (uint32_t)(full_timer_val / TIMG_CLK_TICKS_PER_MS);
}

// 임계 대기 시간(ms) 동안 루프 폴링 상태 유지 (블로킹 딜레이)
void delay_ms(uint32_t ms) {
  const uint32_t start = get_time_ms();
  while ((get_time_ms() - start) < ms) { NOP(); }
}

// =========================================================================
// [UART 제어 규격화 (HAL)]
// =========================================================================

// 하드웨어 IOMUX 매트릭스 제어를 통한 UART1 입출력 포트 초기화
void uart1_init(void) {
  // 1. UART1 보드레이트 명시적 설정 (UART_CLK_FREQ / UART_BAUD_RATE = 347.222...)
  UART1_CLKDIV_REG = UART_CLKDIV_INT | (UART_CLKDIV_FRAG << UART_CLKDIV_FRAG_S);

  // 2. GPIO 17(TX), 18(RX) 핀의 직접 출력 활성화 및 IOMUX 기능 설정
  GPIO_ENABLE_W1TS_REG = (1 << UART1_TX_PIN_NUM);
  UART1_TX_PIN_IO_MUX = (UART1_TX_PIN_IO_MUX & ~IO_MUX_MCU_SEL_M) | IO_MUX_MCU_SEL_GPIO;
  UART1_RX_PIN_IO_MUX = (UART1_RX_PIN_IO_MUX & ~IO_MUX_MCU_SEL_M) | IO_MUX_MCU_SEL_GPIO | IO_MUX_FUN_IE;

  // 3. GPIO 매트릭스를 통한 UART1 TX/RX 신호 라우팅 설정
  UART1_TX_PIN_OUT_SEL = UART1_TXD_OUT_IDX;
  UART1_RX_SIG_IN_SEL = UART1_RX_PIN_NUM | GPIO_SIG_IN_SEL_BIT;
}

// 단일 바이트 단위 UART1 비동기 송신 (블로킹 모드)
void uart1_putc(char c) {
  // TX FIFO 잔량 확인 후 송신 가능 시점까지 스핀 대기
  while (((UART1_STATUS_REG >> UART_TXFIFO_CNT_S) & UART_TXFIFO_CNT_V) >= UART_TXFIFO_MAX_LEN) { NOP(); }
  UART1_FIFO_REG = c;
}

// 문자 배열 순차 UART1 버퍼 적재 송신 (표준 구현체 printf 대체)
void uart1_send_string(const char *msg) {
  const uint32_t len = my_strlen(msg);
  for (uint32_t i = 0; i < len; i++) { uart1_putc(msg[i]); }
}

// 32비트 포맷 정수 데이터를 문자 배열로 분해 및 순차 송신
void uart1_send_number(uint32_t num) {
  if (num == 0) {
    uart1_putc('0');
    return;
  }
  char buf[UINT32_MAX_DIGITS];
  uint32_t i = 0;

  while (num > 0) {
    buf[i++] = (num % 10) + '0';
    num /= 10;
  }

  while (i > 0) { uart1_putc(buf[--i]); }
}

// =========================================================================
// [시스템 전력 및 클럭 제어 (코어 설정)]
// =========================================================================

// 시스템 클럭 및 타이머 초기화
void system_clock_init(void) {
  // 1. 하드웨어 데드락 방지 보호 타이머(Main WDT) 무효화
  TIMG0_WDTWPROTECT_REG = TIMG_WDT_WKEY_VALUE;
  TIMG0_WDTCONFIG0_REG &= ~TIMG_WDT_EN_BIT;
  TIMG0_WDTWPROTECT_REG = 0;

  // 2. ROM 부트로더 기동 저전력 와치독(LP_WDT) 보호 무효화 (누락 시 주기적 하드웨어 재부팅 유발)
  LP_WDT_WPROTECT_REG = TIMG_WDT_WKEY_VALUE;
  LP_WDT_CONFIG0_REG &= ~TIMG_WDT_EN_BIT;
  LP_WDT_WPROTECT_REG = 0;

  // 3. 글로벌 시스템 타이머(TIMG1) 클럭 인가 및 업 카운트 모드 확정
  TIMG1_T0CONFIG_REG = TIMG_T0_INCREASE_BIT | TIMG_T0_EN_BIT;
}

// WDT 타임아웃을 강제 유발하여 하드웨어 시스템 리셋을 수행한다.
void wdt_force_reset(void) {
  TIMG0_WDTWPROTECT_REG = TIMG_WDT_WKEY_VALUE;

  // WDT Stage 0/1 타임아웃을 최소값(1)으로 설정하여 즉시 리셋을 유도한다.
  TIMG0_WDTCONFIG2_REG = 1;
  TIMG0_WDTCONFIG3_REG = 1;

  // Stage 0 액션을 시스템 리셋(3)으로 명시적 설정 후 와치독 활성화
  TIMG0_WDTCONFIG0_REG = TIMG_WDT_EN_BIT | (TIMG_WDT_STG_SYSRST << TIMG_WDT_STG0_S);
  TIMG0_WDTWPROTECT_REG = 0;

  while (1) { NOP(); }
}

// =========================================================================
// [어플리케이션 태스크 로직]
// =========================================================================

// UART1 수신 버퍼 검사 및 'R' 명령 수신 시 WDT 리셋 실행
void uart1_process_rx_and_check_reset(void) {
  // 수신 에러(프레이밍/패리티/오버플로) 발생 시 손상 데이터 폐기 및 플래그 클리어
  if (UART1_INT_RAW_REG & UART_RX_ERR_MASK) {
    if ((UART1_STATUS_REG & UART_RXFIFO_CNT_MASK) > 0) {
      (void)(UART1_FIFO_REG);  // 손상 데이터 1바이트 폐기
    }
    UART1_INT_CLR_REG = UART_RX_ERR_MASK;
    return;
  }

  // RXFIFO 데이터 존재 여부 확인
  if ((UART1_STATUS_REG & UART_RXFIFO_CNT_MASK) > 0) {
    const char c = (char)(UART1_FIFO_REG & UART_RXFIFO_DATA_MASK);

    uart1_putc(c);  // 에코(Echo) 송신 처리

    if (c == 'R' || c == 'r') {
      uart1_send_string("\r\n\r\n[UART RX] 'R' Command Accepted! System Resetting...\r\n");
      delay_ms(50);  // TX FIFO 플러시 대기
      wdt_force_reset();
    }
  }
}

// 1Hz 주기 시스템 상태 로그 출력
void uart1_print_periodic_status(uint32_t current_time) {
  static uint32_t loop_counter = 1;

  uart1_send_string("[System Loop] Pure C Polling Mode! Time: ");
  uart1_send_number(current_time);
  uart1_send_string(" ms (Loop: ");
  uart1_send_number(loop_counter++);
  uart1_send_string(")\r\n");
}

// =========================================================================
// [커스텀 베어메탈 진입점]
// =========================================================================

// C 런타임 초기화 완료 후 실행되는 애플리케이션 진입점
int main(void) {
  system_clock_init();
  uart1_init();

  uart1_send_string(
      "\r\n================================================\r\n"
      "   Pure Baremetal Environment (No ESP-IDF)\r\n"
      "   Feature: Pure C Polling (No Interrupts) \r\n"
      "   Build: " __DATE__ " " __TIME__
      "\r\n"
      "================================================\r\n");

  uint32_t last_time = get_time_ms();

  while (1) {
    uart1_process_rx_and_check_reset();

    uint32_t current_time = get_time_ms();
    if (current_time - last_time >= 1000) {
      uart1_print_periodic_status(current_time);
      last_time = current_time;
    }
  }

  return 0;
}
