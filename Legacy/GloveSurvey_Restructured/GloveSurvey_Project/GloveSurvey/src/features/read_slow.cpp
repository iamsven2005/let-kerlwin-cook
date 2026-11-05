#include "read_slow.h"
#if FEAT_READ_SLOW
#include "../core/state.h"
#include "read_cache.h"
#include "link.h"
#include "wifi.h"

static long sampleSum = 0;
static int sampleCount = 0, sampleMin = 0, sampleMax = -127;
static bool waitForMove = false;     // one average per spot
static unsigned long lastSampleMs = 0, averagesDone = 0;
static int lastAvg = 0, lastLo = 0, lastHi = 0;

void readingsResetAverage() { sampleSum = 0; sampleCount = 0; sampleMin = 0; sampleMax = -127; }
void readingsMeasureHere() { measuring = true; readingsResetAverage(); }

bool readingsSetSlow(bool slow) {
  slowMode = slow;
  measuring = false;
  readingsResetAverage();
  return true;
}

void readSlowUpdate() {
  if (movedSinceStill) { waitForMove = false; movedSinceStill = false; }
  averaging = measuring || (slowMode && still && !waitForMove && tracking);
  if (!averaging) return;
  if (millis() - lastSampleMs < slowIntervalMs) return;
  lastSampleMs = millis();
  int rssi = wifiReadRssi();
  if (rssi == 0) return;
  currentRssi = rssi;
  sampleSum += rssi;
  sampleCount++;
  if (rssi < sampleMin) sampleMin = rssi;
  if (rssi > sampleMax) sampleMax = rssi;
  if (sampleCount >= slowSamples) {
    int avg = (int)((sampleSum - slowSamples / 2) / slowSamples);
    readingsPush('S', avg, sampleMin, sampleMax);
    shownRssi = avg;
    lastAvg = avg; lastLo = sampleMin; lastHi = sampleMax;
    averagesDone++;
    char msg[48];
    snprintf(msg, sizeof(msg), "Measured %d dBm (%d to %d)", avg, sampleMin, sampleMax);
    linkSendMessage(msg);
    readingsResetAverage();
    measuring = false;
    waitForMove = true;
  }
}

#if DEBUG_ANY
void readSlowDebug(DebugText& t) {
  t.add("mode %s%s", slowMode ? "SLOW" : "FAST", measuring ? " +MEASURE" : "");
  t.add("%s", averaging ? "AVERAGING" : waitForMove ? "move on to next spot" : "waiting for still");
  t.add("samples %d / %d", sampleCount, slowSamples);
  t.add("averages %lu", averagesDone);
  if (averagesDone) t.add("last %d (%d..%d)", lastAvg, lastLo, lastHi);
}
#endif

#endif
