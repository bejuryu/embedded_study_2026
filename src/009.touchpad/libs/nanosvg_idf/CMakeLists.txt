# libs/nanosvg_idf/CMakeLists.txt
#
# nanosvg의 ESP-IDF 크로스 컴파일 래퍼.
#
# nanosvg 원본(../nanosvg/)의 CMakeLists.txt는 데스크톱 전용으로,
# find_library(MATH_LIBRARY m)가 호스트 시스템의 libm을 찾아
# 크로스 컴파일 링커에 x86_64 라이브러리를 전달하는 문제가 있다.
#
# 이 래퍼는 원본 CMakeLists.txt를 완전히 우회하고,
# nanosvg_impl.c 단일 파일만 크로스 컴파일러로 빌드한다.
# 원본 nanosvg 소스(../nanosvg/)는 일체 수정하지 않는다.

add_library(nanosvg STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/nanosvg_impl.c"
)

target_include_directories(nanosvg PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../nanosvg/src"
)
