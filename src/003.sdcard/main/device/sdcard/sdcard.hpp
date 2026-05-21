#pragma once

#include <esp_err.h>

#include <expected>
#include <filesystem>
#include <format>
#include <vector>

namespace device::sdcard {

class FileEntry {
 public:
  std::string file_path;
  bool is_directory = false;
  uintmax_t file_size = 0;

  [[nodiscard]] std::string dump() const {
    return std::format("{},{},{}", file_path, is_directory ? "directory" : "file", file_size);
  }
};

class SdCard {
 public:
  SdCard();
  ~SdCard();
  SdCard(const SdCard&) = delete;
  SdCard& operator=(const SdCard&) = delete;
  SdCard(SdCard&&) = delete;
  SdCard& operator=(SdCard&&) = delete;

  esp_err_t mount();
  esp_err_t unmount();

  [[nodiscard]] std::filesystem::path mount_point() const;
  [[nodiscard]] std::filesystem::path pwd() const;
  std::expected<std::filesystem::path, esp_err_t> cd(const std::string& target_path);
  std::vector<FileEntry> ls(const std::string& target_path = "");
  std::expected<std::filesystem::path, esp_err_t> mkdir(const std::string& target_path);
  std::expected<std::filesystem::path, esp_err_t> rm(const std::string& target_path);

 private:
  std::expected<std::filesystem::path, esp_err_t> exist_(const std::filesystem::path& target_path);
  std::filesystem::path path_nomalized_(const std::filesystem::path& target_path);
  std::filesystem::path current_path_;
};

}  // namespace device::sdcard