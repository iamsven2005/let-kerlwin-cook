/*
  steps - counts steps from the accelerometer (wherever the hand points, the size of the
  acceleration rises and falls with every step). Announces each step with evStep().
*/
#pragma once
#include "../core/feature.h"

#if FEAT_STEPS
void stepsUpdate();
void stepsDebug(DebugText& t);
#endif
