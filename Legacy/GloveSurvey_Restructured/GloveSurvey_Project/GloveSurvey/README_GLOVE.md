# Survey glove (right hand)

## What the hardware can and can't do
| Wanted | With the BMA250 accelerometer alone |
|---|---|
| Steps / distance walked | Yes: step peaks at the wrist x calibrated step length (~5-10 % error) |
| Walking vs standing still | Yes |
| Hand tilt (hanging vs raised) | Yes (direction of gravity) |
| Which way the PERSON faces / turns | **No.** Turning about the vertical doesn't change gravity; integrating acceleration drifts within seconds |
| Distance to an obstacle | No: the IR receiver only says YES/NO (hits are placed at 0.6 x the IR range) |

So the person's heading comes from the person: turn buttons (glove or app) and
"I'm here" corrections on the map. Where the heading comes from is one setting,
`HEADING_SOURCE` in `src/core/features.h`, so a gyroscope can replace the buttons later
(write `src/features/heading_gyro.cpp`; nothing else changes).

**Minimum hardware for automatic heading:** a gyroscope (6-axis IMU) worn at the waist
or chest (not the hand), e.g. an IMU Wireling on a long Wireling cable clipped to a belt.
A magnetometer alone is unreliable indoors.

## Hardware and wiring
TinyScreen+ with, stacked: Wi-Fi TinyShield, Accelerometer TinyShield (ASD2511-R-A,
I2C A4/A5), Wireling Adapter. IR Emitter Wireling on port 0, IR Receiver Wireling on
port 1 (not port 3: the Wi-Fi shield uses A3). Mount the board on the back of the right
hand. Mount the IR pair on the **outer (little-finger) side** of the hand, pointing
outwards: with the arm hanging relaxed it then points to your right, at the wall beside
you; with the hand raised it points ahead.

## How the sketch is organised
`GloveSurvey.ino` only starts the main loop. Everything else is in `src/`:

| Folder / file | What it is |
|---|---|
| `src/core/features.h` | **switch each feature on (1) or off (0)** - the only place to do it |
| `src/core/main_loop.cpp` | the main loop: the feature table, run in order on every pass |
| `src/core/config.h/.cpp` | every setting with its default (most changeable from the app: `SET,<key>,<value>`) |
| `src/core/state.h/.cpp` | values the features pass to each other (each has one owner) |
| `src/core/events.h/.cpp` | things that happen (a step) and who listens |
| `src/core/debug.h/.cpp` | debug pages: the glove-screen pager and sending pages to the app |
| `src/core/secrets.h` | your Wi-Fi networks and passwords (copy `secrets_example.h`; not committed) |
| `src/features/*.h/.cpp` | one file pair per feature: `.h` = what it offers, `.cpp` = how |

### The features
| Switch (`features.h`) | File | What it does | Needs |
|---|---|---|---|
| `FEAT_ACCEL` | `accel` | accelerometer driver, gravity (hand tilt) | - |
| `FEAT_STEPS` | `steps` | step counting | ACCEL |
| `FEAT_STILL` | `still` | standing still vs walking | ACCEL |
| `FEAT_HAND_POSE` | `hand_pose` | hand HANG / POINT / OTHER | ACCEL |
| `HEADING_SOURCE` | `heading_buttons` / `heading_gyro` | which way the person faces (a choice, not on/off) | - |
| `FEAT_TRACKER` | `tracker` | position = steps x step length along the heading | STEPS |
| `FEAT_STEP_CAL` | `step_cal` | step-length calibration | TRACKER |
| `FEAT_IR` | `ir_sensor` | IR YES/NO | - |
| `FEAT_IR_PLOT` | `ir_plot` | IR hits to the app as obstacle points | IR, HAND_POSE, TRACKER, a link |
| `FEAT_WIFI` | `wifi` | Wi-Fi connection, network profiles | - |
| `FEAT_READ_FAST` | `read_fast` | FAST readings while walking | WIFI, a link |
| `FEAT_READ_SLOW` | `read_slow` | SLOW averaged readings while still | WIFI, STILL, a link |
| `FEAT_READ_CACHE` | `read_cache` | keep readings until the app confirms (off = send once) | a readings feature |
| `FEAT_ROAM` | `roaming` | look for a stronger access point while still | WIFI, STILL, a readings feature |
| `FEAT_LINK_UDP` | `link_udp` | app link over Wi-Fi | WIFI |
| `FEAT_LINK_SERIAL` | `link_serial` | app link over USB | - |
| `FEAT_TELEMETRY` | `telemetry` | the P line 5 times a second | a link |
| `FEAT_COMMANDS` | `commands` | commands and SET keys from the app | a link |
| `FEAT_SCREEN` | `screen` | the display | - |
| `FEAT_BUTTONS` | `buttons` | the four buttons | SCREEN |
| `DEBUG_SCREEN` | `debug`, `screen` | each feature's debug page on the glove screen | SCREEN |
| `DEBUG_TO_APP` | `debug` | ...and in the app's debug panel | a link |

An off feature is not compiled at all (no flash, no RAM). If you switch off something
another feature needs, the build stops with a plain message saying which. A command for
a switched-off feature is answered "... is off in this build". On HELLO the glove sends
its feature list (`F,...`) and the app disables the glove buttons it can't use.

To debug one feature, open its file: e.g. steps not counting -> `src/features/steps.cpp`,
obstacle in the wrong place -> `ir_plot.cpp`, app not receiving -> `link_udp.cpp`.

## Arduino setup
1. Arduino IDE, board **TinyScreen+**. Libraries: TinyScreen, WiFi101, Wireling.
   (The accelerometer needs no library: the sketch has its own small BMA250 driver.)
2. Copy `src/core/secrets_example.h` to `src/core/secrets.h` and put in your networks.
   The glove joins whichever is in range, so it works at home and at school without
   re-uploading. School limits (the Wi-Fi chip's): username up to 20 characters, password
   up to 40, PEAP/MSCHAPv2 type only, no certificate checking. If your school login is
   longer, or the school needs a certificate or a web login page, the glove can't join
   it: use a phone hotspot (laptop and glove both on it) or the USB cable (source
   "USB Serial"). Many school networks stop devices talking to each other: if the glove
   joins but the app sees nothing, that's why - use the hotspot or USB.
   You can also switch network from the app's command box without re-uploading:
   `SET,ssid,Name` `SET,user,login` (or `SET,user,` for none) `SET,pass,password` `SET,join,1`
3. Open `GloveSurvey/GloveSurvey.ino` and upload. The screen shows the signal, "Facing",
   steps, X/Y and the IR/hand state. "No accelerometer!" = check the Accelerometer
   TinyShield is seated.

## App setup (RoombaMapper)
Source **Wi-Fi (UDP)**, port 4210, Connect. Set **IR range R** in the purple row.
The green **Glove** row has everything else.

## Calibrate (once per session, 2 minutes)
1. **Hand poses:** let the arm hang relaxed -> **Hanging**. Raise the hand pointing straight
   ahead -> **Pointing**. The glove's bottom line shows HANG / POINT / OTHER.
2. **Step length:** mark 5 m on the floor. Stand at the start -> **Start**, walk normally
   to the end -> **Finish**. The glove reports the step length.

## Operate
- **Turning:** every time you turn, press the matching button: glove upper-left = left 90,
  upper-right = right 90 (hold > 0.6 s for 45), lower-right = turned around. Or the app's
  ↶ / ↷ / ↻ buttons. The map arrow is YOUR facing, whatever your hand does.
- **I'm here:** whenever the dot is off, press 📍, click where you stand, then click a
  point in the direction you face.
- **Room walk** (map the room): stand in a corner area facing along a wall, **Start**. Walk
  round the room with the walls on your RIGHT, arm hanging ~20-30 cm from the wall, pressing
  a turn button at each corner. Back at the start: **Finish**. Doorways and inferred
  sections appear as with the robot.
- **Obstacle walk:** stand next to it, **Start**, walk all the way round keeping it on your
  right, arm hanging close to it, **Finish**. Classified pillar / partition.
- **Survey guide:** **Start**, walk the dashed purple lanes; green = covered. FAST: readings
  while walking. SLOW: stand still ~1 s at a spot (or press lower-left on the glove) to take
  a 20-sample average. **Finish** offers the heat-map export.
- Glove lower-right long press: pause step counting (e.g. sitting down). Lower-left long
  press: you're back at the start point (0,0).

IR hits are only plotted when the hand is HANG or POINT; otherwise the screen says so.

## Debug pages
Every feature can show what it is doing on a page of its own.

**On the glove screen** (`DEBUG_SCREEN`): hold **upper-left + upper-right together**
for more than 0.6 s to switch between the normal view and the debug view (or `DEBUG,ON` /
`DEBUG,OFF` from the app). The title bar shows the feature's name and page number
(`A` = auto-cycling). In debug view:

| Button | Does |
|---|---|
| upper-left / upper-right | previous / next page |
| lower-left | hold this page |
| lower-right | auto-cycle, 3 s per page |

Turn buttons don't work in debug view: use the app's turn buttons meanwhile.
From the app, `DEBUG,<feature>` (e.g. `DEBUG,steps`) jumps straight to a page.

**In the app** (`DEBUG_TO_APP`): press **Debug** in the top bar. The window asks the glove
for its pages (`MIRROR,ON`), shows one card per feature, greys out cards with no update for
3 s, and stops the pages again when you close it. Click a card to show that page on the
glove screen too. **Record to CSV** saves every page with a timestamp. `SET,dbg_ms,<ms>`
(the "every ... s" box) sets how often the pages are sent (default 1 s). Debug lines go out
one per packet, only when there is room, and are never cached or resent, so they don't
delay Wi-Fi readings.

## Checking the feature switches on a PC
`tools/firmware_check/matrix.py` builds the sketch with each feature switched off in turn
(and everything that needs it), the debug combinations, the minimum set and everything
off, against stand-ins for the Arduino libraries, then runs each build. Needs python3 and
g++ (Linux or WSL): `python3 tools/firmware_check/matrix.py`. It checks that combinations
compile and run; it doesn't test the hardware.
