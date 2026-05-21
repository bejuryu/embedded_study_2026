#include "sdcard.hpp"

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "sdmmc_cmd.h"

namespace device::sdcard {

namespace {
const char* TAG = "SdCard";
constexpr std::string_view kSdCardMountPoint = CONFIG_BSP_SD_MOUNT_POINT;
}  // namespace

using namespace std::filesystem;

SdCard::SdCard() : current_path_(kSdCardMountPoint) { mount(); }

SdCard::~SdCard() { unmount(); }

esp_err_t SdCard::mount() {
  const auto ret = bsp_sdcard_mount();
  if (ret == ESP_OK) {
    sdmmc_card_print_info(stdout, bsp_sdcard_get_handle());
  } else {
    ESP_LOGE(TAG, "SD-CARD MOUNT ERROR: %s", esp_err_to_name(ret));
  }
  return ret;
}

esp_err_t SdCard::unmount() { return bsp_sdcard_unmount(); }

path SdCard::mount_point() const { return kSdCardMountPoint; }

path SdCard::pwd() const { return current_path_; }

std::expected<path, esp_err_t> SdCard::cd(const std::string& target_path) {
  return exist_(current_path_ / target_path).transform([&](const auto& exist_path) {
    current_path_ = exist_path;
    return current_path_;
  });
}

std::vector<FileEntry> SdCard::ls(const std::string& target_path) {
  std::vector<FileEntry> result{};
  const path ls_file = target_path.empty() ? current_path_ : path_nomalized_(target_path);
  if (is_directory(ls_file)) {
    for (const auto& dir_entry : directory_iterator{ls_file}) {
      result.push_back({.file_path = dir_entry.path().string(),
                        .is_directory = dir_entry.is_directory(),
                        .file_size = dir_entry.file_size()});
    }
  } else if (is_regular_file(ls_file)) {
    result.push_back(
        {.file_path = ls_file.string(), .is_directory = is_directory(ls_file), .file_size = file_size(ls_file)});
  }
  return result;
}

std::expected<path, esp_err_t> SdCard::mkdir(const std::string& target_path) {
  if (target_path.empty()) {
    return std::unexpected{ESP_ERR_INVALID_ARG};
  }
  const auto target_dir = path_nomalized_(target_path);

  std::error_code e;
  if (!create_directories(target_dir.string(), e)) {
    ESP_LOGE(TAG, "Error creating directory: %s", e.message().c_str());
    return std::unexpected{ESP_ERR_NOT_ALLOWED};
  }
  return target_dir;
}

std::expected<path, esp_err_t> SdCard::rm(const std::string& target_path) { return {}; }

std::expected<path, esp_err_t> SdCard::exist_(const path& target_path) {
  std::error_code e;
  auto next_path = target_path.lexically_normal();
  if (!exists(next_path, e)) {
    return std::unexpected{ESP_ERR_NOT_FOUND};
  }
  return next_path;
}

path SdCard::path_nomalized_(const path& target_path) {
  return (target_path.string().starts_with("/") ? target_path : current_path_ / target_path);
}

}  // namespace device::sdcard