#include "../common/mode.h"
#if SURVEY_MODE == HAT
#include "heading.h"
#include "config.h"

static float bodyHeadingDeg = 0;

float headingNow() { return bodyHeadingDeg; }
void headingTurn(float deg) { bodyHeadingDeg = wrap180(bodyHeadingDeg + deg); }
void headingSet(float deg) { bodyHeadingDeg = wrap180(deg); }

#endif // SURVEY_MODE == HAT
