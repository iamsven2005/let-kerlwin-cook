/*
  ir_plot - sends IR hits to the app as obstacle points ("O" lines), only in a
  calibrated hand pose, placed in the direction the SENSOR points:
  HANG = hangDirDeg from the body heading (right), POINT = straight ahead.
*/
#pragma once
#include "../core/feature.h"

#if FEAT_IR_PLOT
void irPlotUpdate();        // every IR_PERIOD_MS, after ir_sensor
void irPlotDebug(DebugText& t);
#endif
