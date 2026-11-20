// Gesture detection: flip and tilt (static gravity) and taps (high-pass spikes).
// Pure C++, no Arduino dependencies, so it can be unit-tested on a PC.
#pragma once
#include <stdint.h>

enum GestureEvent : uint8_t {
  GE_NONE      = 0,
  GE_FLIP_DOWN = 1 << 0,   // F:1
  GE_FLIP_UP   = 1 << 1,   // F:0
  GE_TAP_R     = 1 << 2,   // T:R from a side knock (legacy, only with SIDE_TAPS_ENABLED)
  GE_TAP_L     = 1 << 3,   // T:L from a side knock (legacy, only with SIDE_TAPS_ENABLED)
  GE_TAP_V     = 1 << 4,   // T:V
  GE_TILT_R    = 1 << 5,   // T:R from a tilt (brief v1.7)
  GE_TILT_L    = 1 << 6,   // T:L from a tilt (brief v1.7)
};

// Tilt gesture notes for the debug log (brief v1.7: candidate, arm, commit, cancel with angle).
enum TiltNoteKind : uint8_t { TN_CANDIDATE = 0, TN_ARMED, TN_COMMIT, TN_CANCEL, TN_DROPPED };
enum TiltReason : uint8_t {
  TR_NONE = 0, TR_STEEP, TR_FLIP, TR_TIMEOUT, TR_PITCH, TR_MOVING, TR_SHORT, TR_DIRECTION,
};
const char* tiltNoteName(uint8_t kind);
const char* tiltReasonName(uint8_t reason);
struct TiltNote {
  uint8_t  kind;            // TiltNoteKind
  int8_t   dir;             // -1 left, +1 right
  uint8_t  reason;          // TiltReason (cancel / dropped)
  int16_t  deg;             // roll angle at that moment (degrees)
  int16_t  pitchDeg;
  uint16_t motionMg;        // motion estimate (mg)
  uint32_t tMs;
};

// V-tap reliability counters (brief v1.7 section 3), since boot or the last reset.
struct VStats {
  uint16_t candidates;      // spikes whose vertical part crossed CAL_TAP_V_G
  uint16_t accepted;        // sent as T:V
  uint16_t suppressed;      // rejected: inside a tilt / flip / pickup suppression window
  uint16_t push;            // rejected: smooth push, not an impact (FW-Q26 decay check)
  uint16_t axis;            // rejected: another axis dominated the spike
};

struct GestureConfig {
  float    gPerCount;
  uint8_t  sideAxis;        // 0 = raw X is the left/right axis, 1 = raw Y
  int8_t   xSign;           // sign so that the side axis reads positive when the RIGHT edge is down (tilt)
  int8_t   tapXSign;        // legacy side taps: sign so that a positive first spike = T:R
  int8_t   zSign;           // sign so that face-up = +1 g
  float    tapSideG;
  float    tapVG;
  uint16_t tapRefractoryMs;
  uint8_t  tapWindowMs;
  float    tapBaseAlpha;
  float    flipG;
  uint16_t flipHoldMs;
  float    flipAlpha;
  uint16_t tapSuppressMs;
  float    tiltSuppressG;
  float    fastTiltAlpha;   // 0 disables the tilt detector
  float    tapVDecay;       // V tap must decay below this fraction of its peak by window end (or ring); >= 1 disables
  uint16_t initMs;          // starting orientation = average of the first initMs of samples
  bool     sideTaps;        // legacy side taps instead of tilt (never both)
  // tilt gesture (brief v1.7)
  float    tiltAlpha;       // low-pass for xAvg / yAvg (per sample)
  float    tiltSinMin, tiltSinMax, tiltSinLevel, tiltSinPitch;
  float    tiltFaceUpG;     // zAvg must be above this
  uint16_t tiltHoldMs, tiltReturnMs, tiltSettleMs;
  float    tiltStillG;      // motion must stay below this while the tilt is held (pickup rejection)
  float    tiltSettleStillG;  // ...and below this during the settle at level (a pickup is being lowered)
  float    tiltMotionCancelG;   // motion above this cancels an armed tilt (put-down after carrying)
  float    tiltMotionAlpha;
};

GestureConfig defaultGestureConfig();   // values from config.h

// Diagnostics of the last spike that was classified (for the debug log).
struct TapDiag {
  float    peak[3];         // |high-pass| peak per axis: side, other, vertical (g)
  int8_t   firstSign[3];    // sign of the first threshold crossing per axis
  uint8_t  dominant;        // 0 side, 1 other (ignored), 2 vertical
  bool     suppressed;      // true if dropped because of tilt / flip
  uint8_t  event;           // GestureEvent emitted (GE_NONE if dropped)
  float    vEndRatio;       // |vertical high-pass| at window end / peak
  bool     notImpulse;      // V spike rejected as a smooth push (pickup), not an impact
  uint32_t tMs;
};

class GestureEngine {
 public:
  explicit GestureEngine(const GestureConfig& cfg) : cfg_(cfg), vstats_() { reset(); }
  void reset();

  // Feed one raw sample (12-bit counts, already >>4) taken at time tMs.
  // Returns a bitmask of GestureEvent that fired on this sample.
  uint8_t update(int16_t rawX, int16_t rawY, int16_t rawZ, uint32_t tMs);

  bool  faceDown()    const { return faceDown_; }
  bool  initialised() const { return init_; }
  float zAvg()        const { return zAvg_; }
  bool  suppressing(uint32_t tMs) const {
    return tiltArmedDir() != 0 || (int32_t)(tMs - suppressUntil_) < 0;
  }
  float tiltG2()      const { return hx_ * hx_ + hy_ * hy_; }
  bool  tapDiagReady()      { bool r = diagReady_; diagReady_ = false; return r; }
  const TapDiag& lastTap() const { return diag_; }
  // Tilt gesture state / log
  int8_t tiltArmedDir() const;                        // -1 / +1 while an armed tilt waits for level, else 0
  bool   takeTiltNote(TiltNote& out);                 // oldest pending note, if any
  float  xAvg() const { return xAvg_; }
  float  yAvg() const { return yAvg_; }
  float  motion() const { return motion_; }
  // V-tap counters
  const VStats& vStats() const { return vstats_; }
  void   resetVStats() { vstats_ = VStats(); }

 private:
  void startCapture(const float hp[3], uint32_t tMs);
  uint8_t classify(uint32_t tMs);
  uint8_t updateTilt(const float v[3], uint32_t tMs);
  void note(uint8_t kind, uint8_t reason, uint32_t tMs);
  void tiltCancel(uint8_t reason, uint32_t tMs);

  GestureConfig cfg_;
  bool     init_;
  uint16_t initCount_;
  uint32_t initStart_;
  float    initSum_[3];
  // flip
  float    zAvg_;
  bool     faceDown_;
  bool     flipPending_;
  uint32_t flipSince_;
  // tilt / suppression
  float    hx_, hy_;
  uint8_t  tiltCount_;
  uint32_t suppressUntil_;
  // taps
  float    base_[3];
  bool     capturing_;
  uint32_t captureStart_;
  uint32_t refractoryUntil_;
  bool     captureSuppressed_;
  bool     vReversed_;
  float    lastHp_[3];
  TapDiag  diag_;
  bool     diagReady_;
  VStats   vstats_;
  // tilt gesture
  enum TiltState : uint8_t { TS_NEED_LEVEL, TS_IDLE, TS_CANDIDATE, TS_ARMED, TS_SETTLE };
  float    xAvg_, yAvg_, motion_, mvec_[3];
  uint8_t  tiltState_;
  int8_t   tiltDir_;
  uint32_t tiltSince_, tiltArmedAt_;
  TiltNote notes_[4];
  uint8_t  noteHead_, noteCount_;
};
