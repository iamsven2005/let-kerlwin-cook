// Select the firmware in roles/common/src/mode.h.
#include <SurveyCommon.h>
#include <SurveyHat.h>
#include <SurveyRoomba.h>

#if SURVEY_MODE == HAT
#include <Wire.h>
#include <SPI.h>
#include <WiFi101.h>
#include <Wireling.h>

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
void setup() { roombaBegin(); }
void loop() { roombaUpdate(); }
#endif
