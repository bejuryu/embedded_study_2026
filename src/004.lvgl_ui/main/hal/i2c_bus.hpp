#pragma once

#include <vector>

#include "driver/i2c_master.h"

namespace hal {

class I2CBus {
 public:
  I2CBus();
  explicit I2CBus(i2c_master_bus_handle_t bus_handle);
  ~I2CBus();
  I2CBus(const I2CBus&) = delete;
  I2CBus& operator=(const I2CBus&) = delete;

  [[nodiscard]] i2c_master_bus_handle_t get_bus_handle() const;
  [[nodiscard]] std::vector<uint8_t> scan() const;

 private:
  i2c_master_bus_handle_t master_bus_handle_ = nullptr;
};

};  // namespace hal
