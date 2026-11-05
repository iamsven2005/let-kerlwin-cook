#include <Wire.h>
#include <SPI.h>
#include <TinyScreen.h>
#include <Wireling.h>

// Initialize TinyScreen+ (ASM2022)
TinyScreen display = TinyScreen(TinyScreenPlus);

// Wireling Adapter Port Pin Mapping
const int EMITTER_PIN = A0;  // Port 0: IR Emitter Wireling
const int RECEIVER_PIN = A1; // Port 1: 38kHz IR Receiver Wireling

// Send a short burst of IR pulses at a specific frequency (in kHz)
// Returns true if the IR Receiver detects the reflection off the wall
bool testReflection(int freqKhz) {
  int halfPeriodUs = 500 / freqKhz; // Microseconds per half-wave (e.g., 13us for 38kHz)
  int detectedCount = 0;

  // Send 25 pulses (~650 microseconds burst)
  for (int i = 0; i < 25; i++) {
    digitalWrite(EMITTER_PIN, HIGH);
    delayMicroseconds(halfPeriodUs);
    digitalWrite(EMITTER_PIN, LOW);
    delayMicroseconds(halfPeriodUs);

    // The 38kHz IR Receiver outputs LOW when it sees the IR signal
    if (digitalRead(RECEIVER_PIN) == LOW) {
      detectedCount++;
    }
  }

  delay(2); // Brief pause so the receiver's Automatic Gain Control resets
  return (detectedCount > 5);
}

void setup() {
  Wire.begin();
  
  // Initialize power to the Wireling Adapter ports
  Wireling.begin();

  pinMode(EMITTER_PIN, OUTPUT);
  digitalWrite(EMITTER_PIN, LOW);
  pinMode(RECEIVER_PIN, INPUT_PULLUP);

  // Initialize the OLED screen
  display.begin();
  display.setBrightness(10);
  display.setFont(thinPixel7_10ptFontInfo);
}

void loop() {
  int reflectionStrength = 0;

  // Sweep frequencies from 38kHz (max sensitivity) to 43kHz (min sensitivity)
  // Closer walls reflect stronger light, triggering higher off-center frequencies
  for (int freq = 38; freq <= 43; freq++) {
    if (testReflection(freq)) {
      reflectionStrength++;
    }
  }

  // Clear the screen for the new reading
  display.clearScreen();

  // Header title
  display.setCursor(8, 4);
  display.fontColor(TS_8b_White, TS_8b_Black);
  display.print("IR Wall Distance");

  // Estimate distance based on reflection strength (0 to 6)
  display.setCursor(8, 22);
  if (reflectionStrength == 0) {
    display.fontColor(TS_8b_Red, TS_8b_Black);
    display.print("Out of Range");
    display.setCursor(8, 36);
    display.print("(Over 40 cm)");
  } else {
    // Convert reflection strength (1=far, 6=very close) to approximate cm
    int approxCm = (7 - reflectionStrength) * 6;

    if (reflectionStrength >= 5) {
      display.fontColor(TS_8b_Green, TS_8b_Black); // Close
    } else {
      display.fontColor(TS_8b_Yellow, TS_8b_Black); // Medium
    }

    display.print("Dist: ~");
    display.print(approxCm);
    display.print(" cm");
  }

  // Draw a visual proximity bar at the bottom of the screen
  display.drawRect(8, 50, 80, 10, TSRectangleNoFill, TS_8b_White);
  int barWidth = (reflectionStrength * 76) / 6;
  if (barWidth > 0) {
    display.drawRect(10, 52, barWidth, 6, TSRectangleFilled, TS_8b_Green);
  }

  delay(150); // Update ~6 times per second
}
