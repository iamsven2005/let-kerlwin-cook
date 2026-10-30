// =====================================================
// Master / slave SLAVE (generic board)
// - Based on: "Bluetooth/BleTest.ino" (BLE UART + TinyScreen).
//   UART.ino in this folder is a verbatim copy of Bluetooth/UART.ino;
//   keep it unchanged.
// - A slave talks ONLY to the master. To reach other slaves it
//   asks the master to route for it. There is no slave-to-slave
//   BLE link in this setup (see MasterSlave/PROTOCOL.md §1).
//
// MESSAGES THIS SLAVE UNDERSTANDS (from the master):
//   "ID?"              -> replies "ID <ROLE>" (see below)
//   anything else      -> shown on screen. It arrives WITHOUT any
//                         routing envelope: the master strips
//                         "R <targets> ..." before delivery, so a
//                         delivered message can never loop back.
//
// MESSAGES THIS SLAVE SENDS (to the master):
//   sendToMaster("...")            -> master only, never forwarded.
//                                    Use for sensor reports, DONE,
//                                    or anything only the master
//                                    should act on.
//   sendToFew("MOTOR", "...")     -> master forwards to MOTOR only.
//   sendToFew("IR-F,IR-L", "...") -> master forwards to those two.
//   sendToFew("IR", "...")        -> master expands the IR group.
//   sendToFew("ALL", "...")       -> master forwards to everyone
//                                    except this board. Explicit
//                                    only: never the default.
//
//   On the air a routed message is "R <targets> <payload>", e.g.
//   "R MOTOR STOP" (12 bytes). Keep targets + payload under the
//   20-byte BLE packet limit or the master cannot carry it in one
//   packet. Short roles and short payloads are a feature here.
//
// BUTTONS (same as BleTest, kept for a hands-on test):
//   Upper-left  -> sendToMaster("HELLO")        (master only)
//   Upper-right -> sendToFew("ALL", "HELLO")    (everyone else)
//   Lower-left  -> sendToFew("MOTOR", "STOP")   (one board)
//   Lower-right -> sendToFew("IR", "ALERT")     (the IR group)
//   Change the targets in loop() to match the roles on your build.
//
// CHANGE THIS PER BOARD:
// =====================================================

#include <SPI.h>
#include <Wire.h>
#include <STBLE.h>
#include <TinyScreen.h>

// ---- BLE transport, shared with UART.ino (do not rename) ----
#if defined(ARDUINO_ARCH_SAMD)
  #define SerialMonitorInterface SerialUSB
#else
  #define SerialMonitorInterface Serial
#endif
#define BLE_DEBUG true
#define PIPE_UART_OVER_BTLE_UART_TX_TX 0

uint8_t ble_rx_buffer[21];
uint8_t ble_rx_buffer_len = 0;
uint8_t ble_connection_state = false;

// ---- Change this for each slave board ----
const char ROLE[] = "DISPLAY";       // e.g. IR-F, MOTOR, DISPLAY, WIFI
const char ID_REPLY[] = "ID DISPLAY";  // MUST be "ID " + ROLE

TinyScreen display = TinyScreen(TinyScreenPlus);
bool wasConnected = false;
uint8_t lastButtons = 0;

void bleSend(const char *text) {
  if (!ble_connection_state) return;
  uint8_t len = min(strlen(text), (size_t)20);
  lib_aci_send_data(PIPE_UART_OVER_BTLE_UART_TX_TX, (uint8_t *)text, len);
}

// Master only: never forwarded to another slave.
void sendToMaster(const char *payload) {
  bleSend(payload);
}

// One to few, via the master. targets: "MOTOR", "IR-F,IR-L", "IR", "ALL".
// The master strips "R <targets> " before delivery, so recipients
// see only the payload and cannot loop it back.
void sendToFew(const char *targets, const char *payload) {
  char msg[21];
  snprintf(msg, sizeof(msg), "R %s %s", targets, payload);
  // snprintf truncates at 20 chars rather than sending two packets.
  // If you see a truncated message on the master, shorten the
  // targets or the payload; do not split it here and hope the
  // master reassembles it, because it will not.
  bleSend(msg);
}

void setup() {
  SerialMonitorInterface.begin(9600);
  display.begin();
  display.setBrightness(10);
  display.setFont(thinPixel7_10ptFontInfo);
  display.clearScreen();
  display.setCursor(0, 0);
  display.print("Slave ");
  display.print(ROLE);
  BLEsetup();
}

void loop() {
  aci_loop();

  if (ble_connection_state != wasConnected) {
    wasConnected = ble_connection_state;
    display.setCursor(0, 14);
    display.print(wasConnected ? "Connected: master" : "Waiting: master ");
  }

  if (ble_rx_buffer_len) {
    char msg[21];
    uint8_t len = min(ble_rx_buffer_len, (uint8_t)20);
    memcpy(msg, ble_rx_buffer, len);
    msg[len] = '\0';
    ble_rx_buffer_len = 0;

    if (strcmp(msg, "ID?") == 0) {
      bleSend(ID_REPLY);
    } else if (strncmp(msg, "ERR ", 4) == 0) {
      // Master telling us a routed target was unknown/offline.
      display.setCursor(0, 30);
      display.print(msg);
    } else {
      // Delivered message (envelope already stripped by master),
      // or a direct master command. Show it; do NOT re-send it.
      display.setCursor(0, 30);
      display.print("Recv: ");
      display.print(msg);
    }
  }

  // Buttons: a hands-on way to test one / few / all from one board.
  uint8_t buttons = display.getButtons();
  if (buttons && !lastButtons && ble_connection_state) {
    if (buttons & TSButtonUpperLeft)       sendToMaster("HELLO");
    else if (buttons & TSButtonUpperRight) sendToFew("ALL", "HELLO");
    else if (buttons & TSButtonLowerLeft)  sendToFew("MOTOR", "STOP");
    else if (buttons & TSButtonLowerRight) sendToFew("IR", "ALERT");
  }
  lastButtons = buttons;
}
