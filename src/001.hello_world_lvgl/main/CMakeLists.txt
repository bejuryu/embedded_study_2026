set(SOURCES main.cpp display.cpp assets/GmarketSans_36.c)

idf_component_register(SRCS ${SOURCES}
                    INCLUDE_DIRS ".")
lvgl_port_create_c_image("assets/IMAGE_BACKGROUND.png" "assets/gen/" "ARGB8888" "NONE")
lvgl_port_add_images(${COMPONENT_LIB} "assets/gen/")
