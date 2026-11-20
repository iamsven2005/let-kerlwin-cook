#include "read_fast.h"
#if FEAT_READ_FAST
#include "../core/state.h"
#include "read_cache.h"
#include "wifi.h"

static unsigned long lastReadMs = 0, taken = 0, noSignal = 0;

void readFastUpdate() {
  if (slowMode || averaging) return;               // SLOW mode / averaging: no FAST readings
  if (millis() - lastReadMs < fastIntervalMs) return;
  lastReadMs = millis();
  int rssi = wifiReadRssi();
  if (rssi == 0) { noSignal++; return; }
  currentRssi = rssi;
  readingsPush('F', rssi, rssi, rssi);
  shownRssi = rssi;
  taken++;
}

#if DEBUG_ANY
void readFastDebug(DebugText& t) {
  t.add("%s", slowMode ? "off (SLOW mode)" : averaging ? "paused (averaging)" : "reading");
  t.add("last %d dBm", shownRssi);
  t.add("every %lu ms", fastIntervalMs);
  t.add("taken %lu", taken);
  t.add("no signal %lu", noSignal);
}
#endif

#endif
