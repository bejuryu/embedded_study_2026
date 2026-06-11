# README

## shell

```shell
idf.py set-target esp32p4
idf.py add-dependency espressif/m5stack_tab5
idf.py add-dependency espressif/esp_wifi_remote
idf.py add-dependency espressif/cjson
idf.py add-dependency espressif/mdns
idf.py menuconfig
idf.py save-defconfig
idf.py build
idf.py flash monitor

```

## References

- [HTTP Server](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32p4/api-reference/protocols/esp_http_server.html)
- [HTTPS Server](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32p4/api-reference/protocols/esp_https_server.html)
- [Ring Buffers](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32p4/api-reference/system/freertos_additions.html)