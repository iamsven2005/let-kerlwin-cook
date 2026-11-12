#pragma once
#include <stdint.h>
#include <stddef.h>

struct Reading {
  char board;
  uint8_t outer, inner;
  uint16_t sequence;
};

// One GATT write / notification is one whole message. No dynamic allocation.
inline bool parseReading(const uint8_t *p, size_t n, Reading &r) {
  if (n < 7 || n > 11 || (p[0] != 'L' && p[0] != 'R') ||
      p[1] != ',' || p[2] < '0' || p[2] > '6' || p[3] != ',' ||
      p[4] < '0' || p[4] > '6' || p[5] != ',') return false;
  uint32_t seq = 0;
  for (size_t i = 6; i < n; ++i) {
    if (p[i] < '0' || p[i] > '9') return false;
    seq = seq * 10 + p[i] - '0';
    if (seq > 65535) return false;
  }
  r.board = p[0]; r.outer = p[2] - '0'; r.inner = p[4] - '0';
  r.sequence = static_cast<uint16_t>(seq);
  return true;
}
