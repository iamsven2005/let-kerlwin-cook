// =====================================================
// Multi-device IR NODE (one per direction)
// - Based on: "IR Proximity/IR_Proximity_Test.ino" (38 kHz bursts)
//   + "Bluetooth/BleTest.ino" (BLE UART transport, 20-byte packets)
// - UART.ino in this folder is a verbatim copy of Bluetooth/UART.ino.
//   Keep it unchanged, as BleTest instructs.
//
// WHAT CHANGED vs the old polling code:
//   Old: brain polled the sensor every 1 s (diagram) / every 50 ms
//        (Roomba sketch), and got a level / 0-1 stream every time.
//   New: this node sends NOTHING while the scene is unchanged.
//        Only when a *confirmed* proximity edge occurs does it send
//        exactly one byte: "1".
//        The brain toggles its own memory: has_wall ^= 1.
//        First "1"  -> brain remembers a wall.
//        Second "1" -> brain forgets it (wall gone).
//        See MultiDevice/PROTOCOL.md section 2.
//
// SAFETY, READ THIS BEFORE "FIXING" IT:
//   Do NOT retransmit "1" if the brain's "A" (ACK) is lost. A resent
//   toggle would flip the brain a second time and re-invert it.
//   Instead, the brain asks "SYNC?" on reconnect, and this node
//   answers "S0"/"S1" (absolute state) every 30 s and on request.
//   On "S0"/"S1" the brain SETS has_wall, it does not toggle.
//   That heals a single lost "1" within 30 s, at 1/30th the polling cost.
//
// CHANGE THIS PER BOARD - one node per direction:
//   SENSOR_ID: 'F' (front), 'L', 'R', or 'R' + "ear" -> see ID_REPLY below
// =====================================================

#include <SPI.h>
#include <Wire.h>
#include <Wireling.h>
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

// ---- Change this for each IR board ----
const char SENSOR_ID = 'F';  // 'F', 'L', or 'R' (rear node: ID_REPLY "IR-Rear")
const char ID_REPLY[] = "ID IR-F";  // MUST match the direction: IR-F / IR-L / IR-R / IR-Rear

// ---- IR hardware (same method as IR_Proximity_Test.ino) ----
// Avoid Wireling port 3 / A3: the WiFi TinyShield uses A3 as reset.
const uint8_t EMITTER_PIN  = A0;  // IR Emitter Wireling, port 0
const uint8_t RECEIVER_PIN = A1;  // IR Receiver Wireling, port 1
const uint8_t IR_CONFIRM_COUNT = 2;  // consecutive reads before an edge counts
const unsigned long SAMPLE_MS = 50;  // same cadence as the old Roomba IR poll
const unsigned long SYNC_MS = 30000; // absolute-state resync, NOT a poll

TinyScreen display = TinyScreen(TinyScreenPlus);

bool present = false;        // confirmed local state: wall in range?
bool lastReported = false;   // last state the brain was told about (via "1" or "S")
uint8_t confirmHits = 0;     // consecutive "seen" reads
uint8_t confirmMisses = 0;   // consecutive "not seen" reads
bool wasConnected = false;
unsigned long lastSample = 0, lastSync = 0;

bool burstSeen() {
  // One 38 kHz burst; receiver output goes LOW when it sees reflection.
  // Kept deliberately close to the old Roomba readIr(): ~20 flashes.
  bool seen = false;
  for (int i = 0; i < 20; i++) {
    digitalWrite(EMITTER_PIN, HIGH);
    delayMicroseconds(9);
    digitalWrite(EMITTER_PIN, LOW);
    delayMicroseconds(9);
    if (i >= 8 && digitalRead(RECEIVER_PIN) == LOW) seen = true;
  }
  delayMicroseconds(150);
  if (digitalRead(RECEIVER_PIN) == LOW) seen = true;
  return seen;
}

void bleSend(const char *text) {
  if (!ble_connection_state) return;
  uint8_t len = min(strlen(text), (size_t)20);
  lib_aci_send_data(PIPE_UART_OVER_BTLE_UART_TX_TX, (uint8_t *)text, len);
}

void sendAbsoluteSync() {
  // "S1" = wall present, "S0" = clear. Brain SETS, does not toggle.
  char msg[4];
  snprintf(msg, sizeof(msg), "S%c", present ? '1' : '0');
  bleSend(msg);
  lastReported = present;
  lastSync = millis();
}

void setup() {
  SerialMonitorInterface.begin(9600);
  Wire.begin();
  Wireling.begin();
  pinMode(EMITTER_PIN, OUTPUT);
  digitalWrite(EMITTER_PIN, LOW);
  pinMode(RECEIVER_PIN, INPUT_PULLUP);

  display.begin();
  display.setBrightness(10);
  display.setFont(thinPixel7_10ptFontInfo);
  display.clearScreen();
  display.setCursor(0, 0);
  display.print("IR node ");
  display.print(SENSOR_ID);

  BLEsetup();
}

void loop() {
  aci_loop();

  if (ble_connection_state != wasConnected) {
    wasConnected = ble_connection_state;
    if (wasConnected) {
      // Fresh connection: brain will ask SYNC? anyway; send absolute
      // state immediately so it never has to guess from toggle history.
      sendAbsoluteSync();
    }
  }

  // Incoming brain messages: "ID?", "SYNC?", "A" (ACK, ignore).
  if (ble_rx_buffer_len) {
    char msg[21];
    uint8_t len = min(ble_rx_buffer_len, (uint8_t)20);
    memcpy(msg, ble_rx_buffer, len);
    msg[len] = '\0';
    ble_rx_buffer_len = 0;
    if (strcmp(msg, "ID?") == 0) {
      bleSend(ID_REPLY);
    } else if (strcmp(msg, "SYNC?") == 0) {
      sendAbsoluteSync();
    }
    // "A" = brain ACKed our "1". Nothing to do, and crucially:
    // no retransmit. See header comment.
  }

  unsigned long now = millis();
  if (now - lastSample < SAMPLE_MS) return;
  lastSample = now;

  // Confirm the edge locally before telling the brain anything.
  if (burstSeen()) {
    confirmHits++;
    confirmMisses = 0;
    if (confirmHits >= IR_CONFIRM_COUNT) present = true;
  } else {
    confirmMisses++;
    confirmHits = 0;
    if (confirmMisses >= IR_CONFIRM_COUNT) present = false;
  }

  // THE interrupt: only a confirmed change, only one byte.
  if (present != lastReported) {
    bleSend("1");
    lastReported = present;
    lastSync = now;  // a just-sent edge is fresh knowledge; sync clock restarts
    display.setCursor(0, 14);
    display.print(present ? "wall: sent 1" : "clear: sent 1");
  } else if (now - lastSync >= SYNC_MS && ble_connection_state) {
    // Rare absolute resync. This is the ONLY periodic IR traffic.
    sendAbsoluteSync();
  }
}
