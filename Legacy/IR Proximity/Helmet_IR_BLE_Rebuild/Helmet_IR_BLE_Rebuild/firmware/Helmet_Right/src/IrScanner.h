#pragma once
#include <Arduino.h>

class IrScanner {
 public:
  uint8_t values[2] = {0, 0}; // outer A0/A1, inner A2/A3
  uint16_t sequence = 0;
  bool hasSample = false;

  void begin() {
    for (uint8_t i = 0; i < 2; ++i) {
      pinMode(emitters[i], OUTPUT);
      digitalWrite(emitters[i], LOW);
      pinMode(receivers[i], INPUT_PULLUP);
    }
    nextScan = millis();
  }

  void tick() {
    uint32_t now = millis();
    if (!active) {
      if (static_cast<int32_t>(now - nextScan) < 0) return;
      active = true; pair = 0; frequency = 38;
      working[0] = working[1] = 0;
      nextScan = now + 150;
    }
    if (cooling) {
      // Original 2 ms receiver recovery pause, without blocking BLE polling.
      if (static_cast<uint32_t>(micros() - burstEnded) < 2000) return;
      cooling = false;
      if (++frequency > 43) {
        frequency = 38;
        if (++pair == 2) {
          values[0] = working[0]; values[1] = working[1];
          ++sequence; hasSample = true; active = false;
          return;
        }
      }
    }
    // Kerlwin's algorithm unchanged inside each burst. Both emitters are
    // otherwise LOW. Do not call BLE / draw / print inside this pulse loop.
    const int halfPeriodUs = 500 / frequency;
    int detectedCount = 0;
    for (int pulse = 0; pulse < 25; ++pulse) {
      digitalWrite(emitters[pair], HIGH);
      delayMicroseconds(halfPeriodUs);
      digitalWrite(emitters[pair], LOW);
      delayMicroseconds(halfPeriodUs);
      if (digitalRead(receivers[pair]) == LOW) ++detectedCount;
    }
    if (detectedCount > 5) ++working[pair];
    burstEnded = micros(); cooling = true;
  }

 private:
  const uint8_t emitters[2] = {A0, A2};
  const uint8_t receivers[2] = {A1, A3};
  uint8_t working[2] = {0, 0};
  uint8_t pair = 0, frequency = 38;
  bool active = false, cooling = false;
  uint32_t nextScan = 0, burstEnded = 0;
};
