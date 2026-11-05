/*
  buttons - the TinyScreen+ buttons (short press / long press > LONG_PRESS_MS).
  Normal view:
    Upper-left   : turned LEFT 90   /  LEFT 45
    Upper-right  : turned RIGHT 90  /  RIGHT 45
    Lower-right  : TURNED AROUND    /  pause-resume tracking
    Lower-left   : MEASURE HERE     /  back at the start point (0,0, facing 0)
  Upper-left + upper-right held together (long): debug view on/off (DEBUG_SCREEN).
  Debug view: upper-left / upper-right = previous / next page, lower-left = hold page,
  lower-right = auto-cycle. Turn buttons don't work in debug view.
*/
#pragma once
#include "../core/feature.h"

#if FEAT_BUTTONS
void buttonsUpdate();
void buttonsDebug(DebugText& t);
#endif
