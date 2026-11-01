/*
  WiFi Signal Strength display  -  FAST / SLOW modes
  TinyCircuits TinyScreen+  +  WiFi TinyShield (ATWINC1500)

  Shows the WiFi signal strength (RSSI, in dBm) as a big number.
  Closer to 0 is stronger:
    -50 or above   Excellent (green)
    -50 to -70     Good      (yellow)
    below -70      Weak      (red)

  MODES
    FAST : a new reading every 0.3 s. Good while moving.
    SLOW : takes 20 readings over about 3 s at one spot and shows their
           average, plus the spread (lowest to highest). Then starts again.
           Use it when you're holding still, for a steadier number.

  BUTTONS
    Upper-left  : switch between FAST and SLOW
    Lower-right : SLOW mode only - restart the average (press after moving
                  to a new spot so old readings aren't mixed in)

  Libraries: TinyScreen (TinyCircuits), WiFi101 (Arduino)
  Board: TinyScreen+ (TinyCircuits SAMD board package)
*/

#include <SPI.h>
#include <Wire.h>
#include <WiFi101.h>
#include <TinyScreen.h>

const char* WIFI_SSID = "NOKIA-6AA1";
const char* WIFI_PASS = "358n8An7tS";

// ---- Mode settings ----
const unsigned long FAST_INTERVAL_MS = 300;   // fast: time between readings
const unsigned long SLOW_INTERVAL_MS = 150;   // slow: time between samples
const int           SLOW_SAMPLES     = 20;    // slow: samples per average

TinyScreen display = TinyScreen(TinyScreenPlus);

bool slowMode = false;

// Slow-mode averaging state
long sampleSum = 0;
int  sampleCount = 0;
int  sampleMin = 0, sampleMax = -127;
int  lastAverage = 0;          // 0 = no average yet
int  lastMin = 0, lastMax = 0;

unsigned long lastReadMs = 0;

// ---------- Drawing ----------
void showText(const char* text, const FONT_INFO& font, uint8_t colour, int y) {
  display.setFont(font);
  display.fontColor(colour, TS_8b_Black);
  int x = (96 - display.getPrintWidth((char*)text)) / 2;
  if (x < 0) x = 0;
  display.setCursor(x, y);
  display.print(text);
}

void showMessage(const char* msg) {
  display.clearScreen();
  showText(msg, thinPixel7_10ptFontInfo, TS_8b_White, 28);
}

uint8_t strengthColour(int rssi) {
  if (rssi >= -50) return TS_8b_Green;
  if (rssi >= -70) return TS_8b_Yellow;
  return TS_8b_Red;
}

const char* strengthLabel(int rssi) {
  if (rssi >= -50) return "Excellent";
  if (rssi >= -70) return "Good";
  return "Weak";
}

// Top line: mode name (and progress in slow mode)
void drawTopLine() {
  char buf[24];
  if (slowMode) snprintf(buf, sizeof(buf), "SLOW  %d/%d", sampleCount, SLOW_SAMPLES);
  else          snprintf(buf, sizeof(buf), "FAST");
  display.drawRect(0, 0, 96, 12, TSRectangleFilled, TS_8b_Black);
  showText(buf, thinPixel7_10ptFontInfo, TS_8b_Blue, 1);

  // progress bar under the top line in slow mode
  display.drawRect(0, 12, 96, 2, TSRectangleFilled, TS_8b_Black);
  if (slowMode && sampleCount > 0) {
    int w = (96 * sampleCount) / SLOW_SAMPLES;
    display.drawRect(0, 12, w, 2, TSRectangleFilled, TS_8b_Blue);
  }
}

// Big number + label underneath
void drawReading(int rssi, const char* extra) {
  display.drawRect(0, 18, 96, 46, TSRectangleFilled, TS_8b_Black);
  if (rssi == 0) {
    showText("-- dBm", liberationSans_16ptFontInfo, TS_8b_White, 20);
    showText("measuring...", thinPixel7_10ptFontInfo, TS_8b_White, 46);
    return;
  }
  char num[12];
  snprintf(num, sizeof(num), "%d dBm", rssi);
  showText(num, liberationSans_16ptFontInfo, strengthColour(rssi), 20);
  showText(extra ? extra : strengthLabel(rssi), thinPixel7_10ptFontInfo, TS_8b_White, 46);
}

// ---------- WiFi ----------
void connectWiFi() {
  showMessage("Connecting...");
  while (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(WIFI_SSID, WIFI_PASS);   // waits up to ~10 s
    if (WiFi.status() != WL_CONNECTED) showMessage("Retrying...");
  }
  display.clearScreen();
  drawTopLine();
}

// ---------- Slow-mode averaging ----------
void resetAverage(bool clearShown) {
  sampleSum = 0;
  sampleCount = 0;
  sampleMin = 0;
  sampleMax = -127;
  if (clearShown) lastAverage = 0;
}

void addSlowSample(int rssi) {
  sampleSum += rssi;
  sampleCount++;
  if (rssi < sampleMin) sampleMin = rssi;
  if (rssi > sampleMax) sampleMax = rssi;

  if (sampleCount >= SLOW_SAMPLES) {
    // round to nearest whole dBm
    lastAverage = (int)((sampleSum - SLOW_SAMPLES / 2) / SLOW_SAMPLES);
    lastMin = sampleMin;
    lastMax = sampleMax;

    char extra[24];
    snprintf(extra, sizeof(extra), "avg  %d to %d", lastMin, lastMax);
    drawReading(lastAverage, extra);
    resetAverage(false);   // keep showing this average while the next batch runs
  } else if (lastAverage == 0) {
    drawReading(0, nullptr);
  }
  drawTopLine();
}

// ---------- Buttons ----------
void handleButtons() {
  static unsigned long lastPress = 0;
  if (millis() - lastPress < 300) return;

  if (display.getButtons(TSButtonUpperLeft)) {
    slowMode = !slowMode;
    resetAverage(true);
    drawTopLine();
    drawReading(0, nullptr);
    lastPress = millis();
  } else if (slowMode && display.getButtons(TSButtonLowerRight)) {
    resetAverage(true);
    drawTopLine();
    drawReading(0, nullptr);
    lastPress = millis();
  }
}

// ---------- Setup / loop ----------
void setup() {
  Wire.begin();
  display.begin();
  display.setBrightness(10);
  display.setBitDepth(TSBitDepth8);

  WiFi.setPins(8, 2, A3, -1);   // WiFi TinyShield pins
  if (WiFi.status() == WL_NO_SHIELD) {
    showMessage("No WiFi shield!");
    while (true) delay(1000);
  }
  connectWiFi();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    resetAverage(true);
  }

  handleButtons();

  unsigned long interval = slowMode ? SLOW_INTERVAL_MS : FAST_INTERVAL_MS;
  if (millis() - lastReadMs < interval) return;
  lastReadMs = millis();

  int rssi = WiFi.RSSI();
  if (rssi >= 0) return;   // ignore invalid readings

  if (slowMode) addSlowSample(rssi);
  else          drawReading(rssi, nullptr);
}
