// Select the firmware at compile time in src/common/mode.h.
#include "src/common/mode.h"

#if SURVEY_MODE == HAT
#include <Wire.h>
#include <SPI.h>
#include <WiFi101.h>
#include <Wireling.h>
#include "src/hat/config.h"
#include "src/common/wifi_logic.h"
#include "src/common/accel_bma250.h"
#include "src/hat/motion.h"
#include "src/hat/hand_pose.h"
#include "src/hat/ir_sensor.h"
#include "src/hat/readings.h"
#include "src/hat/link.h"
#include "src/hat/commands.h"
#include "src/hat/debug.h"
#include "src/hat/buttons.h"
#include "src/hat/screen.h"

unsigned long lastAccMs = 0, lastIrMs = 0, lastSendMs = 0, lastScreenMs = 0;

void setup() {
  SerialUSB.begin(115200);
  Wire.begin();
  screenBegin();
  Wireling.begin();
  irBegin();
  if (!bmaBegin()) { screenMessage("No accelerometer!"); while (true) delay(1000); }
  if (!wifiBegin()) { screenMessage("No WiFi shield!"); while (true) delay(1000); }
  wifiConnect();
}

void loop() {
  unsigned long now = millis();
  if (wifiReconnectWanted) { wifiReconnectWanted = false; linkFlush(true); wifiReconnect(); }
  if (!wifiIsConnected()) wifiConnect();
  linkCheckIncoming();
  buttonsUpdate();
  if (now - lastAccMs >= 20) { lastAccMs = now; motionUpdate(); handPoseUpdate(); }
  if (now - lastIrMs >= 50) { lastIrMs = now; irUpdate(); }
  readingsUpdate();
  if (roamRequested) { roamRequested = false; wifiRoamNow(currentRssi); }
  else if (autoRoam && still && !measuring) wifiMaybeRoam(currentRssi);
  readingsService();
  if (now - lastSendMs >= 200) { lastSendMs = now; linkSendTelemetry(); }
  if (now - lastScreenMs >= 250) { lastScreenMs = now; screenUpdate(); }
  debugMirrorUpdate();
  linkFlush(false);
}
#elif SURVEY_MODE == ROOMBA
#include "src/roomba/roomba.h"
void setup() { roombaBegin(); }
void loop() { roombaUpdate(); }
#endif
