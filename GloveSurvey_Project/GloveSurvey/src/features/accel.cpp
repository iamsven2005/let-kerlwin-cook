#include "accel.h"
#if FEAT_ACCEL
#include <Wire.h>
#include "../core/state.h"
#include "../core/main_loop.h"

static uint8_t addr = 0;
static unsigned long lastReadMs = 0, reads = 0, readErrors = 0;
static float magMean = 1;

static void writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

static bool bmaBegin() {
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

static bool bmaRead(float& x, float& y, float& z) {
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

void accelBegin() {
  if (!bmaBegin()) haltWith("No accelerometer!");
}

// Every pass: accelFresh is true only on the pass a new sample arrived (50 Hz)
void accelUpdate() {
  accelFresh = false;
  unsigned long now = millis();
  if (now - lastReadMs < ACCEL_PERIOD_MS) return;
  lastReadMs = now;
  float ax, ay, az;
  if (!bmaRead(ax, ay, az)) { readErrors++; return; }
  reads++;
  accX = ax; accY = ay; accZ = az;
  float mag = sqrt(ax * ax + ay * ay + az * az);
  magMean += 0.02f * (mag - magMean);
  dynAcc = mag - magMean;                  // steps and still detection use this
  gravX += 0.05f * (ax - gravX);           // hand pose uses this
  gravY += 0.05f * (ay - gravY);
  gravZ += 0.05f * (az - gravZ);
  accelFresh = true;
}

#if DEBUG_ANY
void accelDebug(DebugText& t) {
  // milli-g as whole numbers: the Arduino printf has no %f
  t.add("x %ld y %ld mg", lround(accX * 1000), lround(accY * 1000));
  t.add("z %ld mg", lround(accZ * 1000));
  t.add("dyn %ld mg", lround(dynAcc * 1000));
  t.add("reads %lu", reads);
  t.add("errors %lu  @0x%02X", readErrors, addr);
}
#endif

#endif
