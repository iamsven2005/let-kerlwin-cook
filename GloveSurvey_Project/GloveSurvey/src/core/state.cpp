#include "state.h"

// Safe defaults: what everyone sees if the owning feature is switched off
bool  accelFresh = false;
float accX = 0, accY = 0, accZ = 1;
float gravX = 0, gravY = 0, gravZ = 1;
float dynAcc = 0;

bool still = true, movedSinceStill = false;

HandPose handPose = POSE_OTHER;

float posX = 0, posY = 0, distanceMm = 0;
long  steps = 0;
bool  tracking = true;

bool irYes = false;

bool slowMode = false, measuring = false, averaging = false;
int  shownRssi = 0, currentRssi = 0, cacheCount = 0;

bool wifiReconnectWanted = false, roamRequested = false, autoRoam = true;
