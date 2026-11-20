#include "ir_sensor.h"
#if FEAT_IR
#include "../core/state.h"

static int irHits = 0;
static unsigned long yesCount = 0, lastYesMs = 0;
static unsigned long winStartMs = 0, winYes = 0, yesPerSec = 0;

void irBegin() {
  pinMode(IR_EMITTER_PIN, OUTPUT);
  digitalWrite(IR_EMITTER_PIN, LOW);
  pinMode(IR_RECEIVER_PIN, INPUT_PULLUP);
}

// Flash the emitter at ~38 kHz; the receiver goes LOW if it sees the reflection
static bool readIr() {
  bool seen = false;
  for (int i = 0; i < IR_BURST_CYCLES; i++) {
    digitalWrite(IR_EMITTER_PIN, HIGH); delayMicroseconds(IR_HALF_PERIOD_US);
    digitalWrite(IR_EMITTER_PIN, LOW);  delayMicroseconds(IR_HALF_PERIOD_US);
    if (i >= 8 && digitalRead(IR_RECEIVER_PIN) == LOW) seen = true;
  }
  delayMicroseconds(150);
  if (digitalRead(IR_RECEIVER_PIN) == LOW) seen = true;
  return seen;
}

void irUpdate() {
  if (readIr()) { if (irHits < IR_CONFIRM_COUNT) irHits++; }
  else irHits = 0;
  bool was = irYes;
  irYes = irHits >= IR_CONFIRM_COUNT;
  if (irYes && !was) { yesCount++; lastYesMs = millis(); }
  if (irYes) winYes++;
  if (millis() - winStartMs >= 1000) { yesPerSec = winYes; winYes = 0; winStartMs = millis(); }
}

#if DEBUG_ANY
void irDebug(DebugText& t) {
  t.add("IR %s  confirm %d/%d", irYes ? "YES" : "no", irHits, IR_CONFIRM_COUNT);
  t.add("YES reads/s %lu", yesPerSec);
  t.add("detections %lu", yesCount);
  if (lastYesMs) t.add("last %lu s ago", (millis() - lastYesMs) / 1000);
  t.add("range R %ld mm", lround(irRangeMm));
}
#endif

#endif
