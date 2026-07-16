
set(SRCS_DISPLAY
        display/constants.hpp
        display/context.cpp
        display/context.hpp
        display/status_bar.cpp
        display/status_bar.hpp
        display/media_control.cpp
        display/media_control.hpp
        display/media_remote.cpp
        display/media_remote.hpp
        display/numpad.cpp
        display/numpad.hpp
        display/trackpad.cpp
        display/trackpad.hpp
        display/ui_icons.cpp
        display/ui_icons.hpp
)

set(SRCS_DLNA
        dlna/dlna_controller.cpp
        dlna/dlna_controller.hpp
        dlna/dlna_event_listener.cpp
        dlna/dlna_event_listener.hpp
        dlna/dlna_parser.cpp
        dlna/dlna_parser.hpp
        dlna/image_pipeline.cpp
        dlna/image_pipeline.hpp
)

set(SRCS_BLE
        ble/constants.hpp
        ble/ble_hid_device.hpp
        ble/ble_hid_device.cpp
        ble/ble_gatt_services.hpp
        ble/ble_gatt_services.cpp
        ble/ble_hid_report.hpp
        ble/ble_hid_report.cpp
)

set(SRCS main.cpp
        wifi.cpp
        wifi.hpp
        touchpad.h
        ${SRCS_DISPLAY}
        ${SRCS_BLE}
        ${SRCS_DLNA}
)


set(REQUIRES
        nvs_flash
        esp_netif
        esp_wifi_remote
        esp_wifi
        bt
        esp_hosted
        esp_lcd_touch
        esp_http_client
        libpng
        espressif__libjpeg-turbo
)

set(LIBRARIES nanosvg)

idf_component_register(SRCS ${SRCS}
        INCLUDE_DIRS "."
        PRIV_REQUIRES ${REQUIRES})

target_link_libraries(${COMPONENT_LIB} PRIVATE ${LIBRARIES})

# Create an alias target for the managed component to resolve its internal CMake name mismatch
if(TARGET __idf_espressif__libjpeg-turbo AND NOT TARGET idf::libjpeg-turbo)
    add_library(idf::libjpeg-turbo ALIAS __idf_espressif__libjpeg-turbo)
endif()