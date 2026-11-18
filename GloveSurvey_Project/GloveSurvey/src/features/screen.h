/*
  screen - the TinyScreen+ display (96 x 64).
  Normal view:
    top     mode (FAST/SLOW), state (WALK/MEASURE/PAUSED/CAL), readings waiting (Q)
    middle  Wi-Fi signal in dBm (green >= -50, yellow to -70, red below)
    rows    facing + steps, position, IR + hand pose (or this glove's IP until the app finds it)
  Debug view (DEBUG_SCREEN, see debug.h): title bar "name n/N", then that feature's page.
*/
#pragma once
#include "../core/feature.h"

#if FEAT_SCREEN
#include <TinyScreen.h>
extern TinyScreen display;
void screenBegin();                      // safe to call more than once
void screenUpdate();                     // every SCREEN_PERIOD_MS
void screenClear();
void screenMessage(const char* text);    // one centred line on a clear screen
#endif
