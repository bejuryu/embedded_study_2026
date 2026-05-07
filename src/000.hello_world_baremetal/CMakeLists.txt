cmake_minimum_required(VERSION 3.16)

# CMake에게 우리가 크로스 컴파일(Cross-compile)을 진행함을 알림
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv32)
set(CMAKE_C_COMPILER riscv32-esp-elf-gcc)
set(CMAKE_ASM_COMPILER riscv32-esp-elf-gcc)
# libc 등의 표준 라이브러리가 없기 때문에 툴체인 테스트 단계를 강제로 통과시킴
set(CMAKE_C_COMPILER_WORKS 1)

project(hello-world-baremetal C ASM)

# 모던 C17 규격 적용 (컴파일 표준 결함 회피 및 파싱 안정성 확보)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# IDF_PATH는 더 이상 필요하지 않으므로 환경 변수 체크 및 경로 설정을 제거했습니다.

# 베어메탈용 컴파일 플래그
set(COMMON_FLAGS "-O2 -g -Wall -march=rv32imac -mabi=ilp32 -mcmodel=medany -ffunction-sections -fdata-sections -nostdlib -fno-builtin")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMMON_FLAGS} -D__ets__")
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} ${COMMON_FLAGS}")

# 링커 스크립트 연결
set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/esp32p4.ld)

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -T ${LINKER_SCRIPT} -nostdlib -Wl,--gc-sections -Wl,-Map=hello-baremetal.map")

add_executable(${PROJECT_NAME}.elf main.c startup.S)

# 이제 레지스터 주소 매크로가 로컬 헤더에 있으므로,
# ESP-IDF의 경로를 포함(target_include_directories)할 필요가 전혀 없습니다!

# 빌드 후 .elf 파일을 ROM 부트로더 호환 .bin 파일로 변환 (esptool)
add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND esptool --chip esp32p4 elf2image --flash-mode dio --flash-freq 80m --flash-size 16MB -o ${PROJECT_NAME}.bin $<TARGET_FILE:${PROJECT_NAME}.elf>
    BYPRODUCTS ${PROJECT_NAME}.bin
    COMMENT "Generating bootable binary image (${PROJECT_NAME}.bin)"
)

# 플래시를 위한 사용자 정의 타겟 생성 (make flash 역할)
# [주의] ESP32-P4의 1차 ROM 부트로더는 0x0 ~ 0x1FFF 구역(8KB)을 보안 키 및 플래시 암호화 용도로 예약합니다.
# 2차 부트로더나 단독 베어메탈 애플리케이션 바이너리는 반드시 0x2000 번지부터 플래싱해야 정상 부팅됩니다.
add_custom_target(flash
    COMMAND esptool --chip esp32p4 -p /dev/ttyACM0 -b 460800 --before=default-reset --after=hard-reset write_flash --flash-mode dio --flash-freq 80m --flash-size 16MB 0x2000 ${PROJECT_NAME}.bin
    DEPENDS ${PROJECT_NAME}.elf
    COMMENT "Flashing binary to ESP32-P4 (Offset 0x2000)..."
)
