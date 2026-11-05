/*
  tracker - the PERSON's position: steps x step length along headingNow().
  Fills posX, posY, distanceMm, steps, tracking (state.h). Moves on every evStep().
*/
#pragma once
#include "../core/feature.h"

#if FEAT_TRACKER
void trackerOnStep();               // evStep() calls this
void trackerDebug(DebugText& t);
void trackerReset();                // back at the start: (0,0), facing 0
void trackerSetPosition(float x, float y);
void trackerSetTracking(bool on);   // pause / resume step counting
#else
inline void trackerReset() {}
inline void trackerSetPosition(float, float) {}
inline void trackerSetTracking(bool) {}
#endif
