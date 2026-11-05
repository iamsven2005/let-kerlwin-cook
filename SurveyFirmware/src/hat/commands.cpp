#include "../common/mode.h"
#if SURVEY_MODE == HAT
#include "commands.h"
#include "config.h"
#include "heading.h"
#include "tracker.h"
#include "hand_pose.h"
#include "readings.h"
#include "link.h"
#include "debug.h"
#include "../common/wifi_logic.h"

bool wifiReconnectWanted = false, roamRequested = false, autoRoam = true;

static long lastCmdId = -1;
static char lastAck[56] = "";

// SET,<key>,<value>. False = unknown key or bad value.
static bool setKey(const char* key, const char* val) {
  float f = atof(val);
  if (!strcmp(key, "step_mm"))  { if (f < 200 || f > 1500) return false; stepMm = f; return true; }
  if (!strcmp(key, "step_g"))   { if (f < 0.03f || f > 1.5f) return false; stepThreshG = f; return true; }
  if (!strcmp(key, "ir_range")) { if (f < 20 || f > 2000) return false; irRangeMm = f; return true; }
  if (!strcmp(key, "hang_dir")) { hangDirDeg = wrap180(f); return true; }
  if (!strcmp(key, "hang_mm"))  { if (f < 0 || f > 1000) return false; hangMm = f; return true; }
  if (!strcmp(key, "point_mm")) { if (f < 0 || f > 1500) return false; pointMm = f; return true; }
  // Network from the app: SET,ssid,...  SET,user,... (or empty)  SET,pass,...  then SET,join,1
  if (!strcmp(key, "ssid")) { if (strlen(val) < 1 || strlen(val) > 32) return false; strcpy(wifiSsid, val); wifiManual = true; return true; }
  if (!strcmp(key, "user")) { if (strlen(val) > 20) return false; strcpy(wifiUser, val); wifiManual = true; return true; }
  if (!strcmp(key, "pass")) { size_t n = strlen(val); if (n && (n < 8 || n > 63)) return false; strcpy(wifiPass, val); wifiManual = true; return true; }
  if (!strcmp(key, "join")) { wifiReconnectWanted = true; return true; }
  if (!strcmp(key, "fast_ms"))  { fastIntervalMs = constrain((long)f, 50L, 60000L); return true; }
  if (!strcmp(key, "slow_ms"))  { slowIntervalMs = constrain((long)f, 20L, 10000L); return true; }
  if (!strcmp(key, "slow_n"))   { slowSamples = constrain((int)f, 1, 200); readingsResetAverage(); return true; }
  if (!strcmp(key, "ack_ms"))   { ackTimeoutMs = constrain((long)f, 50L, 10000L); return true; }
  if (!strcmp(key, "roam_dbm")) { roamTriggerDbm = (int)f; return true; }
  if (!strcmp(key, "roam_margin"))   { roamMarginDb = (int)f; return true; }
  if (!strcmp(key, "roam_check_ms")) { roamCheckMs = (unsigned long)f; return true; }
  if (!strcmp(key, "roam_cool_ms"))  { roamCooldownMs = (unsigned long)f; return true; }
  if (!strcmp(key, "dbg_ms")) { debugMirrorIntervalMs = constrain((long)f, 200L, 60000L); return true; }
  if (!strcmp(key, "mode")) { slowMode = !strcmp(val, "S"); return true; }
  return false;
}

static const char* runCommand(char* cmd, char* a1, char* a2, char* a3) {
  static char msg[56];
  if (!strcmp(cmd, "HELLO")) return "Hello from the glove";
  if (!strcmp(cmd, "FEATURES")) return "Features listed";
  if (!strcmp(cmd, "DEBUG")) return "Debug view is off in this build";
  if (!strcmp(cmd, "MIRROR")) {
    if (!a1 || (strcmp(a1, "ON") && strcmp(a1, "OFF"))) return "MIRROR needs ON or OFF";
    bool on = !strcmp(a1, "ON");
    debugMirrorSet(on);
    return on ? "Debug to app on" : "Debug to app off";
  }
  if (!strcmp(cmd, "RESET")) { trackerReset(); return "Position reset: this is the start"; }
  if (!strcmp(cmd, "TURN") && a1) { headingTurn(atof(a1)); return "Turned"; }
  if (!strcmp(cmd, "SETPOSE") && a1 && a2) {
    trackerSetPosition(atof(a1), atof(a2));
    if (a3) headingSet(atof(a3));
    return "Position set";
  }
  if (!strcmp(cmd, "PAUSE")) { tracking = false; return "Tracking paused"; }
  if (!strcmp(cmd, "RUN"))   { tracking = true; return "Tracking on"; }
  if (!strcmp(cmd, "MODE") && a1) { slowMode = !strcmp(a1, "SLOW"); measuring = false; readingsResetAverage(); return slowMode ? "SLOW mode" : "FAST mode"; }
  if (!strcmp(cmd, "MEASURE")) { readingsMeasureHere(); return "Measuring here: hold still"; }
  if (!strcmp(cmd, "ROAM")) { roamRequested = true; return "Scanning for a better AP"; }
  if (!strcmp(cmd, "AUTOROAM") && a1) { autoRoam = !strcmp(a1, "ON"); return autoRoam ? "Auto roaming on" : "Auto roaming off"; }
  if (!strcmp(cmd, "CALSTEP_START")) { trackerCalStart(); return "Step calibration: walk the measured distance"; }
  if (!strcmp(cmd, "CALSTEP_END") && a1) { trackerCalEnd(atof(a1), msg, sizeof(msg)); return msg; }
  if (!strcmp(cmd, "POSECAL") && a1) {
    if (!handPoseCalibrate(a1)) return "POSECAL needs HANG or POINT";
    snprintf(msg, sizeof(msg), "%s pose stored", a1);
    return msg;
  }
  return "Unknown command";
}

void commandsHandleLine(char* line) {
  if (!strncmp(line, "PING", 4)) return;
  if (line[0] == 'A' && line[1] == ',') { readingsAck((uint16_t)strtoul(line + 2, nullptr, 10)); return; }

  char* tag = strtok(line, ",\r\n");
  if (!tag || strcmp(tag, "C")) return;
  char* idStr = strtok(nullptr, ",\r\n");
  char* cmd = strtok(nullptr, ",\r\n");
  if (!idStr || !cmd) return;
  long id = atol(idStr);
  // Repeat announcements on retried requests, while still deduplicating actions.
  if (!strcmp(cmd, "HELLO") || !strcmp(cmd, "FEATURES")) debugSendFeatures();

  if (!strcmp(cmd, "SET")) {
    char* rest = strtok(nullptr, "\r\n");          // the value may contain commas
    if (id != lastCmdId) {
      lastCmdId = id;
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
      strncpy(lastAck, runCommand(cmd, a1, a2, a3), sizeof(lastAck) - 1);
    }
  }
  char reply[80];
  snprintf(reply, sizeof(reply), "K,%ld,%s\n", id, lastAck);
  linkSendLine(reply);
}

#endif // SURVEY_MODE == HAT
