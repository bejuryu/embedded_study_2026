#pragma once

#include <expected>
#include <span>

#include "driver/i2c_master.h"

namespace hal {

template <typename AddressType = uint8_t, size_t MaxWriteBufferSize = 32, bool DeviceIsBigEndian = true>
class I2CDevice {
 public:
  I2CDevice(const i2c_master_bus_handle_t bus_handle, const uint16_t address, const uint32_t frequency_hz,
            const i2c_addr_bit_len_t dev_address_length = I2C_ADDR_BIT_LEN_7, const uint32_t scl_wait_us = 0,
            const bool flag_disable_ack_check = false) {
    const i2c_device_config_t device_config = {.dev_addr_length = dev_address_length,
                                               .device_address = address,
                                               .scl_speed_hz = frequency_hz,
                                               .scl_wait_us = scl_wait_us,
                                               .flags = {.disable_ack_check = flag_disable_ack_check}};
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &device_config, &handle_));
  }
  ~I2CDevice() { ESP_ERROR_CHECK(i2c_master_bus_rm_device(handle_)); }

  I2CDevice(const I2CDevice&) = delete;
  I2CDevice& operator=(const I2CDevice&) = delete;
  I2CDevice(I2CDevice&&) = delete;
  I2CDevice& operator=(I2CDevice&&) = delete;

  [[nodiscard]] std::expected<void, esp_err_t> read(const AddressType address, std::span<uint8_t> buffer,
                                                    const int timeout_ms = -1) const {
    const AddressType read_address = get_address_(address);
    const auto err = i2c_master_transmit_receive(handle_, reinterpret_cast<const uint8_t*>(&read_address),
                                                 sizeof(read_address), buffer.data(), buffer.size(), timeout_ms);
    if (err != ESP_OK) {
      return std::unexpected(err);
    }
    return {};
  }

  [[nodiscard]] std::expected<void, esp_err_t> write(const AddressType address, const std::span<const uint8_t> buffer,
                                                     const int timeout_ms = -1) const {
    if ((sizeof(AddressType) + buffer.size()) > MaxWriteBufferSize) {
      return std::unexpected(ESP_ERR_INVALID_SIZE);
    }
    constexpr auto address_size = sizeof(AddressType);
    const auto write_address = get_address_(address);
    const auto* address_ptr = reinterpret_cast<const uint8_t*>(&write_address);
    uint8_t write_buffer[MaxWriteBufferSize];

    std::copy(address_ptr, address_ptr + address_size, write_buffer);
    std::copy(buffer.begin(), buffer.end(), write_buffer + address_size);
    const auto err = i2c_master_transmit(handle_, write_buffer, address_size + buffer.size(), timeout_ms);
    if (err != ESP_OK) {
      return std::unexpected(err);
    }
    return {};
  }

 private:
  i2c_master_dev_handle_t handle_ = nullptr;

  static constexpr AddressType get_address_(AddressType address) {
    if constexpr (DeviceIsBigEndian != (std::endian::native == std::endian::big)) {
      return std::byteswap(address);
    }
    return address;
  }
};

};  // namespace hal