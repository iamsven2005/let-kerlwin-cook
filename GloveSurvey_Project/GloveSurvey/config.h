/*
  config.h - every setting in one place.
  Variables marked "SET key" can also be changed from the app's command box:
  SET,<key>,<value>   e.g.  SET,step_mm,680
*/
#pragma once
#include <Arduino.h>

// ---- Walking (motion.cpp, tracker.cpp)
extern float stepMm;             // SET step_mm   step length (calibrate in the app)
extern float stepThreshG;        // SET step_g    a step peak must rise this far (g)
const unsigned long STEP_MIN_MS = 280;   // fastest walking: ~3.5 steps per second
const float STILL_VAR = 0.0015f;         // movement variance (g^2) below this = still
const unsigned long STILL_MS = 1000;     // ...for this long

// ---- IR sensor on the glove (ir_sensor.cpp)
extern float irRangeMm;          // SET ir_range  trigger range R (YES/NO only: hits at 0.6 R)
extern float hangDirDeg;         // SET hang_dir  HANG pose: sensor direction from the body (-90 = right)
extern float hangMm;             // SET hang_mm   HANG pose: hand distance from the body centre
extern float pointMm;            // SET point_mm  POINT pose: hand distance in front of the body
const int IR_EMITTER_PIN = A0;   // Wireling port 0
const int IR_RECEIVER_PIN = A1;  // Wireling port 1 (not port 3 = A3: the Wi-Fi shield uses it)
const int IR_BURST_CYCLES = 20;
const int IR_HALF_PERIOD_US = 9; // 12-13 = longest range, smaller = shorter range
const int IR_CONFIRM_COUNT = 2;

// ---- Hand pose (hand_pose.cpp)
const float POSE_TOL_DEG = 30;   // within this of a calibrated pose = that pose

// ---- Wi-Fi readings (readings.cpp)
extern unsigned long fastIntervalMs;   // SET fast_ms   FAST: time between readings
extern unsigned long slowIntervalMs;   // SET slow_ms   SLOW: time between samples
extern int slowSamples;                // SET slow_n    SLOW: samples per average
extern unsigned long ackTimeoutMs;     // SET ack_ms    resend a reading if not confirmed
const int CACHE_SIZE = 300;            // readings kept until the app confirms them

// ---- Link to the app (link.cpp). Networks: wifi_logic.cpp
const uint16_t PC_PORT = 4210;         // the app listens here
const uint16_t GLOVE_PORT = 4211;      // the glove listens here
const unsigned long TX_GAP_MS = 15;    // the Wi-Fi chip drops back-to-back packets

// ---- Small helpers
inline float wrap180(float d) { while (d > 180) d -= 360; while (d <= -180) d += 360; return d; }
inline float rad(float d) { return d * PI / 180.0f; }
