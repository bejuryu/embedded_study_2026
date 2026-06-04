#!/bin/bash

# ==============================================================================
# LVGL Font Conversion Script (M5Stack TAB5 Solari Flip Clock)
#
# 이 스크립트는 assets/ 폴더에 다운로드된 static TTF 폰트들을 
# LVGL에서 사용할 수 있는 C 소스코드 형식의 폰트 파일로 변환합니다.
# npx를 통해 lv_font_conv 도구를 글로벌 설치 없이 즉시 실행합니다.
# ==============================================================================

# 에러 발생 시 즉시 중단
set -e

echo "========================================================"
echo "⏳ LVGL 폰트 리소스 변환을 시작합니다..."
echo "========================================================"

# 1. Antonio-Bold.ttf -> 시분초 플립 숫자용 폰트 (antonio_290.c)
# - 크기: 290px
# - 최적화: 시분초에는 숫자 0~9만 사용되므로, 범위(Range)를 0x30-0x39로 극단적으로 제한하여 Flash 용량을 극대화해 아낍니다.
# - bpp: 8 (최고급 256단계 안티앨리어싱 품질)
echo "1. Antonio-Bold (Size: 290px) 변환 중..."
npx -y lv_font_conv --font assets/Antonio-Bold.ttf -r 0x30-0x39 --size 290 --format lvgl -o main/assets/fonts/antonio_290.c --bpp 4

# 2. Inter-Bold.ttf -> 하단 푸터 텍스트용 폰트 (inter_22.c)
# - 크기: 22px
# - 범위: 영어 대문자/소문자, 숫자, 아스키 특수문자 전체(0x20-0x7F) 및 날씨 구분점 '·'(0xB7), 온도 기호 '°'(0xB0)를 명시적으로 포함
# - bpp: 8 (최고급 256단계 안티앨리어싱 품질)
echo "2. Inter-Bold (Size: 22px) 변환 중..."
npx -y lv_font_conv --font assets/Inter-Bold.ttf -r "0x20-0x7F,0xB0,0xB7" --size 22 --format lvgl -o main/assets/fonts/inter_22.c --bpp 4

echo "========================================================"
echo "✅ 폰트 변환이 성공적으로 완료되었습니다!"
echo "생성된 파일:"
echo " 📂 [antonio_290.c](file:///home/bejuryu/workspace/embedded_m5stack_tab5/src/006.internet_clock/main/antonio_290.c)"
echo " 📂 [inter_22.c](file:///home/bejuryu/workspace/embedded_m5stack_tab5/src/006.internet_clock/main/inter_22.c)"
echo "========================================================"
