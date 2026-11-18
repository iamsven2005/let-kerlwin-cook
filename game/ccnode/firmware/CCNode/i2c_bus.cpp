#include <Arduino.h>
#include "i2c_bus.h"
#include "config.h"

#if I2C_USE_WIRE
// -----------------------------------------------------------------------------
// Arduino Wire backend (400 kHz). No timeouts: a vanished device can hang it,
// in which case the watchdog resets the node.
// -----------------------------------------------------------------------------
#include <Wire.h>

namespace I2CBus {
void begin() { Wire.begin(); Wire.setClock(400000); }
bool writeReg(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}
bool readRegs(uint8_t addr, uint8_t reg, uint8_t* buf, uint8_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}
void recover() {}
uint32_t timeouts() { return 0; }
uint32_t recoveries() { return 0; }
const char* backendName() { return "wire"; }
}

#else
// -----------------------------------------------------------------------------
// Bit-banged backend on the board's SDA/SCL pins (from the variant), open-drain
// emulated with DIR: driving low = output 0; released = input with the
// internal pull-up on (the shield's pull-ups do the real work when present; the
// internal ones keep a bare connector from floating).
// -----------------------------------------------------------------------------
#include "soft_i2c.h"

namespace {
  PortGroup* gSda; uint32_t mSda;
  PortGroup* gScl; uint32_t mScl;

  void setupPin(uint32_t pin, PortGroup*& g, uint32_t& m) {
    const PinDescription& d = g_APinDescription[pin];
    g = &PORT->Group[d.ulPort];
    m = 1ul << d.ulPin;
    g->PINCFG[d.ulPin].reg = (uint8_t)(PORT_PINCFG_INEN | PORT_PINCFG_PULLEN);   // GPIO (PMUXEN off), input buffer, pull
    g->DIRCLR.reg = m;
    g->OUTSET.reg = m;   // pull-up
  }

  struct SamdPins {
    static void init() { setupPin(PIN_WIRE_SDA, gSda, mSda); setupPin(PIN_WIRE_SCL, gScl, mScl); }
    static inline void sdaLow()     { gSda->OUTCLR.reg = mSda; gSda->DIRSET.reg = mSda; }
    static inline void sdaRelease() { gSda->DIRCLR.reg = mSda; gSda->OUTSET.reg = mSda; }
    static inline bool sdaRead()    { return (gSda->IN.reg & mSda) != 0; }
    static inline void sclLow()     { gScl->OUTCLR.reg = mScl; gScl->DIRSET.reg = mScl; }
    static inline void sclRelease() { gScl->DIRCLR.reg = mScl; gScl->OUTSET.reg = mScl; }
    static inline bool sclRead()    { return (gScl->IN.reg & mScl) != 0; }
    static inline void halfDelay()  { for (volatile uint32_t i = 0; i < I2C_HALF_PERIOD_LOOPS; i++) {} }
    static inline uint32_t micros() { return ::micros(); }
  };

  SoftI2C<SamdPins, I2C_STRETCH_TIMEOUT_US> bus;
}

namespace I2CBus {
void begin() { bus.begin(); }
bool writeReg(uint8_t addr, uint8_t reg, uint8_t val) { return bus.writeReg(addr, reg, val); }
bool readRegs(uint8_t addr, uint8_t reg, uint8_t* buf, uint8_t n) { return bus.readRegs(addr, reg, buf, n); }
void recover() { bus.recover(); }
uint32_t timeouts() { return bus.timeouts; }
uint32_t recoveries() { return bus.recoveries; }
const char* backendName() { return "soft"; }
}
#endif
