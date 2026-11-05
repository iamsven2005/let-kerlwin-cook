#include "../common/mode.h"
#if SURVEY_MODE == HAT
#include "link.h"
#include "config.h"
#include <WiFi101.h>
#include <WiFiUdp.h>
#include "../common/wifi_logic.h"
#include "commands.h"
#include "heading.h"
#include "tracker.h"
#include "motion.h"
#include "hand_pose.h"
#include "ir_sensor.h"
#include "readings.h"
#include "screen.h"

static WiFiUDP udp;
static IPAddress sendTo, learnedPc(0, 0, 0, 0);
static char txBuf[1000];
static int  txLen = 0;
static unsigned long lastTxMs = 0;
static char serialBuf[96];
static int  serialLen = 0;

bool linkAppFound() { return !(learnedPc == IPAddress(0, 0, 0, 0)); }

void linkBegin() {
  if (linkAppFound()) sendTo = learnedPc;
  else {
    IPAddress ip = WiFi.localIP(), mask = WiFi.subnetMask();   // this network's broadcast address
    sendTo = IPAddress(ip[0] | (uint8_t)~mask[0], ip[1] | (uint8_t)~mask[1],
                       ip[2] | (uint8_t)~mask[2], ip[3] | (uint8_t)~mask[3]);
  }
  udp.stop();
  udp.begin(GLOVE_PORT);
}

void linkFlush(bool force) {
  if (txLen == 0) return;
  if (!force && millis() - lastTxMs < TX_GAP_MS) return;
  if (WiFi.status() == WL_CONNECTED) {
    unsigned long since = millis() - lastTxMs;
    if (since < TX_GAP_MS) delay(TX_GAP_MS - since);
    udp.beginPacket(sendTo, PC_PORT);
    udp.write((const uint8_t*)txBuf, txLen);
    udp.endPacket();
    lastTxMs = millis();
  }
  txLen = 0;
}

void linkSendLine(const char* line) {
  SerialUSB.print(line);
  int n = strlen(line);
  if (txLen + n > (int)sizeof(txBuf)) linkFlush(true);
  if (n <= (int)sizeof(txBuf)) { memcpy(txBuf + txLen, line, n); txLen += n; }
}

bool linkSendLowPriority(const char* line) {
  int n = strlen(line);
  if (txLen + n > (int)sizeof(txBuf)) return false;
  linkSendLine(line);
  return true;
}

void linkSendMessage(const char* text) {
  char line[80];
  snprintf(line, sizeof(line), "M,%s\n", text);
  linkSendLine(line);
}

// P,t,x,y,heading,dist,NA,ir,state,mode,steplen_cm,AP,handpose,0,0,-1,steps,still
void linkSendTelemetry() {
  char line[170];
  const char* st = !tracking ? "PAUSED" : trackerCalibrating() ? "CALSTEP" : measuring ? "MEASURE" : still ? "STILL" : "WALK";
  snprintf(line, sizeof(line), "P,%lu,%ld,%ld,%ld,%ld,NA,%d,%s,%s,%d,%02X:%02X ch%d,%s,0,0,-1,%ld,%d\n",
           millis(), lround(posX), lround(posY), lround(headingNow()), lround(distanceMm), irYes ? 1 : 0,
           st, slowMode ? "SLOW" : "FAST", (int)lround(stepMm / 10), currentBssid[4], currentBssid[5],
           currentChannel, handPoseName(), steps, still ? 1 : 0);
  linkSendLine(line);
}

void linkCheckIncoming() {
  while (SerialUSB.available()) {
    char c = SerialUSB.read();
    if (c == '\n') { serialBuf[serialLen] = 0; commandsHandleLine(serialBuf); serialLen = 0; }
    else if (serialLen < (int)sizeof(serialBuf) - 1) serialBuf[serialLen++] = c;
  }
  int size = udp.parsePacket();
  if (size <= 0) return;
  char buf[128];
  IPAddress from = udp.remoteIP();
  int len = udp.read(buf, sizeof(buf) - 1);
  if (len <= 0) return;
  buf[len] = 0;
  // Any packet from the app (even a PING) tells us where it is: send straight there from now on
  if (!(from == learnedPc) && !(from == IPAddress(0, 0, 0, 0))) { learnedPc = from; sendTo = from; }
  commandsHandleLine(buf);
}

// ---- wifi_logic.cpp reports what the Wi-Fi is doing through these two
void onWifiEvent(WifiEvent ev, int value) {
  (void)value;
  switch (ev) {
    case WIFI_EV_CONNECTING: screenMessage("Connecting..."); break;
    case WIFI_EV_RETRYING:   screenMessage("Retrying..."); break;
    case WIFI_EV_CONNECTED:  linkBegin(); screenClear(); linkSendMessage("Glove connected"); break;
    case WIFI_EV_ROAMING:    linkSendMessage("Roaming to a stronger access point"); linkFlush(true); break;
    default: break;
  }
}

void onWifiIdle() {}

#endif // SURVEY_MODE == HAT
