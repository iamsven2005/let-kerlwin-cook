/*
  hand_pose.h - is the hand HANGING (arm relaxed) or POINTING (raised forward)?
  Worked out from the hand's tilt (gravity), compared with two calibrated poses.
  This says nothing about which way the PERSON faces.
*/
#pragma once

enum HandPose { POSE_OTHER, POSE_HANG, POSE_POINT };

extern HandPose handPose;
void handPoseUpdate();                 // call after motionUpdate()
const char* handPoseName();            // "HANG", "POINT" or "OTHER"
bool handPoseCalibrate(const char* which);   // "HANG" or "POINT": store the current tilt
