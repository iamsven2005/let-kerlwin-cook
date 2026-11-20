#include "ir_plot.h"
#if FEAT_IR_PLOT
#include "../core/state.h"
#include "heading.h"
#include "link.h"

static unsigned long lastSentMs = 0, sent = 0, skippedPose = 0;
static long lastX = 0, lastY = 0;

void irPlotUpdate() {
  if (!irYes || !tracking) return;
  if (handPose == POSE_OTHER) { skippedPose++; return; }   // direction unknown: don't plot
  if (millis() - lastSentMs < IR_PLOT_GAP_MS) return;
  lastSentMs = millis();
  float sensorDir = handPose == POSE_HANG ? headingNow() + hangDirDeg : headingNow();
  float d = (handPose == POSE_HANG ? hangMm : pointMm) + 0.6f * irRangeMm;
  lastX = lround(posX + d * cos(rad(sensorDir)));
  lastY = lround(posY + d * sin(rad(sensorDir)));
  char line[64];
  snprintf(line, sizeof(line), "O,%lu,%ld,%ld,0,%ld\n", millis(), lastX, lastY, lround(wrap180(sensorDir)));
  linkSendLine(line);
  sent++;
}

#if DEBUG_ANY
void irPlotDebug(DebugText& t) {
  t.add("hits sent %lu", sent);
  t.add("skipped (pose) %lu", skippedPose);
  if (sent) t.add("last %ld, %ld", lastX, lastY);
  t.add("pose %s", handPose == POSE_HANG ? "HANG" : handPose == POSE_POINT ? "POINT" : "OTHER");
}
#endif

#endif
