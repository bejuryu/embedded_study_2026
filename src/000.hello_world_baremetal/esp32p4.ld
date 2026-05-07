/* esp32p4.ld - ESP32-P4 베어메탈 링커 스크립트 */
/* 참고: ESP-IDF의 components/esp_system/ld/esp32p4/memory.ld.in 기반 메모리 맵 */

/* -------------------------------------------------------------------------- */
/* ESP32-P4 메모리 맵 설정 (참고: esp32-p4_datasheet_en.pdf 4.1.3.3 Cache)    */
/* -------------------------------------------------------------------------- */
MEMORY
{
  /* 메인 L2 SRAM: 총 768KB 중 최대 512KB가 L2 Cache로 전용될 수 있으므로,      */
  /* 768KB - 512KB = 256KB를 베어메탈 펌웨어 및 데이터 전용 구역으로 할당한다.    */
  sram (RWX)    : ORIGIN = 0x4FF00000, LENGTH = 256K

  /* LP 코어 전용 공간 (HP 코어에서 접근 시 지연(Latency) 발생)                  */
  lp_sram (RWX) : ORIGIN = 0x50108000, LENGTH = 32K
  lp_rom (RX)   : ORIGIN = 0x50100000, LENGTH = 16K
}

ENTRY(_start)

/* ==================================================================== */
/* 프로그램 헤더(Program Headers) 명시적 정의                           */
/* ELF 파일의 LOAD 세그먼트를 분리하여 RWX(읽기/쓰기/실행 동시 부여)    */
/* 권한 보안 경고를 방지합니다.                                         */
/* ==================================================================== */
PHDRS
{
  seg_rx PT_LOAD FLAGS(5); /* 5 = Read(4) | Execute(1) -> 실행 코드 및 읽기 전용 데이터용 */
  seg_rw PT_LOAD FLAGS(6); /* 6 = Read(4) | Write(2)   -> 읽기/쓰기 가능 데이터용 */
}

SECTIONS
{
  /* 1. .text: 실행 코드(기계어 명령어) 섹션 */
  .text :
  {
    . = ALIGN(4);
    /* 스타트업 코드를 SRAM 시작점(0x4FF00000)에 배치하여 진입점을 보장한다. */
    KEEP(*(.text.init))
    *(.text .text.*)
    . = ALIGN(4);
  } > sram :seg_rx

  /* 2. .rodata: 읽기 전용 데이터(문자열 리터럴, const 상수) 섹션 */
  .rodata :
  {
    . = ALIGN(4);
    *(.rodata .rodata.*)
    *(.srodata .srodata.*)
    . = ALIGN(4);
  } > sram :seg_rx

  /* 3. .data: 초기값이 지정된 전역/정적 변수 섹션 */
  .data :
  {
    . = ALIGN(4);
    __data_start = .;
    *(.data .data.*)
    *(.sdata .sdata.*)
    . = ALIGN(4);
    __data_end = .;
  } > sram :seg_rw

  /* 4. .bss: 비초기화 전역 변수 섹션 (바이너리 미포함, startup.S에서 0으로 초기화) */
  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    __bss_start = .;
    *(.bss .bss.*)
    *(.sbss .sbss.*)
    *(COMMON)
    . = ALIGN(4);
    __bss_end = .;
  } > sram :seg_rw

  /* 스택 포인터 초기값: SRAM 최상단 주소 */
  __stack_top = ORIGIN(sram) + LENGTH(sram);
}
