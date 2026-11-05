/*
  ir_sensor.h - the IR pair on the glove (YES/NO only).
  A hit is sent to the app only in a calibrated hand pose, placed in the direction the
  SENSOR points: HANG = hangDirDeg from the body heading (right), POINT = straight ahead.
*/
#pragma once

extern bool irYes;          // something within range right now
void irBegin();
void irUpdate();            // call every 50 ms
