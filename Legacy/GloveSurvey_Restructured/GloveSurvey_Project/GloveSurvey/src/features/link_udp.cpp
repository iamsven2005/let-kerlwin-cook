#include "link.h"
#if FEAT_LINK_UDP
#include <WiFi101.h>
#include <WiFiUdp.h>

static WiFiUDP udp;
static IPAddress sendTo, learnedPc(0, 0, 0, 0);
static char txBuf[TX_BUF_SIZE];
static int  txLen = 0;
static bool lowQueued = false;           // a low-priority line is already in this packet
static unsigned long lastTxMs = 0, packetsOut = 0, packetsIn = 0, lowDropped = 0;

bool linkUdpAppFound() { return !(learnedPc == IPAddress(0, 0, 0, 0)); }

void linkUdpBegin() {
  if (linkUdpAppFound()) sendTo = learnedPc;
  else {
    IPAddress ip = WiFi.localIP(), mask = WiFi.subnetMask();   // this network's broadcast address
    sendTo = IPAddress(ip[0] | (uint8_t)~mask[0], ip[1] | (uint8_t)~mask[1],
                       ip[2] | (uint8_t)~mask[2], ip[3] | (uint8_t)~mask[3]);
  }
  udp.stop();
  udp.begin(GLOVE_PORT);
}

void linkUdpFlush(bool force) {
  if (txLen == 0) return;
  if (!force && millis() - lastTxMs < TX_GAP_MS) return;
  if (WiFi.status() == WL_CONNECTED) {
    unsigned long since = millis() - lastTxMs;
    if (since < TX_GAP_MS) delay(TX_GAP_MS - since);
    udp.beginPacket(sendTo, PC_PORT);
    udp.write((const uint8_t*)txBuf, txLen);
    udp.endPacket();
    lastTxMs = millis();
    packetsOut++;
  }
  txLen = 0;
  lowQueued = false;
}

// Normal lines always go in (flushing first if the packet is full). Low-priority lines
// (debug) only go in if there's room and no other low-priority line is in this packet.
bool linkUdpQueue(const char* line, bool lowPriority) {
  int n = strlen(line);
  if (lowPriority) {
    if (lowQueued || txLen + n > (int)sizeof(txBuf) / 2) { lowDropped++; return false; }
    lowQueued = true;
  } else if (txLen + n > (int)sizeof(txBuf)) linkUdpFlush(true);
  if (n <= (int)sizeof(txBuf)) { memcpy(txBuf + txLen, line, n); txLen += n; }
  return true;
}

void linkUdpUpdate() {
  int size = udp.parsePacket();
  if (size <= 0) return;
  char buf[128];
  IPAddress from = udp.remoteIP();
  int len = udp.read(buf, sizeof(buf) - 1);
  if (len <= 0) return;
  buf[len] = 0;
  packetsIn++;
  // Any packet from the app (even a PING) tells us where it is: send straight there from now on
  if (!(from == learnedPc) && !(from == IPAddress(0, 0, 0, 0))) { learnedPc = from; sendTo = from; }
  linkHandleLine(buf);
}

#if DEBUG_ANY
void linkUdpDebug(DebugText& t) {
  if (linkUdpAppFound()) t.add("app %d.%d.%d.%d", learnedPc[0], learnedPc[1], learnedPc[2], learnedPc[3]);
  else t.add("app not found yet");
  t.add("packets out %lu", packetsOut);
  t.add("packets in %lu", packetsIn);
  t.add("queued %d bytes", txLen);
  t.add("debug dropped %lu", lowDropped);
}
#endif

#endif
