set(INCLUDE_DIRS
        "."
        "ui/generated"
        "ui/custom"
)

file(GLOB_RECURSE SRCS_UI ui/*.c)

set(SRCS main.cpp
        common/util/bcd.hpp
        hal/i2c_bus.hpp
        hal/i2c_bus.cpp
        hal/i2c_device.hpp
        device/rtc/rtc_common.hpp
        device/rtc/rx8130ce.hpp
        device/rtc/rx8130ce.cpp
)

set(REQUIRES sdmmc esp_driver_ppa)

idf_component_register(SRCS ${SRCS} ${SRCS_UI}
        INCLUDE_DIRS ${INCLUDE_DIRS})
