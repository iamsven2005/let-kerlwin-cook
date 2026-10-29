/*
  TinyScreenRoomba.ino
  Wi-Fi survey robot: wheel odometry + IR obstacle sensing + TinyScreen+ display
  + telemetry to the PC app.

  BEHAVIOUR
    Drive forward. When the IR sensor says YES (something in front), mark the
    obstacle, turn a random 90-180 degrees, and drive on.

  LINES SENT TO THE PC
    Every 200 ms:      P,<time_ms>,<x_mm>,<y_mm>,<heading_deg>,<distance_mm>,<rssi or NA>,<ir 0/1>
    On each obstacle:  O,<time_ms>,<x_mm>,<y_mm>

  Robot starts at (0,0) facing +X. Heading is counter-clockwise positive.

  BUTTONS
    Upper-left:  reset position and distance to zero
    Upper-right: pause / run

  TEST MODES
    SIMULATE_MOTORS 1 + SIMULATE_IR 1 : everything virtual. The robot drives around a
                                        virtual room with boxes in it.
    SIMULATE_MOTORS 1 + SIMULATE_IR 0 : virtual wheels, REAL IR Wirelings. Put your hand
                                        in front of the sensor and the robot turns.
    SIMULATE_MOTORS 0                 : real encoders (motor driving still to be added).
*/

#include <Wire.h>
#include <SPI.h>
#include <TinyScreen.h>

// ===================================================================== options
#define SIMULATE_MOTORS 1  // 1 = virtual wheels, 0 = real encoders
#define SIMULATE_IR     0  // 1 = virtual room and obstacles, 0 = real IR Wirelings
#define USE_BLE         1  // 1 = send over the Bluetooth LE TinyShield
#define USE_WIFI        0  // 1 = send over the Wi-Fi TinyShield and measure real RSSI

#if !SIMULATE_IR
  #include <Wireling.h>
#endif

#if USE_BLE
  #include <STBLE.h>
  // UART.ino (from TinyCircuits' STBLE UART example) must be in this sketch folder.
  // These variables are used inside UART.ino:
  uint8_t ble_rx_buffer[21];
  uint8_t ble_rx_buffer_len = 0;
  uint8_t ble_connection_state = false;
#endif

#if USE_WIFI
  #include <WiFi101.h>
  #include <WiFiUdp.h>
  const char* WIFI_SSID = "your-network";
  const char* WIFI_PASS = "your-password";
  IPAddress PC_IP(192, 168, 1, 100);   // your PC's IP address (run "ipconfig" on Windows)
  const uint16_t PC_PORT = 4210;       // must match the app's UDP port
  WiFiUDP udp;
  // Note: the Wi-Fi TinyShield uses pin A3, which is also Wireling port 3. Don't use port 3.
#endif

// ===================================================================== robot geometry
const float WHEEL_DIAMETER_MM = 40.0f;   // from your tire.stl (calibrate later!)
const float WHEEL_BASE_MM     = 120.0f;  // centre of left wheel to centre of right wheel: MEASURE
const float ROBOT_RADIUS_MM   = 80.0f;   // centre of robot to its front edge (where the IR sits)
const float ENCODER_PPR       = 7.0f;    // encoder pulses per MOTOR shaft rev
const float GEAR_RATIO        = 100.0f;  // N20 gearbox ratio printed on your motor

const float COUNTS_PER_WHEEL_REV = ENCODER_PPR * GEAR_RATIO;
const float MM_PER_COUNT = PI * WHEEL_DIAMETER_MM / COUNTS_PER_WHEEL_REV;

// Driving speeds (wheel speed in mm/s)
const float DRIVE_SPEED = 150.0f;
const float TURN_SPEED  = 80.0f;

// ===================================================================== IR Wirelings
// Wireling port 0 = A0, port 1 = A1, port 2 = A2, port 3 = A3
const int IR_EMITTER_PIN  = A0;  // IR Emitter Wireling (AST1026) on port 0
const int IR_RECEIVER_PIN = A1;  // IR Receiver Wireling (AST1008) on port 1

// The receiver is a 38 kHz TV-remote receiver, so we flash the emitter at ~38 kHz
// in short bursts and check if the receiver sees the reflection.
const int IR_BURST_CYCLES   = 20;  // flashes per check
const int IR_HALF_PERIOD_US = 9;  // 12-13 = ~38 kHz = longest range.
                                   // Smaller (e.g. 9) = off-frequency = SHORTER range.
                                   // Lower this if it detects things too far away.
const int IR_CONFIRM_COUNT  = 2;   // checks in a row needed before saying YES
const float IR_ASSUMED_DIST_MM = 100.0f; // real sensor can't measure distance; assume this

// ===================================================================== encoder pins
// Set these when the motors arrive. Avoid A0-A3 (Wireling ports) and pins 2, 8.
const int ENC_L_A = -1, ENC_L_B = -1;
const int ENC_R_A = -1, ENC_R_B = -1;
const bool LEFT_REVERSED  = false;
const bool RIGHT_REVERSED = true;

// ===================================================================== timing
const unsigned long ODOM_INTERVAL_MS    = 20;
const unsigned long IR_INTERVAL_MS      = 50;
const unsigned long SEND_INTERVAL_MS    = 200;
const unsigned long DISPLAY_INTERVAL_MS = 250;
const unsigned long RSSI_INTERVAL_MS    = 500;

// ===================================================================== state
// (Box is declared up here so the Arduino IDE's auto-generated prototypes can see it)
struct Box { float x1, y1, x2, y2; };

TinyScreen display = TinyScreen(TinyScreenPlus);

volatile long encLeft = 0, encRight = 0;
long lastLeft = 0, lastRight = 0;

float posX = 0, posY = 0, heading = 0;  // mm, mm, radians
float distanceMm = 0;
int lastRssi = 0;
bool haveRssi = false;

bool irObstacle = false;     // the YES / NO answer
int irHits = 0;
float irHitDistMm = IR_ASSUMED_DIST_MM;
int obstacleCount = 0;

enum DriveMode { FORWARD, TURNING };
DriveMode driveMode = FORWARD;
float turnRemaining = 0;     // radians still to turn
int turnDir = 1;             // +1 = left (CCW), -1 = right
bool paused = false;
bool lastPauseBtn = false;

float wheelSpeedL = 0, wheelSpeedR = 0;

unsigned long lastOdom = 0, lastIr = 0, lastSend = 0, lastDisplay = 0, lastRssiRead = 0;

// ===================================================================== encoders
void isrLeft()  { bool b = digitalRead(ENC_L_B); encLeft  += (b ^ LEFT_REVERSED)  ? -1 : 1; }
void isrRight() { bool b = digitalRead(ENC_R_B); encRight += (b ^ RIGHT_REVERSED) ? -1 : 1; }

// ===================================================================== motors
void setWheelSpeeds(float left, float right) {
  wheelSpeedL = left;
  wheelSpeedR = right;
#if !SIMULATE_MOTORS
  // TODO when the motors arrive: send left/right to the motor driver here.
#endif
}

#if SIMULATE_MOTORS
float simCarryL = 0, simCarryR = 0;

// Turns the requested wheel speeds into encoder counts, like real wheels would.
void simWheels(float dt) {
  simCarryL += wheelSpeedL * dt / MM_PER_COUNT;
  simCarryR += wheelSpeedR * dt / MM_PER_COUNT;
  long cl = (long)simCarryL, cr = (long)simCarryR;
  simCarryL -= cl;
  simCarryR -= cr;
  noInterrupts();
  encLeft += cl;
  encRight += cr;
  interrupts();
}
#endif

// ===================================================================== IR sensing
#if SIMULATE_IR
// Virtual room. Robot starts at (0,0) facing +X. All sizes in mm.
const Box ROOM = { -500, -500, 3000, 2000 };   // walls
const Box OBSTACLES[] = {
  { 1000,  200, 1400,  600 },   // a box in the middle
  { 2000, -500, 2300,  800 },   // a cabinet against the bottom wall
  {  400, 1300,  900, 2000 },   // a sofa against the top wall
};
const int NUM_OBSTACLES = sizeof(OBSTACLES) / sizeof(OBSTACLES[0]);
const float IR_SIM_RANGE_MM = 120.0f;  // virtual sensor sees this far past the robot's front

bool insideBox(const Box& b, float x, float y) {
  return x >= b.x1 && x <= b.x2 && y >= b.y1 && y <= b.y2;
}

bool readIr() {
  float c = cos(heading), s = sin(heading);
  for (float d = 0; d <= IR_SIM_RANGE_MM; d += 10) {
    float px = posX + (ROBOT_RADIUS_MM + d) * c;
    float py = posY + (ROBOT_RADIUS_MM + d) * s;
    bool hit = !insideBox(ROOM, px, py);
    for (int i = 0; i < NUM_OBSTACLES && !hit; i++) hit = insideBox(OBSTACLES[i], px, py);
    if (hit) { irHitDistMm = d; return true; }
  }
  return false;
}
#else
bool readIr() {
  bool seen = false;
  for (int i = 0; i < IR_BURST_CYCLES; i++) {
    digitalWrite(IR_EMITTER_PIN, HIGH);
    delayMicroseconds(IR_HALF_PERIOD_US);
    digitalWrite(IR_EMITTER_PIN, LOW);
    delayMicroseconds(IR_HALF_PERIOD_US);
    // the receiver needs ~10 flashes before it reacts; its output goes LOW when it sees IR
    if (i >= 8 && digitalRead(IR_RECEIVER_PIN) == LOW) seen = true;
  }
  delayMicroseconds(150);
  if (digitalRead(IR_RECEIVER_PIN) == LOW) seen = true;
  irHitDistMm = IR_ASSUMED_DIST_MM;
  return seen;
}
#endif

void updateIr() {
  // Require a few YES readings in a row so one stray flash doesn't count
  if (readIr()) { if (irHits < IR_CONFIRM_COUNT) irHits++; }
  else irHits = 0;
  irObstacle = (irHits >= IR_CONFIRM_COUNT);
}

// ===================================================================== behaviour
void reportObstacle() {
  float d = ROBOT_RADIUS_MM + irHitDistMm;
  float ox = posX + d * cos(heading);
  float oy = posY + d * sin(heading);
  obstacleCount++;

  char line[48];
  snprintf(line, sizeof(line), "O,%lu,%ld,%ld\n", millis(), lround(ox), lround(oy));
  sendLine(line);
}

void updateBehaviour() {
  if (paused) { setWheelSpeeds(0, 0); return; }

  if (driveMode == FORWARD) {
    if (irObstacle) {
      reportObstacle();
      turnRemaining = random(90, 181) * PI / 180.0f;  // random 90-180 degrees
      turnDir = random(2) ? 1 : -1;
      driveMode = TURNING;
    } else {
      setWheelSpeeds(DRIVE_SPEED, DRIVE_SPEED);
    }
  }

  if (driveMode == TURNING) {
    if (turnRemaining <= 0) {
      driveMode = FORWARD;
      setWheelSpeeds(0, 0);
      irHits = 0;  // take a fresh look before deciding again
      irObstacle = false;
    } else {
      setWheelSpeeds(-turnDir * TURN_SPEED, turnDir * TURN_SPEED);
    }
  }
}

// ===================================================================== odometry
void updateOdometry() {
  noInterrupts();
  long l = encLeft, r = encRight;
  interrupts();

  float dL = (l - lastLeft) * MM_PER_COUNT;
  float dR = (r - lastRight) * MM_PER_COUNT;
  lastLeft = l;
  lastRight = r;

  float dCenter = (dL + dR) / 2.0f;
  float dTheta  = (dR - dL) / WHEEL_BASE_MM;

  posX += dCenter * cos(heading + dTheta / 2.0f);
  posY += dCenter * sin(heading + dTheta / 2.0f);
  heading += dTheta;
  while (heading > PI)   heading -= 2 * PI;
  while (heading <= -PI) heading += 2 * PI;

  distanceMm += fabs(dCenter);
  if (driveMode == TURNING) turnRemaining -= fabs(dTheta);
}

void resetOdometry() {
  noInterrupts();
  encLeft = encRight = 0;
  interrupts();
  lastLeft = lastRight = 0;
  posX = posY = heading = distanceMm = 0;
  obstacleCount = 0;
  driveMode = FORWARD;
}

// ===================================================================== output
#if USE_BLE
void bleSend(const char* s) {
  size_t n = strlen(s);
  for (size_t i = 0; i < n; i += 20) {  // BLE packets carry 20 bytes max
    size_t len = n - i;
    if (len > 20) len = 20;
    Write_UART_TX((char*)(s + i), (uint8_t)len);
  }
}
#endif

void sendLine(const char* line) {
  SerialUSB.print(line);
#if USE_BLE
  bleSend(line);
#endif
#if USE_WIFI
  if (WiFi.status() == WL_CONNECTED) {
    udp.beginPacket(PC_IP, PC_PORT);
    udp.write((const uint8_t*)line, strlen(line));
    udp.endPacket();
  }
#endif
}

void sendTelemetry() {
  char line[72];
  long hdg = lround(heading * 180.0f / PI);
  if (haveRssi) {
    snprintf(line, sizeof(line), "P,%lu,%ld,%ld,%ld,%ld,%d,%d\n",
             millis(), lround(posX), lround(posY), hdg, lround(distanceMm), lastRssi, irObstacle ? 1 : 0);
  } else {
    snprintf(line, sizeof(line), "P,%lu,%ld,%ld,%ld,%ld,NA,%d\n",
             millis(), lround(posX), lround(posY), hdg, lround(distanceMm), irObstacle ? 1 : 0);
  }
  sendLine(line);
}

void printRow(int y, const char* text, uint8_t color) {
  char padded[20];
  snprintf(padded, sizeof(padded), "%-18s", text);  // pad with spaces to overwrite old text
  display.fontColor(color, TS_8b_Black);
  display.setCursor(0, y);
  display.print(padded);
}

void updateDisplay() {
  char buf[24];
  long cm = lround(distanceMm / 10.0f);
  long hdg = lround(heading * 180.0f / PI);

  snprintf(buf, sizeof(buf), "Dist %ld.%02ld m", cm / 100, cm % 100);
  printRow(0, buf, TS_8b_White);

  snprintf(buf, sizeof(buf), "X %ld Y %ld", lround(posX), lround(posY));
  printRow(12, buf, TS_8b_White);

  if (haveRssi) snprintf(buf, sizeof(buf), "Hdg %ld  %d dBm", hdg, lastRssi);
  else          snprintf(buf, sizeof(buf), "Hdg %ld  no wifi", hdg);
  printRow(24, buf, TS_8b_White);

  if (irObstacle) printRow(36, "IR: YES obstacle", TS_8b_Red);
  else            printRow(36, "IR: no", TS_8b_Green);

  if (paused) {
    printRow(50, "PAUSED", TS_8b_Yellow);
  } else {
    snprintf(buf, sizeof(buf), "%s %s %s  #%d",
             SIMULATE_MOTORS ? "vWheel" : "ENC",
             SIMULATE_IR ? "vIR" : "IR",
             USE_WIFI ? "WiFi" : (USE_BLE ? "BLE" : "USB"),
             obstacleCount);
    printRow(50, buf, TS_8b_White);
  }
}

// ===================================================================== setup / loop
void setup() {
  SerialUSB.begin(115200);
  randomSeed(analogRead(A2) ^ micros());

  display.begin();
  display.setBrightness(10);
  display.setFont(thinPixel7_10ptFontInfo);
  display.clearScreen();

#if !SIMULATE_IR
  Wireling.begin();  // powers the Wireling ports
  pinMode(IR_EMITTER_PIN, OUTPUT);
  digitalWrite(IR_EMITTER_PIN, LOW);
  pinMode(IR_RECEIVER_PIN, INPUT_PULLUP);
#endif

#if !SIMULATE_MOTORS
  if (ENC_L_A < 0 || ENC_L_B < 0 || ENC_R_A < 0 || ENC_R_B < 0) {
    printRow(0, "Set encoder pins!", TS_8b_Red);
    while (true) {}
  }
  pinMode(ENC_L_A, INPUT_PULLUP);
  pinMode(ENC_L_B, INPUT_PULLUP);
  pinMode(ENC_R_A, INPUT_PULLUP);
  pinMode(ENC_R_B, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENC_L_A), isrLeft, RISING);
  attachInterrupt(digitalPinToInterrupt(ENC_R_A), isrRight, RISING);
#endif

#if USE_BLE
  BLEsetup();
#endif

#if USE_WIFI
  WiFi.setPins(8, 2, A3, -1);  // pin setup for the TinyCircuits Wi-Fi TinyShield
  printRow(0, "WiFi connecting", TS_8b_White);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  udp.begin(PC_PORT + 1);
  display.clearScreen();
#endif

  lastOdom = millis();
}

void loop() {
  unsigned long now = millis();

#if USE_BLE
  aci_loop();  // keeps the Bluetooth connection alive
#endif

  if (display.getButtons(TSButtonUpperLeft)) resetOdometry();

  bool pauseBtn = display.getButtons(TSButtonUpperRight);
  if (pauseBtn && !lastPauseBtn) paused = !paused;
  lastPauseBtn = pauseBtn;

  if (now - lastIr >= IR_INTERVAL_MS) {
    lastIr = now;
    updateIr();
    updateBehaviour();
  }

  if (now - lastOdom >= ODOM_INTERVAL_MS) {
#if SIMULATE_MOTORS
    simWheels((now - lastOdom) / 1000.0f);
#endif
    lastOdom = now;
    updateOdometry();
  }

  if (now - lastRssiRead >= RSSI_INTERVAL_MS) {
    lastRssiRead = now;
#if USE_WIFI
    haveRssi = (WiFi.status() == WL_CONNECTED);
    if (haveRssi) lastRssi = WiFi.RSSI();
#elif SIMULATE_MOTORS
    // fake signal from an imaginary router at (2.5 m, 1.5 m)
    float dx = posX - 2500.0f, dy = posY - 1500.0f;
    float dm = sqrt(dx * dx + dy * dy) / 1000.0f;
    if (dm < 0.3f) dm = 0.3f;
    lastRssi = (int)lround(-38.0f - 27.0f * log10(dm) + random(-20, 21) / 10.0f);
    haveRssi = true;
#endif
  }

  if (now - lastSend >= SEND_INTERVAL_MS) {
    lastSend = now;
    sendTelemetry();
  }

  if (now - lastDisplay >= DISPLAY_INTERVAL_MS) {
    lastDisplay = now;
    updateDisplay();
  }
}
