#include "still.h"
#if FEAT_STILL
#include "../core/state.h"

static float varEma = 0;
static unsigned long stillSinceMs = 0, stateSinceMs = 0;

void stillUpdate() {
  if (!accelFresh) return;
  varEma += 0.05f * (dynAcc * dynAcc - varEma);
  unsigned long now = millis();
  if (varEma < STILL_VAR) {
    if (!still && now - stillSinceMs > STILL_MS) { still = true; stateSinceMs = now; }
  } else {
    stillSinceMs = now;
    if (still) stateSinceMs = now;
    still = false;
    movedSinceStill = true;
  }
}

#if DEBUG_ANY
void stillDebug(DebugText& t) {
  t.add("%s", still ? "STILL" : "WALK");
  // variance in micro-g squared as a whole number
  t.add("var %ld lim %ld ug2", lround(varEma * 1e6f), lround(STILL_VAR * 1e6f));
  t.add("for %lu s", (millis() - stateSinceMs) / 1000);
  t.add("moved flag %s", onOff(movedSinceStill));
}
#endif

#endif
