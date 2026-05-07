#ifndef SOC_ESP32P4_BAREMETAL_H
#define SOC_ESP32P4_BAREMETAL_H

#include <stdint.h>

// ================== ESP32-P4 하드웨어 레지스터 맵 =======================
// 제조사 TRM 문서(esp32-p4_technical_reference_manual_en.pdf)에서 추출한 물리 주소
// 참조 규칙: [7장 모듈 Base Address] + [각 챕터별 단위 Register Offset]

// UART1 통신용 레지스터 (TRM Base: Page 483)
// - UART_FIFO_REG (Offset 0x0000) 참조: TRM Page 2113
#define UART1_FIFO_REG (*(volatile uint32_t *)0x500CB000)
// - UART_INT_RAW_REG (Offset 0x0004) 참조: TRM Page 2113
#define UART1_INT_RAW_REG (*(volatile uint32_t *)0x500CB004)
// - UART_CLKDIV_REG (Offset 0x0014) 참조: TRM Page 2117
#define UART1_CLKDIV_REG (*(volatile uint32_t *)0x500CB014)
// - UART_STATUS_REG (Offset 0x001C) 참조: TRM Page 2127
#define UART1_STATUS_REG (*(volatile uint32_t *)0x500CB01C)

// UART 제어용 상수 (FIFO 큐 제어)
// - TX_FIFO_CNT 비트 위치 및 마스크 참조: TRM Page 2127 (UART_STATUS_REG 비트 필드 설명)
#define UART_TXFIFO_CNT_S 16     // 상태 레지스터 내 TX_FIFO_CNT 의 시작 비트 (Shift)
#define UART_TXFIFO_CNT_V 0xFF   // TX_FIFO_CNT 데이터 크기 마스크 (8비트)
#define UART_TXFIFO_MAX_LEN 127  // 하드웨어 송신 버퍼의 최대 한계점 (128바이트)

// UART 상태 파악 및 수신 데이터 추출용 비트마스크
#define UART_RXFIFO_CNT_MASK 0x3FF  // UART1_STATUS_REG 하위 10비트 (수신된 바이트 수량 필드)
#define UART_RXFIFO_DATA_MASK 0xFF  // UART1_FIFO_REG 하위 8비트 (수신된 실제 1바이트 문자 데이터)

// UART 에러 감지용 인터럽트 Raw 비트 마스크
#define UART_PARITY_ERR_INT_BIT (1 << 2)  // 패리티 에러 발생 플래그
#define UART_FRM_ERR_INT_BIT (1 << 3)     // 프레이밍 에러 발생 플래그
#define UART_RXFIFO_OVF_INT_BIT (1 << 4)  // RX FIFO 오버플로 발생 플래그
#define UART_RX_ERR_MASK (UART_PARITY_ERR_INT_BIT | UART_FRM_ERR_INT_BIT | UART_RXFIFO_OVF_INT_BIT)

// UART 보드레이트 분주 설정값 (CLKDIV_REG 비트 필드)
#define UART_CLKDIV_FRAG_S 20  // 분수(Fractional) 분주기 비트 시프트 위치

// UART 보드레이트 산출 상수 (40MHz XTAL 기준, 115200 baud)
#define UART_CLK_FREQ 40000000
#define UART_BAUD_RATE 115200
#define UART_CLKDIV_INT (UART_CLK_FREQ / UART_BAUD_RATE)                             // 정수부: 347
#define UART_CLKDIV_FRAG (((UART_CLK_FREQ % UART_BAUD_RATE) * 16) / UART_BAUD_RATE)  // 소수부: 4

// GPIO 핀 및 입출력 매트릭스(IOMUX) 설정 레지스터 (TRM Base: Page 484)
// - GPIO_ENABLE_W1TS_REG 참조: TRM Page 545
#define GPIO_ENABLE_W1TS_REG (*(volatile uint32_t *)0x500E0024)
// - IO_MUX_GPIOxx_REG (Offset 0x0048, 0x004C) 참조: TRM Page 574
#define IO_MUX_GPIO17_REG (*(volatile uint32_t *)0x500E1048)
#define IO_MUX_GPIO18_REG (*(volatile uint32_t *)0x500E104C)
// - GPIO_FUNCn_OUT/IN_SEL (동적 매트릭스 라우팅 레지스터) 참조: TRM Page 577
#define GPIO_FUNC17_OUT_SEL_CFG_REG (*(volatile uint32_t *)0x500E059C)
#define GPIO_FUNC13_IN_SEL_CFG_REG (*(volatile uint32_t *)0x500E018C)

// =========================================================================
// [어플리케이션 직관성 향상을 위한 별칭(Alias) 정의]
// M5Stack TAB5 등 외부 장치 연결용으로 할당된 17(TX), 18(RX) 핀을 UART1용으로 매핑
// =========================================================================
#define UART1_TX_PIN_NUM 17
#define UART1_RX_PIN_NUM 18

#define UART1_TX_PIN_IO_MUX IO_MUX_GPIO17_REG
#define UART1_RX_PIN_IO_MUX IO_MUX_GPIO18_REG
#define UART1_TX_PIN_OUT_SEL GPIO_FUNC17_OUT_SEL_CFG_REG
#define UART1_RX_SIG_IN_SEL GPIO_FUNC13_IN_SEL_CFG_REG  // UART1_RXD 입력 신호의 GPIO 소스 선택 (IN 신호 인덱스 13)

// 내부 신호 인덱스 (UART1 맵핑용) 및 제어 비트 마스크
#define UART1_TXD_OUT_IDX 13
#define IO_MUX_MCU_SEL_M (0xF << 12)
#define IO_MUX_MCU_SEL_GPIO (1 << 12)  // 'Function 1' = MCU_SEL (TRM Page 536)
#define IO_MUX_FUN_IE (1 << 9)         // 입력 활성화(Input Enable) 비트
#define GPIO_SIG_IN_SEL_BIT (1 << 7)   // GPIO 매트릭스 입력 라우팅 활성화 비트

// 타이머 그룹 0 (TIMG0) 와치독 레지스터 (TRM Base: Page 483)
// - TIMG_WDTCONFIG0_REG (Offset 0x0048) 참조: TRM Page 1094
#define TIMG0_WDTCONFIG0_REG (*(volatile uint32_t *)0x500C2048)
// - TIMG_WDTCONFIG2/3_REG (Offset 0x0050, 0x0054): 와치독 경고 임계값 설정 (Stage 0, 1 Timeout)
#define TIMG0_WDTCONFIG2_REG (*(volatile uint32_t *)0x500C2050)
#define TIMG0_WDTCONFIG3_REG (*(volatile uint32_t *)0x500C2054)
// - TIMG_WDTWPROTECT_REG (Offset 0x0064) 참조: TRM Page 1094
#define TIMG0_WDTWPROTECT_REG (*(volatile uint32_t *)0x500C2064)
#define TIMG_WDT_WKEY_VALUE 0x50D83AA1  // 와치독 쓰기 보호 해제 키 (TRM Page 1105)
#define TIMG_WDT_EN_BIT (1 << 31)       // 와치독 활성화 비트 (31번 비트)
#define TIMG_WDT_STG0_S 29              // 와치독 Stage 0 액션 비트 시프트 위치
#define TIMG_WDT_STG_OFF 0              // 스테이지 액션: 비활성
#define TIMG_WDT_STG_INT 1              // 스테이지 액션: 인터럽트 발생
#define TIMG_WDT_STG_CPURST 2           // 스테이지 액션: CPU 리셋
#define TIMG_WDT_STG_SYSRST 3           // 스테이지 액션: 시스템 리셋

// =========================================================================
// [저전력(LP) 타이머 와치독 (RTC_WDT) 레지스터]
// ROM 부트로더가 활성화한 LP WDT를 비활성화하지 않으면 약 9초 주기로 시스템이 재부팅됨
// =========================================================================
#define LP_WDT_BASE 0x50116000
#define LP_WDT_CONFIG0_REG (*(volatile uint32_t *)(LP_WDT_BASE + 0x0000))
#define LP_WDT_WPROTECT_REG (*(volatile uint32_t *)(LP_WDT_BASE + 0x0018))

// =========================================================================
// [6. 인터럽트 매트릭스 (INTC) 레지스터]  [RESERVED: 인터럽트 구현 시 활성화]
// 주변기기 인터럽트 신호를 CPU 코어 인터럽트 라인에 매핑하는 라우팅 레지스터
// =========================================================================
#define INTC_BASE 0x500D6000
#define CORE0_UART1_INT_MAP_REG (*(volatile uint32_t *)(INTC_BASE + 0x0080))
#define CORE0_TIMERGRP1_T0_INT_MAP_REG (*(volatile uint32_t *)(INTC_BASE + 0x00C4))

// =========================================================================
// [7. UART1 인터럽트 레지스터]  [RESERVED: 인터럽트 구현 시 활성화]
// =========================================================================
#define UART1_INT_ENA_REG (*(volatile uint32_t *)(0x500CB000 + 0x000C))
#define UART1_INT_CLR_REG (*(volatile uint32_t *)(0x500CB000 + 0x0010))
#define UART_RXFIFO_FULL_INT_ENA_BIT (1 << 0)
#define UART_RXFIFO_FULL_INT_CLR_BIT (1 << 0)

// =========================================================================
// [8. 타이머 그룹 1 (TIMG1) 레지스터] - 카운터/폴링(백그라운드 Tick)용
//    [RESERVED: ALARM/LOAD/INT 레지스터는 인터럽트 구현 시 활성화]
// =========================================================================
#define TIMG1_BASE 0x500C3000
#define TIMG1_T0CONFIG_REG (*(volatile uint32_t *)(TIMG1_BASE + 0x0000))
#define TIMG1_T0LO_REG (*(volatile uint32_t *)(TIMG1_BASE + 0x0004))
#define TIMG1_T0HI_REG (*(volatile uint32_t *)(TIMG1_BASE + 0x0008))
#define TIMG1_T0UPDATE_REG (*(volatile uint32_t *)(TIMG1_BASE + 0x000C))
#define TIMG1_T0ALARMLO_REG (*(volatile uint32_t *)(TIMG1_BASE + 0x0010))
#define TIMG1_T0ALARMHI_REG (*(volatile uint32_t *)(TIMG1_BASE + 0x0014))
#define TIMG1_T0LOADLO_REG (*(volatile uint32_t *)(TIMG1_BASE + 0x0018))
#define TIMG1_T0LOADHI_REG (*(volatile uint32_t *)(TIMG1_BASE + 0x001C))
#define TIMG1_T0LOAD_REG (*(volatile uint32_t *)(TIMG1_BASE + 0x0020))
#define TIMG1_INT_ENA_TIMERS_REG (*(volatile uint32_t *)(TIMG1_BASE + 0x0070))
#define TIMG1_INT_CLR_TIMERS_REG (*(volatile uint32_t *)(TIMG1_BASE + 0x007C))
#define TIMG_T0_INT_ENA_BIT (1 << 0)
#define TIMG_T0_INT_CLR_BIT (1 << 0)

// 타이머 레지스터 CONFIG 비트 필드 플래그 매크로
#define TIMG_T0_INCREASE_BIT (1 << 30)  // 타이머 카운터 증가(Up) 방향 설정
#define TIMG_T0_EN_BIT (1 << 31)        // 타이머 카운터 자체 활성화(Enable)
#define TIMG_CLK_TICKS_PER_MS 40000     // 40MHz 스탠다드 클럭 기준 1ms당 틱 수

// =========================================================================
// [9. NOP 매크로]
// 스핀 루프에서 컴파일러 최적화에 의한 루프 제거를 방지하고, 파이프라인/버스 점유율을 완화한다.
// =========================================================================
#define NOP() asm volatile("nop")

#endif  // SOC_ESP32P4_BAREMETAL_H
