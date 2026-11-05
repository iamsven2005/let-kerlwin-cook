#include <Wire.h>
#include <Wireling.h>
#include <TinyScreen.h>
#include "diagnostic.h"

// Verify against your actual adapter/board: legacy sketches disagree on pins.
const uint8_t EMITTER_PIN = A2;
const uint8_t RECEIVER_PIN = A0;
const unsigned int frequencies[] = {38000, 36500, 35000, 33500, 32000, 30500, 29000};
TinyScreen display(TinyScreenPlus);

bool ambientIRPresent() {
  unsigned long start = millis();
  while (millis() - start < 30)
    if (digitalRead(RECEIVER_PIN) == LOW) return true;
  return false;
}

bool burstSeen(unsigned int frequency) {
  bool seen = false;
  tone(EMITTER_PIN, frequency);
  unsigned long start = micros();
  while (micros() - start < 1000)
    if (digitalRead(RECEIVER_PIN) == LOW) seen = true;
  noTone(EMITTER_PIN);
  digitalWrite(EMITTER_PIN, LOW);
  start = micros();
  while (micros() - start < 400)
    if (digitalRead(RECEIVER_PIN) == LOW) seen = true;
  delay(5);
  return seen;
}

void setup() {
  SerialUSB.begin(115200);
  Wire.begin();
  Wireling.begin();
  pinMode(EMITTER_PIN, OUTPUT);
  digitalWrite(EMITTER_PIN, LOW);
  pinMode(RECEIVER_PIN, INPUT_PULLUP);
  display.begin();
  display.setBrightness(10);
  display.setFont(liberationSans_8ptFontInfo);
  display.fontColor(TS_8b_White, TS_8b_Black);
  SerialUSB.println(F("IR diagnostic only: uncalibrated, NOT safety or distance"));
}

void loop() {
  bool ambient = ambientIRPresent();
  uint8_t hits[ir_diagnostic::frequencyCount] = {};
  // Scan all frequencies rather than masking non-monotonic responses.
  for (uint8_t i = 0; i < ir_diagnostic::frequencyCount; ++i)
    for (uint8_t j = 0; j < ir_diagnostic::tries; ++j)
      if (burstSeen(frequencies[i])) ++hits[i];
  uint8_t level = ir_diagnostic::consecutiveLevel(hits);
  uint8_t detected = ir_diagnostic::hitCount(hits);
  SerialUSB.print(F("IR level=")); SerialUSB.print(level);
  SerialUSB.print(F("/7, detected=")); SerialUSB.print(detected);
  SerialUSB.print(F("/7, ambient=")); SerialUSB.print(ambient ? 1 : 0);
  SerialUSB.print(F(", hits="));
  for (uint8_t i = 0; i < ir_diagnostic::frequencyCount; ++i) {
    if (i) SerialUSB.print(',');
    SerialUSB.print(hits[i]);
  }
  SerialUSB.println();
  display.clearScreen();
  display.setCursor(0, 0); display.print("IR diagnostic");
  display.setCursor(0, 14); display.print("Level: "); display.print(level); display.print("/7");
  display.drawRect(0, 30, 96, 12, TSRectangleNoFill, TS_8b_White);
  uint8_t width = static_cast<uint8_t>((level * 94U) / 7U);
  if (width) display.drawRect(1, 31, width, 10, TSRectangleFilled, TS_8b_Green);
  if (ambient) { display.setCursor(0, 48); display.print("External IR seen"); }
  delay(100);
}
