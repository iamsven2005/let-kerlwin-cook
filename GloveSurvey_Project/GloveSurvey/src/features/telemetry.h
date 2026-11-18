/*
  telemetry - the P line, TELEMETRY_PERIOD_MS apart:
  P,t,x,y,heading,dist,NA,ir,state,mode,steplen_cm,AP,handpose,0,0,-1,steps,still
  Fields of switched-off features are sent as their safe defaults (state.cpp); the app
  learns which features exist from the F line (HELLO / FEATURES).
*/
#pragma once
#include "../core/feature.h"

#if FEAT_TELEMETRY
void telemetryUpdate();
void telemetryDebug(DebugText& t);
#endif
