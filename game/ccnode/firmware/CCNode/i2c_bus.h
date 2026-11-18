// I2C bus used by the accelerometer. Backend chosen by I2C_USE_WIRE in config.h:
// 0 = SoftI2C (timeouts + bus recovery, default), 1 = Arduino Wire.
#pragma once
#include <stdint.h>

namespace I2CBus {
  void     begin();
  bool     writeReg(uint8_t addr, uint8_t reg, uint8_t val);
  bool     readRegs(uint8_t addr, uint8_t reg, uint8_t* buf, uint8_t n);
  void     recover();
  uint32_t timeouts();
  uint32_t recoveries();
  const char* backendName();
}
