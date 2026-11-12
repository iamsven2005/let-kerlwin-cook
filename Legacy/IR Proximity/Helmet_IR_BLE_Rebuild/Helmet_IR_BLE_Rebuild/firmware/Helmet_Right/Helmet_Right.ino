// Upload this folder to board R. Keep the complete src folder beside this file.
#include "src/HelmetApp.h"

// Isolation switches: change one at a time when diagnosing hardware.
const bool ENABLE_IR = true;
const bool ENABLE_BLE = true;
const bool ENABLE_SCREEN = true;
const bool ENABLE_USB_LOG = false; // Enable only for USB diagnostics.
const bool TRACE_PHASES = false;  // USB Serial Monitor, 115200 baud

void setup() {
  helmetBegin('R', ENABLE_IR, ENABLE_BLE, ENABLE_SCREEN, ENABLE_USB_LOG, TRACE_PHASES);
}

void loop() {
  helmetLoop();
}
