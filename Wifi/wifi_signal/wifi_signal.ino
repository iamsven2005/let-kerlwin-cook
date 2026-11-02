/*
  WiFi Signal Strength module
  FAST / SLOW modes  +  AP roaming  +  reading cache  +  Bluetooth send queue
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

  FILES  (keep all three in the same sketch folder, named wifi_signal)
    wifi_signal.ino   screen, modes, cache, Bluetooth queue, settings from brain
    wifi_logic.h      WiFi functions this sketch can call
    wifi_logic.cpp    WiFi connect, signal readings and AP roaming
                      (WiFi + roaming default settings are at the top of it)

  ROAMING
    Handled in wifi_logic.cpp. Whenever the signal is below "Excellent", it
    keeps scanning for other APs with the same network name and moves to a
    clearly stronger one. No readings are taken during a scan (about 2 s).

  READING CACHE
    Every reading is stored in a cache that holds CACHE_SIZE readings:
      - FAST mode: every reading
      - SLOW mode: every finished average (with its lowest and highest sample)
    When the cache is full, the oldest reading is overwritten.
    The top line shows how many readings are waiting, e.g. "Q12".

  SENDING TO THE BRAIN (master)
    Readings are sent oldest-first, one at a time. Each one stays in the
    cache until the brain acknowledges it. If no acknowledgement arrives
    within ackTimeoutMs, the same reading is sent again.

    This module -> brain:
      R,seq,mode,rssi,min,max,age
        seq   reading number (0-65535, wraps around)
        mode  F = fast (single reading), S = slow (average)
        rssi  dBm (the average in slow mode)
        min   lowest sample  (same as rssi in fast mode)
        max   highest sample (same as rssi in fast mode)
        age   ms between taking the reading and sending it, so the brain can
              work out when (and so where) it was measured
      Example:  R,1042,S,-63,-71,-58,350

    Brain -> this module:
      A,seq      acknowledgement: "I got reading seq, you can delete it"

  INIT FROM BRAIN
    At startup this module sends  I,WIFI  once a second and waits up to
    INIT_WAIT_MS (10 s) for settings. The brain sends any of these, then C,GO:
      C,ssid,MyNetwork        C,pass,MyPassword
      C,fast_ms,300           C,slow_ms,150          C,slow_n,20
      C,roam_dbm,-50          C,roam_margin,8
      C,roam_check_ms,3000    C,roam_cool_ms,10000
      C,ack_ms,500            C,display,1   (0 = screen off)
      C,mode,F                (F = fast, S = slow)
      C,GO                    finished: start measuring
    Each line is answered with  CA,key  (accepted) or  CE,key  (unknown key
    or value out of range; the default is kept). Settings that aren't sent
    keep their defaults. If C,GO never arrives, the module starts with
    whatever it received plus defaults when the wait runs out.
    C,key,value lines sent later are applied straight away (a new ssid or
    pass makes it reconnect).

      C,brain_retries,5       see INDEPENDENT MODE

  INDEPENDENT MODE  (debug mode only, i.e. debugDisplay = true)
    If the brain doesn't answer after brainMaxRetries tries in a row, the module
    stops all Bluetooth sending and works on its own: it keeps measuring,
    roaming and showing readings on the screen, but no longer caches or sends
    them. The top line shows "SOLO". It stays that way until the next reset.
    What counts as one try:
      - at startup: an I,WIFI hello (once a second) that gets no reply
      - while running: a reading resent because no acknowledgement came, or
        one ack_ms period with the Bluetooth link disconnected
    Anything received from the brain resets the count.
    With the screen off (debugDisplay = false) the module never goes
    independent; it keeps retrying, because then the brain is its only output.

    The Bluetooth parts are placeholders: see the "Bluetooth link" section.
    Until you fill them in, messages go over USB serial instead, so you can
    test by opening the Serial Monitor and typing acknowledgements (A,0  A,1 ...).

  BUTTONS
    Upper-left  : switch between FAST and SLOW
    Lower-right : SLOW mode only - restart the average
    Lower-left  : scan and roam now (even if the signal isn't weak)

  Libraries: TinyScreen (TinyCircuits), WiFi101 (Arduino)
  Board: TinyScreen+ (TinyCircuits SAMD board package)
*/

#include <SPI.h>
#include <Wire.h>
#include <TinyScreen.h>
#include "wifi_logic.h"      // WiFi connect, readings and roaming

// =====================================================================
//  Settings
//  These are DEFAULTS. At startup the brain can override any of them
//  (see "Init from brain"). Anything it doesn't send keeps the value here.
//  WiFi network and roaming defaults are at the top of wifi_logic.cpp.
// =====================================================================

// ---- Mode settings ----
unsigned long fastIntervalMs = 300;   // key: fast_ms   fast: time between readings
unsigned long slowIntervalMs = 150;   // key: slow_ms   slow: time between samples
int           slowSamples    = 20;    // key: slow_n    slow: samples per average

// ---- Cache / sending settings ----
const int     CACHE_SIZE     = 300;   // fixed at compile time (it sets the memory used)
unsigned long ackTimeoutMs   = 500;   // key: ack_ms   resend if no acknowledgement in this time

// ---- Init ----
const unsigned long INIT_WAIT_MS = 10000;  // give up waiting for the brain after this

// ---- Independent mode (debug mode only) ----
int  brainMaxRetries = 5;      // key: brain_retries  unanswered tries before going independent
bool independentMode = false;  // true = no brain: Bluetooth sending is off
int  brainRetries    = 0;      // unanswered tries in a row

// Use USB serial instead of Bluetooth until the Bluetooth placeholders are filled in
#define LINK_OVER_USB_FOR_TESTING 1

// ---- Debug display ----
// true  = show readings, mode, queue and AP info on the screen
// false = screen switched off and nothing drawn, to save power.
//         Measuring, roaming, caching and sending all keep running.
// It can also be changed while running with setDebugDisplay(true/false).
bool debugDisplay = true;

TinyScreen display = TinyScreen(TinyScreenPlus);
#define SerialMonitor SerialUSB

bool slowMode = false;

// Slow-mode averaging state
long sampleSum = 0;
int  sampleCount = 0;
int  sampleMin = 0, sampleMax = -127;
int  lastAverage = 0;          // 0 = no average yet

unsigned long lastReadMs = 0;
int currentRssi = 0;           // latest reading (0 = none yet)

// =====================================================================
//  Reading cache (ring buffer, overwrites the oldest when full)
// =====================================================================
struct Reading {
  uint16_t seq;        // reading number, used to match acknowledgements
  uint32_t takenMs;    // millis() when the reading was taken
  int8_t   rssi;       // dBm (average in slow mode)
  int8_t   minRssi;    // lowest sample  (= rssi in fast mode)
  int8_t   maxRssi;    // highest sample (= rssi in fast mode)
  char     mode;       // 'F' or 'S'
};

Reading  cache[CACHE_SIZE];
int      cacheHead  = 0;     // index of the oldest reading
int      cacheCount = 0;     // how many readings are stored
uint16_t nextSeq    = 0;
uint32_t overwrittenCount = 0;   // readings lost because the cache was full

void cachePush(char mode, int rssi, int minRssi, int maxRssi) {
  if (independentMode) return;   // nobody to send to, so don't store
  Reading r;
  r.seq     = nextSeq++;
  r.takenMs = millis();
  r.rssi    = (int8_t)rssi;
  r.minRssi = (int8_t)minRssi;
  r.maxRssi = (int8_t)maxRssi;
  r.mode    = mode;

  if (cacheCount == CACHE_SIZE) {
    // Full: overwrite the oldest reading
    cache[cacheHead] = r;
    cacheHead = (cacheHead + 1) % CACHE_SIZE;
    overwrittenCount++;
  } else {
    cache[(cacheHead + cacheCount) % CACHE_SIZE] = r;
    cacheCount++;
  }
}

bool cachePeekOldest(Reading &out) {
  if (cacheCount == 0) return false;
  out = cache[cacheHead];
  return true;
}

void cacheRemoveOldest() {
  if (cacheCount == 0) return;
  cacheHead = (cacheHead + 1) % CACHE_SIZE;
  cacheCount--;
}

// =====================================================================
//  Bluetooth link  (PLACEHOLDERS - put your BLE TinyShield code here)
// =====================================================================

void onLinkLine(char *line);   // handles one complete line from the brain

// Called once from setup(): start the BLE shield / advertise / wait for the brain.
void linkBegin() {
#if LINK_OVER_USB_FOR_TESTING
  SerialMonitor.begin(115200);
#else
  // TODO: your BLE TinyShield setup code
#endif
}

// Return true when the brain is connected and ready to receive.
bool linkConnected() {
#if LINK_OVER_USB_FOR_TESTING
  return true;
#else
  // TODO: return your BLE "connected" flag
  return false;
#endif
}

// Send one line to the brain (this adds the '\n').
// Lines can be up to ~30 bytes. A standard BLE packet carries 20 bytes, so
// either split the line across packets (the brain joins them back up at '\n')
// or use a larger MTU if your BLE code supports it.
void linkSendLine(const char *line) {
  if (independentMode) return;   // Bluetooth sending disabled
#if LINK_OVER_USB_FOR_TESTING
  SerialMonitor.println(line);
#else
  // TODO: your BLE send code, e.g.
  //   yourBleSend(line, strlen(line));
  //   yourBleSend("\n", 1);
  (void)line;
#endif
}

// Called once when the module goes independent: stop the BLE shield to save power.
void linkEnd() {
#if LINK_OVER_USB_FOR_TESTING
  // nothing to stop for USB
#else
  // TODO: stop advertising / put the BLE shield to sleep
#endif
}

// Collects incoming bytes into lines. Call it with whatever bytes arrive.
char linkBuf[96];   // long enough for "C,pass," + a 63-character password
int  linkLen = 0;
void linkReceiveBytes(const uint8_t *data, int len) {
  for (int i = 0; i < len; i++) {
    char ch = (char)data[i];
    if (ch == '\n' || ch == '\r') {
      if (linkLen > 0) { linkBuf[linkLen] = 0; onLinkLine(linkBuf); linkLen = 0; }
    } else if (linkLen < (int)sizeof(linkBuf) - 1) {
      linkBuf[linkLen++] = ch;
    } else {
      linkLen = 0;   // too long: drop it
    }
  }
}

// Called every loop(): check for bytes from the brain.
void linkPoll() {
#if LINK_OVER_USB_FOR_TESTING
  uint8_t buf[32];
  int n = 0;
  while (SerialMonitor.available() && n < (int)sizeof(buf)) buf[n++] = SerialMonitor.read();
  if (n) linkReceiveBytes(buf, n);
#else
  // TODO: run your BLE update routine; when data arrives call:
  //   linkReceiveBytes(rxData, rxLength);
#endif
}

// =====================================================================
//  Send queue: one reading at a time, wait for "A,seq" before removing it
// =====================================================================
bool          awaitingAck = false;
uint16_t      sentSeq     = 0;
unsigned long sentAtMs    = 0;

void sendOldest() {
  Reading r;
  if (!cachePeekOldest(r)) return;

  char line[40];
  snprintf(line, sizeof(line), "R,%u,%c,%d,%d,%d,%lu",
           r.seq, r.mode, r.rssi, r.minRssi, r.maxRssi,
           (unsigned long)(millis() - r.takenMs));
  linkSendLine(line);

  awaitingAck = true;
  sentSeq     = r.seq;
  sentAtMs    = millis();
}

// =====================================================================
//  Brain detection -> independent mode (debug mode only)
// =====================================================================
void drawTopLine();

void enterIndependentMode() {
  independentMode = true;
  awaitingAck = false;
  cacheHead = 0;               // nothing will be sent, so free the cache
  cacheCount = 0;
  linkEnd();                   // switch the BLE shield off
  drawTopLine();               // top line now shows "SOLO"
}

// One more try with no answer from the brain
void noteBrainRetry() {
  brainRetries++;
  if (debugDisplay && !independentMode && brainRetries >= brainMaxRetries) {
    enterIndependentMode();
  }
}

// Anything received from the brain
void noteBrainHeard() {
  brainRetries = 0;
}

// Called every loop()
void serviceSendQueue() {
  if (independentMode) return;   // no brain: sending is off

  if (!linkConnected()) {
    awaitingAck = false;
    // While disconnected, every ackTimeoutMs with data waiting counts as a try
    static unsigned long lastTryMs = 0;
    if (cacheCount > 0 && millis() - lastTryMs >= ackTimeoutMs) {
      lastTryMs = millis();
      noteBrainRetry();
    }
    return;
  }

  if (awaitingAck) {
    Reading r;
    bool stillThere = cachePeekOldest(r) && r.seq == sentSeq;
    if (!stillThere) {
      // The reading we sent was overwritten while we waited (cache full).
      // Move on to the new oldest reading.
      awaitingAck = false;
    } else if (millis() - sentAtMs >= ackTimeoutMs) {
      noteBrainRetry();      // no acknowledgement in time
      if (independentMode) return;
      sendOldest();          // send it again
      return;
    } else {
      return;                // still waiting
    }
  }

  if (cacheCount > 0) sendOldest();
}

void handleConfigLine(char *body);   // defined in "Init from brain"

// A line arrived from the brain
void onLinkLine(char *line) {
  if (independentMode) return;   // Bluetooth is off; ignore
  noteBrainHeard();              // the brain is there: reset the try count

  if (line[0] == 'C' && line[1] == ',') {
    handleConfigLine(line + 2);
    return;
  }
  if (line[0] == 'A' && line[1] == ',') {
    uint16_t seq = (uint16_t)strtoul(line + 2, nullptr, 10);
    Reading r;
    // Only remove the reading if this acknowledgement is for it
    if (awaitingAck && seq == sentSeq && cachePeekOldest(r) && r.seq == seq) {
      cacheRemoveOldest();
      awaitingAck = false;
      serviceSendQueue();    // send the next one straight away
    }
  }
  // Other messages from the brain can be handled here later.
}

// =====================================================================
//  Drawing
// =====================================================================
void showText(const char* text, const FONT_INFO& font, uint8_t colour, int y) {
  if (!debugDisplay) return;
  display.setFont(font);
  display.fontColor(colour, TS_8b_Black);
  int x = (96 - display.getPrintWidth((char*)text)) / 2;
  if (x < 0) x = 0;
  display.setCursor(x, y);
  display.print(text);
}

void showMessage(const char* msg) {
  if (!debugDisplay) return;
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

// Top line: mode, slow-mode progress, and how many readings are waiting to send
void drawTopLine() {
  if (!debugDisplay) return;
  char buf[28];
  char queue[8];
  if (independentMode) snprintf(queue, sizeof(queue), "SOLO");
  else                 snprintf(queue, sizeof(queue), "Q%d", cacheCount);
  if (slowMode) snprintf(buf, sizeof(buf), "SLOW %d/%d  %s", sampleCount, slowSamples, queue);
  else          snprintf(buf, sizeof(buf), "FAST  %s", queue);
  display.drawRect(0, 0, 96, 12, TSRectangleFilled, TS_8b_Black);
  showText(buf, thinPixel7_10ptFontInfo, TS_8b_Blue, 1);

  display.drawRect(0, 12, 96, 2, TSRectangleFilled, TS_8b_Black);
  if (slowMode && sampleCount > 0) {
    int w = (96 * sampleCount) / slowSamples;
    display.drawRect(0, 12, w, 2, TSRectangleFilled, TS_8b_Blue);
  }
}

// Bottom line: which AP we're on
void drawApLine() {
  if (!debugDisplay) return;
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
  if (!debugDisplay) return;
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
  if (!debugDisplay) return;
  display.clearScreen();
  drawTopLine();
  drawReading(slowMode ? lastAverage : currentRssi, nullptr);
  drawApLine();
}

// Turn the debug display on or off while running
void setDebugDisplay(bool on) {
  debugDisplay = on;
  if (on) {
    display.on();
    redrawAll();
  } else {
    display.clearScreen();
    display.off();   // puts the screen to sleep
  }
}

// =====================================================================
//  WiFi events  (wifi_logic.cpp calls these; this is where the screen
//  reacts to what the WiFi code is doing)
// =====================================================================
void resetAverage(bool clearShown);

void onWifiEvent(WifiEvent ev, int value) {
  char msg[24];
  switch (ev) {
    case WIFI_EV_CONNECTING:
      showMessage("Connecting...");
      break;
    case WIFI_EV_RETRYING:
      showMessage("Retrying...");
      break;
    case WIFI_EV_CONNECTED:      // new connection: start fresh readings
      resetAverage(true);
      currentRssi = 0;
      redrawAll();
      break;
    case WIFI_EV_SCANNING:
      if (debugDisplay) {
        display.drawRect(0, 54, 96, 10, TSRectangleFilled, TS_8b_Black);
        showText("scanning APs...", thinPixel7_10ptFontInfo, TS_8b_Blue, 54);
      }
      break;
    case WIFI_EV_SCAN_DONE:
      drawApLine();
      break;
    case WIFI_EV_ROAMING:
      snprintf(msg, sizeof(msg), "Roaming to ch%d", value);
      showMessage(msg);
      break;
  }
}

// Called while the WiFi code is waiting to connect
void onWifiIdle() {
  linkPoll();   // keep handling acknowledgements / settings from the brain
}

// =====================================================================
//  Slow-mode averaging
// =====================================================================
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

  if (sampleCount >= slowSamples) {
    lastAverage = (int)((sampleSum - slowSamples / 2) / slowSamples);   // rounded
    cachePush('S', lastAverage, sampleMin, sampleMax);

    char extra[24];
    snprintf(extra, sizeof(extra), "avg  %d to %d", sampleMin, sampleMax);
    drawReading(lastAverage, extra);
    resetAverage(false);
  } else if (lastAverage == 0) {
    drawReading(0, nullptr);
  }
  drawTopLine();
}

// =====================================================================
//  Buttons
// =====================================================================
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
    wifiRoamNow(currentRssi);
    lastPress = millis();
  }
}

// =====================================================================
//  Init from brain
//  The brain sends settings as  C,key,value  lines, then  C,GO.
//  Anything it doesn't send keeps its default.
// =====================================================================
bool initPhase         = true;    // true while setup() waits for the brain
bool initDone          = false;   // brain sent C,GO
bool wifiConfigChanged = false;   // ssid/pass changed after startup -> reconnect

// Read a whole number and check it's within lo..hi
bool parseLong(const char *v, long lo, long hi, long &out) {
  char *end;
  long n = strtol(v, &end, 10);
  if (end == v || *end != 0) return false;   // not a number
  if (n < lo || n > hi) return false;        // out of range
  out = n;
  return true;
}

// Apply one setting. Returns false if the key is unknown or the value is invalid.
bool applyParam(const char *key, const char *val) {
  long n;

  if (!strcmp(key, "ssid")) {
    size_t len = strlen(val);
    if (len < 1 || len > 32) return false;
    strcpy(wifiSsid, val);
    if (!initPhase) wifiConfigChanged = true;
    return true;
  }
  if (!strcmp(key, "pass")) {
    size_t len = strlen(val);
    if (len != 0 && (len < 8 || len > 63)) return false;   // WPA2 needs 8-63 chars
    strcpy(wifiPass, val);
    if (!initPhase) wifiConfigChanged = true;
    return true;
  }
  if (!strcmp(key, "fast_ms"))       { if (!parseLong(val, 50, 60000, n))  return false; fastIntervalMs = n; return true; }
  if (!strcmp(key, "slow_ms"))       { if (!parseLong(val, 20, 10000, n))  return false; slowIntervalMs = n; return true; }
  if (!strcmp(key, "slow_n"))        { if (!parseLong(val, 1, 200, n))     return false; slowSamples = n; resetAverage(true); drawTopLine(); return true; }
  if (!strcmp(key, "roam_dbm"))      { if (!parseLong(val, -100, -20, n))  return false; roamTriggerDbm = n; return true; }
  if (!strcmp(key, "roam_margin"))   { if (!parseLong(val, 0, 40, n))      return false; roamMarginDb = n; return true; }
  if (!strcmp(key, "roam_check_ms")) { if (!parseLong(val, 0, 600000, n))  return false; roamCheckMs = n; return true; }
  if (!strcmp(key, "roam_cool_ms"))  { if (!parseLong(val, 0, 600000, n))  return false; roamCooldownMs = n; return true; }
  if (!strcmp(key, "ack_ms"))        { if (!parseLong(val, 50, 10000, n))  return false; ackTimeoutMs = n; return true; }
  if (!strcmp(key, "brain_retries")) { if (!parseLong(val, 1, 1000, n))    return false; brainMaxRetries = n; return true; }
  if (!strcmp(key, "display")) {
    if (!parseLong(val, 0, 1, n)) return false;
    setDebugDisplay(n == 1);
    return true;
  }
  if (!strcmp(key, "mode")) {
    if (!strcmp(val, "F"))      slowMode = false;
    else if (!strcmp(val, "S")) slowMode = true;
    else return false;
    resetAverage(true);
    redrawAll();
    return true;
  }
  return false;   // unknown key
}

// body is everything after "C,"  ->  "key,value"  or  "GO"
// Replies:  CA,key  (accepted)   CE,key  (unknown key or bad value)
void handleConfigLine(char *body) {
  char reply[48];

  if (!strcmp(body, "GO")) {
    initDone = true;
    linkSendLine("CA,GO");
    return;
  }

  char *comma = strchr(body, ',');
  if (!comma) {
    snprintf(reply, sizeof(reply), "CE,%s", body);
    linkSendLine(reply);
    return;
  }
  *comma = 0;                 // split into key and value
  const char *key = body;
  const char *val = comma + 1; // the value is everything after the first comma,
                               // so an SSID may contain commas

  snprintf(reply, sizeof(reply), "%s,%s", applyParam(key, val) ? "CA" : "CE", key);
  linkSendLine(reply);        // the password itself is never echoed back
}

// Called once from setup(), before connecting to WiFi.
// Asks the brain for settings ("I,WIFI" once a second) and waits for C,GO.
// If nothing arrives within INIT_WAIT_MS, carries on with the defaults.
// In debug mode, brainMaxRetries unanswered hellos -> independent mode.
// The brain can still send C,key,value lines later; they apply immediately.
void initFromBrain() {
  showMessage("Waiting for brain");
  unsigned long start = millis();
  unsigned long lastHello = 0;

  while (!initDone && !independentMode && millis() - start < INIT_WAIT_MS) {
    linkPoll();
    if (lastHello == 0 || millis() - lastHello >= 1000) {
      if (lastHello != 0) noteBrainRetry();   // the last hello got no reply
      if (independentMode) break;
      if (linkConnected()) linkSendLine("I,WIFI");
      lastHello = millis();
    }
    delay(10);
  }

  if (independentMode)  showMessage("No brain: SOLO mode");
  else if (initDone)    showMessage("Settings received");
  else                  showMessage("Using defaults");
  delay(700);
  initPhase = false;
}

// =====================================================================
//  Setup / loop
// =====================================================================
void setup() {
  Wire.begin();
  display.begin();
  display.setBrightness(10);
  display.setBitDepth(TSBitDepth8);
  setDebugDisplay(debugDisplay);

  linkBegin();
  initFromBrain();              // fill in settings sent by the brain

  if (!wifiBegin()) {
    showMessage("No WiFi shield!");
    while (true) delay(1000);
  }
  wifiConnect();
}

void loop() {
  linkPoll();
  serviceSendQueue();

  if (wifiConfigChanged) {      // brain sent a new ssid/pass after startup
    wifiConfigChanged = false;
    wifiReconnect();
  }
  if (!wifiIsConnected()) wifiConnect();

  handleButtons();
  wifiMaybeRoam(currentRssi);

  unsigned long interval = slowMode ? slowIntervalMs : fastIntervalMs;
  if (millis() - lastReadMs < interval) return;
  lastReadMs = millis();

  int rssi = wifiReadRssi();
  if (rssi == 0) return;   // ignore invalid readings
  currentRssi = rssi;

  if (slowMode) {
    addSlowSample(rssi);
  } else {
    cachePush('F', rssi, rssi, rssi);
    drawReading(rssi, nullptr);
    drawTopLine();
  }
}
