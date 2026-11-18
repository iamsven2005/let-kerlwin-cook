// BLE peripheral with the Nordic UART Service (brief v1.2 section 5), on the
// TinyCircuits BLE TinyShield (ST ASD2116, BlueNRG-MS). All BLE code lives here.
#pragma once
#include <stdint.h>

namespace BleLink {
  struct Callbacks {
    void (*onRx)(const uint8_t* data, uint8_t len);   // one GATT write from the hub
    void (*onReady)();                                // hub enabled TX notifications (every (re)subscribe): send the HELLO sync
    void (*onDisconnect)();
    void (*log)(const char* msg);                     // debug log (may be null)
  };

  // false if the module doesn't answer. staticAddr: little-endian static random address (node_id.h).
  bool begin(const char* deviceName, const uint8_t staticAddr[6], const Callbacks& cb);
  bool addressSet();                  // our static random address was accepted by the controller
  void bleAddr(char out[18]);         // "C0:CC:00:00:3F:2A"
  void poll(uint32_t nowMs);          // call every loop pass
  bool connected();                   // link layer connected
  const char* stateName();            // "adv-fast", "adv-slow", "connected", "subscribed", "idle"
  bool ready();                       // hub has TX notifications enabled
  // Queue one protocol message (without '\n'; it is added). Sent as one
  // notification. Returns false if the hub isn't subscribed (message dropped).
  bool send(const char* msg);
  void flush();                       // push queued messages to the radio now
  uint16_t connIntervalUnits();       // current interval in 1.25 ms units (0 = unknown)
  uint32_t droppedTx();
}
