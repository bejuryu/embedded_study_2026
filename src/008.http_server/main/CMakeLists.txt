set(SRCS main.cpp
        app_config.hpp
        app_config.cpp
        display.hpp
        display.cpp
        embedded_data.h
        embedded_data.c
        predefine.h
        utility.hpp
        utility.cpp
        wifi.hpp
        wifi.cpp
        web_server.hpp
        web_server.cpp
)

set(REQUIRES nvs_flash
        esp_netif
        esp_wifi_remote
        esp_wifi
        esp_http_server
        esp_https_server
        mdns
        esp_ringbuf
)

idf_component_register(SRCS ${SRCS}
        INCLUDE_DIRS "."
        REQUIRES ${REQUIRES})

# 빌드 시마다 파이썬 스크립트 실행하여 인증서 확인 및 생성
add_custom_target(generate_certs_target ALL
        COMMAND python ${CMAKE_CURRENT_LIST_DIR}/generate_certs.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
)

# 메인 컴파일 전에 스크립트 실행 보장
add_dependencies(${COMPONENT_LIB} generate_certs_target)
