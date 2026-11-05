/*
  wifi_logic.cpp  -  WiFi connection, signal readings and AP roaming
  See wifi_logic.h for the overview.

  ROAMING
    The WINC1500 chip never switches access points by itself. Whenever the
    signal is below roamTriggerDbm, wifiMaybeRoam() keeps scanning (a new scan
    roamCheckMs after the last one finished) for other APs with the SAME
    network name, and rejoins on a clearly stronger one's channel.
    No readings can be taken during a scan (about 2 s).
    Limitation: the chip can only be told a channel, not one exact AP.
*/
#include "wifi_logic.h"
#include <SPI.h>
#include <WiFi101.h>
extern "C" {
  #include "driver/include/m2m_wifi.h"   // low-level connect (lets us pick a channel)
}

// =====================================================================
//  Settings (DEFAULTS - the brain can override these at startup)
// =====================================================================
char          wifiSsid[33]   = "";  // key: ssid
char          wifiPass[64]   = "";     // key: pass   ("" = open network)
int           roamTriggerDbm = -50;    // key: roam_dbm      look for a better AP below this
int           roamMarginDb   = 8;      // key: roam_margin   new AP must be this much stronger
unsigned long roamCheckMs    = 3000;   // key: roam_check_ms gap between scans (0 = back-to-back)
unsigned long roamCooldownMs = 10000;  // key: roam_cool_ms  wait after a roam before scanning again

// =====================================================================
//  State
// =====================================================================
uint8_t currentBssid[6] = {0};
int     currentChannel  = 0;

static unsigned long lastRoamCheckMs = 0;
static unsigned long lastRoamMs      = 0;

// =====================================================================
//  Helpers
// =====================================================================
static void readCurrentBssid() {
  WiFi.BSSID(currentBssid);
}

// WiFi101 versions differ in byte order between the "current AP" and
// "scan result" BSSID functions, so accept either order.
static bool sameBssid(const uint8_t* a, const uint8_t* b) {
  bool fwd = true, rev = true;
  for (int i = 0; i < 6; i++) {
    if (a[i] != b[i])     fwd = false;
    if (a[i] != b[5 - i]) rev = false;
  }
  return fwd || rev;
}

static bool waitForConnect(unsigned long timeoutMs) {
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) return true;
    onWifiIdle();          // let the sketch keep handling the Bluetooth link
    delay(50);
  }
  return false;
}

static void afterConnect() {
  readCurrentBssid();
  onWifiEvent(WIFI_EV_CONNECTED, 0);
}

// Join the same network, but only on the given channel (= the better AP)
static bool connectOnChannel(uint8_t channel) {
  WiFi.disconnect();
  delay(200);

  uint8_t secType = strlen(wifiPass) > 0 ? M2M_WIFI_SEC_WPA_PSK : M2M_WIFI_SEC_OPEN;
  m2m_wifi_connect(wifiSsid, strlen(wifiSsid), secType, (void*)wifiPass, channel);
  return waitForConnect(10000);
}

// Scan for the same SSID. Roam if another AP is clearly stronger.
// force = true roams to the strongest other AP even if the signal isn't weak.
static void checkRoam(bool force, int liveRssi) {
  lastRoamCheckMs = millis();
  onWifiEvent(WIFI_EV_SCANNING, 0);

  int n = WiFi.scanNetworks();   // blocks for a couple of seconds
  if (n <= 0) { onWifiEvent(WIFI_EV_SCAN_DONE, 0); return; }

  int curScanRssi = -127;
  int bestRssi = -127, bestChannel = 0;
  uint8_t bssid[6];

  for (int i = 0; i < n; i++) {
    if (strcmp(WiFi.SSID(i), wifiSsid) != 0) continue;
    WiFi.BSSID(i, bssid);
    int rssi = WiFi.RSSI(i);
    int ch = WiFi.channel(i);
    if (sameBssid(bssid, currentBssid)) {
      curScanRssi = rssi;
      currentChannel = ch;
    } else if (rssi > bestRssi) {
      bestRssi = rssi;
      bestChannel = ch;
    }
  }

  // If the current AP didn't show up in the scan, fall back to the live reading
  if (curScanRssi == -127 && liveRssi < 0) curScanRssi = liveRssi;

  bool better = bestChannel > 0 && bestRssi >= curScanRssi + roamMarginDb;
  if (!(better || (force && bestChannel > 0))) { onWifiEvent(WIFI_EV_SCAN_DONE, 0); return; }

  onWifiEvent(WIFI_EV_ROAMING, bestChannel);

  if (connectOnChannel(bestChannel)) {
    currentChannel = bestChannel;
    lastRoamMs = millis();
    afterConnect();
  } else {
    wifiConnect();   // fall back to a normal connect
  }
}

// =====================================================================
//  Public functions
// =====================================================================
bool wifiBegin() {
  WiFi.setPins(8, 2, A3, -1);   // WiFi TinyShield pins
  return WiFi.status() != WL_NO_SHIELD;
}

// Normal connect: the chip picks whichever AP it finds
void wifiConnect() {
  onWifiEvent(WIFI_EV_CONNECTING, 0);
  currentChannel = 0;
  while (WiFi.status() != WL_CONNECTED) {
    if (strlen(wifiPass) > 0) WiFi.begin(wifiSsid, wifiPass);
    else                      WiFi.begin(wifiSsid);
    if (WiFi.status() != WL_CONNECTED) onWifiEvent(WIFI_EV_RETRYING, 0);
    onWifiIdle();
  }
  afterConnect();
}

void wifiReconnect() {
  WiFi.disconnect();
  wifiConnect();
}

bool wifiIsConnected() {
  return WiFi.status() == WL_CONNECTED;
}

int wifiReadRssi() {
  int rssi = WiFi.RSSI();
  return rssi < 0 ? rssi : 0;
}

void wifiMaybeRoam(int liveRssi) {
  if (millis() - lastRoamMs < roamCooldownMs) return;
  if (millis() - lastRoamCheckMs < roamCheckMs) return;
  if (liveRssi == 0 || liveRssi >= roamTriggerDbm) return;
  checkRoam(false, liveRssi);
  lastRoamCheckMs = millis();   // gap is measured from the END of the scan
}

void wifiRoamNow(int liveRssi) {
  checkRoam(true, liveRssi);
}
