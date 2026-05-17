#pragma once

#include <concepts>
#include <cstdint>

namespace common::util {

/**
Magic 6 BCD 최적화 기법 요약

1. 핵심 원리 (Weight Delta)

10진수와 16진수 간의 자릿수 가중치(Positional Weight) 차이를 대수학적으로 상쇄하는 기법.
십의 자리(상위 4비트)의 가중치는 16진수 공간에서 16, 10진수 공간에서 10이며, 그 차이(Delta)는 6.
2. BCD → 10진수 변환 (bcd2dec) : 잉여 가중치 감산

현재 평가값: (상위 4비트 * 16) + 일의 자리
도출 목표값: (상위 4비트 * 10) + 일의 자리
해결 연산: 현재 값에서 가중치인 (상위 4비트 * 6)을 감산.
코드: bcd - ((bcd >> 4) * 6)
3. 10진수 → BCD 변환 (dec2bcd) : 부족 가중치 보상

현재 평가값: (몫 * 10) + 일의 자리
도출 목표값: 몫(십의 자리)을 상위 4비트로 올리기 위해 가중치를 16으로 격상 필요.
해결 연산: 목표 가중치 도달을 위해 부족분인 (몫 * 6)을 기존 값에 가산.
코드: dec + ((dec / 10) * 6)
4. 기술적 이점 (Performance)

비용이 높은 나머지 연산(% 10) 및 불필요한 비트 마스킹(& 0x0F)을 완전히 제거.
곱하기 6(* 6) 연산은 현대 컴파일러 최적화 단계에서 단일 사이클의 시프트-가산 기계어((x << 2) + (x << 1))로 자동 치환.

dec2bcd: static_cast<uint8_t>(dec + ((dec / 10) * 6));
bcd2dec: static_cast<uint8_t>(((bcd >> 4) * 10) + (bcd & 0x0F));
*/

constexpr uint8_t dec2bcd(std::integral auto dec) noexcept {
  return static_cast<uint8_t>(dec + ((dec / 10) * 6));
}

constexpr uint8_t bcd2dec(std::integral auto bcd) noexcept {
  return static_cast<uint8_t>(bcd - ((bcd >> 4) * 6));
}

}  // namespace common::util