/*
  GloveSurvey.ino - hand-worn Wi-Fi survey glove (right hand). setup() and loop() only:
  each feature is in its own file pair (.h = what it offers, .cpp = how).

    config.h/.cpp        every setting, with defaults (many changeable from the app: SET)
    accel_bma250.h/.cpp  the Accelerometer TinyShield (Bosch BMA250) driver
    motion.h/.cpp        steps, walking vs still, hand tilt (all the accelerometer CAN do)
    heading.h/.cpp       which way the PERSON faces (turn buttons today; a waist gyro later)
    tracker.h/.cpp       position = steps x step length along the heading; step calibration
    hand_pose.h/.cpp     hand HANGING / POINTING / other, from the tilt
    ir_sensor.h/.cpp     IR YES/NO; obstacle points in the direction the SENSOR points
    readings.h/.cpp      Wi-Fi readings FAST/SLOW + the cache the app confirms
    link.h/.cpp          sending to / receiving from the app (one packet per loop)
    commands.h/.cpp      commands from the app (C,<id>,...) and SET keys
    buttons.h/.cpp       the four screen buttons
    screen.h/.cpp        the display
    wifi_logic.h/.cpp    Wi-Fi networks (home/school list), connecting, roaming

  Hardware: TinyScreen+ + Wi-Fi TinyShield + Accelerometer TinyShield ASD2511-R-A +
  Wireling Adapter (IR Emitter on port 0, IR Receiver on port 1).
  The accelerometer can't tell which way the person turns: see heading.h.
*/
#include <Wire.h>
#include <SPI.h>
#include <WiFi101.h>
#include <Wireling.h>
#include "config.h"
#include "wifi_logic.h"
#include "accel_bma250.h"
#include "motion.h"
#include "hand_pose.h"
#include "ir_sensor.h"
#include "readings.h"
#include "link.h"
#include "commands.h"
#include "buttons.h"
#include "screen.h"

unsigned long lastAccMs = 0, lastIrMs = 0, lastSendMs = 0, lastScreenMs = 0;

void setup() {
  SerialUSB.begin(115200);
  Wire.begin();
  screenBegin();
  Wireling.begin();                          // powers the Wireling ports
  irBegin();

  if (!bmaBegin()) { screenMessage("No accelerometer!"); while (true) delay(1000); }
  if (!wifiBegin()) { screenMessage("No WiFi shield!"); while (true) delay(1000); }
  wifiConnect();                             // home or school: whichever is in range
}

void loop() {
  unsigned long now = millis();

  if (wifiReconnectWanted) { wifiReconnectWanted = false; linkFlush(true); wifiReconnect(); }
  if (!wifiIsConnected()) wifiConnect();

  linkCheckIncoming();                       // commands from the app
  buttonsUpdate();

  if (now - lastAccMs >= 20) { lastAccMs = now; motionUpdate(); handPoseUpdate(); }   // 50 Hz
  if (now - lastIrMs >= 50)  { lastIrMs = now; irUpdate(); }

  readingsUpdate();
  if (roamRequested) { roamRequested = false; wifiRoamNow(currentRssi); }
  else if (autoRoam && still && !measuring) wifiMaybeRoam(currentRssi);   // only while standing still

  readingsService();
  if (now - lastSendMs >= 200)   { lastSendMs = now; linkSendTelemetry(); }
  if (now - lastScreenMs >= 250) { lastScreenMs = now; screenUpdate(); }
  linkFlush(false);                          // one packet per pass, spaced out
}
