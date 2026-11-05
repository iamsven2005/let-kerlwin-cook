/*
  read_slow - SLOW readings: stand still ~1 s (or press MEASURE HERE): slow_n samples,
  slow_ms apart, averaged into one reading. Then it waits until you move on before
  averaging again. MEASURE HERE works in FAST mode too.
  Fills slowMode, measuring, averaging (state.h).
*/
#pragma once
#include "../core/feature.h"

#if FEAT_READ_SLOW
void readSlowUpdate();
void readSlowDebug(DebugText& t);
void readingsMeasureHere();
void readingsResetAverage();
bool readingsSetSlow(bool slow);     // pick SLOW (true) or FAST mode
#else
inline void readingsMeasureHere() {}
inline void readingsResetAverage() {}
inline bool readingsSetSlow(bool slow) { return !slow; }   // only FAST exists
#endif
