/*
  RobotReference.ino
  LEGACY REFERENCE: virtual wheels only; telemetry does not represent real motion.
  Separate Wi-Fi sketch, not the main SurveyFirmware and not a BLE motor link.
  Wi-Fi survey robot, controlled from the RoombaMapper app.

  Hardware: TinyScreen+  +  Wi-Fi TinyShield (ATWINC1500)  +  Wireling Adapter
            IR Emitter Wireling on port 0, IR Receiver Wireling on port 1

  WHAT IT DOES
    Tracks its position from the wheels, measures Wi-Fi signal strength, detects
    obstacles with the IR sensor, and sends it all to the app over Wi-Fi.
    It starts PAUSED and waits for commands from the app.

  STATES (chosen from the app)
    PAUSED  : standing still. Still measures Wi-Fi (works like a handheld meter).
    SURVEY  : drives on its own. IR YES -> mark obstacle, turn 90-180 deg, carry on.
    GOTO    : drives to a point you clicked on the map, going around obstacles, then pauses.
    HOME    : same as GOTO, to the starting point, then turns to face the starting direction.
    STEP    : one small manual move (forward/back 20 cm, turn 45 deg), then pauses.
    TRACE   : follows an edge with the wall/object on its RIGHT, using only the front
              sensor ("probing", see below), until it gets back to where it started.
              TRACE,ROOM first drives straight to the nearest wall. TRACE,OBJ starts
              from where it is (used after it gets blocked by an unknown object).

  IR SENSOR SETUP (chosen in the app, or DEFAULT_SIDE_SENSOR below)
    Front only  : SIMPLE tracing (default): drive straight until the front sensor sees
                  something, turn left 90 deg, repeat. Good for plain rooms (rectangles).
                  It doesn't notice where a wall ends (doorways, a cabinet sticking out),
                  so turn on PROBING in the app for rooms like that.
    Front+right : the wall is kept on the right and watched continuously by the right
                  sensor: fast and smooth, and follows every corner.
    Front+left  : same, with the wall on the left.

  PROBING (edge following with one front sensor, optional)
    Drive PROBE_STEP_MM -> turn right 90 deg to look at the wall:
      wall there -> record an edge point, turn back, repeat.
      no wall    -> outside corner: turn back, drive past the corner, turn right.
    Front sensor YES while driving -> inside corner: record it, turn left 90 deg.
    Back near the starting point after turning ~360 deg -> outline finished.
    Overall left turns = it went round the inside of a room. Right turns = an object.

  MAPPING MISSIONS (phases 1-3) are run by the app. The robot just follows its
  commands: TRACE, GOTO with "stop if blocked", MEASURE, HOME.
    Any state can be interrupted by "measure here" (stop, average 20 readings, continue).

  WI-FI MODES
    FAST : a reading every 0.3 s while driving.
    SLOW : in SURVEY, stops every SLOW_STOP_SPACING_MM to average 20 readings.

  FILES  (keep all three in the TinyScreenRoomba sketch folder)
    RobotReference.ino  robot: driving, sensors, mapping, screen, readings cache,
                          talking to the app
    wifi_logic.h / .cpp   Wi-Fi connect, signal readings and AP roaming (shared with
                          the stand-alone Wi-Fi module). The Wi-Fi network name,
                          password and roaming defaults are at the top of wifi_logic.cpp.

  AP ROAMING  (done by wifi_logic.cpp)
    Below roamTriggerDbm it scans for other APs with the SAME network name and moves
    to one at least roamMarginDb stronger. The robot stops while scanning (~2 s),
    because it can't watch the IR sensor during a scan. Scans happen every
    roamCheckMs when paused and every ROAM_CHECK_MOVING_MS while surveying, never
    while measuring, tracing, going to a point or stepping. "Find better AP" forces one.

  READINGS CACHE  (same idea as the stand-alone Wi-Fi module)
    Every Wi-Fi reading (FAST) or finished average (SLOW) goes into a cache of
    CACHE_SIZE readings, together with where the robot was when it was taken.
    They're sent to the app oldest-first, one at a time, and each stays in the cache
    until the app acknowledges it, so none are lost if Wi-Fi drops for a while.
    If the cache fills up, the oldest reading is overwritten.
      robot -> app:  R,seq,mode,rssi,min,max,age,x_mm,y_mm
      app -> robot:  A,seq

  SETTINGS FROM THE APP  (same keys as the stand-alone Wi-Fi module)
    C,<id>,SET,<key>,<value>   keys: ssid, pass, fast_ms, slow_ms, slow_n, roam_dbm,
    roam_margin, roam_check_ms, roam_cool_ms, ack_ms, display (0/1), mode (F/S).
    A new ssid or pass makes the robot reconnect. The password is never echoed back.

  COMMANDS FROM THE APP (UDP port 4211, or USB serial), each acknowledged with K,<id>,<text>
    C,<id>,RUN | PAUSE | HOME | GOTO,<x_mm>,<y_mm>[,S] | STEP,FWD|BACK|LEFT|RIGHT
    C,<id>,MEASURE | MODE,FAST|SLOW | RESET | SPEED,<percent> | ROAM
    C,<id>,TRACE,ROOM|OBJ | AUTOROAM,ON|OFF | SENSORS,F|FL|FR | PROBE,ON|OFF | HELLO
    C,<id>,SET,<key>,<value>   (see SETTINGS)
    (GOTO ...,S = stop and report BLOCKED if something is in the way, instead of going round it)

  LINES SENT TO THE APP (UDP broadcast to port 4210, and USB serial)
    P,<t_ms>,<x_mm>,<y_mm>,<heading_deg>,<dist_mm>,NA,<ir 0/1>,<state>,<mode>,<speed%>,<AP>,
      <sensors F|FL|FR|F+P>,<left ir 0/1>,<right ir 0/1>
    O,<t_ms>,<x_mm>,<y_mm>[,<shape>,<facing_deg>]  obstacle / edge point (shape = which traced
                                 outline, facing = direction the sensor was pointing)
    S,<t_ms>,<shape>,<kind>      outline finished: 1 = object, 2 = room walls, 0 = incomplete
    M,<text>                     message to show in the app
    K,<id>,<text>                command acknowledged
    R,...                        Wi-Fi reading (see READINGS CACHE)

  SCREEN BUTTONS (still work if you're holding the robot)
    Upper-left: reset position · Upper-right: pause/run survey
    Lower-left: FAST/SLOW · Lower-right: measure here
*/

#include <Wire.h>
#include <SPI.h>
#include <TinyScreen.h>

// ===================================================================== options
#define SIMULATE_MOTORS 1  // Reference sketch: virtual wheels only.
#if SIMULATE_MOTORS != 1
#error RobotReference supports simulated motors only
#endif
#define SIMULATE_IR     0  // 1 = virtual room and obstacles, 0 = real IR Wirelings
#define USE_WIFI        1  // must stay 1: wifi_logic.cpp is always part of the sketch

#if !SIMULATE_IR
  #include <Wireling.h>
#endif

#if USE_WIFI
  #include <WiFi101.h>
  #include <WiFiUdp.h>
  #include "wifi_logic.h"   // connect, readings, roaming. Network name + password: wifi_logic.cpp

  const uint16_t PC_PORT    = 4210;  // app listens here (must match the app's UDP port)
  const uint16_t ROBOT_PORT = 4211;  // robot listens here for commands
  // Leave as 0.0.0.0 to broadcast to every PC on the network (no IP needed).
  // Or put your PC's IP here (run "ipconfig" on Windows) if broadcast doesn't arrive.
  IPAddress PC_IP(0, 0, 0, 0);
  IPAddress sendTo;
  IPAddress learnedPc(0, 0, 0, 0);   // the app's address, learned from its first command
  WiFiUDP udp;
#endif

// ===================================================================== Wi-Fi meter settings
// (defaults; the app can change them with SET, see SETTINGS above)
unsigned long fastIntervalMs = 300;    // key: fast_ms  FAST: time between readings
unsigned long slowIntervalMs = 150;    // key: slow_ms  averaging: time between samples
int           slowSamples    = 20;     // key: slow_n   averaging: samples per average
const float SLOW_STOP_SPACING_MM = 300.0f;  // SLOW survey: distance between measuring stops

// ===================================================================== readings cache
const int     CACHE_SIZE   = 300;      // readings kept until the app confirms them
unsigned long ackTimeoutMs = 500;      // key: ack_ms  resend if no confirmation in this time

// ===================================================================== roaming settings
// The other roaming settings (roam_dbm, roam_margin, roam_check_ms, roam_cool_ms)
// are at the top of wifi_logic.cpp.
const unsigned long ROAM_CHECK_MOVING_MS = 15000;  // gap between scans while SURVEYING (robot stops to scan)

// ===================================================================== robot geometry
const float WHEEL_DIAMETER_MM = 40.0f;   // from your tire.stl (calibrate later!)
const float WHEEL_BASE_MM     = 120.0f;  // centre of left wheel to centre of right wheel: MEASURE
const float ROBOT_RADIUS_MM   = 80.0f;   // centre of robot to its front edge (where the IR sits)
const float ENCODER_PPR       = 7.0f;    // encoder pulses per MOTOR shaft rev
const float GEAR_RATIO        = 100.0f;  // N20 gearbox ratio printed on your motor

const float COUNTS_PER_WHEEL_REV = ENCODER_PPR * GEAR_RATIO;
const float MM_PER_COUNT = PI * WHEEL_DIAMETER_MM / COUNTS_PER_WHEEL_REV;

const float DRIVE_SPEED = 150.0f;  // wheel speed at 100%, mm/s
const float TURN_SPEED  = 80.0f;

// ===================================================================== edge tracing settings
const float PROBE_STEP_MM    = 150.0f;     // drive this far between looks at the wall
const float PROBE_ANGLE      = PI / 2.0f;  // turn this far right to look at the wall
const unsigned long PROBE_LOOK_MS = 200;   // how long to look (IR needs 2 readings)
const float CORNER_CLEAR_MM  = 140.0f;     // drive past an outside corner before turning round it
const float TRACE_CLOSE_MM   = 250.0f;     // this close to the start = loop finished
const float TRACE_MIN_MM     = 1000.0f;    // must drive this far before it can finish
const float TRACE_MAX_MM     = 40000.0f;   // give up after this much driving
const float TRACE_MIN_TURN   = 4.0f;       // radians (~230 deg) turned before it can finish
const int   TRACE_MAX_MISSES = 4;          // outside-corner turns in a row with no wall = lost

// ===================================================================== navigation settings
const float STEP_DRIVE_MM     = 200.0f;          // manual step forward/back
const float STEP_TURN_RAD     = PI / 4.0f;       // manual step turn (45 deg)
const float GOAL_TOLERANCE_MM = 60.0f;           // "arrived" when this close
const float AVOID_DRIVE_MM    = 250.0f;          // sidestep distance when blocked on the way

// ===================================================================== IR Wirelings
// Wireling port 0 = A0, port 1 = A1, port 2 = A2. Port 3 (A3) is used by the Wi-Fi shield.
const int IR_EMITTER_PIN  = A0;      // front emitter (a side emitter shares this pin: spliced cable)
const int IR_RECEIVER_PIN = A1;      // front receiver
const int IR_SIDE_RECEIVER_PIN = A2; // left OR right receiver (only one side: port 3 is the Wi-Fi shield)

// Which sensors are fitted at start-up (the app can change it):
//   0 = front only, +1 = front + left, -1 = front + right
const int  DEFAULT_SIDE_SENSOR = 0;
const bool DEFAULT_PROBING     = false;  // front only: look sideways for the wall while tracing

const int IR_BURST_CYCLES   = 20;
const int IR_HALF_PERIOD_US = 9;   // 12-13 = ~38 kHz = longest range. Smaller = shorter range.
const int IR_CONFIRM_COUNT  = 2;
const float IR_ASSUMED_DIST_MM = 100.0f;

// ===================================================================== encoder pins
// Set these when the motors arrive. Avoid A0-A3 and pins 2, 8 (Wi-Fi shield).
const int ENC_L_A = -1, ENC_L_B = -1;
const int ENC_R_A = -1, ENC_R_B = -1;
const bool LEFT_REVERSED  = false;
const bool RIGHT_REVERSED = true;

// ===================================================================== timing
const unsigned long ODOM_INTERVAL_MS    = 20;
const unsigned long IR_INTERVAL_MS      = 50;
const unsigned long SEND_INTERVAL_MS    = 200;
const unsigned long DISPLAY_INTERVAL_MS = 250;

// ===================================================================== types / state
// (declared before any function so the Arduino IDE's auto-generated prototypes work)
struct Box { float x1, y1, x2, y2; };
enum RobotState { ST_PAUSED, ST_SURVEY, ST_GOTO, ST_STEP, ST_TRACE };
enum TraceStep  { T_APPROACH, T_DRIVE, T_PROBE_TURN, T_PROBE_LOOK, T_PROBE_BACK,
                  T_CORNER_BACK, T_CORNER_CLEAR, T_CORNER_TURN, T_INSIDE_TURN, T_FOLLOW };
enum Motion     { MOVE_NONE, MOVE_TURN, MOVE_DRIVE };
struct Reading {
  uint16_t seq;        // reading number, matches the app's acknowledgement
  uint32_t takenMs;    // millis() when it was taken
  int16_t  x, y;       // where the robot was (mm)
  int8_t   rssi;       // dBm (the average in SLOW)
  int8_t   minRssi;    // lowest sample  (= rssi in FAST)
  int8_t   maxRssi;    // highest sample (= rssi in FAST)
  char     mode;       // 'F' or 'S'
};

TinyScreen display = TinyScreen(TinyScreenPlus);

volatile long encLeft = 0, encRight = 0;
long lastLeft = 0, lastRight = 0;

float posX = 0, posY = 0, heading = 0;  // mm, mm, radians
float distanceMm = 0;

// IR
bool irObstacle = false;
int irHits = 0;
float irHitDistMm = IR_ASSUMED_DIST_MM;
int  sideSensor = DEFAULT_SIDE_SENSOR;  // 0 none, +1 left, -1 right
bool probeMode = DEFAULT_PROBING;
bool traceProbe = false;                // this trace uses probing (set when a trace starts)
bool irSide = false;                    // side sensor sees something
int  irSideHits = 0;
float irSideDistMm = IR_ASSUMED_DIST_MM;
int obstacleCount = 0;

// Robot state
RobotState state = ST_PAUSED;
int speedPct = 100;
float wheelSpeedL = 0, wheelSpeedR = 0;

// Current single movement (a turn by some angle, or a drive by some distance)
Motion motion = MOVE_NONE;
float motionLeft = 0;      // radians or mm still to go
int   motionDir = 1;       // turn: +1 left, -1 right. drive: +1 forward, -1 back

// GOTO / HOME
float goalX = 0, goalY = 0;
bool  goalIsHome = false;
bool  goalStopOnBlock = false;   // GOTO,...,S: stop and report instead of going round
bool  blockedFlag = false;       // shown as state BLOCKED until the next command
bool  sidestepNext = false;
bool  motionBlocked = false;     // last forward movement was stopped by the IR sensor

// Edge tracing
TraceStep traceStep = T_APPROACH;
int   shapeId = 0;               // id of the outline being traced (set randomly at start-up)
float traceStartX = 0, traceStartY = 0;
float traceDist = 0, traceTurn = 0;
int   traceMisses = 0;
float traceLostDist = 0, traceLostTurn = 0;  // side-sensor tracing: how long since we saw the wall
float lastPointX = 0, lastPointY = 0;
unsigned long probeStartMs = 0;
bool  autoRoam = true;           // the app turns this off during mapping missions

// Wi-Fi meter
bool slowMode = false;
bool measuring = false;                // stopped to take an average
float distSinceStop = SLOW_STOP_SPACING_MM;
int  shownRssi = 0;
const char* shownExtra = nullptr;
char extraBuf[24];
long sampleSum = 0;
int  sampleCount = 0;
int  sampleMin = 0, sampleMax = -127;
bool bigDirty = true;

// Roaming (currentBssid / currentChannel come from wifi_logic)
int  currentRssi = 0;           // latest raw reading (used to decide when to roam)
bool roamRequested = false;     // set by the app's "Find better AP" button
bool roamForced = false;        // the scan in progress was asked for by the app
unsigned long lastMovingScanMs = 0;
bool wifiConfigChanged = false; // new ssid/pass from the app: reconnect
bool displayOn = true;          // key: display

// Readings cache (ring buffer, overwrites the oldest when full)
Reading  cache[CACHE_SIZE];
int      cacheHead = 0, cacheCount = 0;
uint16_t nextSeq = 0;
uint32_t overwrittenCount = 0;
bool     awaitingAck = false;
uint16_t sentSeq = 0;
unsigned long sentAtMs = 0;

// Commands
long lastCmdId = -1;
char lastAck[40] = "";
char serialBuf[96];
int  serialLen = 0;

unsigned long lastOdom = 0, lastIr = 0, lastSend = 0, lastDisplay = 0, lastReadMs = 0;

// ===================================================================== small helpers
float wrapAngle(float a) {
  while (a > PI)   a -= 2 * PI;
  while (a <= -PI) a += 2 * PI;
  return a;
}

const char* stateName() {
  switch (state) {
    case ST_SURVEY: return "SURVEY";
    case ST_GOTO:   return goalIsHome ? "HOME" : "GOTO";
    case ST_STEP:   return "STEP";
    case ST_TRACE:  return "TRACE";
    default:        return blockedFlag ? "BLOCKED" : "PAUSED";
  }
}

// ===================================================================== encoders
void isrLeft()  { bool b = digitalRead(ENC_L_B); encLeft  += (b ^ LEFT_REVERSED)  ? -1 : 1; }
void isrRight() { bool b = digitalRead(ENC_R_B); encRight += (b ^ RIGHT_REVERSED) ? -1 : 1; }

// ===================================================================== motors
void setWheelSpeeds(float left, float right) {
  float k = speedPct / 100.0f;
  wheelSpeedL = left * k;
  wheelSpeedR = right * k;
#if !SIMULATE_MOTORS
  // TODO when the motors arrive: send wheelSpeedL / wheelSpeedR to the motor driver here.
#endif
}

void stopWheels() { setWheelSpeeds(0, 0); }

// Drive forward while curving. turnSign +1 = curve left, -1 = curve right.
// ratio = inner wheel speed as a fraction of the outer (1 = straight).
void steer(int turnSign, float ratio) {
  if (turnSign > 0) setWheelSpeeds(DRIVE_SPEED * ratio, DRIVE_SPEED);
  else              setWheelSpeeds(DRIVE_SPEED, DRIVE_SPEED * ratio);
}

#if SIMULATE_MOTORS
float simCarryL = 0, simCarryR = 0;

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
// Virtual room (mm), shaped like your drawing: a 2.1 x 2.4 m room with a 0.7 m wide
// alcove going up at the top right. The robot starts at (0,0) in the bottom-left
// corner area, facing right (+X). Corners go round the room in order.
const float ROOM_POLY[][2] = {
  { -300,  -300 },   // bottom-left
  { 1760,  -300 },   // bottom-right
  { 1760,  2750 },   // top of the alcove, right
  { 1080,  2750 },   // top of the alcove, left
  { 1080,  2080 },   // the step (an outside corner, seen from inside the room)
  { -300,  2080 },   // top-left
};
const int ROOM_CORNERS = sizeof(ROOM_POLY) / sizeof(ROOM_POLY[0]);

// Objects in the room, for phase 2 (rectangles: x1, y1, x2, y2)
const Box OBSTACLES[] = {
  { 600, 900, 1000, 1250 },     // a box in the middle
};
const int NUM_OBSTACLES = sizeof(OBSTACLES) / sizeof(OBSTACLES[0]);

bool insideRoom(float x, float y) {
  bool inside = false;
  for (int i = 0, j = ROOM_CORNERS - 1; i < ROOM_CORNERS; j = i++) {
    float xi = ROOM_POLY[i][0], yi = ROOM_POLY[i][1];
    float xj = ROOM_POLY[j][0], yj = ROOM_POLY[j][1];
    if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) inside = !inside;
  }
  return inside;
}
const float IR_SIM_RANGE_MM = 120.0f;

bool insideBox(const Box& b, float x, float y) {
  return x >= b.x1 && x <= b.x2 && y >= b.y1 && y <= b.y2;
}

// Looks along (heading + angle). Returns true if something is within range.
bool readIrAt(float angle, int receiverPin, float& distOut) {
  (void)receiverPin;
  float c = cos(heading + angle), s = sin(heading + angle);
  for (float d = 0; d <= IR_SIM_RANGE_MM; d += 10) {
    float px = posX + (ROBOT_RADIUS_MM + d) * c;
    float py = posY + (ROBOT_RADIUS_MM + d) * s;
    bool hit = !insideRoom(px, py);
    for (int i = 0; i < NUM_OBSTACLES && !hit; i++) hit = insideBox(OBSTACLES[i], px, py);
    if (hit) { distOut = d; return true; }
  }
  return false;
}
#else
bool readIrAt(float angle, int receiverPin, float& distOut) {
  (void)angle;
  bool seen = false;
  for (int i = 0; i < IR_BURST_CYCLES; i++) {
    digitalWrite(IR_EMITTER_PIN, HIGH);
    delayMicroseconds(IR_HALF_PERIOD_US);
    digitalWrite(IR_EMITTER_PIN, LOW);
    delayMicroseconds(IR_HALF_PERIOD_US);
    if (i >= 8 && digitalRead(receiverPin) == LOW) seen = true;
  }
  delayMicroseconds(150);
  if (digitalRead(receiverPin) == LOW) seen = true;
  distOut = IR_ASSUMED_DIST_MM;
  return seen;
}
#endif

void updateIr() {
  // Front
  if (readIrAt(0, IR_RECEIVER_PIN, irHitDistMm)) { if (irHits < IR_CONFIRM_COUNT) irHits++; }
  else irHits = 0;
  irObstacle = (irHits >= IR_CONFIRM_COUNT);

  // Side (if fitted). Checked separately so the two don't confuse each other.
  if (sideSensor != 0) {
#if !SIMULATE_IR
    delay(1);   // let the receivers settle
#endif
    if (readIrAt(sideSensor * PI / 2.0f, IR_SIDE_RECEIVER_PIN, irSideDistMm)) {
      if (irSideHits < IR_CONFIRM_COUNT) irSideHits++;
    } else irSideHits = 0;
    irSide = (irSideHits >= IR_CONFIRM_COUNT);
  } else {
    irSide = false;
    irSideHits = 0;
  }
}

void clearIr() { irHits = 0; irObstacle = false; irSideHits = 0; irSide = false; }

// ===================================================================== sending
void sendLine(const char* line) {
  SerialUSB.print(line);
#if USE_WIFI
  if (WiFi.status() == WL_CONNECTED) {
    udp.beginPacket(sendTo, PC_PORT);
    udp.write((const uint8_t*)line, strlen(line));
    udp.endPacket();
  }
#endif
}

void sendMessage(const char* text) {
  char line[64];
  snprintf(line, sizeof(line), "M,%s\n", text);
  sendLine(line);
}

void sendTelemetry() {
  char line[128];
  const char* rssi = "NA";   // readings are sent separately (R lines) so none get lost

  const char* sensors = sideSensor > 0 ? "FL" : sideSensor < 0 ? "FR" : (probeMode ? "F+P" : "F");
  snprintf(line, sizeof(line), "P,%lu,%ld,%ld,%ld,%ld,%s,%d,%s,%s,%d,%02X:%02X ch%d,%s,%d,%d\n",
           millis(), lround(posX), lround(posY), lround(heading * 180.0f / PI),
           lround(distanceMm), rssi, irObstacle ? 1 : 0,
           measuring ? "MEASURE" : stateName(), slowMode ? "SLOW" : "FAST", speedPct,
           currentBssid[4], currentBssid[5], currentChannel,
           sensors, (sideSensor > 0 && irSide) ? 1 : 0, (sideSensor < 0 && irSide) ? 1 : 0);
  sendLine(line);
}

void reportObstacle() {
  float d = ROBOT_RADIUS_MM + irHitDistMm;
  float ox = posX + d * cos(heading);
  float oy = posY + d * sin(heading);
  obstacleCount++;

  char line[56];
  snprintf(line, sizeof(line), "O,%lu,%ld,%ld,0,%ld\n", millis(), lround(ox), lround(oy),
           lround(heading * 180.0f / PI));   // shape 0 = single hit, then the facing direction
  sendLine(line);
}

// ===================================================================== movements
void startMotion(Motion m, float amount, int dir) {
  motion = m;
  motionLeft = amount;
  motionDir = dir;
}

// Runs the current movement. Returns true while it's still going.
bool runMotion() {
  if (motion == MOVE_NONE) return false;
  if (motionLeft <= 0) {
    if (motion == MOVE_TURN) clearIr();  // take a fresh look after turning
    motion = MOVE_NONE;
    stopWheels();
    return false;
  }
  if (motion == MOVE_TURN) {
    setWheelSpeeds(-motionDir * TURN_SPEED, motionDir * TURN_SPEED);
  } else {
    if (motionDir > 0 && irObstacle) {   // never drive forward into something
      if (state != ST_TRACE) reportObstacle();   // tracing records its own points
      motionBlocked = true;
      motion = MOVE_NONE;
      stopWheels();
      return false;
    }
    setWheelSpeeds(motionDir * DRIVE_SPEED, motionDir * DRIVE_SPEED);
  }
  return true;
}

// ===================================================================== states
void enterState(RobotState s) {
  state = s;
  motion = MOVE_NONE;
  sidestepNext = false;
  stopWheels();
}

void updateSurvey() {
  if (runMotion()) return;

  if (irObstacle) {
    reportObstacle();
    startMotion(MOVE_TURN, random(90, 181) * PI / 180.0f, random(2) ? 1 : -1);
  } else if (slowMode && distSinceStop >= SLOW_STOP_SPACING_MM) {
    measuring = true;   // SLOW mode: stop here and average
    resetAverage();
    stopWheels();
  } else {
    setWheelSpeeds(DRIVE_SPEED, DRIVE_SPEED);
  }
}

void updateGoto() {
  if (runMotion()) return;

  if (sidestepNext) {           // we just turned away from an obstacle: now drive sideways
    sidestepNext = false;
    startMotion(MOVE_DRIVE, AVOID_DRIVE_MM, 1);
    return;
  }

  float dx = goalX - posX, dy = goalY - posY;
  float dist = sqrt(dx * dx + dy * dy);

  if (dist < GOAL_TOLERANCE_MM) {
    if (goalIsHome) {
      float err = wrapAngle(0 - heading);           // face the starting direction
      if (fabs(err) > 0.05f) {
        int d = err > 0 ? 1 : -1;
        setWheelSpeeds(-d * TURN_SPEED * 0.4f, d * TURN_SPEED * 0.4f);
        return;
      }
    }
    sendMessage(goalIsHome ? "Back at the start" : "Arrived");
    enterState(ST_PAUSED);
    return;
  }

  float err = wrapAngle(atan2(dy, dx) - heading);
  if (fabs(err) > 0.2f) {                            // face the goal first
    int d = err > 0 ? 1 : -1;
    float k = fabs(err) > 0.6f ? 1.0f : 0.5f;
    setWheelSpeeds(-d * TURN_SPEED * k, d * TURN_SPEED * k);
    return;
  }

  if (irObstacle) {
    reportObstacle();
    if (goalStopOnBlock) {                           // mapping mission: stop and let the app decide
      enterState(ST_PAUSED);
      blockedFlag = true;
      sendMessage("Blocked");
      return;
    }
    sidestepNext = true;                             // otherwise turn 90 deg and sidestep
    startMotion(MOVE_TURN, PI / 2.0f, random(2) ? 1 : -1);
    return;
  }

  // Drive towards the goal, steering gently and slowing down near the end
  float v = min(DRIVE_SPEED, 40.0f + dist * 0.8f);
  float s = constrain(1.5f * err, -0.5f, 0.5f);
  setWheelSpeeds(v * (1 - s), v * (1 + s));
}

// ===================================================================== edge tracing
// Which side the wall is kept on while tracing: +1 left, -1 right
int wallSide() { return sideSensor != 0 ? sideSensor : -1; }

void reportSidePoint() {        // the edge the side sensor sees
  float a = heading + sideSensor * PI / 2.0f;
  float d = ROBOT_RADIUS_MM + irSideDistMm;
  char line[64];
  snprintf(line, sizeof(line), "O,%lu,%ld,%ld,%d,%ld\n", millis(),
           lround(posX + d * cos(a)), lround(posY + d * sin(a)), shapeId,
           lround(wrapAngle(a) * 180.0f / PI));   // direction the sensor faced
  sendLine(line);
}

void reportTracePoint() {       // the edge the sensor is looking at right now
  float d = ROBOT_RADIUS_MM + irHitDistMm;
  char line[64];
  snprintf(line, sizeof(line), "O,%lu,%ld,%ld,%d,%ld\n", millis(),
           lround(posX + d * cos(heading)), lround(posY + d * sin(heading)), shapeId,
           lround(heading * 180.0f / PI));        // direction the sensor faced
  sendLine(line);
}

void beginTraceLoop() {
  traceStartX = posX;
  traceStartY = posY;
  traceDist = traceTurn = 0;
  traceMisses = 0;
  traceLostDist = traceLostTurn = 0;
  lastPointX = posX;
  lastPointY = posY;
}

void startTrace(bool room) {
  shapeId++;
  enterState(ST_TRACE);
  clearIr();
  // Front sensor only: an OBJECT is always traced with probing, because simple tracing
  // (straight on until something is in front) turns away from an object and never
  // finds it again - it would just drive off across the room.
  traceProbe = (sideSensor == 0) && (probeMode || !room);
  if (room) {
    traceStep = T_APPROACH;      // drive to the first wall
  } else {
    beginTraceLoop();            // already facing the object
    reportTracePoint();
    traceStep = T_INSIDE_TURN;
    startMotion(MOVE_TURN, PI / 2.0f, -wallSide());
  }
}

void traceInsideCorner() {      // something in front: record it, turn away from the wall side
  reportTracePoint();
  traceStep = T_INSIDE_TURN;
  startMotion(MOVE_TURN, PI / 2.0f, -wallSide());
}

void finishTrace(bool closed) {
  int kind = 0;
  // Turned overall AWAY from the wall side = went round the inside of a room
  if (closed) kind = (traceTurn * wallSide() < 0) ? 2 : 1;
  char line[40];
  snprintf(line, sizeof(line), "S,%lu,%d,%d\n", millis(), shapeId, kind);
  sendLine(line);
  sendMessage(kind == 2 ? "Room outline finished" :
              kind == 1 ? "Object outline finished" : "Outline incomplete");
  enterState(ST_PAUSED);
}

bool traceClosed() {
  float dx = posX - traceStartX, dy = posY - traceStartY;
  return traceDist > TRACE_MIN_MM && fabs(traceTurn) > TRACE_MIN_TURN &&
         dx * dx + dy * dy < TRACE_CLOSE_MM * TRACE_CLOSE_MM;
}

// SIMPLE (front only): straight on until something is in front.
// SIDE SENSOR: keep the wall in view on that side, steering gently.
void updateTraceFollow() {
  if (irObstacle) { traceInsideCorner(); return; }
  if (traceClosed()) { finishTrace(true); return; }
  if (traceDist > TRACE_MAX_MM) { finishTrace(false); return; }

  if (sideSensor == 0) { setWheelSpeeds(DRIVE_SPEED, DRIVE_SPEED); return; }

  int toward = sideSensor;   // turning this way = towards the wall
  if (irSide) {
    traceLostDist = traceLostTurn = 0;
    float mx = posX - lastPointX, my = posY - lastPointY;
    if (mx * mx + my * my >= 30.0f * 30.0f) {   // an edge point every 3 cm
      reportSidePoint();
      lastPointX = posX;
      lastPointY = posY;
    }
    steer(-toward, 0.75f);                       // slightly away from the wall
  } else {
    if (traceLostDist > 600.0f || traceLostTurn > 1.5f * PI) { finishTrace(false); return; }
    steer(toward, traceLostDist < 80.0f ? 0.6f : 0.3f);   // curve back towards it / round corners
  }
}

void updateTrace() {
  if (traceStep == T_FOLLOW) { updateTraceFollow(); return; }
  if (traceStep == T_APPROACH) {             // TRACE,ROOM: head straight for a wall
    if (irObstacle) { beginTraceLoop(); traceInsideCorner(); }
    else setWheelSpeeds(DRIVE_SPEED, DRIVE_SPEED);
    return;
  }

  if (traceStep == T_PROBE_LOOK) {           // facing the wall side: is it still there?
    stopWheels();
    if (millis() - probeStartMs < PROBE_LOOK_MS) return;
    if (irObstacle) {
      reportTracePoint();
      traceMisses = 0;
      traceStep = T_PROBE_BACK;
    } else {
      if (++traceMisses > TRACE_MAX_MISSES) { finishTrace(false); return; }
      traceStep = T_CORNER_BACK;             // wall ended: outside corner
    }
    startMotion(MOVE_TURN, PROBE_ANGLE, 1);  // turn back to the direction of travel
    return;
  }

  if (runMotion()) return;                   // current movement still going
  bool blocked = motionBlocked;
  motionBlocked = false;

  switch (traceStep) {
    case T_DRIVE:
      if (blocked) { traceInsideCorner(); return; }
      if (traceClosed()) { finishTrace(true); return; }
      if (traceDist > TRACE_MAX_MM) { finishTrace(false); return; }
      traceStep = T_PROBE_TURN;
      startMotion(MOVE_TURN, PROBE_ANGLE, -1);    // look right, at the wall
      break;
    case T_PROBE_TURN:
      traceStep = T_PROBE_LOOK;
      probeStartMs = millis();
      break;
    case T_CORNER_BACK:
      traceStep = T_CORNER_CLEAR;
      startMotion(MOVE_DRIVE, CORNER_CLEAR_MM, 1);
      break;
    case T_CORNER_CLEAR:
      if (blocked) { traceInsideCorner(); return; }
      traceStep = T_CORNER_TURN;
      startMotion(MOVE_TURN, PI / 2.0f, -1);      // turn right, round the corner
      break;
    default:                                      // a turn finished
      if (!traceProbe) {                          // simple or side-sensor tracing
        traceStep = T_FOLLOW;
        traceLostDist = traceLostTurn = 0;
      } else {                                    // probing: drive the next step
        traceStep = T_DRIVE;
        startMotion(MOVE_DRIVE, PROBE_STEP_MM, 1);
      }
      break;
  }
}

void updateBehaviour() {
  if (measuring) { stopWheels(); return; }

  switch (state) {
    case ST_SURVEY: updateSurvey(); break;
    case ST_GOTO:   updateGoto();   break;
    case ST_TRACE:  updateTrace();  break;
    case ST_STEP:
      if (!runMotion()) enterState(ST_PAUSED);
      break;
    default:
      stopWheels();
      break;
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
  heading = wrapAngle(heading + dTheta);

  distanceMm += fabs(dCenter);
  distSinceStop += fabs(dCenter);
  if (state == ST_TRACE) {
    traceTurn += dTheta;
    traceDist += fabs(dCenter);
    if (traceStep == T_FOLLOW) {
      traceLostDist += fabs(dCenter);
      traceLostTurn += fabs(dTheta);
    }
  }
  if (motion == MOVE_TURN)  motionLeft -= fabs(dTheta);
  if (motion == MOVE_DRIVE) motionLeft -= fabs(dCenter);
}

void resetOdometry() {
  noInterrupts();
  encLeft = encRight = 0;
  interrupts();
  lastLeft = lastRight = 0;
  posX = posY = heading = distanceMm = 0;
  obstacleCount = 0;
  distSinceStop = SLOW_STOP_SPACING_MM;
}

// ===================================================================== readings cache
void cachePush(char mode, int rssi, int minRssi, int maxRssi) {
  Reading r;
  r.seq = nextSeq++;
  r.takenMs = millis();
  r.x = (int16_t)constrain(lround(posX), -32000L, 32000L);
  r.y = (int16_t)constrain(lround(posY), -32000L, 32000L);
  r.rssi = (int8_t)rssi;
  r.minRssi = (int8_t)minRssi;
  r.maxRssi = (int8_t)maxRssi;
  r.mode = mode;
  if (cacheCount == CACHE_SIZE) {           // full: overwrite the oldest
    cache[cacheHead] = r;
    cacheHead = (cacheHead + 1) % CACHE_SIZE;
    overwrittenCount++;
  } else {
    cache[(cacheHead + cacheCount) % CACHE_SIZE] = r;
    cacheCount++;
  }
}

void sendOldest() {
  if (cacheCount == 0) return;
  const Reading& r = cache[cacheHead];
  char line[64];
  snprintf(line, sizeof(line), "R,%u,%c,%d,%d,%d,%lu,%d,%d\n",
           r.seq, r.mode, r.rssi, r.minRssi, r.maxRssi,
           (unsigned long)(millis() - r.takenMs), r.x, r.y);
  sendLine(line);
  awaitingAck = true;
  sentSeq = r.seq;
  sentAtMs = millis();
}

// Called every loop(): one reading at a time, resent until the app confirms it
void serviceSendQueue() {
  if (!wifiIsConnected()) { awaitingAck = false; return; }
  if (awaitingAck) {
    bool stillThere = cacheCount > 0 && cache[cacheHead].seq == sentSeq;
    if (!stillThere) awaitingAck = false;                  // overwritten while waiting
    else if (millis() - sentAtMs >= ackTimeoutMs) { sendOldest(); return; }   // resend
    else return;                                           // still waiting
  }
  if (cacheCount > 0) sendOldest();
}

// "A,seq" from the app: that reading arrived, delete it
void handleAck(uint16_t seq) {
  if (awaitingAck && cacheCount > 0 && cache[cacheHead].seq == seq && seq == sentSeq) {
    cacheHead = (cacheHead + 1) % CACHE_SIZE;
    cacheCount--;
    awaitingAck = false;
    serviceSendQueue();          // send the next one straight away
  }
}

// ===================================================================== Wi-Fi meter
void resetAverage() {
  sampleSum = 0;
  sampleCount = 0;
  sampleMin = 0;
  sampleMax = -127;
}

void addSample(int rssi) {
  sampleSum += rssi;
  sampleCount++;
  if (rssi < sampleMin) sampleMin = rssi;
  if (rssi > sampleMax) sampleMax = rssi;

  if (sampleCount >= slowSamples) {
    int avg = (int)((sampleSum - slowSamples / 2) / slowSamples);  // round to nearest
    int lo = sampleMin, hi = sampleMax;
    cachePush('S', avg, lo, hi);
    snprintf(extraBuf, sizeof(extraBuf), "avg %d to %d", lo, hi);
    shownRssi = avg;
    shownExtra = extraBuf;
    bigDirty = true;
    resetAverage();

    if (measuring) {
      measuring = false;
      distSinceStop = 0;
      char msg[40];
      snprintf(msg, sizeof(msg), "Measured %d dBm (%d to %d)", avg, lo, hi);
      sendMessage(msg);
    }
  }
}

void updateWifiMeter() {
  // Average when stopped to measure, or when paused in SLOW mode (handheld meter)
  bool averaging = measuring || (slowMode && state == ST_PAUSED);
  unsigned long interval = averaging ? slowIntervalMs : fastIntervalMs;
  if (millis() - lastReadMs < interval) return;
  lastReadMs = millis();

  if (!averaging && slowMode) return;   // SLOW mode while moving: no readings

  int rssi = wifiReadRssi();
  if (rssi == 0) return;                // no valid reading
  currentRssi = rssi;

  if (averaging) {
    addSample(rssi);
  } else {
    cachePush('F', rssi, rssi, rssi);
    shownRssi = rssi;
    shownExtra = nullptr;
    bigDirty = true;
  }
}

// ===================================================================== commands
// ===================================================================== settings (SET)
bool parseLongIn(const char* v, long lo, long hi, long& out) {
  char* end;
  long n = strtol(v, &end, 10);
  if (end == v || *end != 0 || n < lo || n > hi) return false;
  out = n;
  return true;
}

void setDisplayOn(bool on) {
  displayOn = on;
  if (on) { display.on(); display.clearScreen(); bigDirty = true; }
  else    { display.clearScreen(); display.off(); }
}

// Same keys as the stand-alone Wi-Fi module. False = unknown key or bad value.
bool applySetting(const char* key, const char* val) {
  long n;
  if (!strcmp(key, "ssid")) {
    size_t len = strlen(val);
    if (len < 1 || len > 32) return false;
    strcpy(wifiSsid, val);
    wifiConfigChanged = true;
    return true;
  }
  if (!strcmp(key, "pass")) {
    size_t len = strlen(val);
    if (len != 0 && (len < 8 || len > 63)) return false;   // WPA2 needs 8-63 characters
    strcpy(wifiPass, val);
    wifiConfigChanged = true;
    return true;
  }
  if (!strcmp(key, "fast_ms"))       { if (!parseLongIn(val, 50, 60000, n)) return false; fastIntervalMs = n; return true; }
  if (!strcmp(key, "slow_ms"))       { if (!parseLongIn(val, 20, 10000, n)) return false; slowIntervalMs = n; return true; }
  if (!strcmp(key, "slow_n"))        { if (!parseLongIn(val, 1, 200, n))    return false; slowSamples = n; resetAverage(); return true; }
  if (!strcmp(key, "roam_dbm"))      { if (!parseLongIn(val, -100, -20, n)) return false; roamTriggerDbm = n; return true; }
  if (!strcmp(key, "roam_margin"))   { if (!parseLongIn(val, 0, 40, n))     return false; roamMarginDb = n; return true; }
  if (!strcmp(key, "roam_check_ms")) { if (!parseLongIn(val, 0, 600000, n)) return false; roamCheckMs = n; return true; }
  if (!strcmp(key, "roam_cool_ms"))  { if (!parseLongIn(val, 0, 600000, n)) return false; roamCooldownMs = n; return true; }
  if (!strcmp(key, "ack_ms"))        { if (!parseLongIn(val, 50, 10000, n)) return false; ackTimeoutMs = n; return true; }
  if (!strcmp(key, "display"))       { if (!parseLongIn(val, 0, 1, n))      return false; setDisplayOn(n == 1); return true; }
  if (!strcmp(key, "mode")) {
    if (!strcmp(val, "F"))      slowMode = false;
    else if (!strcmp(val, "S")) slowMode = true;
    else return false;
    measuring = false;
    resetAverage();
    distSinceStop = SLOW_STOP_SPACING_MM;
    shownRssi = 0; shownExtra = nullptr; bigDirty = true;
    return true;
  }
  return false;
}

// Returns the text sent back to the app
const char* runCommand(char* cmd, char* a1, char* a2, char* a3) {
  if (!strcmp(cmd, "RUN"))   { measuring = false; enterState(ST_SURVEY); return "Survey running"; }
  if (!strcmp(cmd, "PAUSE")) { measuring = false; enterState(ST_PAUSED); return "Paused"; }

  if (!strcmp(cmd, "HOME")) {
    goalX = 0; goalY = 0; goalIsHome = true; goalStopOnBlock = false;
    measuring = false;
    enterState(ST_GOTO);
    return "Returning to start";
  }
  if (!strcmp(cmd, "TRACE") && a1) {
    measuring = false;
    startTrace(!strcmp(a1, "ROOM"));
    return "Tracing";
  }
  if (!strcmp(cmd, "HELLO")) return "Hello, app";
  if (!strcmp(cmd, "SENSORS") && a1) {
    sideSensor = !strcmp(a1, "FL") ? 1 : (!strcmp(a1, "FR") ? -1 : 0);
    clearIr();
    return sideSensor > 0 ? "Sensors: front + left" : sideSensor < 0 ? "Sensors: front + right" : "Sensors: front only";
  }
  if (!strcmp(cmd, "PROBE") && a1) {
    probeMode = !strcmp(a1, "ON");
    return probeMode ? "Wall probing on" : "Wall probing off";
  }
  if (!strcmp(cmd, "AUTOROAM") && a1) {
    autoRoam = !strcmp(a1, "ON");
    return autoRoam ? "Auto roaming on" : "Auto roaming off";
  }
  if (!strcmp(cmd, "GOTO") && a1 && a2) {
    goalX = atof(a1); goalY = atof(a2); goalIsHome = false;
    goalStopOnBlock = (a3 && !strcmp(a3, "S"));
    measuring = false;
    enterState(ST_GOTO);
    return "Going to point";
  }
  if (!strcmp(cmd, "STEP") && a1) {
    measuring = false;
    enterState(ST_STEP);
    if (!strcmp(a1, "FWD"))   { startMotion(MOVE_DRIVE, STEP_DRIVE_MM, 1);  return "Step forward"; }
    if (!strcmp(a1, "BACK"))  { startMotion(MOVE_DRIVE, STEP_DRIVE_MM, -1); return "Step back"; }
    if (!strcmp(a1, "LEFT"))  { startMotion(MOVE_TURN, STEP_TURN_RAD, 1);   return "Turn left"; }
    if (!strcmp(a1, "RIGHT")) { startMotion(MOVE_TURN, STEP_TURN_RAD, -1);  return "Turn right"; }
    enterState(ST_PAUSED);
    return "Unknown step";
  }
  if (!strcmp(cmd, "MEASURE")) {
    measuring = true;
    resetAverage();
    stopWheels();
    return "Measuring here";
  }
  if (!strcmp(cmd, "MODE") && a1) {
    slowMode = !strcmp(a1, "SLOW");
    measuring = false;
    resetAverage();
    distSinceStop = SLOW_STOP_SPACING_MM;
    shownRssi = 0; shownExtra = nullptr; bigDirty = true;
    return slowMode ? "SLOW mode" : "FAST mode";
  }
  if (!strcmp(cmd, "RESET")) {
    resetOdometry();
    if (state == ST_GOTO) enterState(ST_PAUSED);
    return "Position reset: this is the new start";
  }
  if (!strcmp(cmd, "ROAM")) {
    roamRequested = true;     // done in loop(), so this reply isn't delayed by the scan
    return "Scanning for a better AP";
  }
  if (!strcmp(cmd, "SPEED") && a1) {
    speedPct = constrain(atoi(a1), 20, 200);
    return "Speed changed";
  }
  return "Unknown command";
}

// Line format: C,<id>,<COMMAND>[,<arg1>[,<arg2>]]
void handleCommandLine(char* line) {
  // "A,seq": the app got a reading
  if (line[0] == 'A' && line[1] == ',') {
    handleAck((uint16_t)strtoul(line + 2, nullptr, 10));
    return;
  }

  char* tag = strtok(line, ",\r\n");
  if (!tag || strcmp(tag, "C")) return;
  char* idStr = strtok(nullptr, ",\r\n");
  char* cmd   = strtok(nullptr, ",\r\n");
  if (!idStr || !cmd) return;

  // SET,<key>,<value>: the value is everything after the key, so it may contain commas
  if (!strcmp(cmd, "SET")) {
    long id = atol(idStr);
    char* rest = strtok(nullptr, "\r\n");
    if (id != lastCmdId) {
      lastCmdId = id;
      char* comma = rest ? strchr(rest, ',') : nullptr;
      if (!comma) {
        strcpy(lastAck, "Setting needs a key and a value");
      } else {
        *comma = 0;
        bool ok = applySetting(rest, comma + 1);
        snprintf(lastAck, sizeof(lastAck), ok ? "Set %s" : "Bad setting %s", rest);  // never echo the value
      }
    }
    char reply[64];
    snprintf(reply, sizeof(reply), "K,%ld,%s\n", id, lastAck);
    sendLine(reply);
    return;
  }

  char* a1    = strtok(nullptr, ",\r\n");
  char* a2    = strtok(nullptr, ",\r\n");
  char* a3    = strtok(nullptr, ",\r\n");

  long id = atol(idStr);
  if (id != lastCmdId) {          // the app may resend; only act on each command once
    lastCmdId = id;
    blockedFlag = false;          // any new command clears BLOCKED
    strncpy(lastAck, runCommand(cmd, a1, a2, a3), sizeof(lastAck) - 1);
  }
  char reply[64];
  snprintf(reply, sizeof(reply), "K,%ld,%s\n", id, lastAck);
  sendLine(reply);
}

void checkCommands() {
  // USB serial
  while (SerialUSB.available()) {
    char c = SerialUSB.read();
    if (c == '\n') {
      serialBuf[serialLen] = 0;
      handleCommandLine(serialBuf);
      serialLen = 0;
    } else if (serialLen < (int)sizeof(serialBuf) - 1) {
      serialBuf[serialLen++] = c;
    }
  }

#if USE_WIFI
  // Wi-Fi
  int size = udp.parsePacket();
  if (size > 0) {
    char buf[96];
    IPAddress from = udp.remoteIP();
    int len = udp.read(buf, sizeof(buf) - 1);
    if (len > 0) {
      buf[len] = 0;
      // Talk straight to the app from now on: direct packets are acknowledged and re-sent
      // by Wi-Fi if lost, broadcasts aren't (that's how wall points went missing)
      if (PC_IP == IPAddress(0, 0, 0, 0) && !(from == learnedPc) && !(from == IPAddress(0, 0, 0, 0))) {
        learnedPc = from;
        sendTo = from;
      }
      handleCommandLine(buf);
    }
  }
#endif
}

// ===================================================================== display
void showText(const char* text, const FONT_INFO& font, uint8_t colour, int y) {
  display.setFont(font);
  display.fontColor(colour, TS_8b_Black);
  int x = (96 - display.getPrintWidth((char*)text)) / 2;
  if (x < 0) x = 0;
  display.setCursor(x, y);
  display.print(text);
}

void showMessage(const char* msg) {
  if (!displayOn) return;
  display.clearScreen();
  showText(msg, thinPixel7_10ptFontInfo, TS_8b_White, 28);
}

void printAt(int x, int y, int w, const char* text, uint8_t colour) {
  display.drawRect(x, y, w, 9, TSRectangleFilled, TS_8b_Black);
  display.setFont(thinPixel7_10ptFontInfo);
  display.fontColor(colour, TS_8b_Black);
  display.setCursor(x, y);
  display.print(text);
}

uint8_t strengthColour(int rssi) {
  if (rssi >= -50) return TS_8b_Green;
  if (rssi >= -70) return TS_8b_Yellow;
  return TS_8b_Red;
}

const char* strengthLabel(int rssi) {
  if (rssi >= -50) return "Excellent";
  if (rssi >= -70) return "Good";
  return "Weak";
}

void drawTopLine() {
  char buf[32];
  // Q = readings waiting to be confirmed by the app
  if (measuring || (slowMode && state == ST_PAUSED))
    snprintf(buf, sizeof(buf), "%s %d/%d %s Q%d", slowMode ? "S" : "F", sampleCount, slowSamples,
             measuring ? "MEAS" : stateName(), cacheCount);
  else
    snprintf(buf, sizeof(buf), "%s  %s  Q%d", slowMode ? "SLOW" : "FAST", stateName(), cacheCount);
  display.drawRect(0, 0, 96, 11, TSRectangleFilled, TS_8b_Black);
  showText(buf, thinPixel7_10ptFontInfo, state == ST_PAUSED ? TS_8b_Yellow : TS_8b_Blue, 1);

  display.drawRect(0, 11, 96, 2, TSRectangleFilled, TS_8b_Black);
  if (sampleCount > 0) {
    int w = (96 * sampleCount) / slowSamples;
    display.drawRect(0, 11, w, 2, TSRectangleFilled, TS_8b_Blue);
  }
}

void drawBigNumber() {
  display.drawRect(0, 14, 96, 20, TSRectangleFilled, TS_8b_Black);
  char num[12];
  if (shownRssi == 0) {
    showText("-- dBm", liberationSans_16ptFontInfo, TS_8b_White, 15);
  } else {
    snprintf(num, sizeof(num), "%d dBm", shownRssi);
    showText(num, liberationSans_16ptFontInfo, strengthColour(shownRssi), 15);
  }
}

// Bottom line: which AP we're on (last 2 bytes of its MAC address, and channel)
void drawApLine() {
  char buf[24];
  if (currentChannel > 0)
    snprintf(buf, sizeof(buf), "AP %02X:%02X  ch%d", currentBssid[4], currentBssid[5], currentChannel);
  else
    snprintf(buf, sizeof(buf), "AP %02X:%02X", currentBssid[4], currentBssid[5]);
  printAt(0, 55, 96, buf, TS_8b_Blue);
}

void updateDisplay() {
  if (!displayOn) return;
  drawTopLine();
  if (bigDirty) { drawBigNumber(); bigDirty = false; }

  char buf[24];
  const char* label = shownRssi == 0 ? "measuring" : (shownExtra ? shownExtra : strengthLabel(shownRssi));
  printAt(0, 35, 62, label, TS_8b_White);
  char irTxt[12];
  if (sideSensor == 0) strcpy(irTxt, irObstacle ? "IR YES" : "IR no");
  else snprintf(irTxt, sizeof(irTxt), "F%c %c%c", irObstacle ? 'Y' : 'n',
                sideSensor > 0 ? 'L' : 'R', irSide ? 'Y' : 'n');
  bool anyIr = irObstacle || irSide;
  printAt(62, 35, 34, irTxt, anyIr ? TS_8b_Red : TS_8b_Green);

  long cm = lround(distanceMm / 10.0f);
  snprintf(buf, sizeof(buf), "%ld.%02ldm  H%ld  #%d", cm / 100, cm % 100,
           lround(heading * 180.0f / PI), obstacleCount);
  printAt(0, 45, 96, buf, TS_8b_White);

  drawApLine();
}

// ===================================================================== buttons
void handleButtons() {
  static unsigned long lastPress = 0;
  if (millis() - lastPress < 300) return;

  if (display.getButtons(TSButtonUpperLeft)) {
    resetOdometry();
    lastPress = millis();
  } else if (display.getButtons(TSButtonUpperRight)) {
    measuring = false;
    enterState(state == ST_PAUSED ? ST_SURVEY : ST_PAUSED);
    lastPress = millis();
  } else if (display.getButtons(TSButtonLowerLeft)) {
    char a[5];
    strcpy(a, slowMode ? "FAST" : "SLOW");
    runCommand((char*)"MODE", a, nullptr, nullptr);
    lastPress = millis();
  } else if (display.getButtons(TSButtonLowerRight)) {
    runCommand((char*)"MEASURE", nullptr, nullptr, nullptr);
    lastPress = millis();
  }
}

// ===================================================================== Wi-Fi (via wifi_logic)
// After joining (first time, after a drop, or after roaming): set up the link to the app
void setupAppLink() {
  if (PC_IP == IPAddress(0, 0, 0, 0) && !(learnedPc == IPAddress(0, 0, 0, 0))) {
    sendTo = learnedPc;                     // already know where the app is
  } else if (PC_IP == IPAddress(0, 0, 0, 0)) {   // broadcast address of this network
    IPAddress ip = WiFi.localIP(), mask = WiFi.subnetMask();
    sendTo = IPAddress(ip[0] | (uint8_t)~mask[0], ip[1] | (uint8_t)~mask[1],
                       ip[2] | (uint8_t)~mask[2], ip[3] | (uint8_t)~mask[3]);
  } else {
    sendTo = PC_IP;
  }
  udp.stop();
  udp.begin(ROBOT_PORT);
}

// wifi_logic.cpp tells the robot what the Wi-Fi is doing through these two functions
void onWifiEvent(WifiEvent ev, int value) {
  char msg[48];
  switch (ev) {
    case WIFI_EV_CONNECTING:
      stopWheels();                    // never drive blind while (re)connecting
      showMessage("Connecting...");
      break;
    case WIFI_EV_RETRYING:
      showMessage("Retrying...");
      break;
    case WIFI_EV_CONNECTED:
      setupAppLink();
      currentRssi = 0;
      resetAverage();
      if (displayOn) display.clearScreen();
      bigDirty = true;
      lastOdom = millis();             // don't count the waiting time as driving time
      snprintf(msg, sizeof(msg), "Connected to AP %02X:%02X ch%d",
               currentBssid[4], currentBssid[5], currentChannel);
      sendMessage(msg);
      break;
    case WIFI_EV_SCANNING:
      stopWheels();                    // the scan blocks for ~2 s: don't drive blind
      lastMovingScanMs = millis();
      printAt(0, 55, 96, "scanning APs...", TS_8b_Blue);
      break;
    case WIFI_EV_SCAN_DONE:
      lastOdom = millis();
      if (roamForced) sendMessage("No stronger AP found");
      roamForced = false;
      break;
    case WIFI_EV_ROAMING:
      snprintf(msg, sizeof(msg), "Roaming to ch%d", value);
      sendMessage(msg);                // sent before disconnecting, so the app sees it
      showMessage(msg);
      roamForced = false;
      break;
  }
}

// Called while wifi_logic waits (connecting): keep the wheels stopped
void onWifiIdle() {
  stopWheels();
}

// Roam only when it won't interrupt something: paused, or surveying (not measuring)
void maybeRoam() {
  if (roamRequested) {                 // the app asked for it
    roamRequested = false;
    if (!measuring) { roamForced = true; wifiRoamNow(currentRssi); }
    return;
  }
  if (!autoRoam || measuring || !(state == ST_PAUSED || state == ST_SURVEY)) return;
  // While surveying, scan less often (the robot has to stop for each scan)
  if (state == ST_SURVEY && millis() - lastMovingScanMs < ROAM_CHECK_MOVING_MS) return;
  wifiMaybeRoam(currentRssi);          // wifi_logic decides if a scan is due
}

// ===================================================================== setup / loop
void setup() {
  SerialUSB.begin(115200);
  Wire.begin();
  randomSeed(micros());
  shapeId = random(1, 30000) * 10;   // outline ids won't repeat after a restart

  display.begin();
  display.setBrightness(10);
  display.setBitDepth(TSBitDepth8);
  display.clearScreen();

#if !SIMULATE_IR
  Wireling.begin();
  pinMode(IR_EMITTER_PIN, OUTPUT);
  digitalWrite(IR_EMITTER_PIN, LOW);
  pinMode(IR_RECEIVER_PIN, INPUT_PULLUP);
  pinMode(IR_SIDE_RECEIVER_PIN, INPUT_PULLUP);
#endif

#if !SIMULATE_MOTORS
  if (ENC_L_A < 0 || ENC_L_B < 0 || ENC_R_A < 0 || ENC_R_B < 0) {
    showMessage("Set encoder pins!");
    while (true) {}
  }
  pinMode(ENC_L_A, INPUT_PULLUP);
  pinMode(ENC_L_B, INPUT_PULLUP);
  pinMode(ENC_R_A, INPUT_PULLUP);
  pinMode(ENC_R_B, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENC_L_A), isrLeft, RISING);
  attachInterrupt(digitalPinToInterrupt(ENC_R_A), isrRight, RISING);
#endif

  if (!wifiBegin()) {
    showMessage("No WiFi shield!");
    while (true) delay(1000);
  }
  wifiConnect();

  lastOdom = millis();
}

void loop() {
  unsigned long now = millis();

  if (wifiConfigChanged) {        // new network name / password from the app
    wifiConfigChanged = false;
    stopWheels();
    wifiReconnect();
    now = millis();
  }
  if (!wifiIsConnected()) {
    wifiConnect();
    now = millis();
  }

  checkCommands();
  handleButtons();
  maybeRoam();
  serviceSendQueue();

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

  updateWifiMeter();

  if (now - lastSend >= SEND_INTERVAL_MS) {
    lastSend = now;
    sendTelemetry();
  }

  if (now - lastDisplay >= DISPLAY_INTERVAL_MS) {
    lastDisplay = now;
    updateDisplay();
  }
}
