# Update ESP_HOSTED firmeware


## 1. slave firmware 빌드

```shell
idf.py create-project-from-example "espressif/esp_hosted==2.12.0:slave"
cd slave
idf.py set-target esp32c6
idf.py build
```

빌드 후 `build/network_adapter.bin` 파일이 생성 되었는지 확인

## 2. host_performs_slave_ota 빌드

### 2.1 OTA Menuconfig(sdkconfig.defaults)

```ini
CONFIG_IDF_TARGET="esp32p4"
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_OTA_METHOD_PARTITION=y
CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y
CONFIG_ESP_HOSTED_PRIV_SDIO_PIN_CMD_SLOT_1=13
CONFIG_ESP_HOSTED_PRIV_SDIO_PIN_CLK_SLOT_1=12
CONFIG_ESP_HOSTED_PRIV_SDIO_PIN_D0_SLOT_1=11
CONFIG_ESP_HOSTED_PRIV_SDIO_PIN_D1_4BIT_BUS_SLOT_1=10
CONFIG_ESP_HOSTED_PRIV_SDIO_PIN_D2_4BIT_BUS_SLOT_1=9
CONFIG_ESP_HOSTED_PRIV_SDIO_PIN_D3_4BIT_BUS_SLOT_1=8
CONFIG_ESP_HOSTED_SDIO_GPIO_RESET_SLAVE=15
```

### 2.2 OTA Build

```shell
idf.py create-project-from-example "espressif/esp_hosted==2.12.0:host_performs_slave_ota"
cd host_performs_slave_ota
idf.py set-target esp32p4
# slave 에서 빌드한 `build/network_adapter.bin` 파일을 `host_performs_slave_ota\components\ota_partition\slave_fw_bin` 경로에 복사
idf.py build
```

Windows 에서라면 IDF 가 설치된 드라이브에서 작업 필요