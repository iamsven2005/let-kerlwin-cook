/*
  tracker.h - the PERSON's position: steps x step length along headingNow().
  Also the step-length calibration (walk a measured distance).
*/
#pragma once
#include <Arduino.h>

extern float posX, posY;            // mm from the start point
extern float distanceMm;            // walked so far
extern long  steps;
extern bool  tracking;              // false = paused (steps not counted)

void trackerOnStep();               // called by motion.cpp for every step
void trackerReset();                // back at the start: (0,0), facing 0
void trackerSetPosition(float x, float y);
bool trackerCalibrating();
void trackerCalStart();
// Finish calibration over 'mm'. Writes a message for the app; true if the length changed.
bool trackerCalEnd(float mm, char* msg, int msgSize);
