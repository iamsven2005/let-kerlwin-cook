#include "step_cal.h"
#if FEAT_STEP_CAL
#include "../core/state.h"

static long calSteps = -1;          // steps when calibration started (-1 = not calibrating)
static long lastCalSteps = 0;
static float lastCalMm = 0;

bool stepCalActive() { return calSteps >= 0; }
void stepCalStart() { calSteps = steps; }

bool stepCalEnd(float mm, char* msg, int msgSize) {
  if (calSteps < 0) { snprintf(msg, msgSize, "Start the step calibration first"); return false; }
  long n = steps - calSteps;
  calSteps = -1;
  if (n < 4 || mm < 1000) { snprintf(msg, msgSize, "Too few steps: walk at least 5 m"); return false; }
  stepMm = mm / n;
  lastCalSteps = n;
  lastCalMm = mm;
  snprintf(msg, msgSize, "Step length %d mm (%ld steps)", (int)lround(stepMm), n);
  return true;
}

#if DEBUG_ANY
void stepCalDebug(DebugText& t) {
  if (stepCalActive()) { t.add("CALIBRATING"); t.add("steps so far %ld", steps - calSteps); }
  else t.add("idle");
  t.add("step %ld mm", lround(stepMm));
  if (lastCalSteps) t.add("last: %ld st / %ld mm", lastCalSteps, lround(lastCalMm));
}
#endif

#endif
