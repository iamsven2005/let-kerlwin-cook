// =====================================================
// Chef-Eye  STEP 2: board code for the laptop hub
// - Replies "ID x" when the hub asks "ID?"
// - Shows messages relayed from other boards
// - Sends button presses to the hub
//
// Paste this into the BleTest tab (replace everything).
// Keep the UART tab unchanged.
// =====================================================

#include <SPI.h>
#include <Wire.h>
#include <STBLE.h>
#include <TinyScreen.h>

// ---- Settings shared with UART.ino (do not rename) ----
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

// ---- Change this for each board: '1' to '6' ----
const char BOARD_ID = '2';

TinyScreen display = TinyScreen(TinyScreenPlus);

bool wasConnected = false;
uint8_t lastButtons = 0;

void setup() {
  SerialMonitorInterface.begin(9600);

  display.begin();
  display.setBrightness(10);
  display.setFont(thinPixel7_10ptFontInfo);
  display.clearScreen();

  drawHeader();
  showStatus("Starting BLE...");
  BLEsetup();
  showStatus("Waiting for hub");
}

void loop() {
  aci_loop();   // keeps BLE running - must be called often

  // 1. Connection status changed?
  if (ble_connection_state != wasConnected) {
    wasConnected = ble_connection_state;
    showStatus(wasConnected ? "Connected to hub" : "Waiting for hub");
  }

  // 2. Message arrived?
  if (ble_rx_buffer_len) {
    char msg[21];
    uint8_t len = min(ble_rx_buffer_len, (uint8_t)20);
    memcpy(msg, ble_rx_buffer, len);
    msg[len] = '\0';
    ble_rx_buffer_len = 0;

    if (strcmp(msg, "ID?") == 0) {
      // the hub wants to know which board we are
      char reply[8];
      snprintf(reply, sizeof(reply), "ID %c", BOARD_ID);
      bleSend(reply);
      showStatus("Hub knows me!");
    } else {
      showMessage(msg);
    }
  }

  // 3. Button pressed? Send it to the hub
  uint8_t buttons = display.getButtons();
  if (buttons && !lastButtons && ble_connection_state) {
    const char *name = "?";
    if (buttons & TSButtonUpperLeft)  name = "UpLeft";
    if (buttons & TSButtonUpperRight) name = "UpRight";
    if (buttons & TSButtonLowerLeft)  name = "DownLeft";
    if (buttons & TSButtonLowerRight) name = "DownRight";

    char out[21];
    snprintf(out, sizeof(out), "B%c btn %s", BOARD_ID, name);
    bleSend(out);
    showStatus("Sent button!");
  }
  lastButtons = buttons;
}

// ---------- BLE helper ----------

void bleSend(const char *text) {
  if (!ble_connection_state) return;
  uint8_t len = min(strlen(text), (size_t)20);   // BLE max 20 chars
  lib_aci_send_data(PIPE_UART_OVER_BTLE_UART_TX_TX, (uint8_t *)text, len);
}

// ---------- Screen helpers ----------

void drawHeader() {
  display.fontColor(TS_8b_Yellow, TS_8b_Black);
  display.setCursor(0, 0);
  display.print("Chef-Eye  Board ");
  display.print(BOARD_ID);
  display.drawLine(0, 11, 95, 11, TS_8b_Blue);
}

void showStatus(const char *text) {
  display.drawRect(0, 14, 96, 12, TSRectangleFilled, TS_8b_Black);
  display.fontColor(TS_8b_Green, TS_8b_Black);
  display.setCursor(0, 14);
  display.print(text);
}

void showMessage(const char *text) {
  display.drawRect(0, 30, 96, 34, TSRectangleFilled, TS_8b_Black);
  display.fontColor(TS_8b_White, TS_8b_Black);
  display.setCursor(0, 30);
  display.print("Received:");

  char line[11];
  strncpy(line, text, 10);
  line[10] = '\0';
  display.setCursor(0, 42);
  display.print(line);
  if (strlen(text) > 10) {
    display.setCursor(0, 53);
    display.print(text + 10);
  }
}
