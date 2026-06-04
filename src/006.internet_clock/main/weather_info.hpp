#pragma once

#include <string>

struct WeatherData {
  int temperature;        // 정수 온도 (반올림)
  int weather_code;       // WMO 날씨 코드
  const char* condition;  // "CLEAR", "CLOUDY", "RAIN" 등
  bool valid = false;
};

class WeatherInfo {
 public:
  // Open-Meteo API 호출 → WeatherData 반환
  // 실패 시 valid=false
  WeatherData fetch(float latitude, float longitude);

 private:
  static const char* wmo_to_text(int code);
};