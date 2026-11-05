/*
  motion.h - what the accelerometer CAN tell: steps, walking vs still, hand tilt.
  Call motionUpdate() 50 times a second. Every detected step calls trackerOnStep().
*/
#pragma once

extern float gravX, gravY, gravZ;   // slow average of the acceleration = gravity in the
                                    // glove's own axes (used for the hand pose)
extern bool still;                  // standing still (no movement for STILL_MS)
extern bool movedSinceStill;        // set when walking starts after being still

void motionUpdate();
