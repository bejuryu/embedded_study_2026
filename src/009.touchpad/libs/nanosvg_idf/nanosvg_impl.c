/*
 * nanosvg_impl.c
 *
 * nanosvg는 헤더 전용(header-only) 라이브러리입니다.
 * NANOSVG_IMPLEMENTATION 매크로를 정의한 상태로 헤더를 한 번 컴파일하면
 * 구현 코드가 활성화됩니다. 이 파일이 그 단일 컴파일 유닛(translation unit)입니다.
 *
 * ESP-IDF의 newlib가 sinf, cosf, sqrtf 등 모든 수학 함수를 제공하므로
 * 별도의 -lm 링크는 필요하지 않습니다.
 */

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
