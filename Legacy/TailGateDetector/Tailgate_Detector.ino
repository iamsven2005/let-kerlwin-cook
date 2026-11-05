/*
  TAILGATE DETECTOR  (camera + WiFi -> relay -> Telegram/Discord)
  ===============================================================
  Counts people crossing a doorway (camera straight down from ~2.1 m) and
  sends an alert when 2+ pass on one entry.

  No HTTPS on the device, so NO certificate step is needed. The device sends
  a plain-HTTP request to your Val.town relay, and the relay forwards it to
  Telegram (or Discord) over HTTPS.

  STACK
    TinyScreen+ + WiFi TinyShield
    + Wireling Adapter -> Thermal Camera on port 0, 1 or 2 (NOT port 3)

  LIBRARIES: TinyScreen, Wireling, Adafruit AMG88xx (+ Adafruit BusIO), WiFi101

  SET UP THE RELAY FIRST (relay.py on your laptop):
    Run relay.py on your laptop (same WiFi as the device). It prints your
    laptop's IP. Put that IP in RELAY_HOST below. Keep relay.py running.

  FILL IN the values marked <-- below, then upload.
*/

#include <Wire.h>
#include <Wireling.h>
#include <TinyScreen.h>
#include <Adafruit_AMG88xx.h>
#include <SPI.h>
#include <WiFi101.h>

// ---------------- YOUR SETTINGS ----------------
const char* WIFI_SSID  = "<WIFISSID>";                 // <-- 2.4GHz network name
const char* WIFI_PASS  = "<WIFIPASS>";             // <-- WiFi password
const char* RELAY_HOST = "<RELAY-IP>";              // <-- your laptop's IP (relay.py prints it)
const int   RELAY_PORT = 8080;                        // <-- matches relay.py

// ---------------- tuning ----------------
const float PERSON_MARGIN = 2.5;
const int   MIN_BLOB      = 2;
const int   BIG_BLOB      = 12;

const unsigned long GRACE_MS  = 400;
const unsigned long QUIET_MS  = 1500;
const unsigned long REBASE_MS = 20000;
const unsigned long ALERT_MS  = 4000;

// ---------------- objects ----------------
#define SerialMonitor SerialUSB
Adafruit_AMG88xx amg;
TinyScreen display = TinyScreen(TinyScreenPlus);

float pixels[64], baseline[64], delta[64];
int   cameraPort = -1;
bool  wifiOK = false;

enum { IDLE, ENTRY, ALERT } state = IDLE;
int  groups = 0, maxSeen = 0, maxBlob = 0, lastResult = 0;
bool personActive = false;
unsigned long lastActivity = 0, lastThermal = 0, lastIdle = 0, alertUntil = 0, lastPersonSeen = 0;

// ---------------- WiFi ----------------
bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  SerialMonitor.print(F("WiFi connecting"));
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) { delay(500); SerialMonitor.print('.'); }
  SerialMonitor.println();
  wifiOK = (WiFi.status() == WL_CONNECTED);
  SerialMonitor.println(wifiOK ? F("WiFi OK") : F("WiFi FAILED"));
  return wifiOK;
}

// Send a plain-HTTP (port 80) request to the relay. Spaces -> + in the message.
void sendAlert(const char* text) {
  if (!connectWiFi()) return;

  WiFiClient client;                           // plain HTTP, no certificate
  SerialMonitor.println(F("Contacting relay..."));
  if (!client.connect(RELAY_HOST, RELAY_PORT)) {
    SerialMonitor.println(F("Relay connect FAILED"));
    return;
  }

  String path = "/?msg=";
  for (const char* p = text; *p; p++) path += (*p == ' ') ? '+' : *p;

  client.print(F("GET ")); client.print(path); client.println(F(" HTTP/1.1"));
  client.print(F("Host: ")); client.println(RELAY_HOST);
  client.println(F("Connection: close"));
  client.println();

  unsigned long t = millis();
  while (!client.available() && millis() - t < 8000) delay(10);
  if (client.available()) {
    String status = client.readStringUntil('\n');
    SerialMonitor.print(F("Relay reply: ")); SerialMonitor.println(status);  // expect 200 OK
  } else {
    SerialMonitor.println(F("No reply from relay"));
  }
  client.stop();
}

// ---------------- thermal ----------------
void readThermal() {
  Wireling.selectPort(cameraPort);
  amg.readPixels(pixels);
  for (int i = 0; i < 64; i++) delta[i] = pixels[i] - baseline[i];
}
void captureBaseline() {
  display.clearScreen(); display.setCursor(0, 24); display.print("Learning floor");
  float sum[64] = {0};
  for (int f = 0; f < 20; f++) {
    Wireling.selectPort(cameraPort); amg.readPixels(pixels);
    for (int i = 0; i < 64; i++) sum[i] += pixels[i];
    delay(50);
  }
  for (int i = 0; i < 64; i++) baseline[i] = sum[i] / 20.0;
}
int countPeople(int *biggestOut) {
  bool warm[64], seen[64];
  for (int i = 0; i < 64; i++) { warm[i] = delta[i] > PERSON_MARGIN; seen[i] = false; }
  int stack[64], people = 0, biggest = 0;
  for (int s = 0; s < 64; s++) {
    if (!warm[s] || seen[s]) continue;
    int sp = 0, size = 0; stack[sp++] = s; seen[s] = true;
    while (sp > 0) {
      int c = stack[--sp]; size++;
      int r = c / 8, col = c % 8;
      int nb[4] = { r > 0 ? c - 8 : -1, r < 7 ? c + 8 : -1, col > 0 ? c - 1 : -1, col < 7 ? c + 1 : -1 };
      for (int k = 0; k < 4; k++) { int n = nb[k]; if (n >= 0 && warm[n] && !seen[n]) { seen[n] = true; stack[sp++] = n; } }
    }
    if (size >= MIN_BLOB) { people++; if (size > biggest) biggest = size; }
  }
  *biggestOut = biggest; return people;
}

// ---------------- entry result ----------------
void finishEntry() {
  int people = groups; if (maxSeen > people) people = maxSeen;
  bool tailgate = (groups >= 2) || (maxSeen >= 2) || (maxBlob >= BIG_BLOB);
  char msg[48];

  SerialMonitor.print(F("Entry: groups=")); SerialMonitor.print(groups);
  SerialMonitor.print(F(" maxSeen=")); SerialMonitor.print(maxSeen);
  SerialMonitor.print(F(" biggest=")); SerialMonitor.print(maxBlob);
  SerialMonitor.print(F("  -> "));

  if (people >= 1) {
    lastResult = people;
    if (tailgate) {
      SerialMonitor.println(F("*** TAILGATE ***"));
      snprintf(msg, sizeof(msg), "TAILGATE: %d people entered on one open", people);
      sendAlert(msg);
      state = ALERT; alertUntil = millis() + ALERT_MS;
    } else {
      SerialMonitor.println(F("single entry OK"));
      state = IDLE; lastIdle = millis();
    }
  } else {
    SerialMonitor.println(F("no one counted"));
    state = IDLE; lastIdle = millis();
  }
  groups = 0; maxSeen = 0; maxBlob = 0; personActive = false;
}

// ---------------- display ----------------
void drawStatus(int peopleNow) {
  display.clearScreen();
  for (int r = 0; r < 8; r++)
    for (int c = 0; c < 8; c++)
      display.drawRect(c * 5, 12 + r * 5, 5, 5, TSRectangleFilled,
                       delta[r * 8 + c] > PERSON_MARGIN ? TS_8b_Red : TS_8b_DarkBlue);

  display.setCursor(0, 0);
  if (state == ALERT) { display.fontColor(TS_8b_Red, TS_8b_Black); display.print("TAILGATE!"); }
  else if (state == ENTRY) { display.fontColor(TS_8b_Yellow, TS_8b_Black); display.print("Entry..."); }
  else { display.fontColor(TS_8b_Green, TS_8b_Black); display.print("Monitoring"); }

  display.fontColor(wifiOK ? TS_8b_Blue : TS_8b_Gray, TS_8b_Black);
  display.setCursor(78, 0); display.print("WF");
  display.fontColor(TS_8b_White, TS_8b_Black);

  display.setCursor(52, 14); display.print("Now:");  display.setCursor(82, 14); display.print(peopleNow);
  display.setCursor(52, 28); display.print("Grp:");  display.setCursor(82, 28); display.print(groups);
  display.setCursor(52, 42); display.print("Max:");  display.setCursor(82, 42); display.print(maxSeen);
  display.setCursor(52, 54); display.print("Last:"); display.setCursor(86, 54); display.print(lastResult);
}

// ---------------- setup ----------------
void setup() {
  SerialMonitor.begin(115200);
  while (!SerialMonitor && millis() < 4000);

  display.begin();
  display.setBrightness(10);
  display.setFont(liberationSans_8ptFontInfo);
  display.fontColor(TS_8b_White, TS_8b_Black);
  display.clearScreen(); display.setCursor(0, 24); display.print("Starting...");
  SerialMonitor.println(F("\n[1] display ok"));

  Wire.begin();
  Wireling.begin();
  Wire.setClock(400000);

  WiFi.setPins(8, 2, A3, -1);
  SerialMonitor.println(F("[2] connecting WiFi..."));
  connectWiFi();

  for (int p = 0; p < 4; p++) {
    if (p == 3) continue;
    Wireling.selectPort(p); delay(10);
    if (amg.begin()) { cameraPort = p; break; }
  }
  if (cameraPort < 0) {
    SerialMonitor.println(F("[X] NO CAMERA"));
    display.clearScreen(); display.setCursor(0, 24); display.print("No camera");
    while (true);
  }
  SerialMonitor.print(F("[3] camera on port ")); SerialMonitor.println(cameraPort);

  display.clearScreen(); display.setCursor(0, 24); display.print("Settling 15s");
  SerialMonitor.println(F("[4] settling 15s..."));
  delay(15000);

  captureBaseline();
  SerialMonitor.println(F("[5] floor learned - MONITORING"));
  lastIdle = millis();
  if (wifiOK) sendAlert("Tailgate detector online");
}

// ---------------- loop ----------------
void loop() {
  if (millis() - lastThermal >= 100) {
    lastThermal = millis();
    readThermal();
    int biggest; int peopleNow = countPeople(&biggest);

    if (peopleNow > 0) {
      lastActivity = millis(); lastPersonSeen = millis();
      if (state == IDLE) { state = ENTRY; SerialMonitor.println(F("-- entry start --")); }
      if (peopleNow > maxSeen) maxSeen = peopleNow;
      if (biggest > maxBlob) maxBlob = biggest;
      if (!personActive) { groups++; personActive = true; }
    } else if (personActive && millis() - lastPersonSeen > GRACE_MS) {
      personActive = false;
    }

    if (state == ENTRY && millis() - lastActivity > QUIET_MS) finishEntry();
    if (state == ALERT && millis() > alertUntil) { state = IDLE; lastIdle = millis(); }
    if (state == IDLE && millis() - lastIdle > REBASE_MS) { captureBaseline(); lastIdle = millis(); }

    drawStatus(peopleNow);
  }
}
