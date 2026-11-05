/*
  step_cal - step-length calibration: start, walk a measured distance, finish.
  While it runs, steps are counted but the position doesn't move.
*/
#pragma once
#include "../core/feature.h"

#if FEAT_STEP_CAL
bool stepCalActive();
void stepCalStart();
// Finish over 'mm'. Writes a message for the app; true if the step length changed.
bool stepCalEnd(float mm, char* msg, int msgSize);
void stepCalDebug(DebugText& t);
#else
inline bool stepCalActive() { return false; }
#endif
