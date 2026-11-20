#include "buttons.h"
#if FEAT_BUTTONS
#include "../core/state.h"
#include "../core/debug.h"
#include "screen.h"
#include "heading.h"
#include "tracker.h"
#include "read_slow.h"
#include "link.h"

static uint8_t lastButtons = 0;
static bool lastLong = false;
static unsigned long presses = 0;

static void normalPress(uint8_t held, bool longPress) {
  if (held & TSButtonUpperLeft) {
    headingTurn(longPress ? 45 : 90);
    linkSendMessage(longPress ? "Turned left 45" : "Turned left 90");
  } else if (held & TSButtonUpperRight) {
    headingTurn(longPress ? -45 : -90);
    linkSendMessage(longPress ? "Turned right 45" : "Turned right 90");
  } else if (held & TSButtonLowerRight) {
    if (longPress) {
#if FEAT_TRACKER
      trackerSetTracking(!tracking);
      linkSendMessage(tracking ? "Tracking on" : "Tracking paused");
#endif
    } else { headingTurn(180); linkSendMessage("Turned around"); }
  } else if (held & TSButtonLowerLeft) {
    if (longPress) {
#if FEAT_TRACKER
      trackerReset();
      linkSendMessage("Back at the start");
#endif
    } else {
#if FEAT_READ_SLOW
      readingsMeasureHere();
      linkSendMessage("Measuring here: hold still");
#endif
    }
  }
}

#if DEBUG_SCREEN
static void debugPress(uint8_t held) {
  if (held & TSButtonUpperLeft) debugStep(-1);
  else if (held & TSButtonUpperRight) debugStep(+1);
  else if (held & TSButtonLowerLeft) debugHold();
  else if (held & TSButtonLowerRight) debugAutoCycle();
}
#endif

// Act when the buttons are released, on every button that was down during the press
void buttonsUpdate() {
  static uint8_t held = 0;
  static unsigned long downMs = 0;
  uint8_t now = display.getButtons(TSButtonUpperLeft | TSButtonUpperRight | TSButtonLowerLeft | TSButtonLowerRight);
  if (now) {
    if (!held) downMs = millis();
    held |= now;
    return;
  }
  if (!held) return;
  bool longPress = millis() - downMs > LONG_PRESS_MS;
  lastButtons = held;
  lastLong = longPress;
  presses++;
#if DEBUG_SCREEN
  bool bothUpper = (held & TSButtonUpperLeft) && (held & TSButtonUpperRight);
  if (bothUpper && longPress) {
    debugViewSet(!debugViewOn());
    screenClear();
    linkSendMessage(debugViewOn() ? "Debug view on" : "Debug view off");
  } else if (debugViewOn()) debugPress(held);
  else normalPress(held, longPress);
#else
  normalPress(held, longPress);
#endif
  held = 0;
}

#if DEBUG_ANY
static const char* buttonName(uint8_t b) {
  if ((b & TSButtonUpperLeft) && (b & TSButtonUpperRight)) return "UL+UR";
  if (b & TSButtonUpperLeft) return "UL";
  if (b & TSButtonUpperRight) return "UR";
  if (b & TSButtonLowerRight) return "LR";
  if (b & TSButtonLowerLeft) return "LL";
  return "-";
}

void buttonsDebug(DebugText& t) {
  uint8_t now = display.getButtons(TSButtonUpperLeft | TSButtonUpperRight | TSButtonLowerLeft | TSButtonLowerRight);
  t.add("down now %s", buttonName(now));
  t.add("last %s %s", buttonName(lastButtons), lastLong ? "long" : "short");
  t.add("presses %lu", presses);
}
#endif

#endif
