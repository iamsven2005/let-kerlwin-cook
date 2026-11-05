#pragma once
#include <stdint.h>

namespace ir_diagnostic {
const uint8_t frequencyCount = 7;
const uint8_t tries = 5;
const uint8_t hitsNeeded = 3;

inline uint8_t consecutiveLevel(const uint8_t hits[frequencyCount]) {
  uint8_t level = 0;
  while (level < frequencyCount && hits[level] >= hitsNeeded) ++level;
  return level;
}
inline uint8_t hitCount(const uint8_t hits[frequencyCount]) {
  uint8_t count = 0;
  for (uint8_t i = 0; i < frequencyCount; ++i)
    if (hits[i] >= hitsNeeded) ++count;
  return count;
}
}
