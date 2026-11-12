#include "HelmetApp.h"
#include "HelmetBle.h"
#include "IrScanner.h"
#include "Protocol.h"
#include <TinyScreen.h>
#include <Wire.h>
#include <Wireling.h>
#include <stdio.h>
#include <string.h>

namespace {
TinyScreen display(TinyScreenPlus);
IrScanner ir;
char boardId = 'L';
bool useIr, useBle, useScreen, useUsbLog, tracePhases;
Reading remote = {'R',0,0,0};
bool remoteSeen = false, hubSeen = false, lastConnected = false;
uint32_t remoteAt = 0, hubAt = 0, lastScreen = 0, lastTx = 0, lastLog = 0;
uint32_t txOK = 0, txFailed = 0;
uint32_t lastDiagnosticSample = 0;
const uint32_t STALE_MS = 2500;

void trace(const char *phase) {
  if (!useUsbLog || !tracePhases || !SerialUSB) return;
  const size_t n = strlen(phase);
  if (SerialUSB.availableForWrite() >= static_cast<int>(n + 1)) {
    SerialUSB.write(reinterpret_cast<const uint8_t *>(phase), n);
    SerialUSB.write('\n');
  }
}

void row(uint8_t y, const char *label, uint8_t value, bool valid, uint8_t color) {
  display.drawRect(0,y,96,10,TSRectangleFilled,TS_8b_Black);
  display.fontColor(TS_8b_White,TS_8b_Black); display.setCursor(0,y);
  display.print(label);
  for (uint8_t i = 0; i < 6; ++i) {
    display.drawRect(16+i*6,y+2,5,6,TSRectangleFilled,
                    valid && i < value ? color : TS_8b_DarkBlue);
  }
  display.setCursor(56,y);
  if (!valid) display.print("--");
  else if (!value) display.print("NONE");
  else { display.print('~'); display.print((7-value)*6); display.print("cm"); }
}

void draw(uint32_t now) {
  display.drawRect(0,0,96,10,TSRectangleFilled,TS_8b_Black);
  display.fontColor(TS_8b_Yellow,TS_8b_Black); display.setCursor(0,0);
  display.print("IR HELMET "); display.print(boardId);
  // Moving marker: main loop / display heartbeat, separate from radio status.
  display.drawRect(91,2,3,3,TSRectangleFilled,
                   (now/500)&1 ? TS_8b_Green : TS_8b_DarkGreen);
  const bool peerValid = remoteSeen && useBle && bleConnected() &&
      static_cast<uint32_t>(now - remoteAt) < STALE_MS;
  const bool localValid = useIr && ir.hasSample;
  const bool left = boardId == 'L';
  row(11,"L", left?ir.values[0]:remote.outer, left?localValid:peerValid,TS_8b_Green);
  row(21,"ML",left?ir.values[1]:remote.inner, left?localValid:peerValid,TS_8b_Green);
  row(31,"MR",left?remote.inner:ir.values[1], left?peerValid:localValid,TS_8b_Yellow);
  row(41,"R", left?remote.outer:ir.values[0], left?peerValid:localValid,TS_8b_Yellow);
  display.drawRect(0,53,96,11,TSRectangleFilled,TS_8b_Black);
  display.fontColor(TS_8b_White,TS_8b_Black); display.setCursor(0,53);
  if (!useBle) display.print("BLE OFF");
  else if (!bleReady()) { display.print("BLE ERR "); display.print(bleLastError()); }
  else if (!bleConnected()) display.print("BLE WAIT");
  else if (!hubSeen || static_cast<uint32_t>(now-hubAt) >= STALE_MS) display.print("BLE HUB?");
  else display.print("BLE LINK");
}
}

void helmetBegin(char board, bool irEnabled, bool bleEnabled, bool screen, bool usbLog, bool traceEnabled) {
  boardId = board; useIr = irEnabled; useBle = bleEnabled;
  useScreen = screen; useUsbLog = usbLog; tracePhases = traceEnabled;
  SerialUSB.begin(115200); // Never wait for USB host / Serial Monitor.
  if (useIr) {
    trace("WIRE>"); Wire.begin(); Wireling.begin(); ir.begin(); trace("WIRE<");
  }
  if (useScreen) {
    trace("OLED>"); display.begin(); display.setBrightness(8);
    display.setFont(thinPixel7_10ptFontInfo); display.clearScreen();
    draw(millis()); trace("OLED<");
  }
  if (useBle) { trace("BLE INIT>"); bleBegin(boardId); trace("BLE INIT<"); }
}

void helmetLoop() {
  if (useBle) {
    trace("BLE>"); blePoll(); trace("BLE<");
    bool connected = bleConnected();
    if (connected != lastConnected) {
      remoteSeen = hubSeen = false; lastConnected = connected;
    }
    uint8_t bytes[20], n;
    if (bleTakeMessage(bytes,n)) {
      const uint32_t now = millis();
      if (n == 1 && bytes[0] == 'H') { hubSeen = true; hubAt = now; }
      else {
        Reading r;
        if (parseReading(bytes,n,r) && r.board != boardId) {
          // Duplicate packets do not extend a remote reading's freshness.
          if (!remoteSeen || r.sequence != remote.sequence ||
              static_cast<uint32_t>(now-remoteAt) >= STALE_MS) {
            remote = r; remoteAt = now; remoteSeen = true;
          }
          hubSeen = true; hubAt = now;
        }
      }
    }
  }
  if (useIr) { trace("IR>"); ir.tick(); trace("IR<"); }
  uint32_t now = millis();
  if (!useIr && now-lastDiagnosticSample >= 150) {
    // BLE-only isolation mode: zero-valued test packets, NOT sensor readings.
    lastDiagnosticSample = now; ++ir.sequence; ir.hasSample = true;
  }
  if (useBle && bleSubscribed() && ir.hasSample && now-lastTx >= 250) {
    lastTx = now;
    char message[20];
    snprintf(message,sizeof(message),"%c,%u,%u,%u",boardId,ir.values[0],ir.values[1],ir.sequence);
    trace("TX>");
    if (bleSend(message)) ++txOK; else ++txFailed;
    trace("TX<");
  }
  now = millis();
  if (useScreen && now-lastScreen >= 200) {
    lastScreen = now; trace("OLED>"); draw(now); trace("OLED<");
  }
  if (useUsbLog && now-lastLog >= 1000) {
    lastLog = now;
    char line[64];
    int n = snprintf(line,sizeof(line),"%c s=%u ir=%u,%u tx=%lu/%lu rec=%lu e=%u\n",boardId,
        ir.sequence,ir.values[0],ir.values[1],(unsigned long)txOK,
        (unsigned long)txFailed,(unsigned long)bleRecoveries(),bleLastError());
    if (SerialUSB && n > 0 && n < static_cast<int>(sizeof(line)) &&
        SerialUSB.availableForWrite() >= n) SerialUSB.write((uint8_t *)line,n);
  }
}
