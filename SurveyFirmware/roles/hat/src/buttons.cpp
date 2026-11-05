#include <SurveyCommon.h>
#if SURVEY_MODE == HAT
#include "buttons.h"
#include "screen.h"
#include "heading.h"
#include "tracker.h"
#include "readings.h"
#include "link.h"

void buttonsUpdate() {
  static uint8_t held = 0;
  static unsigned long downMs = 0;
  uint8_t now = display.getButtons(TSButtonUpperLeft | TSButtonUpperRight | TSButtonLowerLeft | TSButtonLowerRight);
  if (now && !held) { held = now; downMs = millis(); return; }
  if (now || !held) return;

  bool longPress = millis() - downMs > 600;          // released: act on it
  if (held & TSButtonUpperLeft) {
    headingTurn(longPress ? 45 : 90);
    linkSendMessage(longPress ? "Turned left 45" : "Turned left 90");
  } else if (held & TSButtonUpperRight) {
    headingTurn(longPress ? -45 : -90);
    linkSendMessage(longPress ? "Turned right 45" : "Turned right 90");
  } else if (held & TSButtonLowerRight) {
    if (longPress) { tracking = !tracking; linkSendMessage(tracking ? "Tracking on" : "Tracking paused"); }
    else { headingTurn(180); linkSendMessage("Turned around"); }
  } else if (held & TSButtonLowerLeft) {
    if (longPress) { trackerReset(); linkSendMessage("Back at the start"); }
    else { readingsMeasureHere(); linkSendMessage("Measuring here: hold still"); }
  }
  held = 0;
}

#endif // SURVEY_MODE == HAT
