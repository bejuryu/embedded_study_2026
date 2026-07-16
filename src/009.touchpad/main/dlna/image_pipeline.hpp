#pragma once
#include <cstdint>
#include <string>

namespace Ble::Dlna {

class ImagePipeline {
 public:
  // HTTP로 이미지를 PSRAM에 다운로드받고, 용량/해상도 필터를 거쳐
  // RGB565 버퍼를 PSRAM에 할당하여 반환합니다.
  static bool download_and_decode(const std::string& url, uint16_t** out_rgb565, uint32_t& out_w, uint32_t& out_h);

  static bool parse_jpeg_header(const uint8_t* data, size_t size, uint32_t& w, uint32_t& h);
  static bool parse_png_header(const uint8_t* data, size_t size, uint32_t& w, uint32_t& h);

 private:
  static bool decode_jpeg(const uint8_t* compressed_data, size_t size, uint16_t* out_rgb565, uint32_t w, uint32_t h);
  static bool decode_png(const uint8_t* compressed_data, size_t size, uint16_t* out_rgb565);
};

}  // namespace Ble::Dlna
