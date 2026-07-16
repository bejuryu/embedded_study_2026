# libs/CMakeLists.txt
#
# libs/ 하위의 각 라이브러리를 등록하는 진입점.
#
# 규칙:
#   - 원본 third-party 라이브러리는 그대로 보존 (수정 금지)
#   - 크로스 컴파일 호환성이 필요한 경우 "<lib>_idf/" 래퍼 디렉터리를 생성
#   - 새 라이브러리 추가 시: add_subdirectory(<new_lib>_idf) 한 줄만 추가

# nanosvg: SVG 파싱 (헤더 전용 라이브러리)
# → 원본(nanosvg/)의 CMakeLists.txt가 데스크톱 전용이므로 래퍼 사용
add_subdirectory(nanosvg_idf)