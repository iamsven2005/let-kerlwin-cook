/*
  wifi.cpp - Wi-Fi connection, signal readings and the AP roaming scan. See wifi.h.

  ROAMING
    The WINC1500 chip never switches access points by itself. Whenever the
    signal is below roamTriggerDbm, wifiMaybeRoam() keeps scanning (a new scan
    roamCheckMs after the last one finished) for other APs with the SAME
    network name, and rejoins on a clearly stronger one's channel.
    No readings can be taken during a scan (about 2 s).
    Limitation: the chip can only be told a channel, not one exact AP.
*/
#include "wifi.h"
#if FEAT_WIFI
#include <SPI.h>
// WiFi101 has no function for username + password (WPA2-Enterprise) networks, but its
// internal connect routine supports them. It is private, so it is opened up here, in
// this file only (the class itself is unchanged).
#define private public
#include <WiFi101.h>
#undef private
extern "C" {
  #include "driver/include/m2m_wifi.h"   // low-level connect (lets us pick a channel)
}
#include "../core/secrets.h"
#include "../core/state.h"
#include "../core/main_loop.h"
#include "link.h"
#include "screen.h"

// ---- Known networks (secrets.h), tried in this order: the first one in range is used
static const WifiProfile wifiProfiles[] = { WIFI_PROFILES };
static const int wifiProfileCount = sizeof(wifiProfiles) / sizeof(wifiProfiles[0]);

char wifiSsid[33] = "";            // key: ssid
char wifiUser[65] = "";            // key: user   ("" = password-only network)
char wifiPass[64] = "";            // key: pass   ("" = open network)
bool wifiManual   = false;         // true after SET ssid/user/pass: don't use the profiles

uint8_t currentBssid[6] = {0};
int     currentChannel  = 0;

static unsigned long connects = 0, connectedSinceMs = 0;

// =====================================================================
//  What the Wi-Fi is doing -> screen and app
// =====================================================================
enum WifiEvent { WIFI_EV_CONNECTING, WIFI_EV_RETRYING, WIFI_EV_CONNECTED,
                 WIFI_EV_SCANNING, WIFI_EV_SCAN_DONE, WIFI_EV_ROAMING };

static void onWifiEvent(WifiEvent ev) {
  switch (ev) {
#if FEAT_SCREEN
    case WIFI_EV_CONNECTING: screenMessage("Connecting..."); break;
    case WIFI_EV_RETRYING:   screenMessage("Retrying..."); break;
#endif
    case WIFI_EV_CONNECTED:
      connects++;
      connectedSinceMs = millis();
#if FEAT_LINK_UDP
      linkUdpBegin();
#endif
#if FEAT_SCREEN
      screenClear();
#endif
      linkSendMessage("Glove connected");
      break;
    case WIFI_EV_ROAMING:    linkSendMessage("Roaming to a stronger access point"); linkFlush(true); break;
    default: break;
  }
}

// =====================================================================
//  Helpers
// =====================================================================
static void readCurrentBssid() { WiFi.BSSID(currentBssid); }

static void afterConnect() {
  readCurrentBssid();
  onWifiEvent(WIFI_EV_CONNECTED);
}

// Username + password for an Enterprise network, in the chip's format
static tstr1xAuthCredentials enterpriseCred() {
  tstr1xAuthCredentials c;
  memset(&c, 0, sizeof(c));
  strncpy((char*)c.au8UserName, wifiUser, sizeof(c.au8UserName) - 1);
  strncpy((char*)c.au8Passwd, wifiPass, sizeof(c.au8Passwd) - 1);
  return c;
}

// One connection attempt to the current network (waits up to ~10 s)
static void beginCurrent() {
  if (strlen(wifiUser) > 0) {
    tstr1xAuthCredentials c = enterpriseCred();
    WiFi.startConnect(wifiSsid, M2M_WIFI_SEC_802_1X, &c);
  } else if (strlen(wifiPass) > 0) {
    WiFi.begin(wifiSsid, wifiPass);
  } else {
    WiFi.begin(wifiSsid);
  }
}

// Pick the first known network that is in range (unless the app set one)
static void pickProfile() {
  if (wifiManual || wifiProfileCount == 0) return;
  int n = WiFi.scanNetworks();
  for (int p = 0; p < wifiProfileCount; p++) {
    bool seen = false;
    for (int i = 0; i < n && !seen; i++) seen = strcmp(WiFi.SSID(i), wifiProfiles[p].ssid) == 0;
    if (!seen && n > 0) continue;
    strncpy(wifiSsid, wifiProfiles[p].ssid, sizeof(wifiSsid) - 1);
    strncpy(wifiUser, wifiProfiles[p].user, sizeof(wifiUser) - 1);
    strncpy(wifiPass, wifiProfiles[p].pass, sizeof(wifiPass) - 1);
    return;
  }
}

// =====================================================================
//  Connecting
// =====================================================================
void wifiFeatureBegin() {
  WiFi.setPins(8, 2, A3, -1);   // WiFi TinyShield pins
  if (WiFi.status() == WL_NO_SHIELD) haltWith("No WiFi shield!");
}

void wifiFeatureUpdate() {
  if (wifiReconnectWanted) { wifiReconnectWanted = false; linkFlush(true); wifiReconnect(); }
  if (!wifiIsConnected()) wifiConnect();
}

// Normal connect: the chip picks whichever AP it finds
void wifiConnect() {
  onWifiEvent(WIFI_EV_CONNECTING);
  currentChannel = 0;
  while (WiFi.status() != WL_CONNECTED) {
    pickProfile();                       // home or school: whichever is in range
    beginCurrent();
    if (WiFi.status() != WL_CONNECTED) onWifiEvent(WIFI_EV_RETRYING);
  }
  afterConnect();
}

void wifiReconnect() {
  WiFi.disconnect();
  wifiConnect();
}

bool wifiIsConnected() { return WiFi.status() == WL_CONNECTED; }

int wifiReadRssi() {
  int rssi = WiFi.RSSI();
  return rssi < 0 ? rssi : 0;
}

void wifiLocalIp(uint8_t ip[4]) {
  IPAddress a = WiFi.localIP();
  for (int i = 0; i < 4; i++) ip[i] = a[i];
}

// =====================================================================
//  Roaming scan (when to call it: roaming.cpp)
// =====================================================================
#if FEAT_ROAM
int           roamTriggerDbm = -50;    // key: roam_dbm      look for a better AP below this
int           roamMarginDb   = 8;      // key: roam_margin   new AP must be this much stronger
unsigned long roamCheckMs    = 3000;   // key: roam_check_ms gap between scans (0 = back-to-back)
unsigned long roamCooldownMs = 10000;  // key: roam_cool_ms  wait after a roam before scanning again

static unsigned long lastRoamCheckMs = 0, lastRoamMs = 0, scans = 0;
static int lastScanResult = 0;

static bool waitForConnect(unsigned long timeoutMs) {
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) return true;
    delay(50);
  }
  return false;
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

// Join the same network, but only on the given channel (= the better AP)
static bool connectOnChannel(uint8_t channel) {
  WiFi.disconnect();
  delay(200);
  if (strlen(wifiUser) > 0) {
    tstr1xAuthCredentials c = enterpriseCred();
    m2m_wifi_connect(wifiSsid, strlen(wifiSsid), M2M_WIFI_SEC_802_1X, &c, channel);
  } else {
    uint8_t secType = strlen(wifiPass) > 0 ? M2M_WIFI_SEC_WPA_PSK : M2M_WIFI_SEC_OPEN;
    m2m_wifi_connect(wifiSsid, strlen(wifiSsid), secType, (void*)wifiPass, channel);
  }
  return waitForConnect(10000);
}

// Scan for the same SSID. Roam if another AP is clearly stronger.
// force = true roams to the strongest other AP even if the signal isn't weak.
static void checkRoam(bool force, int liveRssi) {
  lastRoamCheckMs = millis();
  scans++;
  onWifiEvent(WIFI_EV_SCANNING);

  int n = WiFi.scanNetworks();   // blocks for a couple of seconds
  if (n <= 0) { lastScanResult = 3; onWifiEvent(WIFI_EV_SCAN_DONE); return; }

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
  if (!(better || (force && bestChannel > 0))) { lastScanResult = 1; onWifiEvent(WIFI_EV_SCAN_DONE); return; }

  onWifiEvent(WIFI_EV_ROAMING);
  lastScanResult = 2;
  if (connectOnChannel(bestChannel)) {
    currentChannel = bestChannel;
    lastRoamMs = millis();
    afterConnect();
  } else {
    wifiConnect();   // fall back to a normal connect
  }
}

void wifiMaybeRoam(int liveRssi) {
  if (millis() - lastRoamMs < roamCooldownMs) return;
  if (millis() - lastRoamCheckMs < roamCheckMs) return;
  if (liveRssi == 0 || liveRssi >= roamTriggerDbm) return;
  checkRoam(false, liveRssi);
  lastRoamCheckMs = millis();   // gap is measured from the END of the scan
}

void wifiRoamNow(int liveRssi) { checkRoam(true, liveRssi); }

unsigned long wifiMsUntilNextScan() {
  unsigned long now = millis();
  unsigned long a = now - lastRoamMs < roamCooldownMs ? roamCooldownMs - (now - lastRoamMs) : 0;
  unsigned long b = now - lastRoamCheckMs < roamCheckMs ? roamCheckMs - (now - lastRoamCheckMs) : 0;
  return a > b ? a : b;
}
int wifiLastScanResult() { return lastScanResult; }
unsigned long wifiScanCount() { return scans; }
#endif

// =====================================================================
//  Debug page
// =====================================================================
#if DEBUG_ANY
void wifiDebug(DebugText& t) {
  t.add("%s", wifiSsid[0] ? wifiSsid : "(no network)");
  t.add("%s", wifiIsConnected() ? "connected" : "NOT connected");
  t.add("AP ..%02X:%02X ch%d", currentBssid[4], currentBssid[5], currentChannel);
  t.add("rssi %d dBm", wifiIsConnected() ? wifiReadRssi() : 0);
  t.add("connects %lu, %lu s", connects, connects ? (millis() - connectedSinceMs) / 1000 : 0);
}
#endif

#endif
