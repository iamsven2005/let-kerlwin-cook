/*
  config.h - every setting in one place (which features are on: features.h).
  Variables marked "SET key" can also be changed from the app's command box:
  SET,<key>,<value>   e.g.  SET,step_mm,680
  Network names and passwords: secrets.h.
*/
#pragma once
#include <Arduino.h>
#include "features.h"

// ---- Walking (steps, still, tracker)
extern float stepMm;             // SET step_mm   step length (calibrate in the app)
extern float stepThreshG;        // SET step_g    a step peak must rise this far (g)
const unsigned long STEP_MIN_MS = 280;   // fastest walking: ~3.5 steps per second
const float STILL_VAR = 0.0015f;         // movement variance (g^2) below this = still
const unsigned long STILL_MS = 1000;     // ...for this long

// ---- IR sensor on the glove (ir_sensor, ir_plot)
extern float irRangeMm;          // SET ir_range  trigger range R (YES/NO only: hits at 0.6 R)
extern float hangDirDeg;         // SET hang_dir  HANG pose: sensor direction from the body (-90 = right)
extern float hangMm;             // SET hang_mm   HANG pose: hand distance from the body centre
extern float pointMm;            // SET point_mm  POINT pose: hand distance in front of the body
const int IR_EMITTER_PIN = A0;   // Wireling port 0
const int IR_RECEIVER_PIN = A1;  // Wireling port 1 (not port 3 = A3: the Wi-Fi shield uses it)
const int IR_BURST_CYCLES = 20;
const int IR_HALF_PERIOD_US = 9; // 12-13 = longest range, smaller = shorter range
const int IR_CONFIRM_COUNT = 2;
const unsigned long IR_PLOT_GAP_MS = 250;  // at most 4 hits a second to the app

// ---- Hand pose (hand_pose)
const float POSE_TOL_DEG = 30;   // within this of a calibrated pose = that pose

// ---- Wi-Fi readings (read_fast, read_slow, read_cache)
extern unsigned long fastIntervalMs;   // SET fast_ms   FAST: time between readings
extern unsigned long slowIntervalMs;   // SET slow_ms   SLOW: time between samples
extern int slowSamples;                // SET slow_n    SLOW: samples per average
extern unsigned long ackTimeoutMs;     // SET ack_ms    resend a reading if not confirmed
const int CACHE_SIZE = 300;            // readings kept until the app confirms them

// ---- Roaming (roaming): defaults live in wifi.cpp, keys roam_dbm, roam_margin, ...

// ---- Link to the app (link, link_udp)
const uint16_t PC_PORT = 4210;         // the app listens here
const uint16_t GLOVE_PORT = 4211;      // the glove listens here
const unsigned long TX_GAP_MS = 15;    // the Wi-Fi chip drops back-to-back packets
const int TX_BUF_SIZE = 1000;          // one packet's worth of queued lines

// ---- Update rates of the features (main_loop)
const uint16_t ACCEL_PERIOD_MS = 20;       // 50 Hz: accelerometer, steps, still, hand pose
const uint16_t IR_PERIOD_MS = 50;          // 20 Hz
const uint16_t TELEMETRY_PERIOD_MS = 200;  // 5 times a second
const uint16_t SCREEN_PERIOD_MS = 250;     // 4 times a second

// ---- Buttons
const unsigned long LONG_PRESS_MS = 600;

// ---- Debug pages (debug, screen)
const int DBG_LINES = 5;               // lines of text per page (below the title bar)
const int DBG_COLS = 19;               // characters per line
const unsigned long DBG_AUTO_MS = 3000;   // auto-cycle: time per page
extern unsigned long dbgMirrorMs;      // SET dbg_ms  send every page to the app this often

// ---- Small helpers
inline float wrap180(float d) { while (d > 180) d -= 360; while (d <= -180) d += 360; return d; }
inline float rad(float d) { return d * PI / 180.0f; }
