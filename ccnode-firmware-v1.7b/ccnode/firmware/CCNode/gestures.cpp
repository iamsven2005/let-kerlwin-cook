#include "gestures.h"
#include "config.h"
#include <math.h>

static inline float fabs_(float v) { return v < 0 ? -v : v; }
static inline float clampG(float v) { return v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v); }
static const uint8_t TILT_PERSIST_MS = 8;   // samples at 1 kHz
static inline bool timeReached(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }

GestureConfig defaultGestureConfig() {
  GestureConfig c;
  c.gPerCount       = G_PER_COUNT;
  c.sideAxis        = CAL_SIDE_AXIS;
  c.xSign           = CAL_X_SIGN;
  c.zSign           = CAL_Z_SIGN;
  c.tapSideG        = CAL_TAP_SIDE_G;
  c.tapVG           = CAL_TAP_V_G;
  c.tapRefractoryMs = CAL_TAP_REFRACTORY_MS;
  c.tapWindowMs     = CAL_TAP_WINDOW_MS;
  c.tapBaseAlpha    = CAL_TAP_BASE_ALPHA;
  c.flipG           = CAL_FLIP_G;
  c.flipHoldMs      = CAL_FLIP_HOLD_MS;
  c.flipAlpha       = CAL_FLIP_ALPHA;
  c.tapSuppressMs   = CAL_TAP_SUPPRESS_MS;
  c.tiltSuppressG   = CAL_TILT_SUPPRESS_G;
  c.fastTiltAlpha   = FAST_TILT_ALPHA;
  c.tapVDecay       = CAL_TAP_V_DECAY;
  c.initMs          = GESTURE_INIT_MS;
  c.tapXSign        = CAL_TAP_X_SIGN;
  c.sideTaps        = SIDE_TAPS_ENABLED;
  const float d2r = 3.14159265f / 180.0f;
  c.tiltAlpha       = CAL_TILT_ALPHA;
  c.tiltSinMin      = sinf(CAL_TILT_MIN_DEG * d2r);
  c.tiltSinMax      = sinf(CAL_TILT_MAX_DEG * d2r);
  c.tiltSinLevel    = sinf(CAL_TILT_LEVEL_DEG * d2r);
  c.tiltSinPitch    = sinf(CAL_TILT_MAX_PITCH_DEG * d2r);
  c.tiltFaceUpG     = CAL_TILT_FACEUP_G;
  c.tiltHoldMs      = CAL_TILT_HOLD_MS;
  c.tiltReturnMs    = CAL_TILT_RETURN_MS;
  c.tiltSettleMs    = CAL_TILT_SETTLE_MS;
  c.tiltStillG      = CAL_TILT_STILL_G;
  c.tiltSettleStillG = CAL_TILT_SETTLE_STILL_G;
  c.tiltMotionCancelG = CAL_TILT_MOTION_CANCEL_G;
  c.tiltMotionAlpha = CAL_TILT_MOTION_ALPHA;
  return c;
}

const char* tiltNoteName(uint8_t k) {
  static const char* const n[] = { "candidate", "ARMED", "COMMIT", "CANCEL", "dropped" };
  return k < 5 ? n[k] : "?";
}
const char* tiltReasonName(uint8_t r) {
  static const char* const n[] = { "", "too steep", "flip", "no return in time", "pitch (not a roll)",
                                   "moving (handling)", "held too short", "direction changed" };
  return r < 8 ? n[r] : "?";
}

void GestureEngine::reset() {
  init_ = false;
  initCount_ = 0;
  initStart_ = 0;
  initSum_[0] = initSum_[1] = initSum_[2] = 0;
  zAvg_ = 1.0f;
  faceDown_ = false;
  flipPending_ = false;
  flipSince_ = 0;
  hx_ = hy_ = 0;
  tiltCount_ = 0;
  suppressUntil_ = 0;
  base_[0] = base_[1] = base_[2] = 0;
  capturing_ = false;
  captureStart_ = 0;
  refractoryUntil_ = 0;
  captureSuppressed_ = false;
  vReversed_ = false;
  lastHp_[0] = lastHp_[1] = lastHp_[2] = 0;
  diagReady_ = false;
  diag_ = TapDiag();
  xAvg_ = yAvg_ = 0;
  motion_ = 0;
  mvec_[0] = mvec_[1] = 0; mvec_[2] = 1;
  tiltState_ = TS_NEED_LEVEL;
  tiltDir_ = 0;
  tiltSince_ = tiltArmedAt_ = 0;
  noteHead_ = noteCount_ = 0;
  // vstats_ survive a reset (accelerometer recovery); cleared with resetVStats()
}

uint8_t GestureEngine::update(int16_t rawX, int16_t rawY, int16_t rawZ, uint32_t tMs) {
  // Map raw axes to node axes: v[0] = side axis (+ = towards T:R spike),
  // v[1] = other horizontal axis, v[2] = vertical (+1 g face-up).
  const float g = cfg_.gPerCount;
  float ax = rawX * g, ay = rawY * g;
  float v[3];
  v[0] = (cfg_.sideAxis == 0 ? ax : ay) * cfg_.xSign;
  v[1] = (cfg_.sideAxis == 0 ? ay : ax);
  v[2] = rawZ * g * cfg_.zSign;

  if (!init_) {
    // Brief v1.4 A1: take the starting orientation from the average of the
    // first ~200 ms of samples (not from a hard-coded value or one sample), so a
    // node that boots face-down is DOWN at once and a knock at boot can't fool it.
    if (initCount_ == 0) initStart_ = tMs;
    for (int i = 0; i < 3; i++) initSum_[i] += v[i];
    initCount_++;
    if (tMs - initStart_ < cfg_.initMs) return GE_NONE;
    init_ = true;
    float m[3];
    for (int i = 0; i < 3; i++) m[i] = initSum_[i] / initCount_;
    zAvg_ = m[2];
    faceDown_ = m[2] < 0;
    hx_ = clampG(m[0]); hy_ = clampG(m[1]);
    for (int i = 0; i < 3; i++) mvec_[i] = m[i];
    xAvg_ = clampG(m[0]); yAvg_ = clampG(m[1]);
    for (int i = 0; i < 3; i++) base_[i] = m[i];
    suppressUntil_ = tMs + cfg_.tapSuppressMs;   // settle period after boot
    refractoryUntil_ = tMs;
    return GE_NONE;
  }

  uint8_t ev = GE_NONE;

  // ---------------- Flip (brief section 2) ----------------
  zAvg_ += cfg_.flipAlpha * (v[2] - zAvg_);
  bool looksDown = zAvg_ < -cfg_.flipG;
  bool looksUp   = zAvg_ >  cfg_.flipG;
  if ((looksDown && !faceDown_) || (looksUp && faceDown_)) {
    if (!flipPending_) { flipPending_ = true; flipSince_ = tMs; }
    if (tMs - flipSince_ >= cfg_.flipHoldMs) {
      faceDown_ = looksDown;
      flipPending_ = false;
      ev |= faceDown_ ? GE_FLIP_DOWN : GE_FLIP_UP;
      suppressUntil_ = tMs + cfg_.tapSuppressMs;     // through commit (+window)
    }
  } else {
    flipPending_ = false;
  }

  // ---------------- Tap suppression around orientation changes ----------------
  // zAvg off its committed side (in the band, or pending on the other side)
  bool offSide = faceDown_ ? !looksDown : !looksUp;
  bool tilting = false;
  if (cfg_.fastTiltAlpha > 0) {
    // Brief v1.1 Q9 option C: fast horizontal-gravity detector. A flip always
    // rotates gravity through the X/Y plane, so this fires early in a flip but
    // not during a straight vertical tap onto the table.
    // Inputs are clamped to +-1 g (static gravity can't exceed that) so a hard
    // side knock can't look like a tilt, and the tilt must last TILT_PERSIST_MS.
    hx_ += cfg_.fastTiltAlpha * (clampG(v[0]) - hx_);
    hy_ += cfg_.fastTiltAlpha * (clampG(v[1]) - hy_);
    if ((hx_ * hx_ + hy_ * hy_) > cfg_.tiltSuppressG * cfg_.tiltSuppressG) {
      if (tiltCount_ < 255) tiltCount_++;
    } else {
      tiltCount_ = 0;
    }
    tilting = tiltCount_ >= TILT_PERSIST_MS;
    // Brief v1.1 Q13 option A: with the tilt detector on, zAvg leaving its side
    // only counts while the node is also tilted (> 0.3 g): a lift-and-tap onto
    // the table pulls zAvg below 0.7 g too, and must not be suppressed.
    // This also drops a T:V within ~300 ms after a tilt; accepted as is (v1.7 FW-Q37 A).
    if (tilting) suppressUntil_ = tMs + cfg_.tapSuppressMs;
    if (offSide && (hx_ * hx_ + hy_ * hy_) > 0.3f * 0.3f) suppressUntil_ = tMs + cfg_.tapSuppressMs;
  } else {
    if (offSide) suppressUntil_ = tMs + cfg_.tapSuppressMs;   // plain brief behaviour
  }

  // ---------------- Tilt gesture (brief v1.7), replaces side taps ----------------
  if (!cfg_.sideTaps) ev |= updateTilt(v, tMs);

  // ---------------- Taps (brief section 3) ----------------
  float hp[3];
  for (int i = 0; i < 3; i++) {
    hp[i] = v[i] - base_[i];
    base_[i] += cfg_.tapBaseAlpha * (v[i] - base_[i]);
  }

  if (capturing_) {
    const float thr[3] = { cfg_.tapSideG, cfg_.tapSideG, cfg_.tapVG };
    for (int i = 0; i < 3; i++) {
      float a = fabs_(hp[i]);
      if (a > diag_.peak[i]) diag_.peak[i] = a;
      if (diag_.firstSign[i] == 0 && a > thr[i]) diag_.firstSign[i] = hp[i] > 0 ? 1 : -1;
    }
    if (suppressing(tMs)) captureSuppressed_ = true;
    if (diag_.firstSign[2] != 0 && hp[2] * diag_.firstSign[2] < 0) vReversed_ = true;
    for (int i = 0; i < 3; i++) lastHp_[i] = hp[i];
    if (tMs - captureStart_ >= cfg_.tapWindowMs) ev |= classify(tMs);
  } else if (timeReached(tMs, refractoryUntil_)) {
    if (fabs_(hp[0]) > cfg_.tapSideG || fabs_(hp[1]) > cfg_.tapSideG || fabs_(hp[2]) > cfg_.tapVG) {
      startCapture(hp, tMs);
      if (cfg_.tapWindowMs == 0) ev |= classify(tMs);
    }
  }
  return ev;
}

void GestureEngine::startCapture(const float hp[3], uint32_t tMs) {
  const float thr[3] = { cfg_.tapSideG, cfg_.tapSideG, cfg_.tapVG };
  capturing_ = true;
  captureStart_ = tMs;
  captureSuppressed_ = suppressing(tMs);
  for (int i = 0; i < 3; i++) {
    float a = fabs_(hp[i]);
    diag_.peak[i] = a;
    diag_.firstSign[i] = (a > thr[i]) ? (hp[i] > 0 ? 1 : -1) : 0;
  }
  refractoryUntil_ = tMs + cfg_.tapRefractoryMs;   // refractory counts from the spike start
  vReversed_ = false;
  diag_.notImpulse = false;
  for (int i = 0; i < 3; i++) lastHp_[i] = hp[i];
}

uint8_t GestureEngine::classify(uint32_t tMs) {
  capturing_ = false;
  const float thr[3] = { cfg_.tapSideG, cfg_.tapSideG, cfg_.tapVG };
  uint8_t dom = 0;
  float best = -1;
  for (uint8_t i = 0; i < 3; i++) {
    float score = diag_.peak[i] / thr[i];
    if (score > best) { best = score; dom = i; }
  }
  uint8_t ev = GE_NONE;
  // Side knocks are not reported at all since brief v1.7 (tilt replaces them); the
  // legacy code stays behind SIDE_TAPS_ENABLED. dom == 1 (top/bottom edge): nothing.
  if (dom == 0 && cfg_.sideTaps) ev = (diag_.firstSign[0] * cfg_.tapXSign > 0) ? GE_TAP_R : GE_TAP_L;
  else if (dom == 2) ev = GE_TAP_V;

  // V-tap impulse check (brief v1.3, FW-Q26): an impact rings or decays within the window, a fast
  // lift (pickup) is a smooth push that is still near its peak. Lifts must not
  // produce T:V, because "V then flip" is the Start-sequence gesture.
  diag_.vEndRatio = diag_.peak[2] > 0 ? fabs_(lastHp_[2]) / diag_.peak[2] : 0;
  if (ev == GE_TAP_V && cfg_.tapVDecay < 1.0f && !vReversed_ && diag_.vEndRatio > cfg_.tapVDecay) {
    ev = GE_NONE;
    diag_.notImpulse = true;
  }

  diag_.dominant = dom;
  diag_.suppressed = captureSuppressed_ || suppressing(tMs);
  diag_.tMs = captureStart_;
  if (diag_.suppressed) ev = GE_NONE;
  diag_.event = ev;
  diagReady_ = true;

  // V reliability counters (brief v1.7 section 3): every spike whose vertical part
  // crossed the V threshold is a candidate; each is accepted or rejected for one reason.
  if (diag_.peak[2] > cfg_.tapVG) {
    vstats_.candidates++;
    if (ev == GE_TAP_V)       vstats_.accepted++;
    else if (diag_.suppressed) vstats_.suppressed++;
    else if (dom != 2)         vstats_.axis++;
    else                       vstats_.push++;
  }
  return ev;
}

// ---------------------------------------------------------------------------
// Tilt gesture (brief v1.7 section 1)
//   IDLE -> CANDIDATE when face-up, within the configured roll range, held still
//   CANDIDATE -> ARMED after TILT_HOLD_MS
//   ARMED -> COMMIT (T:L / T:R) when back to level within TILT_RETURN_MS
//   cancel: past TILT_MAX_DEG, flip (zAvg leaves face-up), timeout, pitch, handling motion
//   After a cancel the node must be level again before a new tilt can start (v1.7 FW-Q36).
// ---------------------------------------------------------------------------

int8_t GestureEngine::tiltArmedDir() const {
  return (tiltState_ == TS_ARMED || tiltState_ == TS_SETTLE) ? tiltDir_ : 0;
}

bool GestureEngine::takeTiltNote(TiltNote& out) {
  if (!noteCount_) return false;
  out = notes_[noteHead_];
  noteHead_ = (uint8_t)((noteHead_ + 1) % 4);
  noteCount_--;
  return true;
}

void GestureEngine::note(uint8_t kind, uint8_t reason, uint32_t tMs) {
  TiltNote n;
  n.kind = kind;
  n.dir = tiltDir_;
  n.reason = reason;
  float ax = fabs_(xAvg_), ay = fabs_(yAvg_);
  n.deg = (int16_t)(asinf(ax > 1 ? 1 : ax) * 57.2958f + 0.5f);
  n.pitchDeg = (int16_t)(asinf(ay > 1 ? 1 : ay) * 57.2958f + 0.5f);
  n.motionMg = (uint16_t)(motion_ * 1000.0f);
  n.tMs = tMs;
  if (noteCount_ == 4) { noteHead_ = (uint8_t)((noteHead_ + 1) % 4); noteCount_--; }   // keep the newest
  notes_[(noteHead_ + noteCount_) % 4] = n;
  noteCount_++;
}

void GestureEngine::tiltCancel(uint8_t reason, uint32_t tMs) {
  note(TN_CANCEL, reason, tMs);
  tiltState_ = TS_NEED_LEVEL;
}

uint8_t GestureEngine::updateTilt(const float v[3], uint32_t tMs) {
  // Static-gravity roll (X) and pitch (Y), low-passed like zAvg. Inputs clamped to
  // +-1 g so knocks don't move them much.
  xAvg_ += cfg_.tiltAlpha * (clampG(v[0]) - xAvg_);
  yAvg_ += cfg_.tiltAlpha * (clampG(v[1]) - yAvg_);
  // Motion estimate (handling, not knocks): low-pass the acceleration VECTOR first (inputs clamped
  // per step), then take | |a|^2 - 1 | / 2 ~ | |a| - 1 g |. A knock -- e.g. the lifted edge landing
  // back on the table -- is a short oscillation with ~zero mean and mostly filters out; lifting,
  // lowering and carrying are sustained and pass. ~0 when held still at any angle.
  for (int i = 0; i < 3; i++) {
    // step clamped symmetrically around the filtered value, so a clipped / saturated knock
    // still averages out instead of dragging the estimate one way
    const float lim = TILT_MOTION_CLAMP_G;
    float d = v[i] - mvec_[i];
    d = d > lim ? lim : (d < -lim ? -lim : d);
    mvec_[i] += cfg_.tiltMotionAlpha * d;
  }
  float m2 = mvec_[0] * mvec_[0] + mvec_[1] * mvec_[1] + mvec_[2] * mvec_[2];
  motion_ = fabs_(m2 - 1.0f) * 0.5f;

  const bool faceUp = !faceDown_ && zAvg_ > cfg_.tiltFaceUpG;
  const float ax = fabs_(xAvg_);
  const int8_t dirNow = xAvg_ > 0 ? 1 : -1;          // + = right edge down (CAL_X_SIGN)
  const bool level = ax < cfg_.tiltSinLevel;
  const bool inRange = ax >= cfg_.tiltSinMin && ax <= cfg_.tiltSinMax;
  const bool rollOnly = fabs_(yAvg_) <= cfg_.tiltSinPitch;
  const bool still = motion_ < cfg_.tiltStillG;

  switch (tiltState_) {
    case TS_NEED_LEVEL:
      if (faceUp && level) tiltState_ = TS_IDLE;
      return GE_NONE;

    case TS_IDLE:
      if (!faceUp) { tiltState_ = TS_NEED_LEVEL; return GE_NONE; }
      if (inRange && rollOnly && still) {
        tiltState_ = TS_CANDIDATE;
        tiltDir_ = dirNow;
        tiltSince_ = tMs;
        note(TN_CANDIDATE, TR_NONE, tMs);
      }
      return GE_NONE;

    case TS_CANDIDATE:
      if (!faceUp)          { note(TN_DROPPED, TR_FLIP, tMs);      tiltState_ = TS_NEED_LEVEL; return GE_NONE; }
      if (ax > cfg_.tiltSinMax) { note(TN_DROPPED, TR_STEEP, tMs); tiltState_ = TS_NEED_LEVEL; return GE_NONE; }
      if (!rollOnly)        { note(TN_DROPPED, TR_PITCH, tMs);     tiltState_ = TS_NEED_LEVEL; return GE_NONE; }
      if (!still)           { note(TN_DROPPED, TR_MOVING, tMs);    tiltState_ = TS_NEED_LEVEL; return GE_NONE; }
      if (!inRange || dirNow != tiltDir_) { note(TN_DROPPED, TR_SHORT, tMs); tiltState_ = TS_IDLE; return GE_NONE; }
      if ((uint32_t)(tMs - tiltSince_) >= cfg_.tiltHoldMs) {
        tiltState_ = TS_ARMED;
        tiltArmedAt_ = tMs;
        note(TN_ARMED, TR_NONE, tMs);
      }
      return GE_NONE;

    case TS_ARMED:
    case TS_SETTLE: {
      if (!faceUp)                         { tiltCancel(TR_FLIP, tMs); return GE_NONE; }
      if (ax > cfg_.tiltSinMax)            { tiltCancel(TR_STEEP, tMs); return GE_NONE; }
      if (!rollOnly)                       { tiltCancel(TR_PITCH, tMs); return GE_NONE; }
      if (motion_ > cfg_.tiltMotionCancelG) { tiltCancel(TR_MOVING, tMs); return GE_NONE; }
      if (dirNow != tiltDir_ && !level)    { tiltCancel(TR_DIRECTION, tMs); return GE_NONE; }
      if (tiltState_ == TS_ARMED) {
        if ((uint32_t)(tMs - tiltArmedAt_) > cfg_.tiltReturnMs) { tiltCancel(TR_TIMEOUT, tMs); return GE_NONE; }
        if (!level) return GE_NONE;
        if (cfg_.tiltSettleMs == 0) break;                    // commit now (brief behaviour)
        tiltState_ = TS_SETTLE;                               // optional settle at level
        tiltSince_ = tMs;
        return GE_NONE;
      }
      // TS_SETTLE: level again; a real tilt stays still on the table, a pickup that was
      // levelled in the hand is now being lowered / set down (v1.7 FW-Q36: settle 150 ms by default)
      if (motion_ > cfg_.tiltSettleStillG) { tiltCancel(TR_MOVING, tMs); return GE_NONE; }
      if (!level) { tiltState_ = TS_ARMED; return GE_NONE; }
      if ((uint32_t)(tMs - tiltSince_) < cfg_.tiltSettleMs) return GE_NONE;
      break;
    }
  }
  // commit
  note(TN_COMMIT, TR_NONE, tMs);
  tiltState_ = TS_IDLE;
  return tiltDir_ > 0 ? GE_TILT_R : GE_TILT_L;
}
