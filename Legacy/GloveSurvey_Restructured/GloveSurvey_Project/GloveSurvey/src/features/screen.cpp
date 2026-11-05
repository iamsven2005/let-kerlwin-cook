#include "screen.h"
#if FEAT_SCREEN
#include "../core/state.h"
#include "../core/debug.h"
#include "heading.h"
#include "hand_pose.h"
#include "step_cal.h"
#include "link.h"
#include "wifi.h"

TinyScreen display = TinyScreen(TinyScreenPlus);
static bool begun = false;

static void showText(const char* t, const FONT_INFO& f, uint8_t c, int y) {
  display.setFont(f);
  display.fontColor(c, TS_8b_Black);
  int x = (96 - display.getPrintWidth((char*)t)) / 2;
  if (x < 0) x = 0;
  display.setCursor(x, y);
  display.print(t);
}

static void row(int y, const char* t, uint8_t c) {
  display.drawRect(0, y, 96, 10, TSRectangleFilled, TS_8b_Black);
  display.setFont(thinPixel7_10ptFontInfo);
  display.fontColor(c, TS_8b_Black);
  display.setCursor(0, y);
  display.print(t);
}

void screenBegin() {
  if (begun) return;
  begun = true;
  display.begin();
  display.setBrightness(10);
  display.setBitDepth(TSBitDepth8);
  display.clearScreen();
}

void screenClear() { screenBegin(); display.clearScreen(); }

void screenMessage(const char* text) {
  screenBegin();
  display.clearScreen();
  showText(text, thinPixel7_10ptFontInfo, TS_8b_White, 28);
}

static void normalView() {
  char b[32];
  const char* st = !tracking ? "PAUSED" : stepCalActive() ? "CAL" : measuring ? "MEASURE" : "WALK";
  snprintf(b, sizeof(b), "%s %s Q%d", slowMode ? "SLOW" : "FAST", st, cacheCount);
  row(0, b, tracking ? TS_8b_Blue : TS_8b_Yellow);

  display.drawRect(0, 11, 96, 20, TSRectangleFilled, TS_8b_Black);
  if (shownRssi == 0) showText("-- dBm", liberationSans_16ptFontInfo, TS_8b_White, 12);
  else {
    snprintf(b, sizeof(b), "%d dBm", shownRssi);
    showText(b, liberationSans_16ptFontInfo,
             shownRssi >= -50 ? TS_8b_Green : shownRssi >= -70 ? TS_8b_Yellow : TS_8b_Red, 12);
  }

  snprintf(b, sizeof(b), "Facing %ld  %ld steps", lround(headingNow()), steps);
  row(33, b, TS_8b_White);

  long xc = lround(posX / 100), yc = lround(posY / 100);               // tenths of a metre
  snprintf(b, sizeof(b), "X %s%ld.%ld  Y %s%ld.%ld m", xc < 0 ? "-" : "", labs(xc) / 10, labs(xc) % 10,
           yc < 0 ? "-" : "", labs(yc) / 10, labs(yc) % 10);
  row(43, b, TS_8b_White);

#if FEAT_WIFI
  if (!linkAppFound()) {
    // Not found by the app yet: show this glove's address (for the app's "Device IP" box)
    uint8_t ip[4];
    wifiLocalIp(ip);
    snprintf(b, sizeof(b), "IP %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    row(53, b, TS_8b_Yellow);
    return;
  }
#endif
  snprintf(b, sizeof(b), "IR %s  hand %s", irYes ? "YES" : "no", handPoseName());
  row(53, b, irYes ? (handPose == POSE_OTHER ? TS_8b_Yellow : TS_8b_Red) : TS_8b_Green);
}

#if DEBUG_SCREEN
// Title bar: name n/N, plus "A" when auto-cycling. Below it, the feature's own page.
static void debugView() {
  debugTickPager();
  char b[32];
  snprintf(b, sizeof(b), "%s %d/%d%s", debugPageName(), debugPageNumber(), debugPageCount(),
           debugAutoCycling() ? " A" : "");
  row(0, b, TS_8b_Blue);
  DebugText t;
  debugBuildPage(t);
  for (int i = 0; i < DBG_LINES; i++) row(11 + i * 10, i < t.count ? t.lines[i] : "", TS_8b_White);
}
#endif

void screenUpdate() {
#if DEBUG_SCREEN
  if (debugViewOn()) { debugView(); return; }
#endif
  normalView();
}

#endif
