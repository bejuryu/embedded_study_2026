# 005. WiFi Scan & NTP

## Hardware

- M5Stack TAB5 (ESP32p4)

## 목적

- WiFi scan
- SD-CARD 로 접속할 WiFi 정보를 NVS에 저장
- ntp 기능을 이용해서 시스템 시간 설정

## create project

```shell
idf.py create-project 005.wifi_scan_ntp
idf.py set-target esp32p4
idf.py add-dependency espressif/m5stack_tab5
idf.py add-dependency espressif/esp_wifi_remote
idf.py add-dependency espressif/cjson
idf.py menuconfig
idf.py save-defconfig
ninja -C build -j 4
idf.py flash monitor
```

## reference

- [API Reference/Storage API/Non-Volatile Storage Library](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32p4/api-reference/storage/nvs_flash.html)
- [SNTP Time Synchronization](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32p4/api-reference/system/system_time.html#sntp-time-synchronization)
- [github:espressif/esp-hosted-mcu](https://github.com/espressif/esp-hosted-mcu)
- [espregistry:espressif/esp_wifi_remote](https://components.espressif.com/components/espressif/esp_wifi_remote/versions/1.5.3/readme)
- [github:espressif/esp-wifi-remote](https://github.com/espressif/esp-wifi-remote)
- [espregistry:espressif/m5stack_tab5](https://components.espressif.com/components/espressif/m5stack_tab5)
- [espressif/cjson](https://components.espressif.com/components/espressif/cjson/versions/1.7.19~2/readme)