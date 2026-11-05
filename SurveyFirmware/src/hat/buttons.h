/*
  buttons.h - the TinyScreen+ buttons (short press / long press > 0.6 s)
    Upper-left   : turned LEFT 90   /  LEFT 45
    Upper-right  : turned RIGHT 90  /  RIGHT 45
    Lower-right  : TURNED AROUND    /  pause-resume tracking
    Lower-left   : MEASURE HERE     /  back at the start point (0,0, facing 0)
*/
#pragma once

void buttonsUpdate();     // call every loop
