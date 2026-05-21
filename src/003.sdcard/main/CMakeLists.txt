
set(SRCS main.cpp
        device/sdcard/sdcard.cpp
        device/sdcard/sdcard.hpp
)

set(REQUIRES sdmmc esp_driver_ppa)


idf_component_register(
        SRCS ${SRCS}
        INCLUDE_DIRS "."
        REQUIRES ${REQUIRES})
