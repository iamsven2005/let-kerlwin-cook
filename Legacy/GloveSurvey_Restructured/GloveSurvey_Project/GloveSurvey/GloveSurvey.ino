/*
  GloveSurvey.ino - hand-worn Wi-Fi survey glove (right hand).

  This file only starts the main logic loop. Everything else is in src/:
    src/core/features.h   switch each feature on or off (one variable each)
    src/core/main_loop    the main loop: runs the feature table in order
    src/core/config       every setting, with defaults (many changeable from the app: SET)
    src/core/state        values the features pass to each other
    src/core/events       things that happen (a step), and who listens
    src/core/debug        each feature's debug page: on the screen and in the app
    src/core/secrets.h    your Wi-Fi networks and passwords (not committed)
    src/features/...      one file pair per feature (.h = what it offers, .cpp = how)

  Hardware: TinyScreen+ + Wi-Fi TinyShield + Accelerometer TinyShield ASD2511-R-A +
  Wireling Adapter (IR Emitter on port 0, IR Receiver on port 1).
*/
#include "src/core/main_loop.h"

void setup() { mainSetup(); }
void loop()  { mainLoop(); }
