/*
  still - standing still vs walking: movement variance below STILL_VAR for STILL_MS.
  Fills still and movedSinceStill (state.h).
*/
#pragma once
#include "../core/feature.h"

#if FEAT_STILL
void stillUpdate();
void stillDebug(DebugText& t);
#endif
