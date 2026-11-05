#include "events.h"
#include "features.h"
#include "../features/tracker.h"

void evStep() {
#if FEAT_TRACKER
  trackerOnStep();
#endif
}
