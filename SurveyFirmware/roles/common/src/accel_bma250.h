/*
  accel_bma250.h - Accelerometer TinyShield ASD2511-R-A (Bosch BMA250), on I2C (A4/A5).
  A small driver (no library needed): range +/-2 g, 10-bit, 256 counts per g.
*/
#pragma once
#include <Arduino.h>

bool bmaBegin();                                  // false if no BMA250 answers (0x18 or 0x19)
bool bmaRead(float& x, float& y, float& z);       // latest acceleration in g
uint8_t bmaAddress();                             // 0 if not found
