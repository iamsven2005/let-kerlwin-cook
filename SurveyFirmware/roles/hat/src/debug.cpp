#include <SurveyCommon.h>
#if SURVEY_MODE == HAT
#include "debug.h"
#include "link.h"
#include "tracker.h"
#include "heading.h"
#include "motion.h"
#include "ir_sensor.h"
#include "hand_pose.h"
#include "readings.h"

unsigned long debugMirrorIntervalMs = 2000;
static bool mirrorOn = false;
static int page = -1;
static unsigned long roundStart = 0;
static bool started = false;

void debugSendFeatures() {
  linkSendLine("F,tracker,motion,ir,hand_pose,readings,link,heading=buttons\n");
}

void debugMirrorSet(bool on) {
  mirrorOn = on;
  page = -1;
  started = false;
}

// One bounded page per loop; never force a packet flush for debug traffic.
void debugMirrorUpdate() {
  if (!mirrorOn) return;
  if (page < 0) {
    unsigned long now = millis();
    if (started && now - roundStart < debugMirrorIntervalMs) return;
    started = true;
    roundStart = now;
    page = 0;
  }
  char line[128];
  switch (page) {
    case 0: snprintf(line, sizeof(line), "D,tracker,x %ld mm|y %ld mm|heading %ld deg|steps %ld\n", lround(posX), lround(posY), lround(headingNow()), steps); break;
    case 1: snprintf(line, sizeof(line), "D,motion,still %s|tracking %s\n", still ? "yes" : "no", tracking ? "on" : "off"); break;
    case 2: snprintf(line, sizeof(line), "D,ir,detected %s\n", irYes ? "yes" : "no"); break;
    case 3: snprintf(line, sizeof(line), "D,hand_pose,pose %s\n", handPoseName()); break;
    case 4: snprintf(line, sizeof(line), "D,readings,mode %s|RSSI %d|queued %d\n", slowMode ? "SLOW" : "FAST", shownRssi, cacheCount); break;
    default: snprintf(line, sizeof(line), "D,link,app %s\n", linkAppFound() ? "found" : "not found"); break;
  }
  if (linkSendLowPriority(line) && ++page >= 6) page = -1;
}
#endif
