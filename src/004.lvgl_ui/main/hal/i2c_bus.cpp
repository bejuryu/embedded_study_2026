#include "i2c_bus.hpp"

#include <ranges>

#include "bsp/esp-bsp.h"

namespace hal {

namespace {
constexpr uint8_t kI2CAddressBegin = 0x08;
constexpr uint8_t kI2CAddressEnd = 0x7F;

constexpr uint16_t kProbeTimeoutMs = 100;
}  // namespace

I2CBus::I2CBus() {
  constexpr i2c_master_bus_config_t i2c_config = {.i2c_port = BSP_I2C_NUM,
                                                  .sda_io_num = BSP_I2C_SDA,
                                                  .scl_io_num = BSP_I2C_SCL,
                                                  .clk_source = I2C_CLK_SRC_DEFAULT,
                                                  .glitch_ignore_cnt = 0,
                                                  .intr_priority = 0,
                                                  .trans_queue_depth = 0,
                                                  .flags = {.enable_internal_pullup = true, .allow_pd = false}};
  ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_config, &master_bus_handle_));
}

I2CBus::I2CBus(const i2c_master_bus_handle_t bus_handle) { master_bus_handle_ = bus_handle; }

I2CBus::~I2CBus() { ESP_ERROR_CHECK(i2c_del_master_bus(master_bus_handle_)); }

i2c_master_bus_handle_t I2CBus::get_bus_handle() const { return master_bus_handle_; }

std::vector<uint8_t> I2CBus::scan() const {
  const auto bus = master_bus_handle_;
  return std::views::iota(kI2CAddressBegin, kI2CAddressEnd) | std::views::filter([bus](const uint8_t addr) {
           return i2c_master_probe(bus, addr, kProbeTimeoutMs) == ESP_OK;
         }) |
         std::ranges::to<std::vector>();
}

}  // namespace hal