set(SRCS main.cpp
        common/util/bcd.hpp
        hal/i2c_bus.hpp
        hal/i2c_bus.cpp
        hal/i2c_device.hpp
        device/rtc/rtc_common.hpp
        device/rtc/rx8130ce.hpp
        device/rtc/rx8130ce.cpp
)

set(REQUIRES esp_driver_i2c)

idf_component_register(
        SRCS ${SRCS}
        INCLUDE_DIRS "."
        REQUIRES ${REQUIRES}
)
