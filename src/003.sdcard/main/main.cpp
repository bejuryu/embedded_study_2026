#include <algorithm>
#include <print>
#include <thread>

#include "bsp/esp-bsp.h"
#include "device/sdcard/sdcard.hpp"
#include "esp_log.h"
#include "esp_system.h"
#include "sdkconfig.h"

struct display_data_t {
  lv_display_t* display = nullptr;
  lv_obj_t* screen = nullptr;
  lv_obj_t* image = nullptr;
};

namespace {
constexpr auto kTag = "Main";
// 1바이트 단위로 구조체를 정렬 (컴파일러 최적화로 인한 패딩 방지)
#pragma pack(push, 1)
typedef struct {
  uint16_t bfType;  // 파일 타입 (값은 항상 0x4D42, 즉 'BM')
  uint32_t bfSize;  // 파일 크기
  uint16_t bfReserved1;
  uint16_t bfReserved2;
  uint32_t bfOffBits;  // 실제 픽셀 데이터까지의 오프셋
} BMPFileHeader;

typedef struct {
  uint32_t biSize;         // 이 구조체의 크기 (일반적으로 40)
  int32_t biWidth;         // 이미지 가로 크기 (픽셀)
  int32_t biHeight;        // 이미지 세로 크기 (픽셀, 음수면 탑다운 방식)
  uint16_t biPlanes;       // 항상 1
  uint16_t biBitCount;     // 픽셀당 비트 수 (1, 4, 8, 16, 24, 32)
  uint32_t biCompression;  // 압축 방식
  uint32_t biSizeImage;    // 이미지의 데이터 크기
  int32_t biXPelsPerMeter;
  int32_t biYPelsPerMeter;
  uint32_t biClrUsed;
  uint32_t biClrImportant;
} BMPInfoHeader;
#pragma pack(pop)

}  // namespace

extern "C" void app_main(void) {
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  device::sdcard::SdCard sd_card;

  std::print("SD Card Mound Point: {}\n", sd_card.mount_point().string());

  sd_card.cd("image");
  const auto& ls_result = sd_card.ls();
  std::print("ls result count: {}\n", ls_result.size());
  size_t ls_count = 0;
  for (const auto& item : sd_card.ls()) {
    std::print("{:04}: {}\n", ls_count++, item.dump());
  }
  std::print("--------\n");

  const auto to_upper = [](const std::string& str) {
    std::string result = str;
    std::ranges::transform(result, result.begin(), [](unsigned char c) { return std::toupper(c); });
    return result;
  };

  constexpr size_t kMaxImageSize = 4 * 1024 * 1024;
  std::vector<std::string> image_list;
  const auto image_dir = sd_card.pwd();
  for (const auto& item : ls_result) {
    const auto upper_file_path = to_upper(item.file_path);
    const bool is_image = upper_file_path.ends_with(".BMP");  // || upper_file_path.ends_with(".BIN");
    if (is_image && item.file_size <= kMaxImageSize) {
      image_list.push_back(image_dir / item.file_path);
    }
  }

  display_data_t display;
  display.display = bsp_display_start();

  bsp_display_lock(0);
  display.screen = lv_disp_get_scr_act(display.display);
  display.image = lv_image_create(display.screen);
  lv_obj_center(display.image);
  lv_obj_set_style_bg_color(display.screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(display.screen, LV_OPA_COVER, 0);
  bsp_display_rotate(display.display, LV_DISPLAY_ROTATION_90);
  bsp_display_brightness_set(25);
  bsp_display_unlock();

  if (image_list.empty()) {
    ESP_LOGI(kTag, "No images found, restarting in 10 seconds");
    std::this_thread::sleep_for(std::chrono::seconds(10));
  } else {
    uint8_t* binary_buffer = nullptr;

    while (true) {
      for (const auto& image_item : image_list) {
        ESP_LOGI(kTag, "Displaying image: %s", image_item.c_str());

        size_t buffer_size = std::filesystem::file_size(image_item);

        BMPFileHeader file_header{};
        BMPInfoHeader info_header{};

        FILE* f_bitmap = fopen(image_item.c_str(), "rb");
        fread(&file_header, 1, sizeof(file_header), f_bitmap);
        fread(&info_header, 1, sizeof(info_header), f_bitmap);

        uint8_t* new_binary_buffer =
            static_cast<uint8_t*>(heap_caps_aligned_alloc(64, buffer_size - file_header.bfOffBits, MALLOC_CAP_SPIRAM));
        if (new_binary_buffer == nullptr) {
          fclose(f_bitmap);
          ESP_LOGE(kTag, "Failed to allocate memory for new binary buffer");
          std::this_thread::sleep_for(std::chrono::milliseconds(500));
          continue;
        }

        fseek(f_bitmap, file_header.bfOffBits, SEEK_SET);
        fread(new_binary_buffer, 1, buffer_size - file_header.bfOffBits, f_bitmap);
        fclose(f_bitmap);

        bsp_display_lock(0);
        if (binary_buffer != nullptr) {
          heap_caps_free(binary_buffer);
          binary_buffer = nullptr;
        }

        binary_buffer = new_binary_buffer;

        static lv_image_dsc_t image_dsc{};
        image_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        image_dsc.header.w = info_header.biWidth;
        image_dsc.header.h = std::abs(info_header.biHeight);
        image_dsc.header.stride = ((info_header.biWidth * info_header.biBitCount + 31) / 32) * 4;
        switch (info_header.biBitCount) {
          case 16: {
            image_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
            break;
          }
          case 24: {
            image_dsc.header.cf = LV_COLOR_FORMAT_RGB888;
            break;
          }
          case 32: {
            image_dsc.header.cf = LV_COLOR_FORMAT_ARGB8888;
          }
        }

        image_dsc.data = binary_buffer;
        image_dsc.data_size = buffer_size - file_header.bfOffBits;
        lv_image_set_src(display.image, &image_dsc);

        // const auto new_image_path = std::string{(char)CONFIG_LV_FS_STDIO_LETTER} + ":" + image_item;
        // lv_image_set_src(display.image, new_image_path.c_str());

        bsp_display_unlock();

        std::this_thread::sleep_for(std::chrono::seconds(10));
      }
    }
    if (binary_buffer != nullptr) {
      heap_caps_free(binary_buffer);
    }
  }

  esp_restart();
}
