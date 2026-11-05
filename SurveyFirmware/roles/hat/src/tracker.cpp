#include <SurveyCommon.h>
#if SURVEY_MODE == HAT
#include "tracker.h"
#include "config.h"
#include "heading.h"

float posX = 0, posY = 0, distanceMm = 0;
long  steps = 0;
bool  tracking = true;
static long calSteps = -1;          // steps when calibration started (-1 = not calibrating)

void trackerOnStep() {
  if (!tracking) return;
  steps++;
  if (calSteps >= 0) return;        // calibrating: count only, don't move
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

void trackerSetPosition(float x, float y) { posX = x; posY = y; }

bool trackerCalibrating() { return calSteps >= 0; }

void trackerCalStart() { calSteps = steps; }

bool trackerCalEnd(float mm, char* msg, int msgSize) {
  if (calSteps < 0) { snprintf(msg, msgSize, "Start the step calibration first"); return false; }
  long n = steps - calSteps;
  calSteps = -1;
  if (n < 4 || mm < 1000) { snprintf(msg, msgSize, "Too few steps: walk at least 5 m"); return false; }
  stepMm = mm / n;
  snprintf(msg, msgSize, "Step length %d mm (%ld steps)", (int)lround(stepMm), n);
  return true;
}

#endif // SURVEY_MODE == HAT
