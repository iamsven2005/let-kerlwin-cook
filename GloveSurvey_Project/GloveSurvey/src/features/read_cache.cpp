#include "read_cache.h"
#if FEAT_ANY_READINGS
#include "../core/state.h"
#include "link.h"
#include "wifi.h"

struct Reading { uint16_t seq; uint32_t takenMs; int16_t x, y; int8_t rssi, minRssi, maxRssi; char mode; };

static uint16_t nextSeq = 0;

static void sendReading(const Reading& r) {
  char line[64];
  snprintf(line, sizeof(line), "R,%u,%c,%d,%d,%d,%lu,%d,%d\n", r.seq, r.mode, r.rssi, r.minRssi, r.maxRssi,
           (unsigned long)(millis() - r.takenMs), r.x, r.y);
  linkSendLine(line);
}

static Reading makeReading(char mode, int rssi, int lo, int hi) {
  Reading r;
  r.seq = nextSeq++;
  r.takenMs = millis();
  r.x = (int16_t)constrain(lround(posX), -32000L, 32000L);
  r.y = (int16_t)constrain(lround(posY), -32000L, 32000L);
  r.rssi = rssi; r.minRssi = lo; r.maxRssi = hi; r.mode = mode;
  return r;
}

#if FEAT_READ_CACHE
// ---------------------------------------------------------------- with the cache
static Reading cache[CACHE_SIZE];
static int cacheHead = 0;
static uint16_t sentSeq = 0;
static bool awaitingAck = false;
static unsigned long sentAtMs = 0, pushed = 0, confirmed = 0, resends = 0, overwritten = 0;

void readingsPush(char mode, int rssi, int lo, int hi) {
  Reading r = makeReading(mode, rssi, lo, hi);
  pushed++;
  if (cacheCount == CACHE_SIZE) { cache[cacheHead] = r; cacheHead = (cacheHead + 1) % CACHE_SIZE; overwritten++; }
  else { cache[(cacheHead + cacheCount) % CACHE_SIZE] = r; cacheCount++; }
}

static void sendOldest() {
  sendReading(cache[cacheHead]);
  awaitingAck = true;
  sentSeq = cache[cacheHead].seq;
  sentAtMs = millis();
}

void readCacheUpdate() {
#if FEAT_LINK_UDP
  if (!wifiIsConnected()) { awaitingAck = false; return; }
#endif
  if (awaitingAck) {
    if (!(cacheCount > 0 && cache[cacheHead].seq == sentSeq)) awaitingAck = false;           // overwritten
    else if (millis() - sentAtMs >= ackTimeoutMs) { resends++; sendOldest(); return; }      // resend
    else return;
  }
  if (cacheCount > 0) sendOldest();
}

void readingsAck(uint16_t seq) {
  if (awaitingAck && cacheCount > 0 && cache[cacheHead].seq == seq) {
    cacheHead = (cacheHead + 1) % CACHE_SIZE;
    cacheCount--;
    awaitingAck = false;
    confirmed++;
  }
}

#if DEBUG_ANY
void readCacheDebug(DebugText& t) {
  t.add("waiting %d / %d", cacheCount, CACHE_SIZE);
  t.add("taken %lu", pushed);
  t.add("confirmed %lu", confirmed);
  t.add("resends %lu", resends);
  t.add("LOST %lu", overwritten);
}
#endif

#else
// ---------------------------------------------------------------- no cache: send once
void readingsPush(char mode, int rssi, int lo, int hi) { sendReading(makeReading(mode, rssi, lo, hi)); }
#endif

#endif
