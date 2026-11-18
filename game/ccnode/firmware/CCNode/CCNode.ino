// =============================================================================
//  CCNode - controller node firmware (TinyScreen+ / BLE TinyShield / BMA253)
//
//  A thin I/O device: detects flips, taps and button presses, sends them to
//  the hub as raw events, and draws what the hub tells it to. No game logic.
//  Protocol: README.md section "Protocol"; tunables: config.h. Brief v1.4.
// =============================================================================
#include <Arduino.h>
#include <SPI.h>
#include <TinyScreen.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "config.h"
#include "gestures.h"
#include "buttons.h"
#include "protocol.h"
#include "display.h"
#include "i2c_bus.h"
#include "accel_core.h"
#include "ble_link.h"
#include "node_id.h"
#include "battery.h"
#include "watchdog.h"

// All USB-serial output goes through this one port: on the TinyScreen+ (SAMD21)
// the USB serial port is SerialUSB. ("Serial" is the hardware UART on the pins.)
// Nothing ever waits for the port to be opened.
#define NODE_SERIAL SerialUSB

// ----------------------------------------------------------------------------- state
TinyScreen      display(TinyScreenPlus);
Renderer        renderer(display);
GestureEngine   gestures(defaultGestureConfig());
ButtonDebouncer buttons(BUTTON_DEBOUNCE_MS);
UiState         ui;
StatusInfo      status;
LineAssembler   bleLines, serialLines;
const AccelIO   accelIO = { I2CBus::writeReg, I2CBus::readRegs, I2CBus::recover };
AccelDriver     accel(accelIO);

bool     debugMode = DEBUG_DEFAULT;   // set to true here or with DEBUG_DEFAULT; boot buttons can only turn it on
bool     testMode = false;            // serial test mode: BLE off, events printed
bool     testHubSeen = false;         // test mode: a hub command arrived over serial
bool     testHelloPending = false;
bool     bleOk = false;
uint16_t nodeId = 0;
char     deviceName[20];
int16_t  lastRaw[3];
bool     syncDone = false;            // HELLO sync sent on the current subscription
bool     lastSentFaceDown = false;    // last F: value the hub was given
bool     faceCheckPending = false;    // re-check orientation once gestures re-initialise (after ACC returns)
uint32_t lastBatMs = 0;               // last battery reading
uint8_t  batPct = 100;
uint32_t lastHubMs = 0;               // last message from the hub (NO HUB timer)
uint32_t lastRxMs = 0;                // for the rx arrival log
uint32_t bootDoneMs = 0;
uint32_t lastSplashRefresh = 0;
uint32_t lastDebugAccel = 0;
// debug toggle (all 4 buttons held DEBUG_TOGGLE_HOLD_MS)
uint32_t allHeldSince = 0;
bool     toggleArmed = false;         // only after all buttons were released once (boot combo)
bool     toggleDone = false;
// loop metrics (heartbeat)
uint32_t lastHeartbeat = 0, loopCount = 0, lastLoopUs = 0, maxLoopGapUs = 0;
uint32_t lastBlePollUs = 0, maxBleGapUs = 0;

static uint32_t microsFn() { return micros(); }

// ----------------------------------------------------------------------------- output
static void dbg(const char* s) {
  if (!debugMode) return;
  NODE_SERIAL.print("# ");
  NODE_SERIAL.println(s);
}

// Send one node->hub protocol message (without '\n').
static void emit(const char* msg) {
  if (testMode) {
    NODE_SERIAL.println(msg);          // serial test mode: events go to USB serial instead of BLE
  } else {
    bool queued = BleLink::send(msg);
    if (debugMode) {
      char b[48];
      snprintf(b, sizeof(b), "tx t=%lu %s%s", (unsigned long)millis(), msg, queued ? "" : "  (dropped: hub not subscribed)");
      dbg(b);
    }
  }
}

static void emitFace(bool down) {
  emit(down ? "F:1" : "F:0");
  lastSentFaceDown = down;
}

static void setLastEvent(const char* e) {
  strncpy(status.lastEvent, e, sizeof(status.lastEvent) - 1);
  status.lastEvent[sizeof(status.lastEvent) - 1] = 0;
  renderer.markStatusDirty();
}

static void readBattery() {
  batPct = Battery::readPercent();
  uiSetBatLow(ui, batPct <= LOW_BAT_PCT);
  lastBatMs = millis();
}

static void sendBattery() {
  char b[12];
  readBattery();
  fmtBattery(b, sizeof(b), batPct);
  emit(b);
}

// HELLO sync (brief section 5): HELLO, then exactly one F, one B and one BAT.
// Sent on every CCCD enable (subscribe / re-subscribe).
static void sendHelloAndState() {
  char b[24];
  fmtHello(b, sizeof(b), nodeId, FW_VERSION);
  emit(b);
  // With no accelerometer the last known orientation is reported (the hub treats this F as sync).
  emitFace(gestures.initialised() ? gestures.faceDown() : lastSentFaceDown);
  fmtButtons(b, sizeof(b), buttons.state());
  emit(b);
  sendBattery();
  syncDone = true;
  lastHubMs = millis();          // NO HUB timer starts at the sync (FW-Q24)
  uiSetNoHub(ui, false);
}

// ----------------------------------------------------------------------------- gestures
static void onSample(int16_t x, int16_t y, int16_t z, uint32_t tMs) {
  lastRaw[0] = x; lastRaw[1] = y; lastRaw[2] = z;
  uint8_t ev = gestures.update(x, y, z, tMs);
  if (ev & GE_FLIP_DOWN) { emitFace(true);  setLastEvent("F:1"); }
  if (ev & GE_FLIP_UP)   { emitFace(false); setLastEvent("F:0"); }
  if (ev & GE_TAP_R)     { emit("T:R"); setLastEvent("T:R"); }
  if (ev & GE_TAP_L)     { emit("T:L"); setLastEvent("T:L"); }
  if (ev & GE_TAP_V)     { emit("T:V"); setLastEvent("T:V"); }

  if (debugMode) {
    if (ev & (GE_FLIP_DOWN | GE_FLIP_UP)) {
      char b[48];
      snprintf(b, sizeof(b), "flip commit %s zAvg=%dmg", (ev & GE_FLIP_DOWN) ? "F:1" : "F:0", (int)(gestures.zAvg() * 1000));
      dbg(b);
    }
    if (gestures.tapDiagReady()) {
      const TapDiag& d = gestures.lastTap();
      static const char* const ax[3] = { "side", "other", "vert" };
      const char* res = d.suppressed ? "SUPPRESSED" : d.notImpulse ? "rejected(pickup push, not impact)" :
                        (d.event == GE_TAP_R ? "T:R" : d.event == GE_TAP_L ? "T:L" : d.event == GE_TAP_V ? "T:V" : "ignored(other axis)");
      char b[120];
      snprintf(b, sizeof(b), "spike side=%+dmg(%+d) other=%dmg vert=%dmg end=%d%% dom=%s -> %s",
               (int)(d.peak[0] * 1000), d.firstSign[0], (int)(d.peak[1] * 1000), (int)(d.peak[2] * 1000),
               (int)(d.vEndRatio * 100), ax[d.dominant], res);
      dbg(b);
    }
  }
}

// ----------------------------------------------------------------------------- hub commands
static void printInfo() {
  char b[160];
  char addrStr[18];
  BleLink::bleAddr(addrStr);
  snprintf(b, sizeof(b), "# %s addr=%s fw " FW_VERSION " rst=%s acc=%s i2c=%s ble=%s int=%u bat=%u%% ovr=%lu drop=%lu",
           deviceName, addrStr, status.resetCause, accel.ok() ? "ok" : "MISSING", I2CBus::backendName(),
           testMode ? "off" : (bleOk ? BleLink::stateName() : "FAIL"), BleLink::connIntervalUnits(), batPct,
           (unsigned long)accel.overruns(), (unsigned long)BleLink::droppedTx());
  NODE_SERIAL.println(b);
  snprintf(b, sizeof(b), "# cal side_axis=%d x_sign=%d z_sign=%d tap=%d/%dmg refr=%d flip=%dmg/%dms supp=%dms vdecay=%d%%",
           CAL_SIDE_AXIS, CAL_X_SIGN, CAL_Z_SIGN, (int)(CAL_TAP_SIDE_G * 1000), (int)(CAL_TAP_V_G * 1000),
           CAL_TAP_REFRACTORY_MS, (int)(CAL_FLIP_G * 1000), CAL_FLIP_HOLD_MS, CAL_TAP_SUPPRESS_MS, (int)(CAL_TAP_V_DECAY * 100));
  NODE_SERIAL.println(b);
}

static void setDebug(bool on) {
  debugMode = on;
  NODE_SERIAL.println(on ? "# debug ON" : "# debug OFF");
  renderer.showToast(on ? "DEBUG ON" : "DEBUG OFF", millis());
}

static void onHubLine(const char* line, void* fromSerial) {
  if (fromSerial && line[0] == '!') {          // local serial-only commands
    if (strcmp(line, "!INFO") == 0) printInfo();
    else if (strcmp(line, "!DEBUG") == 0) setDebug(!debugMode);
    return;
  }
  if (fromSerial && testMode) testHubSeen = true;

  uint32_t now = millis();
  // v1.4 A3: log every hub message with its arrival time and the gap since the
  // previous one, so a missing PING can be told apart from a late one.
  if (debugMode) {
    char b[80];
    snprintf(b, sizeof(b), "rx t=%lu +%lums %.40s", (unsigned long)now, (unsigned long)(now - lastRxMs), line);
    dbg(b);
  }
  lastRxMs = now;

  // Any hub message proves the hub is alive: reset the 6 s timer and clear
  // the "NO HUB" indicator (brief v1.3, FW-Q24).
  lastHubMs = now;
  uiSetNoHub(ui, false);

  HubCmd r = applyHubLine(ui, line);
  if (r == HC_PING) emit("PONG");
  else if (r == HC_BLINK) renderer.startBlink(now);
  else if (r == HC_IGNORED) dbg("   ^ ignored (unknown or malformed)");
}

static void pollSerialInput() {
  uint8_t buf[32];
  uint8_t n = 0;
  while (NODE_SERIAL.available() && n < sizeof(buf)) buf[n++] = (uint8_t)NODE_SERIAL.read();
  if (n) serialLines.feed(buf, n, onHubLine, (void*)1);
}

// ----------------------------------------------------------------------------- BLE callbacks
static void bleOnRx(const uint8_t* data, uint8_t len) {
  bleLines.feed(data, len, onHubLine, nullptr);
  bleLines.endOfWrite(onHubLine, nullptr);      // a write without '\n' is one complete message
}
static void bleOnReady() { sendHelloAndState(); }
static void bleOnDisconnect() {
  uiStateReset(ui);                             // back to the defaults (v1.4: DOFF:0)
  bleLines.clear();
  syncDone = false;
}
static void bleLog(const char* s) { dbg(s); }

// ----------------------------------------------------------------------------- boot trace
// Each step is shown on screen and printed to USB serial BEFORE it runs, so if
// anything hangs, the last line tells you where. Then the splash takes over.
static uint8_t bootLine = 0;

static void bootStep(const char* s, uint16_t color = 0xFFFF) {
  Watchdog::feed();
  NODE_SERIAL.print("# boot: ");
  NODE_SERIAL.println(s);
  const FONT_INFO& f = liberationSansNarrow_8ptFontInfo;
  uint8_t y = (uint8_t)(1 + bootLine * (f.height + 1));
  if (y + f.height > 64) { display.clearWindow(0, 0, 96, 64); bootLine = 0; y = 1; }
  display.setFont(f);
  display.fontColor(color, 0x0000);
  display.setCursor(2, y);
  display.print(s);
  bootLine++;
}

static void shortResetCause(char out[6]) {
  const char* c = Watchdog::resetCause();
  const char* s = !strcmp(c, "WATCHDOG") ? "WDT" : !strcmp(c, "SOFTWARE") ? "SW" : !strcmp(c, "RESET-PIN") ? "RST" :
                  !strcmp(c, "BROWNOUT") ? "BOD" : "PWR";
  strncpy(out, s, 5);
  out[5] = 0;
}

// ----------------------------------------------------------------------------- setup
void setup() {
  Watchdog::begin();                            // ~2 s; every boot step and loop pass feeds it
  NODE_SERIAL.begin(SERIAL_BAUD);               // never waited for

  renderer.begin(DISPLAY_BRIGHTNESS, DISPLAY_FLIP);   // display first, so every later step is visible
  uint8_t held = tsButtonsToProtocol(display.getButtons());
  debugMode = debugMode || held == 0x0F;        // all four buttons (never overrides a code-set true)
  testMode  = SERIAL_TEST_MODE || held == 0x0C; // both bottom buttons
  bootStep(testMode ? "CCNODE fw " FW_VERSION " TEST" : debugMode ? "CCNODE fw " FW_VERSION " DEBUG" : "CCNODE fw " FW_VERSION);

  nodeId = NODE_ID_OVERRIDE ? (uint16_t)NODE_ID_OVERRIDE : readNodeId();
  snprintf(deviceName, sizeof(deviceName), BLE_NAME_PREFIX "%04X", nodeId);
  bootStep(deviceName);

  uiStateInit(ui);
  status.nodeId = nodeId;
  status.faceKnown = false;
  status.faceDown = false;
  status.zAvg = 0;
  status.acc = ACCS_PROBING;
  status.testMode = false;
  status.bleOk = true;
  status.bleState = "";
  status.lastEvent[0] = 0;
  shortResetCause(status.resetCause);

  bootStep("accel...");
  I2CBus::begin();
  accel.begin(millis());
  for (uint32_t t0 = millis(); !accel.settled() && (uint32_t)(millis() - t0) < 20; ) accel.service(millis(), nullptr);
  status.acc = accel.ok() ? ACCS_OK : ACCS_MISSING;
  uiSetAccMissing(ui, !accel.ok());
  accel.takeEvents();
  bootStep(accel.ok() ? "accel ok" : "accel not found", accel.ok() ? tsColor(0, 220, 0) : tsColor(255, 60, 60));

  buttons.begin(held, millis());

  if (testMode) {
    bootStep("ble skipped (test mode)", tsColor(140, 140, 140));
    status.testMode = true;
    testHelloPending = true;
  } else {
    bootStep("ble init...");
    BleLink::Callbacks cb = { bleOnRx, bleOnReady, bleOnDisconnect, bleLog };
    uint8_t addr[6];
    bleStaticAddress(nodeId, addr);
    bleOk = BleLink::begin(deviceName, addr, cb);
    status.bleOk = bleOk;
    bootStep(bleOk ? "ble ok" : "ble FAIL (shield?)", bleOk ? tsColor(0, 220, 0) : tsColor(255, 60, 60));
  }
  readBattery();
  renderer.setMode(SM_SPLASH);

  char b[200];
  char addrStr[18];
  { uint8_t addr[6]; bleStaticAddress(nodeId, addr); bleAddressString(addr, addrStr); }
  snprintf(b, sizeof(b), "# CCNODE %s fw " FW_VERSION " addr=%s%s reset=%s acc=%s i2c=%s ble=%s mode=%s debug=%s bat=%u%%",
           deviceName, addrStr, (!testMode && bleOk && !BleLink::addressSet()) ? "(NOT SET)" : "",
           Watchdog::resetCause(), accel.ok() ? "ok" : "not-found", I2CBus::backendName(),
           testMode ? "off" : (bleOk ? "ok" : "FAIL"), testMode ? "serial-test" : "ble", debugMode ? "on" : "off", batPct);
  NODE_SERIAL.println(b);   // boot banner (always printed; the heartbeat follows in debug mode)
  bootDoneMs = millis();
  lastHeartbeat = bootDoneMs;
  lastLoopUs = lastBlePollUs = micros();
}

// ----------------------------------------------------------------------------- loop
static void heartbeat(uint32_t now) {
  char b[160];
  const char* face = !gestures.initialised() ? "--" : gestures.faceDown() ? "DOWN" : "UP";
  int z = (int)(gestures.zAvg() * 1000);
  snprintf(b, sizeof(b),
           "hb t=%lus loop=%lu/s maxgap=%lums ble=%s blegap=%lums int=%u hub_age=%s%lums zAvg=%+dmg %s acc=%s bat=%u%% ovr=%lu i2c_to=%lu",
           (unsigned long)(now / 1000), (unsigned long)loopCount, (unsigned long)(maxLoopGapUs / 1000),
           testMode ? "off" : (bleOk ? BleLink::stateName() : "FAIL"), (unsigned long)(maxBleGapUs / 1000),
           BleLink::connIntervalUnits(), syncDone ? "" : "(no sync)", (unsigned long)(syncDone ? now - lastHubMs : 0),
           z, face, accel.ok() ? "ok" : "MISSING", batPct, (unsigned long)accel.overruns(), (unsigned long)I2CBus::timeouts());
  dbg(b);
  loopCount = 0;
  maxLoopGapUs = 0;
  maxBleGapUs = 0;
}

void loop() {
  Watchdog::feed();
  uint32_t now = millis();
  uint32_t us = micros();
  if ((uint32_t)(us - lastLoopUs) > maxLoopGapUs) maxLoopGapUs = us - lastLoopUs;
  lastLoopUs = us;
  loopCount++;

  // 1) Gestures: drain the accelerometer FIFO (1 kHz on the sensor's clock, 32 frames buffered).
  accel.service(now, onSample);
  uint8_t aev = accel.takeEvents();
  if (aev & AccelDriver::EV_LOST) {
    uiSetAccMissing(ui, true);
    gestures.reset();
    dbg("accelerometer lost: gestures paused, re-probing every 5 s");
  }
  if (aev & AccelDriver::EV_FOUND) {
    uiSetAccMissing(ui, false);
    gestures.reset();
    faceCheckPending = true;
    dbg("accelerometer found: gestures resume");
  }
  status.acc = accel.ok() ? ACCS_OK : (accel.state() == AccelDriver::ST_MISSING ? ACCS_MISSING : ACCS_PROBING);

  // 2) BLE: process radio events on EVERY pass and push queued events out (v1.4 A3).
  if (!testMode && bleOk) {
    us = micros();
    if ((uint32_t)(us - lastBlePollUs) > maxBleGapUs) maxBleGapUs = us - lastBlePollUs;
    lastBlePollUs = us;
    BleLink::poll(now);
  }

  // 3) Buttons, and the runtime debug toggle (all 4 held for 2 s). The chord is still reported to the
  //    hub raw (B:1, B:3, B:7, B:F, ...): buttons are never interpreted. Don't use it mid-game (v1.5 FW-Q35).
  uint8_t raw = tsButtonsToProtocol(display.getButtons());
  if (buttons.update(raw, now)) {
    char b[8];
    fmtButtons(b, sizeof(b), buttons.state());
    emit(b);
    setLastEvent(b);
  }
  if (buttons.state() == 0) { toggleArmed = true; toggleDone = false; allHeldSince = 0; }
  else if (buttons.state() == 0x0F && toggleArmed && !toggleDone) {
    if (!allHeldSince) allHeldSince = now ? now : 1;
    else if ((uint32_t)(now - allHeldSince) >= DEBUG_TOGGLE_HOLD_MS) { toggleDone = true; setDebug(!debugMode); }
  } else allHeldSince = 0;
  if (!testMode && bleOk) BleLink::flush();

  // 4) Serial input (hub commands for testing, plus !INFO / !DEBUG)
  pollSerialInput();

  // 5) Orientation bookkeeping
  bool known = gestures.initialised();
  if (known != status.faceKnown || (known && gestures.faceDown() != status.faceDown)) {
    status.faceKnown = known;
    status.faceDown = known && gestures.faceDown();
    renderer.markStatusDirty();
  }
  status.zAvg = gestures.zAvg();
  // After the accelerometer comes back, tell the hub if the node was turned over meanwhile, with the
  // silent face-state sync FS:<0|1> (never an action, unlike a flip commit F:) - brief v1.5 FW-Q33.
  if (faceCheckPending && known) {
    faceCheckPending = false;
    if (syncDone && gestures.faceDown() != lastSentFaceDown) {
      char b[8];
      fmtFaceSync(b, sizeof(b), gestures.faceDown());
      emit(b);
      lastSentFaceDown = gestures.faceDown();
      setLastEvent(b);
    }
  }
  if (testMode && testHelloPending && (known || accel.state() == AccelDriver::ST_MISSING)) {
    testHelloPending = false;
    sendHelloAndState();
  }

  // 6) Battery (every 60 s; BAT is sent when the hub is subscribed) and the NO HUB indicator
  bool linkUp = testMode ? testHubSeen : BleLink::ready();
  if ((uint32_t)(now - lastBatMs) >= BAT_PERIOD_MS) {
    if (syncDone && (testMode || BleLink::ready())) sendBattery();
    else readBattery();
  }
  if (syncDone && linkUp && (uint32_t)(now - lastHubMs) >= NO_HUB_MS && !ui.noHub) {
    uiSetNoHub(ui, true);
    dbg("no hub message for 6 s: NO HUB shown in the status area");
  }

  // 7) Screen mode, power and drawing (time-boxed)
  status.bleState = bleOk ? BleLink::stateName() : "";
  bool splash = (uint32_t)(now - bootDoneMs) < SPLASH_MS;
  ScreenMode m;
  if (splash)        m = SM_SPLASH;
  else if (testMode) m = testHubSeen ? SM_GAME : SM_TEST_IDLE;
  else               m = BleLink::connected() ? SM_GAME : SM_DISCONNECTED;
  renderer.setMode(m);
  if (splash && (uint32_t)(now - lastSplashRefresh) >= SPLASH_REFRESH_MS) {
    lastSplashRefresh = now;
    renderer.markStatusDirty();                 // live zAvg / UP-DOWN on the splash
  }

  // Power save only with DOFF:1 from the hub (default DOFF:0 = always on), never during
  // the splash, a blink or debug mode (calibration needs the screen).
  bool off = DISPLAY_OFF_FACE_DOWN && ui.doffAllowed && gestures.initialised() && gestures.faceDown();
  bool wantOn = !off || splash || renderer.blinking() || debugMode;
  renderer.setPower(wantOn, ui, now);
  renderer.step(ui, status, now, RENDER_BUDGET_US, microsFn);

  // 8) Debug output: 1 s heartbeat, optional raw accel lines
  if (debugMode && (uint32_t)(now - lastHeartbeat) >= HEARTBEAT_MS) {
    lastHeartbeat = now;
    heartbeat(now);
  }
#if DEBUG_ACCEL_PRINT_MS > 0
  if (debugMode && (uint32_t)(now - lastDebugAccel) >= DEBUG_ACCEL_PRINT_MS) {
    lastDebugAccel = now;
    const float g = G_PER_COUNT;
    char b[96];
    snprintf(b, sizeof(b), "acc x=%dmg y=%dmg z=%dmg zAvg=%dmg tilt=%dmg%s",
             (int)(lastRaw[0] * g * 1000), (int)(lastRaw[1] * g * 1000), (int)(lastRaw[2] * g * 1000),
             (int)(gestures.zAvg() * 1000), (int)(sqrtf(gestures.tiltG2()) * 1000), gestures.suppressing(now) ? " supp" : "");
    dbg(b);
  }
#endif
}
