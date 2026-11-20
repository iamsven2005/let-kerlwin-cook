/*
  wifi - Wi-Fi connection for the Wi-Fi TinyShield (ATWINC1500): network profiles
  (home/school, from secrets.h), connecting, reconnecting, signal readings.
  The roaming scan itself also lives here (it needs the chip's low-level connect);
  when to roam is decided by the roaming feature.
  Stops with "No WiFi shield!" at start-up if the shield doesn't answer.
*/
#pragma once
#include "../core/feature.h"

#if FEAT_WIFI
#include <Arduino.h>

// ---- feature table
void wifiFeatureBegin();           // finds the shield (connecting happens in the loop)
void wifiFeatureUpdate();          // (re)connects when needed
void wifiDebug(DebugText& t);

// ---- network in use (filled from a profile, or set from the app with SET ssid/user/pass)
struct WifiProfile { const char* ssid; const char* user; const char* pass; };
extern char wifiSsid[33];          // network name in use
extern char wifiUser[65];          // username ("" = password-only network)
extern char wifiPass[64];          // password ("" = open network)
extern bool wifiManual;            // set from the app: ignore the profiles

// ---- current AP (read-only for other features)
extern uint8_t currentBssid[6];    // MAC address of the AP we're on
extern int     currentChannel;     // its channel (0 = unknown until the first scan)

bool wifiIsConnected();
int  wifiReadRssi();               // current signal in dBm, or 0 if no valid reading
void wifiConnect();                // connect, retrying until it works (blocks)
void wifiReconnect();              // disconnect, then wifiConnect()
void wifiLocalIp(uint8_t ip[4]);

#if FEAT_ROAM
extern int           roamTriggerDbm;   // look for a better AP below this
extern int           roamMarginDb;     // new AP must be this much stronger
extern unsigned long roamCheckMs;      // gap between scans (0 = back-to-back)
extern unsigned long roamCooldownMs;   // wait after a roam before scanning again
// liveRssi = the latest reading from wifiReadRssi()
void wifiMaybeRoam(int liveRssi);  // scans/roams only when due
void wifiRoamNow(int liveRssi);    // scan now and move to the strongest other AP
// For the roaming debug page
unsigned long wifiMsUntilNextScan();
int  wifiLastScanResult();         // 0 none yet, 1 stayed, 2 roamed, 3 scan found nothing
unsigned long wifiScanCount();
#endif

#else
inline bool wifiIsConnected() { return false; }
inline int  wifiReadRssi() { return 0; }
#endif
