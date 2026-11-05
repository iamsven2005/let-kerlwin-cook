#include "mode.h"
#if SURVEY_MODE == HAT
#include "accel_bma250.h"
#include <Wire.h>

static uint8_t addr = 0;

uint8_t bmaAddress() { return addr; }

static void writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

bool bmaBegin() {
  const uint8_t addrs[] = { 0x18, 0x19 };
  for (uint8_t a : addrs) {
    Wire.beginTransmission(a);
    Wire.write(0x00);                                  // chip id register
    if (Wire.endTransmission() != 0) continue;
    Wire.requestFrom(a, (uint8_t)1);
    if (!Wire.available()) continue;
    uint8_t id = Wire.read();
    if (id == 0x03 || id == 0xF9 || id == 0xFA) {      // BMA250 / BMA250E family
      addr = a;
      writeReg(0x0F, 0x03);                            // range +/-2 g
      writeReg(0x10, 0x0C);                            // filter bandwidth 125 Hz
      return true;
    }
  }
  return false;
}

bool bmaRead(float& x, float& y, float& z) {
  if (addr == 0) return false;
  Wire.beginTransmission(addr);
  Wire.write(0x02);                                    // X LSB, X MSB, Y LSB, Y MSB, Z LSB, Z MSB
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, (uint8_t)6) != 6) return false;
  int16_t v[3];
  for (int i = 0; i < 3; i++) {
    uint8_t lsb = Wire.read(), msb = Wire.read();
    v[i] = (int16_t)((msb << 8) | lsb) >> 6;          // 10-bit signed
  }
  x = v[0] / 256.0f;
  y = v[1] / 256.0f;
  z = v[2] / 256.0f;
  return true;
}

#endif // SURVEY_MODE == HAT
