/*
  heading - which way the PERSON is facing (degrees, anticlockwise, 0 = start direction).
  Always present; HEADING_SOURCE (features.h) picks where it comes from:
    HEADING_BUTTONS  heading_buttons.cpp: the person reports turns (glove buttons / app)
                     and corrects it with "I'm here". The accelerometer can't measure
                     turning, and the hand doesn't point where the person faces.
    HEADING_GYRO     heading_gyro.cpp: a gyroscope worn at the waist (not written yet).
*/
#pragma once
#include "../core/feature.h"

void headingBegin();
void headingUpdate();
void headingDebug(DebugText& t);

float headingNow();                // current facing
void headingTurn(float deg);       // the person turned (+ = left)
void headingSet(float deg);        // set directly ("I'm here", reset)
