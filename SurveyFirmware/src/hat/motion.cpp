#include "../common/mode.h"
#if SURVEY_MODE == HAT
#include "motion.h"
#include "config.h"
#include "../common/accel_bma250.h"
#include "tracker.h"

float gravX = 0, gravY = 0, gravZ = 1;
bool still = true, movedSinceStill = false;

static float magMean = 1, dynSmooth = 0, varEma = 0;
static bool aboveThresh = false;
static unsigned long lastStepMs = 0, stillSinceMs = 0;

// The size of the acceleration rises and falls with every step (wherever the hand
// points). Take away the slow average (gravity), smooth it, and count a step each time
// it rises above the threshold and comes back down, no faster than STEP_MIN_MS apart.
void motionUpdate() {
  float ax, ay, az;
  if (!bmaRead(ax, ay, az)) return;
  float mag = sqrt(ax * ax + ay * ay + az * az);
  magMean += 0.02f * (mag - magMean);
  float dyn = mag - magMean;
  dynSmooth += 0.3f * (dyn - dynSmooth);
  varEma += 0.05f * (dyn * dyn - varEma);

  gravX += 0.05f * (ax - gravX);
  gravY += 0.05f * (ay - gravY);
  gravZ += 0.05f * (az - gravZ);

  unsigned long now = millis();
  if (varEma < STILL_VAR) {
    if (!still && now - stillSinceMs > STILL_MS) still = true;
  } else {
    stillSinceMs = now;
    still = false;
    movedSinceStill = true;
  }

  if (!aboveThresh && dynSmooth > stepThreshG) aboveThresh = true;
  else if (aboveThresh && dynSmooth < stepThreshG * 0.3f) {
    aboveThresh = false;
    if (now - lastStepMs >= STEP_MIN_MS) {
      lastStepMs = now;
      trackerOnStep();
    }
  }
}

#endif // SURVEY_MODE == HAT
