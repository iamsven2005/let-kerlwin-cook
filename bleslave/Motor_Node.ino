// =====================================================
// Multi-device MOTOR CONTROLLER node
// - Based on: "Roomba_v1/TinyScreenRoomba/TinyScreenRoomba.ino"
//   (geometry, encoder math, turn logic) + "Bluetooth/BleTest.ino"
//   (BLE UART transport). UART.ino here is a verbatim copy of
//   Bluetooth/UART.ino; keep it unchanged.
//
// WHAT CHANGED vs the old brain-driven code:
//   Old: the brain (TinyScreenRoomba) called setWheelSpeeds() every
//        loop and owned FORWARD/TURNING state, turn angle, and
//        odometry. The motor was a TODO ("send left/right to the
//        motor driver here").
//   New: the brain sends ONE high-level command and stops. This
//        node owns the whole step: encoder counting, turn angle,
//        straight driving, and completion reporting.
//          Brain -> Motor:  "M<n> DRIVE" | "M<n> DRIVE <mm>"
//                           "M<n> TURN <deg> L|R" | "M<n> STOP"
//          Motor -> Brain:  "A<n>" (ACK) | "DONE" | "STALLED"
//        The brain only interrupts with a new command when
//        has_wall changes or a step reports DONE. It never streams
//        wheel speeds.
//
// SEQUENCE / DUPLICATE RULE:
//   <n> is the brain's sequence (0-99, wraps). A command with a
//   sequence already executed is ACKed but NOT executed again:
//   resending "M5 TURN 90 L" after a lost ACK must not turn twice.
//   A new command pre-empts the current step immediately (that is
//   how the brain's wall interrupt works).
//
// FAIL-SAFE: if BLE disconnects, the motors stop. A route command
//   must never outlive its brain connection.
// =====================================================

#include <SPI.h>
#include <Wire.h>
#include <STBLE.h>
#include <TinyScreen.h>

// ---- BLE transport, shared with UART.ino (do not rename) ----
#if defined(ARDUINO_ARCH_SAMD)
  #define SerialMonitorInterface SerialUSB
#else
  #define SerialMonitorInterface Serial
#endif
#define BLE_DEBUG true
#define PIPE_UART_OVER_BTLE_UART_TX_TX 0

uint8_t ble_rx_buffer[21];
uint8_t ble_rx_buffer_len = 0;
uint8_t ble_connection_state = false;

// ---- Robot geometry (same values as TinyScreenRoomba.ino) ----
const float WHEEL_DIAMETER_MM = 40.0f;
const float WHEEL_BASE_MM     = 120.0f;
const float ENCODER_PPR       = 7.0f;
const float GEAR_RATIO        = 100.0f;
const float COUNTS_PER_WHEEL_REV = ENCODER_PPR * GEAR_RATIO;
const float MM_PER_COUNT = PI * WHEEL_DIAMETER_MM / COUNTS_PER_WHEEL_REV;
const float DRIVE_SPEED = 150.0f;  // mm/s
const float TURN_SPEED  = 80.0f;   // mm/s at the wheel, in place

// ---- Motor driver pins: SET THESE when the driver arrives ----
// Left as -1 on purpose, like the old sketch's encoder pins: the
// sketch refuses to run with unset pins rather than driving blind.
const int MOTOR_L_PWM = -1, MOTOR_L_DIR = -1;
const int MOTOR_R_PWM = -1, MOTOR_R_DIR = -1;
const int ENC_L_A = -1, ENC_L_B = -1;
const int ENC_R_A = -1, ENC_R_B = -1;
const bool LEFT_REVERSED  = false;
const bool RIGHT_REVERSED = true;

#define SIMULATE_MOTORS 1  // 1 = virtual wheels (as in the old sketch), 0 = real

TinyScreen display = TinyScreen(TinyScreenPlus);

enum Step { IDLE, DRIVING, TURNING };
Step step = IDLE;
float stepTarget = 0;   // mm to drive, or radians to turn
float stepProgress = 0; // mm driven, or radians turned
int turnDir = 1;        // +1 = left (CCW), -1 = right
int lastSeq = -1;       // last executed command sequence
bool wasConnected = false;
float wheelSpeedL = 0, wheelSpeedR = 0;

volatile long encLeft = 0, encRight = 0;
long lastLeft = 0, lastRight = 0;
float simCarryL = 0, simCarryR = 0;
unsigned long lastOdom = 0, lastStallCheck = 0;
long lastEncTotal = 0;

void isrLeft()  { bool b = digitalRead(ENC_L_B); encLeft  += (b ^ LEFT_REVERSED)  ? -1 : 1; }
void isrRight() { bool b = digitalRead(ENC_R_B); encRight += (b ^ RIGHT_REVERSED) ? -1 : 1; }

void bleSend(const char *text) {
  if (!ble_connection_state) return;
  uint8_t len = min(strlen(text), (size_t)20);
  lib_aci_send_data(PIPE_UART_OVER_BTLE_UART_TX_TX, (uint8_t *)text, len);
}

void setWheelSpeeds(float left, float right) {
  wheelSpeedL = left;
  wheelSpeedR = right;
#if !SIMULATE_MOTORS
  // TODO when the driver arrives: write left/right to MOTOR_* pins here.
  // This is the only place wheel speeds are set, and it is owned by
  // this node now, not by the brain.
#endif
}

void stopAndReport(const char *report) {
  setWheelSpeeds(0, 0);
  step = IDLE;
  stepTarget = stepProgress = 0;
  bleSend(report);
}

void startDrive(float mm) {
  step = DRIVING;
  stepTarget = mm;  // 0 = drive until interrupted (brain's wall interrupt)
  stepProgress = 0;
  lastLeft = encLeft;
  lastRight = encRight;
  setWheelSpeeds(DRIVE_SPEED, DRIVE_SPEED);
}

void startTurn(float degrees, int dir) {
  step = TURNING;
  stepTarget = degrees * PI / 180.0f;
  stepProgress = 0;
  turnDir = dir;
  lastLeft = encLeft;
  lastRight = encRight;
  setWheelSpeeds(-dir * TURN_SPEED, dir * TURN_SPEED);
}

// Parse "M<n> ..." ; returns true if this is a new command to execute.
bool handleCommand(char *msg) {
  // msg format: M<n> VERB [args]. Sequence is parsed, then stripped.
  if (msg[0] != 'M') return false;
  char *space = strchr(msg, ' ');
  if (!space) return false;
  int seq = atoi(msg + 1);

  char ack[8];
  snprintf(ack, sizeof(ack), "A%d", seq);
  bleSend(ack);  // ACK even for duplicates: brain must not resend forever.

  if (seq == lastSeq) return false;  // duplicate resend: ACK only, do not re-execute
  lastSeq = seq;

  char *verb = space + 1;
  if (strncmp(verb, "STOP", 4) == 0) {
    stopAndReport("DONE");
  } else if (strncmp(verb, "DRIVE", 5) == 0) {
    float mm = 0;
    if (strlen(verb) > 6) mm = atof(verb + 6);
    startDrive(mm);
  } else if (strncmp(verb, "TURN", 4) == 0) {
    float deg = atof(verb + 5);
    // Last token is the direction: "TURN 90 L" -> dirSpace points at " L".
    char *dirSpace = strrchr(verb, ' ');
    int dir = (dirSpace && dirSpace[1] == 'R') ? -1 : 1;  // -1 = right, +1 = left
    startTurn(deg, dir);
  }
  return true;
}

void updateOdometry(float dt) {
#if SIMULATE_MOTORS
  simCarryL += wheelSpeedL * dt / MM_PER_COUNT;
  simCarryR += wheelSpeedR * dt / MM_PER_COUNT;
  long cl = (long)simCarryL, cr = (long)simCarryR;
  simCarryL -= cl;
  simCarryR -= cr;
  noInterrupts();
  encLeft += cl;
  encRight += cr;
  interrupts();
#endif
  noInterrupts();
  long l = encLeft, r = encRight;
  interrupts();
  float dL = (l - lastLeft) * MM_PER_COUNT;
  float dR = (r - lastRight) * MM_PER_COUNT;
  lastLeft = l;
  lastRight = r;
  float dCenter = (dL + dR) / 2.0f;
  float dTheta = (dR - dL) / WHEEL_BASE_MM;
  if (step == DRIVING) {
    stepProgress += fabs(dCenter);
    if (stepTarget > 0 && stepProgress >= stepTarget) stopAndReport("DONE");
  } else if (step == TURNING) {
    stepProgress += fabs(dTheta);
    if (stepProgress >= stepTarget) stopAndReport("DONE");
  }
}

void setup() {
  SerialMonitorInterface.begin(9600);
  display.begin();
  display.setBrightness(10);
  display.setFont(thinPixel7_10ptFontInfo);
  display.clearScreen();
  display.setCursor(0, 0);
  display.print("Motor node");

#if !SIMULATE_MOTORS
  if (ENC_L_A < 0 || MOTOR_L_PWM < 0) {
    display.setCursor(0, 14);
    display.print("Set motor pins!");
    while (true) {}
  }
  pinMode(ENC_L_A, INPUT_PULLUP);
  pinMode(ENC_L_B, INPUT_PULLUP);
  pinMode(ENC_R_A, INPUT_PULLUP);
  pinMode(ENC_R_B, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENC_L_A), isrLeft, RISING);
  attachInterrupt(digitalPinToInterrupt(ENC_R_A), isrRight, RISING);
#endif

  BLEsetup();
  lastOdom = millis();
  lastStallCheck = millis();
}

void loop() {
  aci_loop();

  // Fail-safe: connection lost -> stop, do not keep driving the old step.
  if (ble_connection_state != wasConnected) {
    wasConnected = ble_connection_state;
    if (!wasConnected) {
      setWheelSpeeds(0, 0);
      step = IDLE;
      lastSeq = -1;  // new connection gets a fresh sequence space
    }
  }

  if (ble_rx_buffer_len) {
    char msg[21];
    uint8_t len = min(ble_rx_buffer_len, (uint8_t)20);
    memcpy(msg, ble_rx_buffer, len);
    msg[len] = '\0';
    ble_rx_buffer_len = 0;
    if (strcmp(msg, "ID?") == 0) {
      bleSend("ID MOTOR");
    } else if (msg[0] == 'M') {
      handleCommand(msg);
    }
  }

  unsigned long now = millis();
  float dt = (now - lastOdom) / 1000.0f;
  lastOdom = now;
  if (step != IDLE) updateOdometry(dt);

  // Stall: commanded to move, encoders not moving for 1 s.
  if (step != IDLE && now - lastStallCheck >= 1000) {
    lastStallCheck = now;
    long total = labs(encLeft) + labs(encRight);
    if (total == lastEncTotal) {
      stopAndReport("STALLED");
    }
    lastEncTotal = total;
  }

  display.setCursor(0, 14);
  display.print(step == IDLE ? "IDLE   " : (step == DRIVING ? "DRIVING" : "TURNING"));
}
