#include "steps.h"
#if FEAT_STEPS
#include "../core/state.h"
#include "../core/events.h"

static float dynSmooth = 0, lastPeak = 0, peak = 0;
static bool aboveThresh = false;
static unsigned long lastStepMs = 0, detected = 0, tooSoon = 0;

// Smooth the dynamic acceleration and count a step each time it rises above the
// threshold and comes back down, no faster than STEP_MIN_MS apart.
void stepsUpdate() {
  if (!accelFresh) return;
  dynSmooth += 0.3f * (dynAcc - dynSmooth);
  if (aboveThresh && dynSmooth > peak) peak = dynSmooth;
  if (!aboveThresh && dynSmooth > stepThreshG) { aboveThresh = true; peak = dynSmooth; }
  else if (aboveThresh && dynSmooth < stepThreshG * 0.3f) {
    aboveThresh = false;
    lastPeak = peak;
    unsigned long now = millis();
    if (now - lastStepMs >= STEP_MIN_MS) {
      lastStepMs = now;
      detected++;
      evStep();
    } else tooSoon++;
  }
}

#if DEBUG_ANY
void stepsDebug(DebugText& t) {
  t.add("steps %lu", detected);
  t.add("sig %ld thr %ld mg", lround(dynSmooth * 1000), lround(stepThreshG * 1000));
  t.add("last peak %ld mg", lround(lastPeak * 1000));
  if (lastStepMs) t.add("last %lu ms ago", millis() - lastStepMs);
  else t.add("no step yet");
  t.add("too soon %lu", tooSoon);
}
#endif

#endif
