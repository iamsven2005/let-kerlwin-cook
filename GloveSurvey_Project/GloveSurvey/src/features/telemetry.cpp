#include "telemetry.h"
#if FEAT_TELEMETRY
#include "../core/state.h"
#include "heading.h"
#include "hand_pose.h"
#include "step_cal.h"
#include "link.h"
#include "wifi.h"

static unsigned long sent = 0;

void telemetryUpdate() {
  char line[170];
  const char* st = !tracking ? "PAUSED" : stepCalActive() ? "CALSTEP" : measuring ? "MEASURE" : still ? "STILL" : "WALK";
#if FEAT_WIFI
  const uint8_t* b = currentBssid;
  int ch = currentChannel;
#else
  static const uint8_t b[6] = {0};
  int ch = 0;
#endif
  snprintf(line, sizeof(line), "P,%lu,%ld,%ld,%ld,%ld,NA,%d,%s,%s,%d,%02X:%02X ch%d,%s,0,0,-1,%ld,%d\n",
           millis(), lround(posX), lround(posY), lround(headingNow()), lround(distanceMm), irYes ? 1 : 0,
           st, slowMode ? "SLOW" : "FAST", (int)lround(stepMm / 10), b[4], b[5], ch,
           handPoseName(), steps, still ? 1 : 0);
  linkSendLine(line);
  sent++;
}

#if DEBUG_ANY
void telemetryDebug(DebugText& t) {
  t.add("P lines %lu", sent);
  t.add("every %u ms", TELEMETRY_PERIOD_MS);
}
#endif

#endif
