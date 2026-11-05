#include "tracker.h"
#if FEAT_TRACKER
#include "../core/state.h"
#include "heading.h"
#include "step_cal.h"

static unsigned long corrections = 0;

void trackerOnStep() {
  if (!tracking) return;
  steps++;
  if (stepCalActive()) return;      // calibrating: count only, don't move
  float h = rad(headingNow());
  posX += stepMm * cos(h);
  posY += stepMm * sin(h);
  distanceMm += stepMm;
}

void trackerReset() {
  posX = posY = distanceMm = 0;
  steps = 0;
  headingSet(0);
}

void trackerSetPosition(float x, float y) { posX = x; posY = y; corrections++; }
void trackerSetTracking(bool on) { tracking = on; }

#if DEBUG_ANY
void trackerDebug(DebugText& t) {
  t.add("x %ld y %ld mm", lround(posX), lround(posY));
  t.add("walked %ld mm", lround(distanceMm));
  t.add("steps %ld x %ld mm", steps, lround(stepMm));
  t.add("tracking %s", onOff(tracking));
  t.add("corrections %lu", corrections);
}
#endif

#endif
