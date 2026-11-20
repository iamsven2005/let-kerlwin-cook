// Host-side I2C bus + BMA253 simulation for testing SoftI2C and AccelDriver.
// Open-drain bus: a line is low if the master, the slave, or an injected fault
// pulls it low. The slave is an edge-driven state machine (START/STOP, address,
// register pointer, auto-increment, FIFO data register that doesn't increment).
#pragma once
#include <stdint.h>
#include <deque>
#include <array>
#include <string.h>

struct BmaModel {
  uint8_t regs[64];
  std::deque<std::array<uint8_t, 6>> fifo;
  uint8_t frameByte = 0;            // index into the front frame while reading 0x3F
  int softResets = 0, fifoConfigs = 0;
  BmaModel() { reset(); }
  void reset() { memset(regs, 0, sizeof(regs)); regs[0x00] = 0xFA; fifo.clear(); frameByte = 0; }
  void pushFrame(int16_t x, int16_t y, int16_t z) {
    std::array<uint8_t, 6> f;
    int16_t v[3] = { x, y, z };
    for (int i = 0; i < 3; i++) {
      uint16_t u = (uint16_t)((uint16_t)v[i] << 4);    // 12-bit left-justified
      f[2 * i] = (uint8_t)(u & 0xF0); f[2 * i + 1] = (uint8_t)(u >> 8);
    }
    if (fifo.size() >= 32) fifo.pop_front();  // stream mode keeps the newest
    fifo.push_back(f);
  }
  uint8_t read(uint8_t r) {
    if (r == 0x0E) return (uint8_t)(fifo.size() > 32 ? 32 : fifo.size());
    if (r == 0x3F) {
      if (fifo.empty()) return 0;
      uint8_t b = fifo.front()[frameByte++];
      if (frameByte == 6) { fifo.pop_front(); frameByte = 0; }
      return b;
    }
    return regs[r & 63];
  }
  void write(uint8_t r, uint8_t v) {
    if (r == 0x14 && v == 0xB6) { reset(); softResets++; return; }
    if (r == 0x3E) { fifo.clear(); frameByte = 0; fifoConfigs++; }
    regs[r & 63] = v;
  }
};

struct SimBus {
  // line drivers
  bool mSda = false, mScl = false;  // master pulling low
  bool sSda = false;                // slave pulling low
  bool sclStuck = false;            // fault: SCL held low externally
  int  holdSdaClocks = 0;           // fault: slave holds SDA low for this many more SCL pulses
  bool present = true;              // device attached
  uint32_t us = 0;                  // fake microsecond clock
  uint32_t edges = 0;               // SCL rising edges (bus activity)
  uint8_t addr = 0x18;
  BmaModel dev;

  bool sda() const { return !(mSda || sSda || holdSdaClocks > 0); }
  bool scl() const { return !(mScl || sclStuck); }

  // slave state
  enum St { IDLE, RECV, ACKING, SEND, WAIT_MACK } st = IDLE;
  uint8_t shift = 0, bits = 0, regPtr = 0, out = 0, bitIdx = 0;
  bool addrPhase = false, rw = false, regSet = false, masterAck = false, justAddressed = false;
  bool prevSda = true, prevScl = true;

  void eval() {
    bool d = sda(), c = scl();
    if (c && !prevScl) edges++;
    if (present) {
      if (c && prevScl && d != prevSda) { if (!d) onStart(); else onStop(); }
      else if (c && !prevScl) onRise(d);
      else if (!c && prevScl) onFall();
    }
    if (!c && prevScl && holdSdaClocks > 0) holdSdaClocks--;
    prevSda = sda(); prevScl = scl();
  }
  void onStart() { st = RECV; bits = 0; shift = 0; addrPhase = true; regSet = false; sSda = false; }
  void onStop()  { st = IDLE; sSda = false; }
  void onRise(bool d) {
    if (st == RECV) { shift = (uint8_t)((shift << 1) | (d ? 1 : 0)); bits++; }
    else if (st == WAIT_MACK) masterAck = !d;
  }
  uint8_t nextOut() {
    uint8_t b = dev.read(regPtr);
    if (regPtr != 0x3F) regPtr++;
    return b;
  }
  void onFall() {
    switch (st) {
      case RECV:
        if (bits < 8) return;
        if (addrPhase) {
          addrPhase = false;
          if ((shift >> 1) != addr) { st = IDLE; return; }    // not us: no ACK
          rw = shift & 1;
          justAddressed = true;
        } else {
          justAddressed = false;
          if (!regSet) { regPtr = shift; regSet = true; }
          else { dev.write(regPtr, shift); if (regPtr != 0x3F) regPtr++; }
        }
        sSda = true;                 // ACK
        st = ACKING;
        return;
      case ACKING:
        sSda = false;
        if (rw && justAddressed) {
          out = nextOut(); bitIdx = 1;
          sSda = !(out & 0x80);
          st = SEND;
        } else { st = RECV; bits = 0; shift = 0; }
        return;
      case SEND:
        if (bitIdx < 8) { sSda = !((out >> (7 - bitIdx)) & 1); bitIdx++; }
        else { sSda = false; st = WAIT_MACK; }
        return;
      case WAIT_MACK:
        if (masterAck) { out = nextOut(); bitIdx = 1; sSda = !(out & 0x80); st = SEND; }
        else st = IDLE;
        return;
      case IDLE:
        return;
    }
  }
};

extern SimBus* g_bus;

struct SimIO {
  static void init() {}
  static void sdaLow()     { g_bus->mSda = true;  g_bus->eval(); }
  static void sdaRelease() { g_bus->mSda = false; g_bus->eval(); }
  static bool sdaRead()    { return g_bus->sda(); }
  static void sclLow()     { g_bus->mScl = true;  g_bus->eval(); }
  static void sclRelease() { g_bus->mScl = false; g_bus->eval(); }
  static bool sclRead()    { return g_bus->scl(); }
  static void halfDelay()  { g_bus->us += 2; }
  static uint32_t micros() { return ++g_bus->us; }
};
