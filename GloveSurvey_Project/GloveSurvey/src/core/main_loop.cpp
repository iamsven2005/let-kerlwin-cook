#include "main_loop.h"
#include <Wire.h>
#include "feature.h"
#include "debug.h"
#if FEAT_IR
#include <Wireling.h>
#endif

#include "../features/wifi.h"
#include "../features/link.h"
#include "../features/buttons.h"
#include "../features/accel.h"
#include "../features/steps.h"
#include "../features/still.h"
#include "../features/hand_pose.h"
#include "../features/heading.h"
#include "../features/tracker.h"
#include "../features/step_cal.h"
#include "../features/ir_sensor.h"
#include "../features/ir_plot.h"
#include "../features/read_slow.h"
#include "../features/read_fast.h"
#include "../features/roaming.h"
#include "../features/read_cache.h"
#include "../features/telemetry.h"
#include "../features/commands.h"
#include "../features/screen.h"

// =====================================================================
//  The feature table. Rows run in this order on every pass of loop().
//  To switch a feature off, use features.h - not this table.
// =====================================================================
static const Feature FEATURES[] = {
#if FEAT_WIFI
  { "wifi",      wifiFeatureBegin, wifiFeatureUpdate, 0,               DBG(wifiDebug) },
#endif
#if FEAT_LINK_SERIAL
  { "usb",       nullptr,          linkSerialUpdate,  0,               DBG(linkSerialDebug) },
#endif
#if FEAT_LINK_UDP
  { "udp",       nullptr,          linkUdpUpdate,     0,               DBG(linkUdpDebug) },
#endif
#if FEAT_BUTTONS
  { "buttons",   nullptr,          buttonsUpdate,     0,               DBG(buttonsDebug) },
#endif
#if FEAT_ACCEL
  { "accel",     accelBegin,       accelUpdate,       0,               DBG(accelDebug) },   // 50 Hz inside
#endif
#if FEAT_STEPS
  { "steps",     nullptr,          stepsUpdate,       0,               DBG(stepsDebug) },
#endif
#if FEAT_STILL
  { "still",     nullptr,          stillUpdate,       0,               DBG(stillDebug) },
#endif
#if FEAT_HAND_POSE
  { "hand",      nullptr,          handPoseUpdate,    0,               DBG(handPoseDebug) },
#endif
  { "heading",   headingBegin,     headingUpdate,     0,               DBG(headingDebug) },
#if FEAT_TRACKER
  { "tracker",   nullptr,          nullptr,           0,               DBG(trackerDebug) },
#endif
#if FEAT_STEP_CAL
  { "stepcal",   nullptr,          nullptr,           0,               DBG(stepCalDebug) },
#endif
#if FEAT_IR
  { "ir",        irBegin,          irUpdate,          IR_PERIOD_MS,    DBG(irDebug) },
#endif
#if FEAT_IR_PLOT
  { "irplot",    nullptr,          irPlotUpdate,      IR_PERIOD_MS,    DBG(irPlotDebug) },
#endif
#if FEAT_READ_SLOW
  { "slow",      nullptr,          readSlowUpdate,    0,               DBG(readSlowDebug) },
#endif
#if FEAT_READ_FAST
  { "fast",      nullptr,          readFastUpdate,    0,               DBG(readFastDebug) },
#endif
#if FEAT_ROAM
  { "roam",      nullptr,          roamingUpdate,     0,               DBG(roamingDebug) },
#endif
#if FEAT_READ_CACHE
  { "cache",     nullptr,          readCacheUpdate,   0,               DBG(readCacheDebug) },
#endif
#if FEAT_TELEMETRY
  { "telemetry", nullptr,          telemetryUpdate,   TELEMETRY_PERIOD_MS, DBG(telemetryDebug) },
#endif
#if FEAT_COMMANDS
  { "commands",  nullptr,          nullptr,           0,               DBG(commandsDebug) },
#endif
#if DEBUG_TO_APP
  { "mirror",    nullptr,          debugMirrorUpdate, 0,               nullptr },
#endif
#if FEAT_SCREEN
  { "screen",    screenBegin,      screenUpdate,      SCREEN_PERIOD_MS, nullptr },
#endif
};

static const int FEATURE_COUNT = sizeof(FEATURES) / sizeof(FEATURES[0]);
static unsigned long lastRunMs[FEATURE_COUNT];

const Feature* featureTable() { return FEATURES; }
int featureCount() { return FEATURE_COUNT; }

void haltWith(const char* message) {
  SerialUSB.println(message);
#if FEAT_SCREEN
  screenMessage(message);
#endif
  while (true) delay(1000);
}

void mainSetup() {
  SerialUSB.begin(115200);
  Wire.begin();
#if FEAT_IR
  Wireling.begin();                     // powers the Wireling ports (IR pair)
#endif
  for (int i = 0; i < FEATURE_COUNT; i++)
    if (FEATURES[i].begin) FEATURES[i].begin();
}

void mainLoop() {
  unsigned long now = millis();
  for (int i = 0; i < FEATURE_COUNT; i++) {
    const Feature& f = FEATURES[i];
    if (!f.update) continue;
    if (f.periodMs && now - lastRunMs[i] < f.periodMs) continue;
    lastRunMs[i] = now;
    f.update();
  }
  linkFlush(false);                     // one packet per pass, spaced out
}
