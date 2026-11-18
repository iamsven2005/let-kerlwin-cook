// Bit-banged I2C master with a timeout on every wait and stuck-bus recovery
// (brief v1.4 A4). Arduino's SAMD Wire library can spin forever when a device
// disappears mid-transfer; this driver can't: the worst case for a failed
// transfer is about one stretch timeout plus a 9-clock recovery.
//
// Header-only template over the pin layer IO, which provides static functions:
//   init(), sclLow(), sclRelease(), sclRead(), sdaLow(), sdaRelease(), sdaRead(),
//   halfDelay(), micros()
// "Release" means stop driving: the line is pulled high by the pull-ups.
// The same code runs on the SAMD21 (i2c_bus.cpp) and against a simulated
// BMA253 in the host tests.
#pragma once
#include <stdint.h>

template <class IO, uint32_t STRETCH_TIMEOUT_US>
class SoftI2C {
 public:
  void begin() {
    IO::init();
    IO::sdaRelease();
    IO::sclRelease();
    if (!IO::sdaRead() || !IO::sclRead()) recover();
  }

  bool writeReg(uint8_t addr, uint8_t reg, uint8_t val) {
    if (!start()) return fail();
    if (!sendOrFail((uint8_t)(addr << 1))) return false;
    if (!sendOrFail(reg)) return false;
    if (!sendOrFail(val)) return false;
    if (!stop()) return fail();
    return true;
  }

  bool readRegs(uint8_t addr, uint8_t reg, uint8_t* buf, uint8_t n) {
    if (!start()) return fail();
    if (!sendOrFail((uint8_t)(addr << 1))) return false;
    if (!sendOrFail(reg)) return false;
    if (!restart()) return fail();
    if (!sendOrFail((uint8_t)((addr << 1) | 1))) return false;
    for (uint8_t i = 0; i < n; i++)
      if (!readByte(buf[i], i + 1 < n)) return fail();
    if (!stop()) return fail();
    return true;
  }

  // Free a bus held by a slave that lost sync: clock until it lets go of SDA
  // (at most 9 clocks), then issue a STOP.
  void recover() {
    recoveries++;
    IO::sdaRelease();
    for (uint8_t i = 0; i < 9 && !IO::sdaRead(); i++) {
      IO::sclLow();     IO::halfDelay();
      IO::sclRelease(); IO::halfDelay();
    }
    IO::sclLow();     IO::halfDelay();
    IO::sdaLow();     IO::halfDelay();
    IO::sclRelease(); IO::halfDelay();
    IO::sdaRelease(); IO::halfDelay();
  }

  uint32_t timeouts = 0, nacks = 0, recoveries = 0;

 private:
  // Release SCL and wait (bounded) for it to read high: clock stretching or a stuck line.
  bool sclHigh() {
    IO::sclRelease();
    if (IO::sclRead()) return true;
    uint32_t t0 = IO::micros();
    while (!IO::sclRead()) {
      if ((uint32_t)(IO::micros() - t0) > STRETCH_TIMEOUT_US) { timeouts++; return false; }
    }
    return true;
  }

  bool start() {
    if (!IO::sdaRead() || !IO::sclRead()) {        // bus not idle: try to free it once
      recover();
      if (!IO::sdaRead() || !IO::sclRead()) return false;
    }
    IO::sdaLow(); IO::halfDelay();
    IO::sclLow(); IO::halfDelay();
    return true;
  }

  bool restart() {
    IO::sdaRelease(); IO::halfDelay();
    if (!sclHigh()) return false;
    IO::halfDelay();
    IO::sdaLow(); IO::halfDelay();
    IO::sclLow(); IO::halfDelay();
    return true;
  }

  bool stop() {
    IO::sdaLow(); IO::halfDelay();
    bool ok = sclHigh();
    IO::halfDelay();
    IO::sdaRelease(); IO::halfDelay();
    return ok && IO::sdaRead();
  }

  // 1 = ACK, 0 = NACK, -1 = bus error (timeout, or SDA held low by someone else)
  int writeByte(uint8_t b) {
    for (uint8_t i = 0; i < 8; i++) {
      bool one = b & 0x80;
      if (one) IO::sdaRelease(); else IO::sdaLow();
      IO::halfDelay();
      if (!sclHigh()) return -1;
      if (one && !IO::sdaRead()) { IO::sclLow(); return -1; }   // a slave is holding SDA
      IO::halfDelay();
      IO::sclLow();
      b <<= 1;
    }
    IO::sdaRelease(); IO::halfDelay();
    if (!sclHigh()) return -1;
    bool ack = !IO::sdaRead();
    IO::halfDelay();
    IO::sclLow();
    return ack ? 1 : 0;
  }

  bool readByte(uint8_t& out, bool ack) {
    uint8_t v = 0;
    IO::sdaRelease();
    for (uint8_t i = 0; i < 8; i++) {
      IO::halfDelay();
      if (!sclHigh()) return false;
      v = (uint8_t)((v << 1) | (IO::sdaRead() ? 1 : 0));
      IO::halfDelay();
      IO::sclLow();
    }
    if (ack) IO::sdaLow(); else IO::sdaRelease();
    IO::halfDelay();
    if (!sclHigh()) return false;
    IO::halfDelay();
    IO::sclLow();
    IO::sdaRelease();
    out = v;
    return true;
  }

  bool sendOrFail(uint8_t b) {
    int r = writeByte(b);
    if (r == 1) return true;
    if (r == 0) { nacks++; stop(); return false; }   // device absent / refused: clean STOP
    return fail();                                    // bus error
  }

  bool fail() {
    recover();
    return false;
  }
};
