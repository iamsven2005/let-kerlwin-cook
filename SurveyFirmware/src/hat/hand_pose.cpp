#include "../common/mode.h"
#if SURVEY_MODE == HAT
#include "hand_pose.h"
#include "config.h"
#include "motion.h"

HandPose handPose = POSE_OTHER;

// Defaults: fingers down when hanging, back of the hand up when pointing.
// Depends on how the board is mounted: calibrate both from the app.
static float hangVec[3]  = { -1, 0, 0 };
static float pointVec[3] = { 0, 0, 1 };

static float angleTo(const float* v) {
  float n1 = sqrt(gravX * gravX + gravY * gravY + gravZ * gravZ);
  float n2 = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (n1 < 0.3f || n2 < 0.3f) return 180;
  float c = (gravX * v[0] + gravY * v[1] + gravZ * v[2]) / (n1 * n2);
  return acos(constrain(c, -1.0f, 1.0f)) * 180.0f / PI;
}

void handPoseUpdate() {
  if (angleTo(hangVec) < POSE_TOL_DEG) handPose = POSE_HANG;
  else if (angleTo(pointVec) < POSE_TOL_DEG) handPose = POSE_POINT;
  else handPose = POSE_OTHER;
}

const char* handPoseName() {
  return handPose == POSE_HANG ? "HANG" : handPose == POSE_POINT ? "POINT" : "OTHER";
}

bool handPoseCalibrate(const char* which) {
  float* v = !strcmp(which, "HANG") ? hangVec : !strcmp(which, "POINT") ? pointVec : nullptr;
  if (!v) return false;
  v[0] = gravX; v[1] = gravY; v[2] = gravZ;
  return true;
}

#endif // SURVEY_MODE == HAT
