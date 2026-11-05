#include "heading.h"
#if HEADING_SOURCE == HEADING_BUTTONS

static float bodyHeadingDeg = 0;
static float lastTurnDeg = 0;
static unsigned long turns = 0, lastTurnMs = 0;

void headingBegin() {}
void headingUpdate() {}

float headingNow() { return bodyHeadingDeg; }

void headingTurn(float deg) {
  bodyHeadingDeg = wrap180(bodyHeadingDeg + deg);
  lastTurnDeg = deg;
  lastTurnMs = millis();
  turns++;
}

void headingSet(float deg) { bodyHeadingDeg = wrap180(deg); }

#if DEBUG_ANY
void headingDebug(DebugText& t) {
  t.add("facing %ld deg", lround(bodyHeadingDeg));
  t.add("source: buttons");
  t.add("turns %lu", turns);
  if (turns) t.add("last %+ld, %lu s ago", lround(lastTurnDeg), (millis() - lastTurnMs) / 1000);
}
#endif

#endif
