/*
  accel - Accelerometer TinyShield ASD2511-R-A (Bosch BMA250), on I2C (A4/A5).
  A small driver (no library needed): range +/-2 g, 10-bit, 256 counts per g.
  Reads 50 times a second and fills accX/Y/Z, gravX/Y/Z and dynAcc (state.h).
  Stops with "No accelerometer!" at start-up if the shield doesn't answer.
*/
#pragma once
#include "../core/feature.h"

#if FEAT_ACCEL
void accelBegin();
void accelUpdate();
void accelDebug(DebugText& t);
#endif
