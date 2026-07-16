# README

## create project

```shell
idf.py add-dependency espressif/m5stack_tab5
idf.py add-dependency espressif/esp_wifi_remote
idf.py add-dependency espressif/cjson
idf.py add-dependency espressif/libpng
idf.py add-dependency espressif/esp_media_protocols
idf.py fullclean
idf.py reconfigure
idf.py menuconfig
idf.py save-defconfig
idf.py build
idf.py flash monitor
```

## create hid

```shell
sudo pacman -S mono-msbuild nuget
nuget restore Waratah.sln
dotnet tool install --global Microsoft.HidTools.Waratah
waratahcmd --source touchpad.wara
idf.py update-dependencies 
```

## fixed thorvg build error for linux

- `managed_components/lvgl__lvgl/src/libs/thorvg/tvgInitializer.cpp`

```diff
+#include <inttypes.h>
-snprintf(sum, sizeof(sum), "%d%02d%02d", majorVal, minorVal, microVal);
+snprintf(sum, sizeof(sum), "%" PRIu32 "%02" PRIu32 "%02" PRIu32, majorVal, minorVal, microVal);
```


- https://developer.spotify.com/dashboard


## references

- [microsoft/hidtools](https://github.com/microsoft/hidtools)
- [HID Usage Table](https://www.usb.org/sites/default/files/hut1_6.pdf)
- [Precision Touchpad Devices (touchpad-devices)](https://learn.microsoft.com/en-us/windows-hardware/design/component-guidelines/touchpad-devices)
- [NimBLE-based Host APIs](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/bluetooth/nimble/index.html)
- [Sample Report Descriptors (touchpad-sample-report-descriptors)](https://learn.microsoft.com/en-us/windows-hardware/design/component-guidelines/touchpad-sample-report-descriptors)
- [Windows Precision Touchpad 컬렉션 (터치패드-윈도우즈-프리시전-터치패드-컬렉션)](https://learn.microsoft.com/ko-kr/windows-hardware/design/component-guidelines/touchpad-windows-precision-touchpad-collection)
- [JPEG Encoder and Decoder](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/jpeg.html)