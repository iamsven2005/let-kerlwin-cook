/*
  ir_sensor - the IR pair on the glove (YES/NO only): flashes the emitter at ~38 kHz and
  checks for the reflection. YES after IR_CONFIRM_COUNT reads in a row. Fills irYes.
  Sending hits to the app is a separate feature: ir_plot.
*/
#pragma once
#include "../core/feature.h"

#if FEAT_IR
void irBegin();
void irUpdate();            // every IR_PERIOD_MS
void irDebug(DebugText& t);
#endif
