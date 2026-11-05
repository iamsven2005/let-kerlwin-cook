#include "commands.h"
#if FEAT_COMMANDS
#include "../core/state.h"
#include "../core/debug.h"
#include "heading.h"
#include "tracker.h"
#include "step_cal.h"
#include "hand_pose.h"
#include "read_slow.h"
#include "link.h"
#include "wifi.h"

static long lastCmdId = -1;
static char lastAck[56] = "";
static char lastCmd[16] = "";
static unsigned long handled = 0, unknown = 0;

#define OFF(name) (name " is off in this build")

// The F line: every feature compiled in, in table order
static void sendFeatureLine() {
  char line[200];
  int n = snprintf(line, sizeof(line), "F");
  for (int i = 0; i < featureCount() && n < (int)sizeof(line) - 24; i++)
    n += snprintf(line + n, sizeof(line) - n, ",%s", featureTable()[i].name);
#if HEADING_SOURCE == HEADING_BUTTONS
  n += snprintf(line + n, sizeof(line) - n, ",heading=buttons");
#else
  n += snprintf(line + n, sizeof(line) - n, ",heading=gyro");
#endif
#if DEBUG_SCREEN
  n += snprintf(line + n, sizeof(line) - n, ",debugview");
#endif
  snprintf(line + n, sizeof(line) - n, "\n");
  linkSendLine(line);
}

// SET,<key>,<value>. False = unknown key, bad value or switched-off feature.
static bool setKey(const char* key, const char* val) {
  float f = atof(val);
  if (!strcmp(key, "step_mm"))  { if (f < 200 || f > 1500) return false; stepMm = f; return true; }
  if (!strcmp(key, "step_g"))   { if (f < 0.03f || f > 1.5f) return false; stepThreshG = f; return true; }
  if (!strcmp(key, "ir_range")) { if (f < 20 || f > 2000) return false; irRangeMm = f; return true; }
  if (!strcmp(key, "hang_dir")) { hangDirDeg = wrap180(f); return true; }
  if (!strcmp(key, "hang_mm"))  { if (f < 0 || f > 1000) return false; hangMm = f; return true; }
  if (!strcmp(key, "point_mm")) { if (f < 0 || f > 1500) return false; pointMm = f; return true; }
#if FEAT_WIFI
  // Network from the app: SET,ssid,...  SET,user,... (or empty)  SET,pass,...  then SET,join,1
  if (!strcmp(key, "ssid")) { if (strlen(val) < 1 || strlen(val) > 32) return false; strcpy(wifiSsid, val); wifiManual = true; return true; }
  if (!strcmp(key, "user")) { if (strlen(val) > 20) return false; strcpy(wifiUser, val); wifiManual = true; return true; }
  if (!strcmp(key, "pass")) { size_t n = strlen(val); if (n && (n < 8 || n > 63)) return false; strcpy(wifiPass, val); wifiManual = true; return true; }
  if (!strcmp(key, "join")) { wifiReconnectWanted = true; return true; }
#endif
  if (!strcmp(key, "fast_ms"))  { fastIntervalMs = constrain((long)f, 50L, 60000L); return true; }
  if (!strcmp(key, "slow_ms"))  { slowIntervalMs = constrain((long)f, 20L, 10000L); return true; }
  if (!strcmp(key, "slow_n"))   { slowSamples = constrain((int)f, 1, 200); readingsResetAverage(); return true; }
  if (!strcmp(key, "ack_ms"))   { ackTimeoutMs = constrain((long)f, 50L, 10000L); return true; }
#if FEAT_ROAM
  if (!strcmp(key, "roam_dbm")) { roamTriggerDbm = (int)f; return true; }
  if (!strcmp(key, "roam_margin"))   { roamMarginDb = (int)f; return true; }
  if (!strcmp(key, "roam_check_ms")) { roamCheckMs = (unsigned long)f; return true; }
  if (!strcmp(key, "roam_cool_ms"))  { roamCooldownMs = (unsigned long)f; return true; }
#endif
  if (!strcmp(key, "mode")) return readingsSetSlow(!strcmp(val, "S"));
  if (!strcmp(key, "dbg_ms"))   { dbgMirrorMs = constrain((long)f, 200L, 60000L); return true; }
  return false;
}

static const char* debugCommand(const char* a1) {
#if DEBUG_SCREEN
  if (!strcmp(a1, "ON"))   { debugViewSet(true); return "Debug view on"; }
  if (!strcmp(a1, "OFF"))  { debugViewSet(false); return "Debug view off"; }
  if (!strcmp(a1, "NEXT")) { debugStep(+1); return debugPageName(); }
  if (!strcmp(a1, "PREV")) { debugStep(-1); return debugPageName(); }
  if (!strcmp(a1, "HOLD")) { debugHold(); return "Debug page held"; }
  if (!strcmp(a1, "AUTO")) { debugAutoCycle(); return "Debug pages cycling"; }
  return debugGoto(a1) ? "Debug page shown" : "No debug page with that name";
#else
  (void)a1;
  return OFF("Debug view");
#endif
}

static const char* runCommand(char* cmd, char* a1, char* a2, char* a3) {
  static char msg[56];
  (void)msg;
  if (!strcmp(cmd, "HELLO"))    { sendFeatureLine(); return "Hello from the glove"; }
  if (!strcmp(cmd, "FEATURES")) { sendFeatureLine(); return "Features listed"; }
#if FEAT_TRACKER
  if (!strcmp(cmd, "RESET")) { trackerReset(); return "Position reset: this is the start"; }
  if (!strcmp(cmd, "SETPOSE") && a1 && a2) {
    trackerSetPosition(atof(a1), atof(a2));
    if (a3) headingSet(atof(a3));
    return "Position set";
  }
  if (!strcmp(cmd, "PAUSE")) { trackerSetTracking(false); return "Tracking paused"; }
  if (!strcmp(cmd, "RUN"))   { trackerSetTracking(true); return "Tracking on"; }
#else
  if (!strcmp(cmd, "RESET") || !strcmp(cmd, "SETPOSE") || !strcmp(cmd, "PAUSE") || !strcmp(cmd, "RUN")) return OFF("Position tracker");
#endif
  if (!strcmp(cmd, "TURN") && a1) { headingTurn(atof(a1)); return "Turned"; }
#if FEAT_READ_SLOW
  if (!strcmp(cmd, "MODE") && a1) { readingsSetSlow(!strcmp(a1, "SLOW")); return slowMode ? "SLOW mode" : "FAST mode"; }
  if (!strcmp(cmd, "MEASURE")) { readingsMeasureHere(); return "Measuring here: hold still"; }
#else
  if (!strcmp(cmd, "MODE") || !strcmp(cmd, "MEASURE")) return OFF("SLOW readings");
#endif
#if FEAT_ROAM
  if (!strcmp(cmd, "ROAM")) { roamRequested = true; return "Scanning for a better AP"; }
  if (!strcmp(cmd, "AUTOROAM") && a1) { autoRoam = !strcmp(a1, "ON"); return autoRoam ? "Auto roaming on" : "Auto roaming off"; }
#else
  if (!strcmp(cmd, "ROAM") || !strcmp(cmd, "AUTOROAM")) return OFF("Roaming");
#endif
#if FEAT_STEP_CAL
  if (!strcmp(cmd, "CALSTEP_START")) { stepCalStart(); return "Step calibration: walk the measured distance"; }
  if (!strcmp(cmd, "CALSTEP_END") && a1) { stepCalEnd(atof(a1), msg, sizeof(msg)); return msg; }
#else
  if (!strncmp(cmd, "CALSTEP", 7)) return OFF("Step calibration");
#endif
#if FEAT_HAND_POSE
  if (!strcmp(cmd, "POSECAL") && a1) {
    if (!handPoseCalibrate(a1)) return "POSECAL needs HANG or POINT";
    snprintf(msg, sizeof(msg), "%s pose stored", a1);
    return msg;
  }
#else
  if (!strcmp(cmd, "POSECAL")) return OFF("Hand pose");
#endif
  if (!strcmp(cmd, "DEBUG") && a1) return debugCommand(a1);
#if DEBUG_TO_APP
  if (!strcmp(cmd, "MIRROR") && a1) { debugMirrorSet(!strcmp(a1, "ON")); return debugMirrorOn() ? "Debug to app on" : "Debug to app off"; }
#else
  if (!strcmp(cmd, "MIRROR")) return OFF("Debug to app");
#endif
  unknown++;
  return "Unknown command";
}

void commandsHandleLine(char* line) {
  char* tag = strtok(line, ",\r\n");
  if (!tag || strcmp(tag, "C")) return;
  char* idStr = strtok(nullptr, ",\r\n");
  char* cmd = strtok(nullptr, ",\r\n");
  if (!idStr || !cmd) return;
  long id = atol(idStr);
  strncpy(lastCmd, cmd, sizeof(lastCmd) - 1);
  if (!strcmp(cmd, "SET")) {
    char* rest = strtok(nullptr, "\r\n");          // the value may contain commas
    if (id != lastCmdId) {
      lastCmdId = id;
      handled++;
      char* comma = rest ? strchr(rest, ',') : nullptr;
      if (!comma) strcpy(lastAck, "Setting needs a key and a value");
      else { *comma = 0; snprintf(lastAck, sizeof(lastAck), setKey(rest, comma + 1) ? "Set %s" : "Bad setting %s", rest); }
    }
  } else {
    char* a1 = strtok(nullptr, ",\r\n");
    char* a2 = strtok(nullptr, ",\r\n");
    char* a3 = strtok(nullptr, ",\r\n");
    if (id != lastCmdId) {                          // the app may resend: act only once
      lastCmdId = id;
      handled++;
      strncpy(lastAck, runCommand(cmd, a1, a2, a3), sizeof(lastAck) - 1);
    }
  }
  char reply[80];
  snprintf(reply, sizeof(reply), "K,%ld,%s\n", id, lastAck);
  linkSendLine(reply);
}

#if DEBUG_ANY
void commandsDebug(DebugText& t) {
  t.add("handled %lu", handled);
  t.add("unknown %lu", unknown);
  t.add("last %s", lastCmd[0] ? lastCmd : "-");
  t.add("%s", lastAck);
}
#endif

#endif
