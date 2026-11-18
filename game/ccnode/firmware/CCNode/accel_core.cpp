#include "accel_core.h"
#include "config.h"

// BMA253 / BMA255 registers
#define REG_CHIPID       0x00
#define REG_FIFO_STATUS  0x0E   // bit7 overrun, bits6:0 frame count
#define REG_PMU_RANGE    0x0F
#define REG_PMU_BW       0x10
#define REG_PMU_LPW      0x11
#define REG_ACCD_HBW     0x13
#define REG_SOFTRESET    0x14
#define REG_FIFO_CONFIG1 0x3E
#define REG_FIFO_DATA    0x3F

#define RANGE_8G         0x08
#define BW_500HZ         0x0E   // 500 Hz bandwidth -> 1000 Hz data rate
#define FIFO_STREAM_XYZ  0x80   // stream mode (keep newest), X+Y+Z frames
#define RESET_WAIT_MS    3      // datasheet start-up after soft reset: 1.8 ms
#define FRAMES_PER_READ  10     // 60 bytes per transfer

static inline bool reached(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }
static inline int16_t toCounts(uint8_t lsb, uint8_t msb) {
  return (int16_t)((uint16_t)msb << 8 | lsb) >> 4;   // 12-bit, left-justified
}

bool AccelDriver::configure() {
  return io_.writeReg(ACCEL_I2C_ADDR, REG_PMU_LPW, 0x00)            // normal mode
      && io_.writeReg(ACCEL_I2C_ADDR, REG_PMU_RANGE, RANGE_8G)
      && io_.writeReg(ACCEL_I2C_ADDR, REG_PMU_BW, BW_500HZ)
      && io_.writeReg(ACCEL_I2C_ADDR, REG_ACCD_HBW, 0x00)           // filtered data, shadowing on
      && io_.writeReg(ACCEL_I2C_ADDR, REG_FIFO_CONFIG1, FIFO_STREAM_XYZ);   // also clears the FIFO
}

void AccelDriver::lose(uint32_t nowMs) {
  if (state_ == ST_OK) events_ |= EV_LOST;
  state_ = ST_MISSING;
  nextAt_ = nowMs + ACCEL_REPROBE_MS;
  io_.recover();
}

uint16_t AccelDriver::service(uint32_t nowMs, AccelSampleCb cb) {
  switch (state_) {
    case ST_MISSING:
      if (!reached(nowMs, nextAt_)) return 0;
      state_ = ST_PROBE;
      // fall through
    case ST_PROBE: {
      if (!reached(nowMs, nextAt_)) return 0;
      probes_++;
      uint8_t id = 0;
      if (!io_.readRegs(ACCEL_I2C_ADDR, REG_CHIPID, &id, 1) || id != ACCEL_CHIP_ID ||
          !io_.writeReg(ACCEL_I2C_ADDR, REG_SOFTRESET, 0xB6)) {
        state_ = ST_MISSING;
        nextAt_ = nowMs + ACCEL_REPROBE_MS;
        return 0;
      }
      state_ = ST_RESET_WAIT;
      nextAt_ = nowMs + RESET_WAIT_MS;
      return 0;
    }
    case ST_RESET_WAIT:
      if (!reached(nowMs, nextAt_)) return 0;
      if (!configure()) { state_ = ST_MISSING; nextAt_ = nowMs + ACCEL_REPROBE_MS; io_.recover(); return 0; }
      state_ = ST_OK;
      events_ |= EV_FOUND;
      everOk_ = true;
      failStreak_ = 0;
      lastPoll_ = nowMs;
      lastT_ = nowMs;
      return 0;
    case ST_OK:
      break;
  }

  if (!cb) return 0;
  if ((uint32_t)(nowMs - lastPoll_) < ACCEL_POLL_MS) return 0;
  lastPoll_ = nowMs;

  uint8_t st;
  if (!io_.readRegs(ACCEL_I2C_ADDR, REG_FIFO_STATUS, &st, 1)) {
    errors_++;
    if (++failStreak_ >= ACCEL_FAIL_LIMIT) lose(nowMs);
    return 0;
  }
  if (st == 0xFF) {                       // all-ones: floating bus, no device driving it
    errors_++;
    if (++failStreak_ >= ACCEL_FAIL_LIMIT) lose(nowMs);
    return 0;
  }
  if (st & 0x80) overruns_++;
  uint8_t frames = st & 0x7F;
  if (frames > 32) frames = 32;
  if (frames == 0) { failStreak_ = 0; return 0; }

  // Sample times: the newest frame is "now", older ones are 1 ms apart.
  uint32_t t = nowMs - (uint32_t)(frames - 1) * (1000 / ACCEL_SAMPLE_HZ);
  if ((int32_t)(t - lastT_) <= 0) t = lastT_ + 1;

  uint16_t delivered = 0;
  uint8_t buf[FRAMES_PER_READ * 6];
  while (frames) {
    uint8_t n = frames > FRAMES_PER_READ ? FRAMES_PER_READ : frames;
    if (!io_.readRegs(ACCEL_I2C_ADDR, REG_FIFO_DATA, buf, (uint8_t)(n * 6))) {
      errors_++;
      if (++failStreak_ >= ACCEL_FAIL_LIMIT) lose(nowMs);
      return delivered;
    }
    for (uint8_t i = 0; i < n; i++) {
      const uint8_t* f = &buf[i * 6];
      cb(toCounts(f[0], f[1]), toCounts(f[2], f[3]), toCounts(f[4], f[5]), t);
      lastT_ = t;
      t++;
      delivered++;
    }
    frames -= n;
  }
  failStreak_ = 0;
  return delivered;
}
