#pragma once
#include <Arduino.h>

void debugSendFeatures();
void debugMirrorSet(bool on);
void debugMirrorUpdate();
extern unsigned long debugMirrorIntervalMs;
