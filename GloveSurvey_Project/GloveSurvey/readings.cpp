#include "readings.h"
#include "config.h"
#include "tracker.h"
#include "motion.h"
#include "link.h"
#include "wifi_logic.h"

struct Reading { uint16_t seq; uint32_t takenMs; int16_t x, y; int8_t rssi, minRssi, maxRssi; char mode; };

bool slowMode = false, measuring = false;
int shownRssi = 0, currentRssi = 0, cacheCount = 0;

static Reading cache[CACHE_SIZE];
static int cacheHead = 0;
static uint16_t nextSeq = 0, sentSeq = 0;
static bool awaitingAck = false;
static unsigned long sentAtMs = 0, lastReadMs = 0;
static long sampleSum = 0;
static int sampleCount = 0, sampleMin = 0, sampleMax = -127;
static bool waitForMove = false;     // SLOW: one average per spot

void readingsResetAverage() { sampleSum = 0; sampleCount = 0; sampleMin = 0; sampleMax = -127; }

void readingsMeasureHere() { measuring = true; readingsResetAverage(); }

static void cachePush(char mode, int rssi, int lo, int hi) {
  Reading r;
  r.seq = nextSeq++;
  r.takenMs = millis();
  r.x = (int16_t)constrain(lround(posX), -32000L, 32000L);
  r.y = (int16_t)constrain(lround(posY), -32000L, 32000L);
  r.rssi = rssi; r.minRssi = lo; r.maxRssi = hi; r.mode = mode;
  if (cacheCount == CACHE_SIZE) { cache[cacheHead] = r; cacheHead = (cacheHead + 1) % CACHE_SIZE; }
  else { cache[(cacheHead + cacheCount) % CACHE_SIZE] = r; cacheCount++; }
}

static void sendOldest() {
  const Reading& r = cache[cacheHead];
  char line[64];
  snprintf(line, sizeof(line), "R,%u,%c,%d,%d,%d,%lu,%d,%d\n", r.seq, r.mode, r.rssi, r.minRssi, r.maxRssi,
           (unsigned long)(millis() - r.takenMs), r.x, r.y);
  linkSendLine(line);
  awaitingAck = true;
  sentSeq = r.seq;
  sentAtMs = millis();
}

void readingsService() {
  if (!wifiIsConnected()) { awaitingAck = false; return; }
  if (awaitingAck) {
    if (!(cacheCount > 0 && cache[cacheHead].seq == sentSeq)) awaitingAck = false;   // overwritten
    else if (millis() - sentAtMs >= ackTimeoutMs) { sendOldest(); return; }          // resend
    else return;
  }
  if (cacheCount > 0) sendOldest();
}

void readingsAck(uint16_t seq) {
  if (awaitingAck && cacheCount > 0 && cache[cacheHead].seq == seq) {
    cacheHead = (cacheHead + 1) % CACHE_SIZE;
    cacheCount--;
    awaitingAck = false;
  }
}

void readingsUpdate() {
  if (movedSinceStill) { waitForMove = false; movedSinceStill = false; }
  bool averaging = measuring || (slowMode && still && !waitForMove && tracking);
  if (millis() - lastReadMs < (averaging ? slowIntervalMs : fastIntervalMs)) return;
  lastReadMs = millis();
  if (!averaging && slowMode) return;              // SLOW and walking: no readings
  int rssi = wifiReadRssi();
  if (rssi == 0) return;
  currentRssi = rssi;
  if (!averaging) { cachePush('F', rssi, rssi, rssi); shownRssi = rssi; return; }

  sampleSum += rssi;
  sampleCount++;
  if (rssi < sampleMin) sampleMin = rssi;
  if (rssi > sampleMax) sampleMax = rssi;
  if (sampleCount >= slowSamples) {
    int avg = (int)((sampleSum - slowSamples / 2) / slowSamples);
    cachePush('S', avg, sampleMin, sampleMax);
    shownRssi = avg;
    char msg[48];
    snprintf(msg, sizeof(msg), "Measured %d dBm (%d to %d)", avg, sampleMin, sampleMax);
    linkSendMessage(msg);
    readingsResetAverage();
    measuring = false;
    waitForMove = true;
  }
}
