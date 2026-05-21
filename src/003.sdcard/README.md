# 003. SDCARD

## 목적

1. SD-CARD를 인식하여 콘솔에 정보 출력
2. SD-CARD의 이미지 파일을 30초 간격으로 전환

## create project

```shell
idf.py create-project 003.sdcard
idf.py set-target esp32p4
idf.py add-dependency espressif/m5stack_tab5
idf.py menuconfig
idf.py save-defconfig
ninja -C build -j 4
idf.py flash monitor
```

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
        - ESP PSRAM
            - [x] Support for external PSRAM
        - FAT Filesystem support
            - API character encoding
                - [x] API uses UTF-8 encoding
        - ESP LVGL PORT
            - [x] Enable PPA for screen rotation
        - LVGL configuration
            - Feature Configuration
                - Logging
                    - [x] Enable the log module
                    - [x] Print the log with printf
                - Rendering Configuration
                    - [x] Buffer address alignment: 64
                - Compiler Settings
                    - [x] Required alignment size for buffers: 64

## RGB565 convert

- https://lvgl.github.io/lv_img_conv/

## Reference

- [driver/ppa.h: No such file or directory](https://github.com/lvgl/lvgl/issues/9137)
- [ESP-IDF/API Reference Peripherals API/SDMMC Host Driver](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32p4/api-reference/peripherals/sdmmc_host.html)
- [esp-bsp/bsp/m5stack_tab5](https://github.com/espressif/esp-bsp/tree/master/bsp/m5stack_tab5)
- [espressif/m5stack_tab5](https://components.espressif.com/components/espressif/m5stack_tab5/versions/1.2.0~1/readme) 