#include "image_pipeline.hpp"

#include <cstring>

#include "dlna_constants.hpp"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "lvgl.h"

// 외부 라이브러리 디코더 헤더
#include <setjmp.h>

#include "jpeglib.h"
#include "png.h"

namespace Ble::Dlna {

static const char* TAG = "ImagePipeline";

// PNG 메모리 읽기용 구조체
struct PngBufferReader {
  const uint8_t* buffer;
  size_t         offset;
  size_t         size;
};

bool ImagePipeline::parse_jpeg_header(const uint8_t* data, size_t size, uint32_t& w, uint32_t& h) {
  if (size < 4) return false;
  if (data[0] != 0xFF || data[1] != 0xD8) return false;  // SOI 체크

  size_t offset = 2;
  while (offset < size - 8) {
    if (data[offset] != 0xFF) return false;
    uint8_t marker = data[offset + 1];
    if (marker == 0xD9 || marker == 0xDA) break;  // SOS or EOI

    uint16_t length = (data[offset + 2] << 8) | data[offset + 3];
    if (marker == 0xC0 || marker == 0xC2) {  // SOF0 or SOF2
      h = (data[offset + 5] << 8) | data[offset + 6];
      w = (data[offset + 7] << 8) | data[offset + 8];
      return true;
    }
    offset += 2 + length;
  }
  return false;
}

bool ImagePipeline::parse_png_header(const uint8_t* data, size_t size, uint32_t& w, uint32_t& h) {
  if (size < 24) return false;
  // PNG signature check
  if (data[0] != 0x89 || data[1] != 0x50 || data[2] != 0x4E || data[3] != 0x47) return false;

  // IHDR chunk search
  if (data[12] == 'I' && data[13] == 'H' && data[14] == 'D' && data[15] == 'R') {
    w = (data[16] << 24) | (data[17] << 16) | (data[18] << 8) | data[19];
    h = (data[20] << 24) | (data[21] << 16) | (data[22] << 8) | data[23];
    return true;
  }
  return false;
}

namespace {
struct my_error_mgr {
  struct jpeg_error_mgr pub;
  jmp_buf               setjmp_buffer;
};

void my_error_exit(j_common_ptr cinfo) {
  my_error_mgr* myerr = (my_error_mgr*)cinfo->err;
  (*cinfo->err->output_message)(cinfo);
  longjmp(myerr->setjmp_buffer, 1);
}

static const uint8_t bayer_8x8[8][8] = {{0, 48, 12, 60, 3, 51, 15, 63}, {32, 16, 44, 28, 35, 19, 47, 31}, {8, 56, 4, 52, 11, 59, 7, 55}, {40, 24, 36, 20, 43, 27, 39, 23},
                                        {2, 50, 14, 62, 1, 49, 13, 61}, {34, 18, 46, 30, 33, 17, 45, 29}, {10, 58, 6, 54, 9, 57, 5, 53}, {42, 26, 38, 22, 41, 25, 37, 21}};

void apply_dithered_dimming(uint16_t* rgb565, uint32_t w, uint32_t h, uint32_t stride_bytes) {
  uint32_t start_y = h / 4;  // 이미지 상단 25% 영역부터 어두워짐 시작
  uint32_t fade_h  = h - start_y;
  if (fade_h <= 0) return;

  uint8_t*        dst        = reinterpret_cast<uint8_t*>(rgb565);
  constexpr float max_darken = 0.8f;  // 최대 80% 어둡게 함 (20% 밝기 보존)

  for (uint32_t y = start_y; y < h; y++) {
    float ratio  = (y - start_y) / (float)(fade_h - 1);
    float factor = 1.0f - (ratio * max_darken);

    uint16_t* dst_row = reinterpret_cast<uint16_t*>(dst + y * stride_bytes);
    for (uint32_t x = 0; x < w; x++) {
      uint16_t pixel = dst_row[x];
      // RGB565 분해
      uint8_t  r     = ((pixel >> 11) & 0x1F) << 3;
      uint8_t  g     = ((pixel >> 5) & 0x3F) << 2;
      uint8_t  b     = (pixel & 0x1F) << 3;

      // 어둡게 조절
      float r_dark = r * factor;
      float g_dark = g * factor;
      float b_dark = b * factor;

      // 16비트 다운샘플링 계단선 제거용 베이어 매트릭스 디더 노이즈
      float dither = (bayer_8x8[y % 8][x % 8] / 63.0f - 0.5f) * 8.0f;

      int r_final = static_cast<int>(r_dark + dither);
      int g_final = static_cast<int>(g_dark + dither / 2.0f);
      int b_final = static_cast<int>(b_dark + dither);

      if (r_final < 0)
        r_final = 0;
      else if (r_final > 255)
        r_final = 255;
      if (g_final < 0)
        g_final = 0;
      else if (g_final > 255)
        g_final = 255;
      if (b_final < 0)
        b_final = 0;
      else if (b_final > 255)
        b_final = 255;

      dst_row[x] = ((r_final & 0xF8) << 8) | ((g_final & 0xFC) << 3) | (b_final >> 3);
    }
  }
}
}  // namespace

bool ImagePipeline::decode_jpeg(const uint8_t* compressed_data, size_t size, uint16_t* out_rgb565, uint32_t w, uint32_t h) {
  struct jpeg_decompress_struct cinfo;
  struct my_error_mgr           jerr;

  cinfo.err           = jpeg_std_error(&jerr.pub);
  jerr.pub.error_exit = my_error_exit;

  if (setjmp(jerr.setjmp_buffer)) {
    jpeg_destroy_decompress(&cinfo);
    ESP_LOGE(TAG, "libjpeg-turbo decompress error handled via longjmp");
    return false;
  }

  jpeg_create_decompress(&cinfo);
  jpeg_mem_src(&cinfo, compressed_data, size);

  if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
    jpeg_destroy_decompress(&cinfo);
    ESP_LOGE(TAG, "Failed to read JPEG header");
    return false;
  }

  // libjpeg-turbo 네이티브 RGB565 출력 포맷 지정 (빠른 고성능 디코딩)
  cinfo.out_color_space = JCS_RGB565;
  cinfo.dither_mode     = JDITHER_NONE;  // 모아레 간섭 줄무늬 방지를 위해 디더링 비활성화

  jpeg_start_decompress(&cinfo);

  uint32_t out_w = cinfo.output_width;
  uint32_t out_h = cinfo.output_height;
  if (out_w != w || out_h != h) {
    ESP_LOGW(TAG, "Dimension mismatch - header: %lux%lu, target: %lux%lu", (unsigned long)out_w, (unsigned long)out_h, (unsigned long)w, (unsigned long)h);
  }

  int        row_stride = cinfo.output_width * 2;
  JSAMPARRAY buffer     = (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo, JPOOL_IMAGE, row_stride, 1);

  // LVGL 공식 stride 계산식에 정렬된 stride 획득 (VG_Lite 가속 정렬 대응)
  uint32_t aligned_stride = lv_draw_buf_width_to_stride(w, LV_COLOR_FORMAT_RGB565);
  uint8_t* dst            = reinterpret_cast<uint8_t*>(out_rgb565);
  int      row            = 0;
  while (cinfo.output_scanline < cinfo.output_height) {
    jpeg_read_scanlines(&cinfo, buffer, 1);
    memcpy(dst + row * aligned_stride, buffer[0], row_stride);
    row++;
  }

  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);

  // 하단 50% 영역에 디더링 기반 어둡기 처리 직접 베이킹
  apply_dithered_dimming(out_rgb565, w, h, aligned_stride);

  // CPU 캐시를 PSRAM으로 Flush (동기화)하여 GPU/DMA 가 최신 화소 정보를 읽을 수 있도록 보장
  size_t    data_bytes = aligned_stride * h;
  esp_err_t cache_err  = esp_cache_msync(out_rgb565, data_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  if (cache_err == ESP_OK) {
    ESP_LOGI(TAG, "Cache sync success: flushed %d bytes at address %p to memory.", (int)data_bytes, out_rgb565);
  } else {
    ESP_LOGE(TAG, "Cache sync FAILED: esp_err = 0x%x", cache_err);
  }

  return true;
}

bool ImagePipeline::decode_png(const uint8_t* compressed_data, size_t size, uint16_t* out_rgb565) {
  png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (!png_ptr) return false;

  png_infop info_ptr = png_create_info_struct(png_ptr);
  if (!info_ptr) {
    png_destroy_read_struct(&png_ptr, nullptr, nullptr);
    return false;
  }

  // libpng internal error 핸들링용 setjmp (4.2절 사양)
  if (setjmp(png_jmpbuf(png_ptr))) {
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    ESP_LOGE(TAG, "libpng runtime error handled via longjmp");
    return false;
  }

  PngBufferReader reader{compressed_data, 0, size};
  png_set_read_fn(png_ptr, &reader, [](png_structp png_ptr, png_bytep data, png_size_t length) {
    auto* rd = static_cast<PngBufferReader*>(png_get_io_ptr(png_ptr));
    if (rd->offset + length > rd->size) {
      png_error(png_ptr, "Read overflow");
    }
    memcpy(data, rd->buffer + rd->offset, length);
    rd->offset += length;
  });

  png_read_info(png_ptr, info_ptr);

  png_uint_32 width, height;
  int         bit_depth, color_type;
  png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, nullptr, nullptr, nullptr);

  // RGB888 로 강제 변환
  if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png_ptr);
  if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png_ptr);
  if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png_ptr);
  if (bit_depth == 16) png_set_strip_16(png_ptr);
  if (color_type & PNG_COLOR_MASK_ALPHA) png_set_strip_alpha(png_ptr);

  png_read_update_info(png_ptr, info_ptr);

  size_t     row_bytes    = png_get_rowbytes(png_ptr, info_ptr);
  png_bytep* row_pointers = (png_bytep*)heap_caps_malloc(sizeof(png_bytep) * height, MALLOC_CAP_SPIRAM);
  if (!row_pointers) {
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    return false;
  }

  for (png_uint_32 y = 0; y < height; y++) {
    row_pointers[y] = (png_byte*)heap_caps_malloc(row_bytes, MALLOC_CAP_SPIRAM);
    if (!row_pointers[y]) {
      for (png_uint_32 i = 0; i < y; i++) heap_caps_free(row_pointers[i]);
      heap_caps_free(row_pointers);
      png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
      return false;
    }
  }

  png_read_image(png_ptr, row_pointers);

  // LVGL 공식 stride 계산식에 정렬된 stride 획득 (VG_Lite 가속 정렬 대응)
  uint32_t aligned_stride = lv_draw_buf_width_to_stride(width, LV_COLOR_FORMAT_RGB565);
  uint8_t* dst            = reinterpret_cast<uint8_t*>(out_rgb565);

  // RGB565 변환
  for (png_uint_32 y = 0; y < height; y++) {
    png_bytep row     = row_pointers[y];
    uint16_t* dst_row = reinterpret_cast<uint16_t*>(dst + y * aligned_stride);
    for (png_uint_32 x = 0; x < width; x++) {
      uint8_t r = row[x * 3];
      uint8_t g = row[x * 3 + 1];
      uint8_t b = row[x * 3 + 2];

      uint16_t rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
      dst_row[x]      = rgb565;
    }
    heap_caps_free(row);
  }
  heap_caps_free(row_pointers);
  png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);

  // 하단 50% 영역에 디더링 기반 어둡기 처리 직접 베이킹
  apply_dithered_dimming(out_rgb565, width, height, aligned_stride);

  // CPU 캐시를 PSRAM으로 Flush (동기화)하여 GPU/DMA 가 최신 화소 정보를 읽을 수 있도록 보장
  size_t    data_bytes = aligned_stride * height;
  esp_err_t cache_err  = esp_cache_msync(out_rgb565, data_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  if (cache_err == ESP_OK) {
    ESP_LOGI(TAG, "Cache sync success: flushed %d bytes at address %p to memory.", (int)data_bytes, out_rgb565);
  } else {
    ESP_LOGE(TAG, "Cache sync FAILED: esp_err = 0x%x", cache_err);
  }

  return true;
}

bool ImagePipeline::download_and_decode(const std::string& url, uint16_t** out_rgb565, uint32_t& out_w, uint32_t& out_h) {
  if (url.empty()) return false;

  ESP_LOGI(TAG, "Starting image download: %s", url.c_str());

  esp_http_client_config_t config = {};
  config.url                      = url.c_str();
  config.timeout_ms               = 4000;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) return false;

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open connection to image URL: %d", err);
    esp_http_client_cleanup(client);
    return false;
  }

  int content_length = esp_http_client_fetch_headers(client);

  char* content_type = nullptr;
  esp_http_client_get_header(client, "Content-Type", &content_type);
  ESP_LOGI(TAG, "HTTP Response Headers - Content-Type: %s, Content-Length: %d", content_type ? content_type : "unknown", content_length);

  // [1단계 가드: Content-Length 가 1MB 초과 시 연결 중단] (4.1.1 사양)
  if (content_length > static_cast<int>(Image::Config::kMaxDownloadBytes)) {
    ESP_LOGE(TAG, "Content length exceeds 1MB limit: %d", content_length);
    esp_http_client_cleanup(client);
    return false;
  }

  // 버퍼 할당 (PSRAM 명시적 강제 할당 - 4.4.1 사양)
  size_t   alloc_size     = (content_length > 0) ? content_length : Image::Config::kMaxDownloadBytes;
  uint8_t* compressed_buf = static_cast<uint8_t*>(heap_caps_malloc(alloc_size, MALLOC_CAP_SPIRAM));
  if (!compressed_buf) {
    ESP_LOGE(TAG, "Failed to allocate memory for download buffer in PSRAM");
    esp_http_client_cleanup(client);
    return false;
  }

  int  read_bytes       = 0;
  int  total_read       = 0;
  bool is_header_parsed = false;
  bool is_valid         = true;

  char temp_buf[512];
  while (is_valid) {
    read_bytes = esp_http_client_read(client, temp_buf, sizeof(temp_buf));
    if (read_bytes <= 0) break;

    if (total_read + read_bytes > static_cast<int>(Image::Config::kMaxDownloadBytes)) {
      ESP_LOGE(TAG, "Download bytes exceeded 1MB during streaming");
      is_valid = false;
      break;
    }

    memcpy(compressed_buf + total_read, temp_buf, read_bytes);
    total_read += read_bytes;

    // [2단계 가드: 다운로드 중간 헤더 파싱 해상도 체크] (4.1.2 사양)
    if (!is_header_parsed && total_read >= 32) {
      uint32_t w = 0, h = 0;
      bool     parsed = false;

      if (parse_jpeg_header(compressed_buf, total_read, w, h)) {
        parsed = true;
      } else if (parse_png_header(compressed_buf, total_read, w, h)) {
        parsed = true;
      }

      if (parsed) {
        is_header_parsed = true;
        out_w            = w;
        out_h            = h;
        if (w > Image::Config::kMaxResolutionLimit || h > Image::Config::kMaxResolutionLimit) {
          ESP_LOGE(TAG, "Resolution limit exceeded: %lux%lu", w, h);
          is_valid = false;
          break;
        }
      }
    }
  }
  esp_http_client_cleanup(client);

  if (total_read >= 4) {
    ESP_LOGI(TAG, "Download finished. Bytes read: %d. Magic Bytes: 0x%02X 0x%02X 0x%02X 0x%02X", total_read, compressed_buf[0], compressed_buf[1], compressed_buf[2],
             compressed_buf[3]);
  }

  if (!is_valid || total_read == 0) {
    heap_caps_free(compressed_buf);
    return false;
  }

  if (!is_header_parsed) {
    uint32_t w = 0, h = 0;
    if (parse_jpeg_header(compressed_buf, total_read, w, h) || parse_png_header(compressed_buf, total_read, w, h)) {
      out_w = w;
      out_h = h;
      if (w > Image::Config::kMaxResolutionLimit || h > Image::Config::kMaxResolutionLimit) {
        ESP_LOGE(TAG, "Final resolution check failed: %lux%lu", w, h);
        heap_caps_free(compressed_buf);
        return false;
      }
    } else {
      ESP_LOGE(TAG, "Failed to parse image header (Unsupported format)");
      heap_caps_free(compressed_buf);
      return false;
    }
  }

  // 디코딩 출력 버퍼 할당 (PSRAM 명시적 할당, 정렬된 stride 크기 적용)
  uint32_t aligned_stride = lv_draw_buf_width_to_stride(out_w, LV_COLOR_FORMAT_RGB565);
  size_t   buffer_size    = aligned_stride * out_h;
  *out_rgb565             = static_cast<uint16_t*>(heap_caps_aligned_alloc(64, buffer_size, MALLOC_CAP_SPIRAM));
  if (!*out_rgb565) {
    ESP_LOGE(TAG, "Failed to allocate RGB565 pixel buffer in PSRAM");
    heap_caps_free(compressed_buf);
    return false;
  }

  bool decode_ok = false;
  if (compressed_buf[0] == 0xFF && compressed_buf[1] == 0xD8) {
    decode_ok = decode_jpeg(compressed_buf, total_read, *out_rgb565, out_w, out_h);
  } else if (compressed_buf[0] == 0x89 && compressed_buf[1] == 0x50) {
    decode_ok = decode_png(compressed_buf, total_read, *out_rgb565);
  }

  // [4.4.2 사양: 디코딩 성공 직후 원시 압축 버퍼 즉각 heap_caps_free 해제]
  heap_caps_free(compressed_buf);

  if (!decode_ok) {
    ESP_LOGE(TAG, "Decoding failed");
    heap_caps_free(*out_rgb565);
    *out_rgb565 = nullptr;
    return false;
  }

  ESP_LOGI(TAG, "Decoded successfully: %lux%lu in PSRAM", out_w, out_h);
  return true;
}

}  // namespace Ble::Dlna
