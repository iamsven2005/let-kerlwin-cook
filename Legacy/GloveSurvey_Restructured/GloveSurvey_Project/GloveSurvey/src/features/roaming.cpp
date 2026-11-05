#include "roaming.h"
#if FEAT_ROAM
#include "../core/state.h"
#include "wifi.h"

void roamingUpdate() {
  if (roamRequested) { roamRequested = false; wifiRoamNow(currentRssi); }
  else if (autoRoam && still && !measuring) wifiMaybeRoam(currentRssi);   // only while standing still
}

#if DEBUG_ANY
void roamingDebug(DebugText& t) {
  static const char* results[] = { "none yet", "stayed", "roamed", "nothing found" };
  t.add("auto %s", onOff(autoRoam));
  t.add("rssi %d trig %d dBm", currentRssi, roamTriggerDbm);
  t.add("next scan in %lu s", wifiMsUntilNextScan() / 1000);
  t.add("scans %lu", wifiScanCount());
  t.add("last: %s", results[wifiLastScanResult()]);
}
#endif

#endif
