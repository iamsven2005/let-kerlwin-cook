#include "../common/mode.h"
#if SURVEY_MODE == HAT
#include "ir_sensor.h"
#include "config.h"
#include "heading.h"
#include "hand_pose.h"
#include "tracker.h"
#include "link.h"

bool irYes = false;
static int irHits = 0;
static unsigned long lastSentMs = 0;

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
  irYes = irHits >= IR_CONFIRM_COUNT;
  if (!irYes || handPose == POSE_OTHER || !tracking) return;   // direction unknown: don't plot
  if (millis() - lastSentMs < 250) return;
  lastSentMs = millis();

  float sensorDir = handPose == POSE_HANG ? headingNow() + hangDirDeg : headingNow();
  float d = (handPose == POSE_HANG ? hangMm : pointMm) + 0.6f * irRangeMm;
  float hx = posX + d * cos(rad(sensorDir)), hy = posY + d * sin(rad(sensorDir));
  char line[64];
  snprintf(line, sizeof(line), "O,%lu,%ld,%ld,0,%ld\n", millis(), lround(hx), lround(hy), lround(wrap180(sensorDir)));
  linkSendLine(line);
}

#endif // SURVEY_MODE == HAT
