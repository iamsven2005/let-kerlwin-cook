/*
  PHASE 1 - TEST 1: IR emitter + IR receiver proximity check
  ----------------------------------------------------------
  Hardware:
    TinyScreen+  ->  Wireling Adapter TinyShield
    IR Emitter Wireling   -> Port 0  (pin A0)
    IR Receiver Wireling  -> Port 1  (pin A1)
  (Avoid Port 3 / A3 - the WiFi TinyShield uses A3 as its reset pin.)

  How it works:
    The 38kHz receiver is DIGITAL - it only says "I see a 38kHz signal" or not.
    It cannot measure light intensity directly. To get a rough distance, we
    send short IR bursts at frequencies further and further away from 38kHz.
    The receiver gets less sensitive off-centre, so off-centre bursts are only
    detected when the reflecting object is CLOSE.
      Level 0              = nothing detected (far / no object)
      Level 1              = detected only at 38kHz (object far-ish)
      Level max (NUM_FREQS)= detected even far off-centre (object very close)
*/

#include <Wire.h>
#include <Wireling.h>
#include <TinyScreen.h>

#define SerialMonitor SerialUSB   // TinyScreen+ uses native USB serial

const uint8_t EMITTER_PIN  = A2;  // Wireling port 0
const uint8_t RECEIVER_PIN = A0;  // Wireling port 1

// Test frequencies: from most sensitive (38kHz) to least sensitive
const unsigned int FREQS[] = {38000, 36500, 35000, 33500, 32000, 30500, 29000};
const uint8_t NUM_FREQS = sizeof(FREQS) / sizeof(FREQS[0]);

const uint8_t TRIES       = 5;    // bursts per frequency
const uint8_t HITS_NEEDED = 3;    // how many must be seen to count as "detected"

TinyScreen display = TinyScreen(TinyScreenPlus);

// With the emitter OFF, any LOW on the receiver means IR from somewhere else
// (TV remote, sunlight, fluorescent lights). Useful for checking the receiver works.
bool ambientIRPresent() {
  unsigned long t = millis();
  while (millis() - t < 30) {
    if (digitalRead(RECEIVER_PIN) == LOW) return true;
  }
  return false;
}

// Send one short burst and watch the receiver (output goes LOW when it sees IR)
bool burstSeen(unsigned int freq) {
  bool seen = false;

  tone(EMITTER_PIN, freq);                         // start carrier
  unsigned long t = micros();
  while (micros() - t < 1000) {                     // ~23 cycles at 38kHz
    if (digitalRead(RECEIVER_PIN) == LOW) seen = true;
  }
  noTone(EMITTER_PIN);                             // stop carrier
  digitalWrite(EMITTER_PIN, LOW);

  t = micros();
  while (micros() - t < 400) {                     // receiver output lags a little
    if (digitalRead(RECEIVER_PIN) == LOW) seen = true;
  }

  delay(5);                                        // gap so receiver can recover
  return seen;
}

bool reliablySeen(unsigned int freq) {
  uint8_t hits = 0;
  for (uint8_t i = 0; i < TRIES; i++) {
    if (burstSeen(freq)) hits++;
  }
  return hits >= HITS_NEEDED;
}

uint8_t proximityLevel() {
  uint8_t level = 0;
  for (uint8_t i = 0; i < NUM_FREQS; i++) {
    if (reliablySeen(FREQS[i])) level = i + 1;
    else break;                                    // stop at first miss
  }
  return level;
}

void showOnScreen(uint8_t level, bool ambient) {
  display.clearScreen();
  display.setCursor(0, 0);
  display.print("IR proximity");

  display.setCursor(0, 14);
  display.print("Level: ");
  display.print(level);
  display.print(" / ");
  display.print(NUM_FREQS);

  // bar graph
  display.drawRect(0, 30, 96, 12, TSRectangleNoFill, TS_8b_White);
  uint8_t w = map(level, 0, NUM_FREQS, 0, 94);
  if (w > 0) display.drawRect(1, 31, w, 10, TSRectangleFilled, TS_8b_Green);

  if (ambient) {
    display.setCursor(0, 48);
    display.print("External IR seen");
  }
}

void setup() {
  SerialMonitor.begin(115200);
  while (!SerialMonitor && millis() < 3000);       // wait up to 3s for Serial Monitor

  Wire.begin();
  Wireling.begin();                                // powers the Wireling ports

  pinMode(EMITTER_PIN, OUTPUT);
  digitalWrite(EMITTER_PIN, LOW);
  pinMode(RECEIVER_PIN, INPUT_PULLUP);             // receiver output is active LOW

  display.begin();
  display.setBrightness(10);
  display.setFont(liberationSans_8ptFontInfo);
  display.fontColor(TS_8b_White, TS_8b_Black);

  SerialMonitor.println(F("=== IR proximity test ==="));
  SerialMonitor.println(F("Move your hand / a card toward the sensors."));
}

void loop() {
  bool ambient = ambientIRPresent();
  uint8_t level = proximityLevel();

  SerialMonitor.print(F("Level: "));
  SerialMonitor.print(level);
  SerialMonitor.print(F("/"));
  SerialMonitor.print(NUM_FREQS);
  if (ambient) SerialMonitor.print(F("   (external IR detected)"));
  SerialMonitor.println();

  showOnScreen(level, ambient);
  delay(100);
}
