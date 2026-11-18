/*
  features.h - switch each feature on (1) or off (0). This is the ONLY place to do it.

  An off feature is not compiled at all: it takes no flash and no RAM, and anything
  that asks it for a value gets a safe default (e.g. hand pose = OTHER).
  The build stops with a plain message if a feature is on but something it needs is off.

  Each value can also be overridden at build time (e.g. -DFEAT_IR_PLOT=0), which is how
  the build-matrix check script tests the combinations.
*/
#pragma once

// ---------------------------------------------------------------- sensors
#ifndef FEAT_ACCEL
#define FEAT_ACCEL        1   // accelerometer driver: raw acceleration, gravity (hand tilt)
#endif
#ifndef FEAT_STEPS
#define FEAT_STEPS        1   // step counting
#endif
#ifndef FEAT_STILL
#define FEAT_STILL        1   // standing still vs walking
#endif
#ifndef FEAT_HAND_POSE
#define FEAT_HAND_POSE    1   // hand HANG / POINT / OTHER
#endif
#ifndef FEAT_IR
#define FEAT_IR           1   // IR sensor YES/NO
#endif

// ---------------------------------------------------------------- position
// Where the PERSON's heading comes from: one choice, not an on/off.
#define HEADING_BUTTONS   1   // turn buttons on the glove or in the app (today)
#define HEADING_GYRO      2   // a gyroscope worn at the waist (not written yet)
#ifndef HEADING_SOURCE
#define HEADING_SOURCE    HEADING_BUTTONS
#endif

#ifndef FEAT_TRACKER
#define FEAT_TRACKER      1   // position = steps x step length along the heading
#endif
#ifndef FEAT_STEP_CAL
#define FEAT_STEP_CAL     1   // step-length calibration over a measured walk
#endif
#ifndef FEAT_IR_PLOT
#define FEAT_IR_PLOT      1   // send IR hits to the app as obstacle points
#endif

// ---------------------------------------------------------------- Wi-Fi survey
#ifndef FEAT_WIFI
#define FEAT_WIFI         1   // Wi-Fi connection and network profiles
#endif
#ifndef FEAT_READ_FAST
#define FEAT_READ_FAST    1   // FAST readings: one every fast_ms while walking
#endif
#ifndef FEAT_READ_SLOW
#define FEAT_READ_SLOW    1   // SLOW readings: an average while standing still
#endif
#ifndef FEAT_READ_CACHE
#define FEAT_READ_CACHE   1   // keep readings until the app confirms them (off = send once)
#endif
#ifndef FEAT_ROAM
#define FEAT_ROAM         1   // look for a stronger access point while standing still
#endif

// ---------------------------------------------------------------- talking to the app
#ifndef FEAT_LINK_UDP
#define FEAT_LINK_UDP     1   // app link over Wi-Fi (UDP)
#endif
#ifndef FEAT_LINK_SERIAL
#define FEAT_LINK_SERIAL  1   // app link over the USB cable
#endif
#ifndef FEAT_TELEMETRY
#define FEAT_TELEMETRY    1   // the P line 5 times a second
#endif
#ifndef FEAT_COMMANDS
#define FEAT_COMMANDS     1   // commands and SET keys from the app
#endif

// ---------------------------------------------------------------- on the glove
#ifndef FEAT_SCREEN
#define FEAT_SCREEN       1   // the display
#endif
#ifndef FEAT_BUTTONS
#define FEAT_BUTTONS      1   // the four buttons
#endif

// ---------------------------------------------------------------- debug
#ifndef DEBUG_SCREEN
#define DEBUG_SCREEN      1   // each feature can show its own page on the glove screen
#endif
#ifndef DEBUG_TO_APP
#define DEBUG_TO_APP      1   // ...and send it to the app's debug panel
#endif

// =====================================================================
//  Derived values and dependency checks (no need to edit below)
// =====================================================================
#define FEAT_ANY_LINK     (FEAT_LINK_UDP || FEAT_LINK_SERIAL)
#define FEAT_ANY_READINGS (FEAT_READ_FAST || FEAT_READ_SLOW)
#define DEBUG_ANY         (DEBUG_SCREEN || DEBUG_TO_APP)

#if HEADING_SOURCE != HEADING_BUTTONS && HEADING_SOURCE != HEADING_GYRO
#error "HEADING_SOURCE must be HEADING_BUTTONS or HEADING_GYRO"
#endif
#if FEAT_STEPS && !FEAT_ACCEL
#error "FEAT_STEPS (step counting) needs FEAT_ACCEL (accelerometer)"
#endif
#if FEAT_STILL && !FEAT_ACCEL
#error "FEAT_STILL (still detection) needs FEAT_ACCEL (accelerometer)"
#endif
#if FEAT_HAND_POSE && !FEAT_ACCEL
#error "FEAT_HAND_POSE (hand pose) needs FEAT_ACCEL (accelerometer)"
#endif
#if FEAT_TRACKER && !FEAT_STEPS
#error "FEAT_TRACKER (position) needs FEAT_STEPS (step counting)"
#endif
#if FEAT_STEP_CAL && !FEAT_TRACKER
#error "FEAT_STEP_CAL (step calibration) needs FEAT_TRACKER (position)"
#endif
#if FEAT_IR_PLOT && !(FEAT_IR && FEAT_HAND_POSE && FEAT_TRACKER && FEAT_ANY_LINK)
#error "FEAT_IR_PLOT (IR hits to the app) needs FEAT_IR, FEAT_HAND_POSE, FEAT_TRACKER and a link (FEAT_LINK_UDP or FEAT_LINK_SERIAL)"
#endif
#if FEAT_READ_FAST && !(FEAT_WIFI && FEAT_ANY_LINK)
#error "FEAT_READ_FAST (FAST readings) needs FEAT_WIFI and a link (FEAT_LINK_UDP or FEAT_LINK_SERIAL)"
#endif
#if FEAT_READ_SLOW && !(FEAT_WIFI && FEAT_STILL && FEAT_ANY_LINK)
#error "FEAT_READ_SLOW (SLOW readings) needs FEAT_WIFI, FEAT_STILL and a link (FEAT_LINK_UDP or FEAT_LINK_SERIAL)"
#endif
#if FEAT_READ_CACHE && !FEAT_ANY_READINGS
#error "FEAT_READ_CACHE (reading cache) needs FEAT_READ_FAST or FEAT_READ_SLOW"
#endif
#if FEAT_ROAM && !(FEAT_WIFI && FEAT_STILL && FEAT_ANY_READINGS)
#error "FEAT_ROAM (roaming) needs FEAT_WIFI, FEAT_STILL and FEAT_READ_FAST or FEAT_READ_SLOW"
#endif
#if FEAT_LINK_UDP && !FEAT_WIFI
#error "FEAT_LINK_UDP (Wi-Fi link) needs FEAT_WIFI"
#endif
#if FEAT_TELEMETRY && !FEAT_ANY_LINK
#error "FEAT_TELEMETRY needs a link (FEAT_LINK_UDP or FEAT_LINK_SERIAL)"
#endif
#if FEAT_COMMANDS && !FEAT_ANY_LINK
#error "FEAT_COMMANDS needs a link (FEAT_LINK_UDP or FEAT_LINK_SERIAL)"
#endif
#if FEAT_BUTTONS && !FEAT_SCREEN
#error "FEAT_BUTTONS needs FEAT_SCREEN (the buttons are read through the display)"
#endif
#if DEBUG_SCREEN && !FEAT_SCREEN
#error "DEBUG_SCREEN needs FEAT_SCREEN"
#endif
#if DEBUG_TO_APP && !FEAT_ANY_LINK
#error "DEBUG_TO_APP needs a link (FEAT_LINK_UDP or FEAT_LINK_SERIAL)"
#endif
