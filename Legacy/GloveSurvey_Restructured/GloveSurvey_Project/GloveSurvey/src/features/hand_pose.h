/*
  hand_pose - is the hand HANGING (arm relaxed) or POINTING (raised forward)?
  Worked out from the hand's tilt (gravity), compared with two calibrated poses.
  Fills handPose (state.h). Says nothing about which way the PERSON faces.
*/
#pragma once
#include "../core/feature.h"
#include "../core/state.h"

#if FEAT_HAND_POSE
void handPoseUpdate();
void handPoseDebug(DebugText& t);
bool handPoseCalibrate(const char* which);   // "HANG" or "POINT": store the current tilt
#else
inline bool handPoseCalibrate(const char*) { return false; }
#endif

inline const char* handPoseName() {
  return handPose == POSE_HANG ? "HANG" : handPose == POSE_POINT ? "POINT" : "OTHER";
}
