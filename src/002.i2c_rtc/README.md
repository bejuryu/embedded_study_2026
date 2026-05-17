# README

## 목적

- ESP-BSP 에 기반한 RTC/I2C 드라이버 개발

## create project

```shell
idf.py create-project 002.i2c_rtc
idf.py set-target esp32p4
idf.py add-dependency espressif/m5stack_tab5
idf.py menuconfig
idf.py save-defconfig
ninja -C build -j 4
idf.py flash monitor
```

## IDF에서 I2C 사용을 위한 설정

1. `#include "driver/i2c_master.h"`
2. CMakeLists.txt에 `REQUIRES esp_driver_i2c` 나 `PRIV_REQUIRES esp_driver_i2c` 추가

## menuconfig

- C++23 의 기능 사용으로 사용 라이브러리 사이즈 증가로 인해 각 stack 사이즈 조절
- serial 입력을 위해 stdio 를 usb serial/jtag 으로 변경
- 세부 설정
    - Serial flash config
        - [x] Flash size : 16MB
    - Partition Table
        - Partition Table
            - [x] Single factory app(large), no OTA
    - Component config
        - Hardware Settings
            - Chip revision
                - [x] Select ESP32-P4 revisions <3.0 (No >= 3.x Support)
        - ESP System Settings
            - [x] Main task stack size: 32768
        - ESP-STDIO
            - [x] USB Serial/JTAG Controller
        - PThreads
            - [x] Default task stack size: 8192

## Reference

- [ESP-IDF/API Reference/Peripherals API/Inter-Integrated Circuit (I2C)](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32p4/api-reference/peripherals/i2c.html)
- [ESP-IDF/API Reference/Peripherals API/Universal Asynchronous Receiver/Transmitter (UART)](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/uart.html)
- [esp-bsp/bsp/m5stack_tab5](https://github.com/espressif/esp-bsp/tree/master/bsp/m5stack_tab5)
- [espressif/m5stack_tab5](https://components.espressif.com/components/espressif/m5stack_tab5/versions/1.2.0~1/readme) 