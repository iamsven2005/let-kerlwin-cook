// Button debouncer and bit remapping. Pure C++ (host-testable).
#pragma once
#include <stdint.h>

// Protocol bit order (brief section 4): bit0 TL, bit1 TR, bit2 BL, bit3 BR.
// TinyScreen library order: LowerLeft=1<<0, UpperLeft=1<<1, UpperRight=1<<2, LowerRight=1<<3.
inline uint8_t tsButtonsToProtocol(uint8_t ts) {
  uint8_t p = 0;
  if (ts & (1 << 1)) p |= 1 << 0;   // upper-left  -> bit0
  if (ts & (1 << 2)) p |= 1 << 1;   // upper-right -> bit1
  if (ts & (1 << 0)) p |= 1 << 2;   // lower-left  -> bit2
  if (ts & (1 << 3)) p |= 1 << 3;   // lower-right -> bit3
  return p;
}

class ButtonDebouncer {
 public:
  explicit ButtonDebouncer(uint16_t debounceMs) : ms_(debounceMs) {}
  void begin(uint8_t raw, uint32_t now) { stable_ = cand_ = raw; since_ = now; }
  // Returns true when the debounced state changed; read it with state().
  bool update(uint8_t raw, uint32_t now) {
    if (raw != cand_) { cand_ = raw; since_ = now; return false; }
    if (cand_ != stable_ && (uint32_t)(now - since_) >= ms_) { stable_ = cand_; return true; }
    return false;
  }
  uint8_t state() const { return stable_; }
 private:
  uint16_t ms_;
  uint8_t  stable_ = 0, cand_ = 0;
  uint32_t since_ = 0;
};
