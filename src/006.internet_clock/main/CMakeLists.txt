set(SRCS main.cpp
        utility.cpp
        utility.hpp
        display.cpp
        display.hpp
        wifi.cpp
        wifi.hpp
        weather_info.cpp
        weather_info.hpp
        assets/fonts/antonio_290.c
        assets/fonts/inter_22.c)

idf_component_register(SRCS ${SRCS}
        INCLUDE_DIRS ".")

lvgl_port_create_c_image(assets/image/IMAGE_BACKGROUND.png
        assets/gen
        RGB565
        NONE)
lvgl_port_add_images(${COMPONENT_LIB} assets/gen/)
