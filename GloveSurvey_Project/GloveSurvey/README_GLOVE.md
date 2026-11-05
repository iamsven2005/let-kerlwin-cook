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
"I'm here" corrections on the map. The heading is in one place in the sketch
(`headingNow()` / `headingTurn()`), so a gyroscope can replace it later.

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

## Files (one feature per file pair: .h = what it offers, .cpp = how)
| File | Feature |
|---|---|
| `GloveSurvey.ino` | setup() and loop() only |
| `config.h/.cpp` | every setting with its default (most changeable from the app: `SET,<key>,<value>`) |
| `accel_bma250.h/.cpp` | Accelerometer TinyShield (BMA250) driver |
| `motion.h/.cpp` | steps, walking vs still, hand tilt |
| `heading.h/.cpp` | which way the PERSON faces (turn buttons now; a waist gyro later: change only this file) |
| `tracker.h/.cpp` | position (steps x step length along the heading), step calibration |
| `hand_pose.h/.cpp` | hand hanging / pointing / other |
| `ir_sensor.h/.cpp` | IR YES/NO and plotting hits in the sensor's direction |
| `readings.h/.cpp` | Wi-Fi readings FAST/SLOW and the confirmed cache |
| `link.h/.cpp` | sending to / receiving from the app |
| `commands.h/.cpp` | commands and SET keys from the app |
| `buttons.h/.cpp` | the four buttons |
| `screen.h/.cpp` | the display |
| `wifi_logic.h/.cpp` | Wi-Fi network list (home/school), connecting, roaming |

All of them must be in the `GloveSurvey` folder; the Arduino IDE shows them as tabs.
To debug one feature, open its tab: e.g. steps not counting -> `motion.cpp`,
obstacle in the wrong place -> `ir_sensor.cpp`, app not receiving -> `link.cpp`.

## Arduino setup
1. Arduino IDE, board **TinyScreen+**. Libraries: TinyScreen, WiFi101, Wireling.
   (The accelerometer needs no library: the sketch has its own small BMA250 driver.)
2. Open `GloveSurvey/GloveSurvey.ino` (folder also holds `wifi_logic.h/.cpp`).
3. Networks: top of `wifi_logic.cpp`, in the list `wifiProfiles`. Put your home network
   (user left as "") and your school network (with its username). The glove joins
   whichever is in range, so it works at home and at school without re-uploading:
   ```
   { "NOKIA-6AA1", "",           "358n8An7tS"    },   // home: password only
   { "SchoolWiFi", "your-login", "your-password" },   // school: username + password
   ```
   School limits (the Wi-Fi chip's): username up to 20 characters, password up to 40,
   PEAP/MSCHAPv2 type only, no certificate checking. If your school login is longer,
   or the school needs a certificate or a web login page, the glove can't join it:
   use a phone hotspot (laptop and glove both on it) or the USB cable (source "USB Serial").
   Also: many school networks stop devices talking to each other. If the glove joins but
   the app sees nothing, that's why - use the hotspot or USB.
   You can also switch network from the app's command box without re-uploading:
   `SET,ssid,Name` `SET,user,login` (or `SET,user,` for none) `SET,pass,password` `SET,join,1`
4. Upload. The screen shows the signal, "Facing", steps, X/Y and the IR/hand state.
   "No accelerometer!" = check the Accelerometer TinyShield is seated.

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
