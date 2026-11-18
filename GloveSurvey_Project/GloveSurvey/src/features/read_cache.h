/*
  read_cache - every Wi-Fi reading goes through readingsPush(). With the cache on, each
  reading is kept until the app confirms it (R,seq,... -> A,seq) and resent after ack_ms.
  With FEAT_READ_CACHE off, readingsPush() sends each reading once and forgets it.
  Fills cacheCount (state.h).
*/
#pragma once
#include "../core/feature.h"

#if FEAT_ANY_READINGS
// mode 'F' = single reading, 'S' = average (lo/hi = its spread)
void readingsPush(char mode, int rssi, int lo, int hi);
#endif

#if FEAT_READ_CACHE
void readCacheUpdate();             // send / resend the oldest unconfirmed reading
void readCacheDebug(DebugText& t);
void readingsAck(uint16_t seq);     // the app confirmed a reading
#else
inline void readingsAck(uint16_t) {}
#endif
