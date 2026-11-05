/*
  readings.h - Wi-Fi signal readings, FAST / SLOW, and the cache that keeps every reading
  until the app confirms it (R,seq,... -> A,seq).
    FAST: a reading every fast_ms while walking.
    SLOW: stand still ~1 s (or press MEASURE HERE): 20 samples averaged into one reading,
          then it waits until you move on before averaging again.
*/
#pragma once
#include <Arduino.h>

extern bool slowMode;
extern bool measuring;
extern int  shownRssi;          // last value shown on the screen (0 = none yet)
extern int  currentRssi;        // latest raw reading (for roaming)
extern int  cacheCount;         // readings waiting for the app

void readingsUpdate();          // call every loop
void readingsService();         // send the oldest unconfirmed reading
void readingsAck(uint16_t seq); // the app confirmed a reading
void readingsMeasureHere();
void readingsResetAverage();
