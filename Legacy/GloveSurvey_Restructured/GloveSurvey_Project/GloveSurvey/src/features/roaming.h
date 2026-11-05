/*
  roaming - decides WHEN to look for a stronger access point: straight away when the
  app asks (ROAM), or by itself while standing still and not measuring (AUTOROAM,ON).
  The scan and switch are in wifi.cpp.
*/
#pragma once
#include "../core/feature.h"

#if FEAT_ROAM
void roamingUpdate();
void roamingDebug(DebugText& t);
#endif
