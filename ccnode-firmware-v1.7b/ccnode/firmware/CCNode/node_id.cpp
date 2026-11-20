#include "node_id.h"
#include <stdio.h>

uint16_t nodeIdFromSerial(const uint32_t serial[4]) {
  uint32_t h = 2166136261u;
  for (int w = 0; w < 4; w++)
    for (int b = 0; b < 4; b++) {
      h ^= (serial[w] >> (8 * b)) & 0xFF;
      h *= 16777619u;
    }
  uint16_t id = (uint16_t)(h ^ (h >> 16));
  return id;
}

void bleStaticAddress(uint16_t nodeId, uint8_t out[6]) {
  out[0] = (uint8_t)(nodeId & 0xFF);
  out[1] = (uint8_t)(nodeId >> 8);
  out[2] = 0x00;
  out[3] = 0x00;
  out[4] = 0xCC;
  out[5] = 0xC0;          // top two bits 11 = static random; the random part is never all-0 or all-1
}

void bleAddressString(const uint8_t a[6], char out[18]) {
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", a[5], a[4], a[3], a[2], a[1], a[0]);
}
