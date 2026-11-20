/*
  state.h - the values features pass to each other. Each value has ONE owner that
  writes it; everyone else only reads it. If the owner is switched off (features.h), the
  value stays at the safe default given in state.cpp.
*/
#pragma once
#include <Arduino.h>

// ---- owner: accel
extern bool  accelFresh;             // a new sample arrived this pass
extern float accX, accY, accZ;       // latest acceleration (g, glove axes)
extern float gravX, gravY, gravZ;    // slow average = gravity in the glove's axes (hand tilt)
extern float dynAcc;                 // size of the acceleration minus its slow average (g)

// ---- owner: still
extern bool still;                   // standing still (no movement for STILL_MS)
extern bool movedSinceStill;         // set while moving; read_slow clears it

// ---- owner: hand_pose
enum HandPose { POSE_OTHER, POSE_HANG, POSE_POINT };
extern HandPose handPose;

// ---- owner: tracker (heading: heading.h)
extern float posX, posY;             // mm from the start point
extern float distanceMm;             // walked so far
extern long  steps;
extern bool  tracking;               // false = paused (steps not counted)

// ---- owner: ir_sensor
extern bool irYes;                   // something within range right now

// ---- owner: read_slow / read_fast / read_cache
extern bool slowMode;                // SLOW mode chosen (read_slow)
extern bool measuring;               // MEASURE HERE requested (read_slow)
extern bool averaging;               // a SLOW average is being taken (read_slow)
extern int  shownRssi;               // last value shown on the screen (0 = none yet)
extern int  currentRssi;             // latest raw reading (for roaming)
extern int  cacheCount;              // readings waiting for the app (read_cache)

// ---- owner: commands (asked for by the app, acted on by wifi / roaming)
extern bool wifiReconnectWanted;     // SET,join,1: connect with new network details
extern bool roamRequested;           // ROAM: look for a stronger access point now
extern bool autoRoam;                // AUTOROAM: roam by itself while standing still
