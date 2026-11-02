/*
  WiFi Signal Strength display  -  FAST / SLOW modes  +  AP roaming
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

  ROAMING
    The WINC1500 chip never switches access points by itself: it stays on the
    AP it joined until the link drops completely. So this sketch does it:
      - Whenever the signal is below "Excellent" (ROAM_TRIGGER_DBM), it keeps
        scanning (a new scan ROAM_CHECK_MS after the last one finished) for
        other APs broadcasting the SAME network name.
      - If one is at least ROAM_MARGIN_DB stronger than the current AP,
        it disconnects and rejoins on that AP's channel.
    The bottom line shows which AP you're on (last 2 bytes of its MAC address
    and its channel), so you can see a roam happen.
    Limitation: the chip can't be told to join one exact AP, only a channel.
    If two APs share a channel, the chip chooses between them itself.

  BUTTONS
    Upper-left  : switch between FAST and SLOW
    Lower-right : SLOW mode only - restart the average
    Lower-left  : scan and roam now (even if the signal isn't weak)

  Libraries: TinyScreen (TinyCircuits), WiFi101 (Arduino)
  Board: TinyScreen+ (TinyCircuits SAMD board package)
*/

#include <SPI.h>
#include <Wire.h>
#include <WiFi101.h>
#include <TinyScreen.h>
extern "C" {
  #include "driver/include/m2m_wifi.h"   // low-level connect (lets us pick a channel)
}

const char* WIFI_SSID = "NOKIA-6AA1";
const char* WIFI_PASS = "358n8An7tS";   // leave "" for an open network

// ---- Mode settings ----
const unsigned long FAST_INTERVAL_MS = 300;   // fast: time between readings
const unsigned long SLOW_INTERVAL_MS = 150;   // slow: time between samples
const int           SLOW_SAMPLES     = 20;    // slow: samples per average

// ---- Roaming settings ----
const int           ROAM_TRIGGER_DBM = -50;   // look for a better AP whenever below "Excellent"
const int           ROAM_MARGIN_DB   = 8;     // new AP must be this much stronger
const unsigned long ROAM_CHECK_MS    = 3000;  // gap between scans (0 = scan back-to-back)
const unsigned long ROAM_COOLDOWN_MS = 10000; // wait after a roam before scanning again

TinyScreen display = TinyScreen(TinyScreenPlus);

bool slowMode = false;

// Slow-mode averaging state
long sampleSum = 0;
int  sampleCount = 0;
int  sampleMin = 0, sampleMax = -127;
int  lastAverage = 0;          // 0 = no average yet

unsigned long lastReadMs = 0;
unsigned long lastRoamCheckMs = 0;
unsigned long lastRoamMs = 0;
int currentRssi = 0;

// Current AP (for display)
uint8_t currentBssid[6] = {0};
int currentChannel = 0;        // 0 = unknown until the first scan

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

  display.drawRect(0, 12, 96, 2, TSRectangleFilled, TS_8b_Black);
  if (slowMode && sampleCount > 0) {
    int w = (96 * sampleCount) / SLOW_SAMPLES;
    display.drawRect(0, 12, w, 2, TSRectangleFilled, TS_8b_Blue);
  }
}

// Bottom line: which AP we're on
void drawApLine() {
  char buf[24];
  if (currentChannel > 0)
    snprintf(buf, sizeof(buf), "AP %02X:%02X  ch%d", currentBssid[4], currentBssid[5], currentChannel);
  else
    snprintf(buf, sizeof(buf), "AP %02X:%02X", currentBssid[4], currentBssid[5]);
  display.drawRect(0, 54, 96, 10, TSRectangleFilled, TS_8b_Black);
  showText(buf, thinPixel7_10ptFontInfo, TS_8b_Blue, 54);
}

// Big number + label underneath
void drawReading(int rssi, const char* extra) {
  display.drawRect(0, 16, 96, 38, TSRectangleFilled, TS_8b_Black);
  if (rssi == 0) {
    showText("-- dBm", liberationSans_16ptFontInfo, TS_8b_White, 17);
    showText("measuring...", thinPixel7_10ptFontInfo, TS_8b_White, 42);
    return;
  }
  char num[12];
  snprintf(num, sizeof(num), "%d dBm", rssi);
  showText(num, liberationSans_16ptFontInfo, strengthColour(rssi), 17);
  showText(extra ? extra : strengthLabel(rssi), thinPixel7_10ptFontInfo, TS_8b_White, 42);
}

void redrawAll() {
  display.clearScreen();
  drawTopLine();
  drawReading(slowMode ? lastAverage : currentRssi, nullptr);
  drawApLine();
}

// ---------- BSSID helpers ----------
void readCurrentBssid() {
  WiFi.BSSID(currentBssid);
}

// WiFi101 versions differ in byte order between the "current AP" and
// "scan result" BSSID functions, so accept either order.
bool sameBssid(const uint8_t* a, const uint8_t* b) {
  bool fwd = true, rev = true;
  for (int i = 0; i < 6; i++) {
    if (a[i] != b[i])     fwd = false;
    if (a[i] != b[5 - i]) rev = false;
  }
  return fwd || rev;
}

// ---------- Connecting ----------
bool waitForConnect(unsigned long timeoutMs) {
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) return true;
    delay(50);
  }
  return false;
}

void afterConnect() {
  readCurrentBssid();
  resetAverage(true);
  currentRssi = 0;
  redrawAll();
}

// Normal connect: the chip picks whichever AP it finds
void connectWiFi() {
  showMessage("Connecting...");
  currentChannel = 0;
  while (WiFi.status() != WL_CONNECTED) {
    if (strlen(WIFI_PASS) > 0) WiFi.begin(WIFI_SSID, WIFI_PASS);
    else                       WiFi.begin(WIFI_SSID);
    if (WiFi.status() != WL_CONNECTED) showMessage("Retrying...");
  }
  afterConnect();
}

// Join the same network, but only on the given channel (= the better AP)
bool connectOnChannel(uint8_t channel) {
  WiFi.disconnect();
  delay(200);

  uint8_t secType = strlen(WIFI_PASS) > 0 ? M2M_WIFI_SEC_WPA_PSK : M2M_WIFI_SEC_OPEN;
  m2m_wifi_connect((char*)WIFI_SSID, strlen(WIFI_SSID), secType,
                   (void*)WIFI_PASS, channel);
  return waitForConnect(10000);
}

// ---------- Roaming ----------
// Scan for the same SSID. Roam if another AP is clearly stronger.
// force = true roams to the strongest other AP even if the signal isn't weak.
void checkRoam(bool force) {
  lastRoamCheckMs = millis();

  display.drawRect(0, 54, 96, 10, TSRectangleFilled, TS_8b_Black);
  showText("scanning APs...", thinPixel7_10ptFontInfo, TS_8b_Blue, 54);

  int n = WiFi.scanNetworks();   // blocks for a couple of seconds
  if (n <= 0) { drawApLine(); return; }

  int curScanRssi = -127;
  int bestRssi = -127, bestChannel = 0;
  uint8_t bssid[6];

  for (int i = 0; i < n; i++) {
    if (strcmp(WiFi.SSID(i), WIFI_SSID) != 0) continue;
    WiFi.BSSID(i, bssid);
    int rssi = WiFi.RSSI(i);
    int ch = WiFi.channel(i);
    if (sameBssid(bssid, currentBssid)) {
      curScanRssi = rssi;
      currentChannel = ch;
    } else if (rssi > bestRssi) {
      bestRssi = rssi;
      bestChannel = ch;
    }
  }

  // If the current AP didn't show up in the scan, fall back to the live reading
  if (curScanRssi == -127 && currentRssi < 0) curScanRssi = currentRssi;

  bool better = bestChannel > 0 && bestRssi >= curScanRssi + ROAM_MARGIN_DB;
  if (!(better || (force && bestChannel > 0))) { drawApLine(); return; }

  char msg[24];
  snprintf(msg, sizeof(msg), "Roaming to ch%d", bestChannel);
  showMessage(msg);

  if (connectOnChannel(bestChannel)) {
    currentChannel = bestChannel;
    lastRoamMs = millis();
    afterConnect();
  } else {
    connectWiFi();   // fall back to a normal connect
  }
}

void maybeRoam() {
  if (millis() - lastRoamMs < ROAM_COOLDOWN_MS) return;
  if (millis() - lastRoamCheckMs < ROAM_CHECK_MS) return;
  if (currentRssi == 0 || currentRssi >= ROAM_TRIGGER_DBM) return;
  checkRoam(false);
  lastRoamCheckMs = millis();   // gap is measured from the END of the scan
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
    lastAverage = (int)((sampleSum - SLOW_SAMPLES / 2) / SLOW_SAMPLES);   // rounded
    char extra[24];
    snprintf(extra, sizeof(extra), "avg  %d to %d", sampleMin, sampleMax);
    drawReading(lastAverage, extra);
    resetAverage(false);
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
  } else if (display.getButtons(TSButtonLowerLeft)) {
    checkRoam(true);
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
  if (WiFi.status() != WL_CONNECTED) connectWiFi();

  handleButtons();
  maybeRoam();

  unsigned long interval = slowMode ? SLOW_INTERVAL_MS : FAST_INTERVAL_MS;
  if (millis() - lastReadMs < interval) return;
  lastReadMs = millis();

  int rssi = WiFi.RSSI();
  if (rssi >= 0) return;   // ignore invalid readings
  currentRssi = rssi;

  if (slowMode) addSlowSample(rssi);
  else          drawReading(rssi, nullptr);
}
