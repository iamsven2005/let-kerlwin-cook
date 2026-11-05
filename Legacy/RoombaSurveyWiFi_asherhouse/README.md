# Roomba Wi-Fi Survey (Wi-Fi version)

- **TinyScreenRoomba/**: robot sketch for the TinyScreen+ with the Wi-Fi TinyShield and
  the IR Wirelings. Measures Wi-Fi signal (FAST / SLOW), tracks position, avoids
  obstacles, and sends everything to the PC over Wi-Fi.
- **RoombaMapper/**: Windows app. Draws the path, heat map and obstacles.

## Hardware
TinyScreen+ + Wi-Fi TinyShield + Wireling Adapter.
IR Emitter on Wireling port 0, IR Receiver on port 1. Don't use port 3 (the Wi-Fi shield uses A3).

## Arduino
1. The `TinyScreenRoomba` folder must contain exactly these three files:
   `TinyScreenRoomba.ino`, `wifi_logic.h`, `wifi_logic.cpp`. Don't copy `wifi_signal.ino`
   or `UART.ino` in (two files with setup()/loop() won't compile together).
   The Wi-Fi network name, password and roaming defaults are at the top of `wifi_logic.cpp`.
2. Libraries: TinyScreen, WiFi101, Wireling.
3. In `.vscode/arduino.json` set `"sketch": "TinyScreenRoomba.ino"`.
4. Upload.

## App
1. The PC must be on the same Wi-Fi network as the robot.
2. `dotnet run` in `RoombaMapper`, Source = **Wi-Fi (UDP)**, port 4210, press **Connect**.
3. When Windows Firewall asks, allow it on **Private networks**. If your network is set
   to Public in Windows, data will be blocked: switch it to Private.
4. The status line says "Receiving from robot at ..." once data arrives.

If nothing arrives, put the PC's IP (from `ipconfig`) into `PC_IP` in the sketch instead
of broadcasting.

## Controlling the robot from the app
The robot starts **paused**. Once the app shows "Receiving from robot at ...", the
blue Robot row is enabled:

| Control | What it does |
|---|---|
| Start survey | Drives on its own, avoiding obstacles, measuring Wi-Fi |
| Pause | Stops on the spot (still measures, like a handheld meter) |
| Return to start | Drives back to Home (0,0), faces the starting direction, pauses |
| Measure here | Stops, averages 20 readings, puts that value on the map, carries on |
| Forward / Back | Moves 20 cm (no sensor at the back, so check behind first) |
| Left / Right | Turns 45 degrees |
| Wi-Fi mode | FAST (reading every 0.3 s) or SLOW (stop every 30 cm to average) |
| Speed % | 20 to 200 % of normal speed |
| Find better AP | Scans for a stronger access point with the same network name and switches to it |
| Set start here | Makes the current position the new Home and clears the map |
| Right-click map | "Send robot here": drives to that point, then pauses |

Every command is confirmed by the robot; the app resends it automatically if the
confirmation doesn't arrive, and tells you if the robot never replies.

## Robot screen buttons (still work)
Upper-left: reset position · Upper-right: pause/run survey · Lower-left: FAST/SLOW ·
Lower-right: measure here.

## AP roaming
If your network has several access points (mesh / extenders) with the same name, the
robot switches to a clearly stronger one (8 dB or more) whenever its signal drops below
-50 dBm. It stops for about 2 s to scan. Scans happen every 3 s when paused and every
15 s while surveying, never while measuring or driving to a point. Settings are near
the top of the sketch (ROAM_...). The app shows which AP the robot is on.

## Mapping phases (purple "Mapping" row in the app)

Always start the robot from the **same spot, facing the same way**. All maps are
measured from that spot, so it's the robot's "dock".

1. **Map room**: the robot drives to the nearest wall and follows the walls round the
   room. When it's back where it started, the app turns the trace into corners with
   wall lengths, and the robot goes home. How it follows walls depends on the
   **IR sensors** setting in the app:
   - **Front only** (default): drive straight until something is in front, turn left,
     repeat. Fine for plain rectangular rooms. Tick **Probe wall** for rooms with
     doorways or furniture sticking out: it then turns right every 15 cm to check the
     wall is still there (slower, but it notices where walls end).
   - **Front + right / Front + left**: the side sensor watches the wall continuously, so
     it follows every corner smoothly without stopping. Wire the side receiver to
     Wireling port 2 and its emitter to port 0 together with the front emitter.
2. **Find obstacles** and 3. **Wi-Fi survey** both use the same serpentine planner:
   - Lanes run along the longer side of the room, a fixed distance apart (35 cm in
     phase 2, the "Survey lane spacing" setting in phase 3). The robot drives a lane end
     to end, then starts the next lane from its nearer end, so it goes back and forth.
   - **Obstacle in the way**: the robot stops, traces round it (probing with the front
     sensor), and its outline goes on the map. Then the remaining lanes are re-planned
     from scratch around it, and the robot carries on: first the rest of the lane it was
     on (the far side of the obstacle), then the next lanes as before.
   - If an obstacle can't be traced all the way round, the part it saw is blocked off
     with a 10 cm margin so the robot doesn't keep poking at it.
   - The app keeps a coverage map: **green** = done, **amber** = skipped (couldn't be
     reached, or failed 3 times). Done and skipped areas are never planned again, so
     there are no repeated attempts and no endless loops. The dashed purple line is the
     plan for everything still to do, and it changes whenever the map does.
   - Phase 3 clears the old path and heat map first, measures Wi-Fi along the lanes
     (FAST: while driving, SLOW: stops every 30 cm), and offers to export the heat map
     at the end. Both phases finish by going home.

**Save map** writes two files: `name.json` (room + obstacles) and
`name_room_only.json` (just the room). **Load map** opens either. Load the room-only
file to re-run phase 2 after furniture has moved, or the full one to repeat phase 3
without mapping again. **Stop** pauses the robot and ends the phase.

Testing without motors: set `SIMULATE_IR 1` in the sketch. The robot then drives
around a virtual 2.1 x 2.4 m room with an alcove at the top right (edit `ROOM_POLY`
to change its shape) and one box in the middle. Set Speed % to 200 to make it quicker.
With `SIMULATE_IR 0` and virtual wheels, the robot isn't really moving, so the map only
shows when you put your hand in front of the sensor, not your real room.

## Wi-Fi readings and settings

Wi-Fi readings are kept in a cache on the robot (300 readings, with where each was
taken) and sent to the app one at a time. Each stays on the robot until the app
confirms it, so readings aren't lost if Wi-Fi drops for a moment. The robot's top line
shows how many are waiting (Q).

Settings can be changed from the **Robot command** box in the app (same keys as the
stand-alone Wi-Fi module), for example:

    SET,ssid,MyNetwork        SET,pass,MyPassword      (the robot reconnects)
    SET,fast_ms,300           SET,slow_ms,150          SET,slow_n,20
    SET,roam_dbm,-50          SET,roam_margin,8
    SET,roam_check_ms,3000    SET,roam_cool_ms,10000
    SET,ack_ms,500            SET,display,0            SET,mode,S

## Walls get more accurate as the robot drives

A real robot can't drive through a wall. So if it's ever beyond one of the mapped room
walls, or its sensor hits something beyond one (while facing that wall), the wall
must really be further out, and the app moves it out to there. Walls only ever move
outwards. Saved maps keep the adjusted walls.

With virtual wheels and the real sensor (desk testing), nothing physically stops the
robot, so walls can get pushed further than they really are. That can't happen once
it's driving for real.

## Clearance and no-go areas

Routes keep the robot's centre at least **Clearance** away from walls and obstacles
(default 13 cm: the robot's 8 cm radius plus 5 cm). Bigger = safer but less space to
drive through; in a small room with several obstacles, keep it small. Tick
**Show no-go areas** to see where routes can't go: dark = something is there,
light = too close to it.
