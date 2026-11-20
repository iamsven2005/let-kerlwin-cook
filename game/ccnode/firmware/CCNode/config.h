// =============================================================================
//  CCNode firmware - configuration and calibration
//  Every tunable value lives in this file. Change, re-flash, re-test.
// =============================================================================
#pragma once
#include <stdint.h>

#define FW_VERSION "1.6"          // sent in HELLO:<id>:<fw> (tracks the brief version it implements)
#define NODE_ID_OVERRIDE 0        // 0 = derive the ID from the SAMD21 serial number; else a fixed 16-bit ID

// -----------------------------------------------------------------------------
//  Run modes (see README "Modes")
//    Normal      : BLE on. USB serial prints only the boot trace.
//    Debug       : BLE on, boot banner + 1 s heartbeat + event log on USB serial.
//                  DEBUG_DEFAULT 1, or hold all 4 buttons at power-on, or hold
//                  all 4 buttons for 2 s while running (toggles), or type !DEBUG.
//    Serial test : BLE off, protocol events printed to USB serial, hub
//                  commands accepted from USB serial. Hold BOTH BOTTOM buttons
//                  at power-on, or set SERIAL_TEST_MODE 1.
//  USB serial is always SerialUSB (the native USB port). Nothing ever waits for it.
// -----------------------------------------------------------------------------
#ifndef DEBUG_DEFAULT
#define DEBUG_DEFAULT      0
#endif
#ifndef SERIAL_TEST_MODE
#define SERIAL_TEST_MODE   0
#endif
#define SERIAL_BAUD          115200   // any value works for USB CDC; use 115200 in the serial monitor
#define DEBUG_ACCEL_PRINT_MS 0        // >0: also print raw accel lines this often (ms) in debug mode; 0 = off
#define HEARTBEAT_MS         1000     // debug heartbeat period
#define DEBUG_TOGGLE_HOLD_MS 2000     // hold all 4 buttons this long to toggle debug at runtime

// -----------------------------------------------------------------------------
//  CALIBRATION BLOCK  (README section "Calibration" explains each step)
// -----------------------------------------------------------------------------
// Axis mapping.  The accelerometer axes depend on how the shield is mounted.
#define CAL_SIDE_AXIS         0      // which raw axis runs left<->right on the screen: 0 = X, 1 = Y
#define CAL_X_SIGN           (+1)    // +1: a positive first spike on the side axis means T:R.  Flip to -1 if R/L come out swapped.
// Brief v1.5 FW-Q27: on the ASD2511 Rev5 stack raw Z reads -1 g face-up, so the default is -1.
// Check on the boot splash: face-up must show zAvg ~ +1.00 and UP.
#define CAL_Z_SIGN           (-1)    // sign that makes face-up read +1 g

// Tap detection
#define CAL_TAP_SIDE_G        0.6f   // high-pass threshold for side taps (g)
#define CAL_TAP_V_G           1.6f   // high-pass threshold for vertical taps (g)
#define CAL_TAP_REFRACTORY_MS 200    // after a tap, ignore further spikes this long
#define CAL_TAP_WINDOW_MS     4      // spike capture window used to pick the dominant axis
#define CAL_TAP_BASE_ALPHA    0.01f  // slow low-pass used as each axis' baseline (per 1 kHz sample, tau ~100 ms)
#define CAL_TAP_V_DECAY       0.5f   // V tap: vertical spike must ring (sign flip) or fall below this x peak within the window,
                                     // otherwise it is a pickup push, not an impact. 1.0 = check off. Debug log prints "end=" ratio.

// Flip detection (designer's reference algorithm, section 2 of the brief)
#define CAL_FLIP_G            0.7f   // hysteresis threshold on zAvg (g)
#define CAL_FLIP_HOLD_MS      500    // new orientation must persist this long
#define CAL_FLIP_ALPHA        0.02f  // zAvg low-pass coefficient per sample (tau ~50 ms at 1 kHz)
#define GESTURE_INIT_MS       200    // v1.4 A1: starting orientation = average of the first 200 ms of samples

// Tap suppression around orientation changes (brief v1.1: Q9 option C, Q13 option A)
#define CAL_TAP_SUPPRESS_MS   300    // keep taps suppressed this long after tilt ends / flip commits
#define CAL_TILT_SUPPRESS_G   0.5f   // horizontal gravity component that counts as "tilting" (0.5 g ~ 30 deg)
#define FAST_TILT_ALPHA       0.1f   // fast low-pass for the tilt detector (tau ~10 ms). 0 = disable (plain brief behaviour)

// Buttons
#define BUTTON_DEBOUNCE_MS    20

// -----------------------------------------------------------------------------
//  Accelerometer (BMA253 on Accelerometer TinyShield ASD2511 Rev5) and I2C
// -----------------------------------------------------------------------------
#define ACCEL_I2C_ADDR        0x18
#define ACCEL_CHIP_ID         0xFA
#define ACCEL_SAMPLE_HZ       1000   // BMA253 bandwidth 500 Hz -> 1000 Hz data rate
#define G_PER_COUNT           (16.0f / 4096.0f)   // +-8 g, 12-bit
#define ACCEL_POLL_MS         3      // drain the 32-frame FIFO this often (frames arrive at 1 kHz)
#define ACCEL_REPROBE_MS      5000   // v1.4 A4: look for a missing accelerometer every 5 s
#define ACCEL_FAIL_LIMIT      3      // consecutive failed transfers before the sensor is marked missing
// I2C backend (brief v1.5 FW-Q32). 0 = CCNode's own bit-banged I2C on the SDA/SCL pins, where every wait times out
// and a stuck bus is recovered (default). 1 = Arduino Wire at 400 kHz (no timeouts; watchdog only).
#define I2C_USE_WIRE          0
#define I2C_HALF_PERIOD_LOOPS 14     // soft I2C speed: half-period busy loop (~250 kHz at 48 MHz)
#define I2C_STRETCH_TIMEOUT_US 1000  // longest clock stretch / bus wait before a transfer is abandoned

// -----------------------------------------------------------------------------
//  Watchdog (v1.4 A4)
// -----------------------------------------------------------------------------
#define WATCHDOG_ENABLE       1      // ~2 s; fed every main-loop pass. A stall resets the node.

// -----------------------------------------------------------------------------
//  BLE
// -----------------------------------------------------------------------------
#define BLE_NAME_PREFIX       "CCNODE-"
#define BLE_ADV_FAST_MS       100    // v1.4 A5: advertising interval for the first 30 s
#define BLE_ADV_SLOW_MS       500    // ...then this, to save battery
#define BLE_ADV_FAST_PERIOD_MS 30000
#define BLE_ADV_RETRY_MS      250    // retry interval if starting advertising fails
#define BLE_CONN_INT_MIN      6      // x1.25 ms = 7.5 ms
#define BLE_CONN_INT_MAX      12     // x1.25 ms = 15 ms
#define BLE_CONN_LATENCY      0
#define BLE_CONN_TIMEOUT      400    // x10 ms = 4 s
#define BLE_CONN_UPDATE_DELAY_MS 1000   // wait after connect before asking for a short interval
#define BLE_SUBSCRIBE_WARN_MS 2000     // debug warning if the hub connects but doesn't subscribe
#define TX_QUEUE_LEN          16

// -----------------------------------------------------------------------------
//  Hub liveness and battery
// -----------------------------------------------------------------------------
#define NO_HUB_MS             6000   // link up but no hub message this long -> "NO HUB" in the status area
#define BAT_PERIOD_MS         60000  // BAT:<percent> after the HELLO sync and then every 60 s
#define BAT_PIN               A4     // TinyScreen+ battery divider input (TinyCircuits' getBattVoltage())
#define BAT_DIVIDER           0.5f   // divider ratio on BAT_PIN
#define LOW_BAT_PCT           20     // battery icon in the status area at or below this (brief v1.5 FW-Q30: = hub's battery_warn_percent)

// -----------------------------------------------------------------------------
//  Display
// -----------------------------------------------------------------------------
#define DISPLAY_BRIGHTNESS    10     // 0..15
#define DISPLAY_FLIP          0      // 1 = rotate display 180 deg (buttons follow, so B: bits stay screen-relative)
#define DISPLAY_OFF_FACE_DOWN 1      // master switch: honour DOFF:1 (the default is DOFF:0, display always on)
#define DISPLAY_ON_DELAY_MS   10     // OLED boost converter settle time when switching the display back on
#define RENDER_BUDGET_US      1000   // max time spent drawing per main-loop pass
#define SPLASH_MS             3000   // v1.4 A1: boot splash (fw, ID, ACC, live zAvg + UP/DOWN, BLE)
#define SPLASH_REFRESH_MS     200    // live zAvg refresh on the splash
#define TOAST_MS              1500   // "DEBUG ON/OFF" message in the hint line
#define BLINK_MS              1000
#define BLINK_PHASE_MS        125
#define CARD_STEP_NUMBERS     0      // optional step numbers 1..4 in the blocks (off by default, brief v1.1)
#define CARD_MAX_CELL         20     // card cells scale to fit the card area, never larger than this (px)
