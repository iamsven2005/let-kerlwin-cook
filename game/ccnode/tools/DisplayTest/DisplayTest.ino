// Minimal TinyScreen+ check: no BLE, no accelerometer, no CCNode code.
// If this stays blank too, check the board setting (Tools > Board > TinyScreen+),
// the upload, and the power switch. If this works but CCNode doesn't, it's the firmware or a shield.
#include <TinyScreen.h>

TinyScreen display = TinyScreen(TinyScreenPlus);
uint32_t n = 0;

void setup() {
  SerialUSB.begin(115200);
  display.begin();
  display.setBrightness(10);
  display.clearScreen();
  display.setFont(liberationSans_10ptFontInfo);
  display.fontColor(TS_8b_Green, TS_8b_Black);
  display.setCursor(4, 4);
  display.print("Display OK");
}

void loop() {
  char b[24];
  snprintf(b, sizeof(b), "t=%lus btn=%X   ", (unsigned long)(millis() / 1000), display.getButtons());
  display.setFont(liberationSans_8ptFontInfo);
  display.fontColor(TS_8b_White, TS_8b_Black);
  display.setCursor(4, 30);
  display.print(b);
  SerialUSB.print("DisplayTest alive ");
  SerialUSB.println(n++);
  delay(1000);
}
