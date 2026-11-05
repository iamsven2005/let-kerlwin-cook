/*
  heading.h - which way the PERSON is facing (degrees, anticlockwise, 0 = start direction).

  The accelerometer CANNOT measure this (turning about the vertical doesn't change
  gravity), and the hand doesn't point where the person faces. So today the person
  reports turns (glove buttons / app) and corrects it with "I'm here".

  To add a gyroscope worn at the waist later, change ONLY heading.cpp.
*/
#pragma once

float headingNow();                // current facing
void headingTurn(float deg);       // the person turned (+ = left)
void headingSet(float deg);        // set directly ("I'm here", reset)
