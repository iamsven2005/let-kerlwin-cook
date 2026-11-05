/*
  main_loop.h - the main logic loop. setup() and loop() in GloveSurvey.ino only call these.
  The loop runs down the feature table (main_loop.cpp) in order, calling each feature's
  update when its period is due, then sends whatever the features queued for the app.
*/
#pragma once

void mainSetup();
void mainLoop();
void haltWith(const char* message);   // show a fatal message and stop (missing hardware)
