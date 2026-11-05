# Glove Survey project

Two parts:

| Folder | What it is |
|---|---|
| `GloveSurvey/` | the survey glove's Arduino sketch (TinyScreen+). Setup, calibration, operation and debug pages: `GloveSurvey/README_GLOVE.md` |
| `RoombaMapper/` | the Windows app (.NET 8, WinForms): connects to the glove or the robot, maps the room, draws the Wi-Fi heat map |
| `tools/firmware_check/` | builds the glove sketch on a PC with every combination of feature switches |

Both parts have the same structure: **a main logic loop**, **one file per feature**, and
**one switch per feature** in one place.

## Glove sketch
- Switches: `GloveSurvey/src/core/features.h` (`1` = on, `0` = off)
- Main loop: `GloveSurvey/src/core/main_loop.cpp` runs the feature table in order
- Features: `GloveSurvey/src/features/` (one `.h/.cpp` pair each)
- Every feature can show a debug page on the glove screen and in the app

## App
- Switches: `RoombaMapper/Features.cs` (`true` = on, `false` = off)
- Main loop: `RoombaMapper/AppFeature.cs` (`FeatureRunner`): every line from the glove or
  robot is parsed once, handled by the window's core (`MainForm.cs`), then passed to each
  switched-on feature in the order of the feature table (`MainForm.Features.cs`)
- Features, one file each:

| File | Feature |
|---|---|
| `MainForm.Controls.cs` | robot control row (drive buttons, mode, speed, sensors, "send robot here") |
| `MainForm.Mission.cs` | robot mapping phases, robot settings, save / load map, command box |
| `MainForm.Glove.cs` | survey glove: turns, "I'm here", calibration, room / obstacle walks, survey guide |
| `MainForm.Stats.cs` | right-hand panel values |
| `MainForm.MapLayers.cs` | heat-map and path checkboxes |
| `MainForm.Export.cs` | CSV, image and heat-map export |
| `MainForm.Debug.cs` | debug panel: every glove feature's debug page |
| `Core/Phase1MapRoom.cs`, `Phase15Revisit.cs` (placeholder), `Phase2FindObstacles.cs` (+ `Phase2.Solid.cs`, `Phase2.Cluster.cs`), `Phase3WifiSurvey.cs` | the robot's mapping phases |
| `MapView.cs` (+ `MapView.Heat.cs`, `.Raw.cs`, `.Overlays.cs`, `.World.cs`) | the map, one file per layer |
| `Sources/` | Simulation, USB serial, Bluetooth LE, Wi-Fi (UDP) |

Open `RoombaMapper/RoombaMapper.csproj` in Visual Studio 2022 (or `dotnet build`) on Windows.

## Messages between glove and app
| Line | Direction | Meaning |
|---|---|---|
| `P,...` | glove → app | telemetry, 5 times a second |
| `R,seq,...` / `A,seq` | glove → app / app → glove | a Wi-Fi reading / its confirmation |
| `O,...` | glove → app | an IR hit (obstacle point) |
| `M,text` | glove → app | a message |
| `C,id,command` / `K,id,text` | app → glove / glove → app | a command / its answer |
| `F,feature,...` | glove → app | the features compiled into the glove (sent on HELLO and FEATURES) |
| `D,feature,line\|line\|...` | glove → app | one feature's debug page (when MIRROR is on) |
