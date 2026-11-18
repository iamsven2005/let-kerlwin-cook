#include "gestures.h"
#include "config.h"

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
  return c;
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
    if (tilting) suppressUntil_ = tMs + cfg_.tapSuppressMs;
    if (offSide && (hx_ * hx_ + hy_ * hy_) > 0.3f * 0.3f) suppressUntil_ = tMs + cfg_.tapSuppressMs;
  } else {
    if (offSide) suppressUntil_ = tMs + cfg_.tapSuppressMs;   // plain brief behaviour
  }

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
  if (dom == 0)      ev = (diag_.firstSign[0] > 0) ? GE_TAP_R : GE_TAP_L;
  else if (dom == 2) ev = GE_TAP_V;
  // dom == 1: knock on the top/bottom edge -> not a protocol event.
  // R/L are in the device frame, also face-down (brief v1.1 Q10).

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
  return ev;
}
