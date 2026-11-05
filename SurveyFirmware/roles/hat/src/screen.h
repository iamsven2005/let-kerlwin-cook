/*
  screen.h - the TinyScreen+ display:
    top     mode (FAST/SLOW), state (WALK/STILL/MEASURE/PAUSED), readings waiting (Q)
    middle  Wi-Fi signal in dBm (green >= -50, yellow to -70, red below)
    rows    facing + steps, position, IR + hand pose (or this glove's IP until the app finds it)
*/
#pragma once
#include <TinyScreen.h>

extern TinyScreen display;

void screenBegin();
void screenClear();
void screenMessage(const char* text);   // one centred line on a clear screen
void screenUpdate();                     // call every 250 ms
