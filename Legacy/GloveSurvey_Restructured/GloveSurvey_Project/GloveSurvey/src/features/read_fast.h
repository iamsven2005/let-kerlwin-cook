/*
  read_fast - FAST readings: one every fast_ms while walking. Pauses while a SLOW
  average is being taken, and in SLOW mode.
*/
#pragma once
#include "../core/feature.h"

#if FEAT_READ_FAST
void readFastUpdate();
void readFastDebug(DebugText& t);
#endif
