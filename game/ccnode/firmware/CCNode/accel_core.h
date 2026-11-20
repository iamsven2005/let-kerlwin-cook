// BMA253 driver core (Accelerometer TinyShield ASD2511 Rev5), pure C++ so it can
// be host-tested against a simulated sensor. Non-blocking state machine:
//   PROBE -> (chip ID 0xFA) soft reset -> RESET_WAIT (3 ms) -> configure -> OK
//   OK: drain the 32-frame FIFO every ACCEL_POLL_MS; ACCEL_FAIL_LIMIT failed
//       transfers in a row -> MISSING (polling stops)
//   MISSING: re-probe every ACCEL_REPROBE_MS (brief v1.4 A4)
#pragma once
#include <stdint.h>

typedef void (*AccelSampleCb)(int16_t x, int16_t y, int16_t z, uint32_t tMs);

struct AccelIO {
  bool (*writeReg)(uint8_t addr, uint8_t reg, uint8_t val);
  bool (*readRegs)(uint8_t addr, uint8_t reg, uint8_t* buf, uint8_t n);
  void (*recover)();
};

class AccelDriver {
 public:
  enum State : uint8_t { ST_PROBE, ST_RESET_WAIT, ST_OK, ST_MISSING };
  enum Event : uint8_t { EV_NONE = 0, EV_FOUND = 1, EV_LOST = 2 };

  explicit AccelDriver(const AccelIO& io) : io_(io) {}
  void begin(uint32_t nowMs) { state_ = ST_PROBE; nextAt_ = nowMs; everOk_ = false; }

  // Call every main-loop pass. Runs the state machine; in OK, drains the FIFO
  // (at most every ACCEL_POLL_MS) and passes each sample to cb (if not null).
  uint16_t service(uint32_t nowMs, AccelSampleCb cb);

  State    state() const { return state_; }
  bool     ok() const { return state_ == ST_OK; }
  bool     settled() const { return state_ == ST_OK || state_ == ST_MISSING; }
  uint8_t  takeEvents() { uint8_t e = events_; events_ = EV_NONE; return e; }
  uint32_t overruns() const { return overruns_; }
  uint32_t errors() const { return errors_; }
  uint32_t probes() const { return probes_; }

 private:
  void lose(uint32_t nowMs);
  bool configure();

  AccelIO  io_;
  State    state_ = ST_PROBE;
  uint32_t nextAt_ = 0;
  uint32_t lastPoll_ = 0;
  uint32_t lastT_ = 0;
  uint8_t  failStreak_ = 0;
  uint8_t  events_ = EV_NONE;
  bool     everOk_ = false;
  uint32_t overruns_ = 0, errors_ = 0, probes_ = 0;
};
