// Unique, stable node ID from the SAMD21's 128-bit factory serial number.
#pragma once
#include <stdint.h>

// FNV-1a over the 16 serial bytes, folded to 16 bits. Pure function (testable).
uint16_t nodeIdFromSerial(const uint32_t serial[4]);

// BLE static random address for a node (brief v1.5 FW-Q31): the two most significant bits are 11, the
// rest comes from the node ID, so it never changes across power cycles. Readable form
// C0:CC:00:00:<ID hi>:<ID lo>, e.g. C0:CC:00:00:3F:2A for CCNODE-3F2A.
// out[] is in the stack's byte order (little-endian: out[0] = least significant byte).
void bleStaticAddress(uint16_t nodeId, uint8_t out[6]);
void bleAddressString(const uint8_t addr[6], char out[18]);   // "C0:CC:00:00:3F:2A"

#if defined(ARDUINO_ARCH_SAMD)
inline uint16_t readNodeId() {
  // SAMD21 datasheet 9.3.3: serial number words.
  const uint32_t s[4] = {
    *(volatile uint32_t*)0x0080A00C, *(volatile uint32_t*)0x0080A040,
    *(volatile uint32_t*)0x0080A044, *(volatile uint32_t*)0x0080A048,
  };
  return nodeIdFromSerial(s);
}
#endif
