/*
  debug.h - every feature's debug page, on the glove screen and/or in the app.

  DEBUG_SCREEN (features.h): the screen can switch from the normal view to a pager with
  one page per feature that has one. Enter/leave: hold upper-left + upper-right together
  (> 0.6 s), or DEBUG,ON / DEBUG,OFF from the app. While in debug view:
    upper-left / upper-right  previous / next page
    lower-left                hold this page (stop auto-cycling)
    lower-right               auto-cycle, DBG_AUTO_MS per page
  Turn buttons don't work in debug view (use the app's turn buttons).

  DEBUG_TO_APP: when switched on (MIRROR,ON - the app's debug panel does this), every
  page is sent to the app every dbg_ms as "D,<feature>,<line>|<line>|...". Low priority:
  one page per packet, only if there is room, never cached or resent.
*/
#pragma once
#include "feature.h"

// ---- pager (DEBUG_SCREEN)
bool debugViewOn();
void debugViewSet(bool on);
void debugStep(int dir);                 // +1 next page, -1 previous
bool debugGoto(const char* name);        // jump to a feature's page; false if none
void debugHold();                        // stop auto-cycling
void debugAutoCycle();                   // start auto-cycling
bool debugAutoCycling();
int  debugPageNumber();                  // 1..debugPageCount()
int  debugPageCount();
const char* debugPageName();
void debugBuildPage(DebugText& out);     // the current page's text
void debugTickPager();                   // advance the page when auto-cycling (screen calls it)

// ---- mirror to the app (DEBUG_TO_APP)
bool debugMirrorOn();
void debugMirrorSet(bool on);
void debugMirrorUpdate();                // in the feature table
