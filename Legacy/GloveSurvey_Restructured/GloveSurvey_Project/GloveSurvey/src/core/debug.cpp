#include "debug.h"
#include <stdarg.h>
#include "../features/link.h"

#if DEBUG_ANY

void DebugText::add(const char* fmt, ...) {
  if (count >= DBG_LINES) return;
  va_list args;
  va_start(args, fmt);
  vsnprintf(lines[count], sizeof(lines[count]), fmt, args);
  va_end(args);
  for (char* c = lines[count]; *c; c++) if (*c == '|') *c = '/';   // '|' separates lines
  count++;
}

#if DEBUG_SCREEN
// Index i of the table rows that have a page
static int pageRow(int page) {
  int n = 0;
  for (int i = 0; i < featureCount(); i++)
    if (featureTable()[i].debug) { if (n == page) return i; n++; }
  return -1;
}

static int countPages() {
  int n = 0;
  for (int i = 0; i < featureCount(); i++) if (featureTable()[i].debug) n++;
  return n;
}
#endif

#else
void DebugText::add(const char*, ...) {}
#endif

// =====================================================================
//  Pager
// =====================================================================
#if DEBUG_SCREEN
static bool viewOn = false, autoCycle = false;
static int page = 0;
static unsigned long pageShownMs = 0;

bool debugViewOn() { return viewOn; }
void debugViewSet(bool on) { viewOn = on; pageShownMs = millis(); }
int  debugPageCount() { return countPages(); }
int  debugPageNumber() { return page + 1; }
bool debugAutoCycling() { return autoCycle; }
void debugHold() { autoCycle = false; }
void debugAutoCycle() { autoCycle = true; pageShownMs = millis(); }

void debugStep(int dir) {
  int n = countPages();
  if (n == 0) return;
  page = (page + dir + n) % n;
  pageShownMs = millis();
}

bool debugGoto(const char* name) {
  for (int p = 0; p < countPages(); p++) {
    int row = pageRow(p);
    if (!strcmp(featureTable()[row].name, name)) { page = p; viewOn = true; pageShownMs = millis(); return true; }
  }
  return false;
}

const char* debugPageName() {
  int row = pageRow(page);
  return row < 0 ? "-" : featureTable()[row].name;
}

void debugBuildPage(DebugText& out) {
  out.clear();
  int row = pageRow(page);
  if (row < 0) { out.add("no pages"); return; }
  featureTable()[row].debug(out);
}

void debugTickPager() {
  if (viewOn && autoCycle && millis() - pageShownMs >= DBG_AUTO_MS) debugStep(+1);
}
#else
bool debugViewOn() { return false; }
void debugViewSet(bool) {}
void debugStep(int) {}
bool debugGoto(const char*) { return false; }
void debugHold() {}
void debugAutoCycle() {}
bool debugAutoCycling() { return false; }
int  debugPageNumber() { return 0; }
int  debugPageCount() { return 0; }
const char* debugPageName() { return "-"; }
void debugBuildPage(DebugText& out) { out.clear(); }
void debugTickPager() {}
#endif

// =====================================================================
//  Mirror to the app
// =====================================================================
#if DEBUG_TO_APP
static bool mirrorOn = false;
static unsigned long roundStartMs = 0;
static int nextRow = -1;                 // -1 = waiting for the next round

bool debugMirrorOn() { return mirrorOn; }
void debugMirrorSet(bool on) { mirrorOn = on; nextRow = -1; roundStartMs = 0; }

// One page per pass, so debug lines never crowd out readings or telemetry
void debugMirrorUpdate() {
  if (!mirrorOn) return;
  if (nextRow < 0) {
    if (roundStartMs && millis() - roundStartMs < dbgMirrorMs) return;
    roundStartMs = millis();
    nextRow = 0;
  }
  while (nextRow < featureCount() && !featureTable()[nextRow].debug) nextRow++;
  if (nextRow >= featureCount()) { nextRow = -1; return; }

  const Feature& f = featureTable()[nextRow];
  DebugText t;
  f.debug(t);
  char line[DBG_LINES * (DBG_COLS + 1) + 24];
  int n = snprintf(line, sizeof(line), "D,%s,", f.name);
  for (int i = 0; i < t.count && n < (int)sizeof(line) - 2; i++)
    n += snprintf(line + n, sizeof(line) - n, "%s%s", i ? "|" : "", t.lines[i]);
  if (n > (int)sizeof(line) - 2) n = sizeof(line) - 2;
  line[n++] = '\n';
  line[n] = 0;
  if (linkSendLowPriority(line)) nextRow++;     // no room: try the same page next pass
}
#else
bool debugMirrorOn() { return false; }
void debugMirrorSet(bool) {}
void debugMirrorUpdate() {}
#endif
