/*
  feature.h - what every feature offers the main loop, and the text page it can show.

  A feature is a row in the table in main_loop.cpp:
    name     short name (shown as the debug page title, sent to the app)
    begin    called once in setup()             (nullptr = nothing to do)
    update   called from loop() every periodMs  (0 = every pass; nullptr = nothing)
    debug    writes the feature's debug page    (nullptr = no page)
*/
#pragma once
#include <Arduino.h>
#include "config.h"

// One debug page: up to DBG_LINES short lines. Lines must not contain '|'.
struct DebugText {
  char lines[DBG_LINES][DBG_COLS + 1];
  int count = 0;
  void clear() { count = 0; }
  void add(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
};

inline const char* onOff(bool b) { return b ? "on" : "off"; }

typedef void (*FeatureFn)();
typedef void (*DebugFn)(DebugText&);

struct Feature {
  const char* name;
  FeatureFn begin;
  FeatureFn update;
  uint16_t periodMs;
  DebugFn debug;
};

// Debug functions only exist when a debug option is on (features.h)
#if DEBUG_ANY
#define DBG(fn) fn
#else
#define DBG(fn) nullptr
#endif

// The feature table (main_loop.cpp)
const Feature* featureTable();
int featureCount();
