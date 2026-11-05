/*
  wifi_logic.h  -  WiFi connection, signal readings and AP roaming
  for the WiFi TinyShield (ATWINC1500).

  This file has no screen code. It reports what it's doing through two
  functions that the main sketch must provide:
    onWifiEvent(event, value)  - e.g. update the screen
    onWifiIdle()               - called while it waits (connecting), so the
                                 sketch can keep servicing the Bluetooth link
*/
#pragma once
#include <Arduino.h>

// ---------------- Settings (defaults live in wifi_logic.cpp) ----------------
extern char          wifiSsid[33];     // network name
extern char          wifiPass[64];     // password ("" = open network)
extern int           roamTriggerDbm;   // look for a better AP below this
extern int           roamMarginDb;     // new AP must be this much stronger
extern unsigned long roamCheckMs;      // gap between scans (0 = back-to-back)
extern unsigned long roamCooldownMs;   // wait after a roam before scanning again

// ---------------- Current AP (read-only for the sketch) ----------------
extern uint8_t currentBssid[6];        // MAC address of the AP we're on
extern int     currentChannel;         // its channel (0 = unknown until the first scan)

// ---------------- Events reported to the sketch ----------------
enum WifiEvent {
  WIFI_EV_CONNECTING,   // starting to connect
  WIFI_EV_RETRYING,     // a connect attempt failed, trying again
  WIFI_EV_CONNECTED,    // connected (possibly to a different AP than before)
  WIFI_EV_SCANNING,     // roaming scan started (blocks for ~2 s)
  WIFI_EV_SCAN_DONE,    // scan finished, staying on the same AP
  WIFI_EV_ROAMING       // switching AP; value = target channel
};

// The main sketch must define these two functions.
void onWifiEvent(WifiEvent ev, int value);
void onWifiIdle();

// ---------------- Functions ----------------
bool wifiBegin();                  // set up the shield; false if no shield found
void wifiConnect();                // connect, retrying until it works (blocks)
void wifiReconnect();              // disconnect, then wifiConnect() (after ssid/pass change)
bool wifiIsConnected();
int  wifiReadRssi();               // current signal in dBm, or 0 if no valid reading

// Roaming. liveRssi = the latest reading from wifiReadRssi().
void wifiMaybeRoam(int liveRssi);  // call every loop: scans/roams only when due
void wifiRoamNow(int liveRssi);    // scan now and move to the strongest other AP
