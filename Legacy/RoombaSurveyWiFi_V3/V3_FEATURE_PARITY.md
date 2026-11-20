# V3 feature parity matrix

Baseline: `App/` at `9b7d37f` (branch `feat/roomba-v3-ui`), copied byte-for-byte into this folder before any change.
V3 must do everything App/ does. This file maps every App/ feature to where it lives in V3 and how it was checked.

## How to read the Result column

| Result | Meaning |
|---|---|
| **PASS (auto)** | Checked by `Tests/Program.cs` (run: see V3_TESTING.md). |
| **PASS (render)** | Seen working in a real WinForms render: the `--screenshots` harness or a `PrintWindow` capture of `dotnet run`. |
| **PASS (unchanged)** | The code is byte-identical to App/ (checked with `diff`), and only its parent control moved. Its behaviour is inherited, not re-tested. |
| **UNTESTED** | Needs hardware, a file dialog, or a person. Listed in V3_TESTING.md as a manual check. |

## Files identical to App/ (checked with `diff -rq`)

`Core/` (all 9 files), `Sources/` (all 7), `Telemetry.cs`, `RobotLink.cs`, `SimRobot.cs`, `SimRooms.cs`, `GloveFeatures.cs`, `Program.cs` and `Tools/` are identical to App/.

So nothing changed in:

- packet parsing
- newline assembly
- the BLE, UDP and Serial transports
- the `A,<seq>` acknowledgement
- mapping, planning and coverage
- the simulations

All 33 `SendCommand(...)`/`Send(...)` command literals and labels in the `MainForm*.cs` files are identical to App/. This was checked by extracting and diffing them.

## Connections and data paths

| Feature | App/ source | V3 location | Verification | Result |
|---|---|---|---|---|
| Source selector: Simulation, USB Serial, Bluetooth LE, Wi-Fi (UDP), Simulated robot | `MainForm()` `_source` | Connection bar | Test switches sources; renders | PASS (auto) |
| Only the selected source's fields are shown (port + Refresh, device name, UDP port + device IP) | `UpdateSourceUi` | Connection bar | Test checks each group's visibility | PASS (auto) |
| COM port list and Refresh | `RefreshPorts` | Connection bar (USB) | Code unchanged | PASS (unchanged) |
| USB with no port: honest status, no connection | `ConnectClicked` | Inspector status, log | Test clears ports, clicks Connect, checks status and that nothing connected | PASS (auto) |
| Connect / Disconnect lifecycle, `_source` locked while connected | `ConnectClicked`, `Disconnect` | Connection bar | Tests connect Simulation and Frontier, then Disconnect | PASS (auto) |
| Frontier simulation button (`FrontierSimulationSource`) | `_frontierSim` click | Connection bar, always visible | Original test: telemetry arrives | PASS (auto) |
| In-app Simulated robot (`SimRobot`, `AnimatedSim`) | `ConnectClicked` index 4 | Source list | Harness: Phase 1 runs, STOP | PASS (render) |
| Real USB Serial, BLE and UDP robot traffic | `Sources/*` | (unchanged) | Needs TinyScreen+ hardware | UNTESTED |
| HELLO on first telemetry; `A,<seq>` acknowledgement; `_pending` and retry timer (15 tries); PING every 2 s; `OnUi` marshalling | `OnTelemetry`, `ConnectClicked`, `RetryCommands`, `BuildMissionRow` | (unchanged) | Code unchanged. Tests: commands enter `_pending` as `C,<id>,...` | PASS (unchanged) |
| Error status on a failed connect (`Could not connect: … [Type, 0xHRESULT]`) | `ConnectClicked` | Inspector status, log | Code unchanged | PASS (unchanged) |

## Robot driving and surveying (Robot page; the whole `_controlRow` is enabled only when `RobotReady`)

| Feature | App/ source | V3 location | Verification | Result |
|---|---|---|---|---|
| Start survey `RUN`, Pause `PAUSE`, Return to start `HOME`, Measure here `MEASURE`, Find better AP `ROAM` | ctor handlers | Robot → Robot survey (firmware) | Test clicks each and checks the exact `C,<id>,<cmd>` sent | PASS (auto) |
| Forward, Back, Left, Right (`STEP,FWD`, `STEP,BACK`, `STEP,LEFT`, `STEP,RIGHT`) | ctor handlers | Robot → Manual drive (cross pad) | Test, exact literals | PASS (auto) |
| Wi-Fi mode FAST/SLOW (`MODE,…`) | `_modeBox` | Robot → Drive and sensing | Test sends `MODE,SLOW` | PASS (auto) |
| Speed % (`SPEED,n`; not sent for the simulated robot) | `_speedBox` | Robot → Drive and sensing | Test sends `SPEED,120` | PASS (auto) |
| IR sensors Front / Front+left / Front+right (`SENSORS,F/FL/FR`) | `_sensorBox` | Robot → Drive and sensing | Test, exact literals | PASS (auto) |
| Probe wall (`PROBE,ON/OFF`), only with front-only sensing | `_probeBox` | Robot → Drive and sensing | Test checks it is disabled with FL and sends `PROBE,ON` | PASS (auto) |
| Controls follow robot-reported mode, speed and sensors without echoing commands (`_syncingUi`) | `OnTelemetry` | (unchanged) | Code unchanged | PASS (unchanged) |
| Set start here (`RESET`), with its OK/Cancel confirmation, then clears the map and room | ctor `_resetPosBtn` | Robot → Origin | Code unchanged; the dialog needs a person | UNTESTED |
| Right-click map: "Send robot here (x m, y m)" → `GOTO,x,y`, target marker; disabled when the robot can't receive | `_mapMenu`, `_sendHereItem` | Map context menu | Test sends `GOTO,1234,-57` and checks `Target` | PASS (auto) |
| Robot controls disabled when no command path | `UpdateControlRow` | Robot page | Test: enabled with a sending source, disabled for BLE | PASS (auto) |

## Mapping phases (Missions page)

| Feature | App/ source | V3 location | Verification | Result |
|---|---|---|---|---|
| 1 Map room / 2 Find obstacles / 3 Wi-Fi survey | `StartPhase1/2/3Async`, `RunMission` | Missions → Mapping phases | Harness: Phase 1 starts with the simulated robot | PASS (render) |
| Prerequisites: robot ready, no phase running; phases 2 and 3 need a room | `UpdateMissionButtons` | Missions | Test: disabled when disconnected. Harness: Phase 2 disabled with no room | PASS (auto) |
| STOP: cancels the phase and sends `PAUSE` if possible ("software stop" in its tooltip) | `_stopMissionBtn` | Header, always visible | Test sends `PAUSE`. Harness: phase reports STOPPED | PASS (auto) |
| Progress bar, progress detail, mission status | `MissionHost`, `MissionStatus` | Inspector → Mission (always visible) | Harness render while running | PASS (render) |
| Phase prompts: re-map confirm, incomplete outline, "save maps?" after Phase 2, "export heat map?" after Phase 3, keep/discard temporary obstacles | `StartPhase*Async` | (unchanged dialogs) | Code unchanged. No phase completed in the harness window (see V3_TESTING) | UNTESTED |
| Survey lane spacing, Guard survey | `_laneSpacing`, `_guardBox` | Missions → Wi-Fi survey (phase 3) | Render; locked while running (render) | PASS (render) |
| Robot L × W, IR range R (feed `ApplyConfig` and no-go) | `_lenBox`, `_widBox`, `_rangeBox` | Settings → Robot geometry | Render | PASS (render) |
| Show no-go overlay | `_showNoGo`, `RefreshNoGo` | Inspector → Map layers ("No-go overlay") | Code unchanged; render | PASS (render) |
| Save map (`name.json` + `name_room_only.json`) / Load map | `SaveMapFiles`, `LoadMapFile` | Missions → Room map files | Code unchanged; file dialogs | UNTESTED |
| Custom robot command (Enter or Send; `C,` prefix stripped; `SET,pass` masked) | `BuildMissionRow` `sendTyped` | Diagnostics → Robot command | Test: literal sent, password masked in log | PASS (auto) |
| Keep-alive PING | `_pingTimer` | (unchanged) | Code unchanged | PASS (unchanged) |

## Map, analytics and persistence

| Feature | App/ source | V3 location | Verification | Result |
|---|---|---|---|---|
| One `MapView` instance with all layers: grid, heat map, path, room, sections, obstacles, seeds, coverage, no-go, planned path, robot, home/target, legend, map key, scale bar | `MapView` | Centre | Test: same instance across pages. Renders | PASS (auto) |
| Zoom (wheel), pan (drag), Fit view, click events | `MapView`, `_fit` | Map + toolbar | Fit render; mouse code unchanged | PASS (unchanged) |
| Heat map / Path toggles drive `MapView.ShowHeat` / `ShowPath` | `_showHeat`, `_showPath` | Inspector → Map layers | Test toggles and reads the map properties | PASS (auto) |
| Clear map (display only; the room map `_world` is kept) | `_clear` → `ClearAll` | Map toolbar | Test: cancel keeps data, OK clears, no command sent | PASS (auto) |
| Export CSV (P/R/O rows, same columns) | `ExportCsv`, `BuildCsv` | Map toolbar | Code unchanged; file dialog | UNTESTED |
| Save image (PNG of the view) | `SavePng` | Map toolbar | Code unchanged; file dialog | UNTESTED |
| Heat-map export (PNG + CSV, no route or target) | `ExportHeatmap` | Phase prompts, plus a new toolbar button | Code unchanged; file dialog | UNTESTED |
| Telemetry values: state/mode/speed, position, heading, distance, wheel rotations (40 mm), RSSI dBm + quality word, AP, readings count, IR states, obstacles/seeds | `OnTelemetry`, `OnReading`, `UpdateShapeLabel`, `ClearAll` | Inspector → Robot / Wi-Fi / Sensors | Renders (simulation); same text formats | PASS (render) |
| Status line | `SetStatus` | Inspector → Status (wraps) | Renders | PASS (render) |
| Duplicate-reading filter (`_recentSeqs`) | `OnReading` | (unchanged) | Code unchanged | PASS (unchanged) |

## Wearable glove and diagnostics

| Feature | App/ source | V3 location | Verification | Result |
|---|---|---|---|---|
| Glove row: turn ±90 / 180 (`TURN,…`), I'm here (`SETPOSE`), step calibration (`CALSTEP_START/END`), hand pose (`POSECAL,HANG/POINT`), room walk, obstacle walk, survey guide | `BuildGloveRow` | Wearable page (same 14 buttons, now grouped) | Test: `TURN,90` literal. All literals identical (diff) | PASS (auto) |
| Feature gating from the glove's `F,` line (13 tagged buttons; null resets) | `ApplyGloveFeatures` | Wearable | Original test (13 buttons, stepcal disabled, tracker enabled, reset) | PASS (auto) |
| Real glove walks, calibration, guide lanes | `MainForm.Glove.cs` | Wearable | Needs a glove | UNTESTED |
| Debug pages window: cards, `MIRROR,ON/OFF`, rate, record to CSV with quoting, stale cards, glove-screen buttons, hide instead of close | `MainForm.Debug.cs` | "Debug pages" in the header | Original tests (card, CSV quoting, hide on close) | PASS (auto) |
| Raw log with status, commands and robot lines (30 000/15 000 character trim) | `AppendLog` | Event log under the map | Test: full log keeps every line while filtered | PASS (auto) |

## Test-reflected identifiers (all kept)

`_gloveRow` (still a `FlowLayoutPanel` whose direct children are the 13 tagged buttons), `_frontierSim`, `_active`, `_connect`, `_telemetrySeq`, `_debugBtn`, `_debugWindow`, `ApplyGloveFeatures`, `Disconnect`, `ConnectClicked`, `DebugWindow.OnLine`, `DebugWindow._cards`, `DebugWindow._writer`.

`_connect`, `_frontierSim` and `_debugBtn` sit in the always-visible header and connection bar. This matters because `PerformClick` only fires on visible controls.

## New in V3 (UI only)

| Feature | Data it uses | Verification | Result |
|---|---|---|---|
| REAL HARDWARE / SIMULATION / NOT CONNECTED badge, source name, last-data age, STALE after 3 s, command path (open / not yet / receive-only BLE / none) | `_active` type, `CanSend`, local receive time (`_lastDataAt`, set when lines arrive) | Tests: each badge state, stale vs receiving, BLE receive-only | PASS (auto) |
| Inspector note: "Not connected: values are the last received, not live" / "SIMULATION: generated, not measured" / "No data for N s" | Same | Renders | PASS (render) |
| Plain-language hints for why robot controls or phases are disabled | `RobotReady`, source type, `_missionCts`, `_world.Room` | Tests (BLE hint, phase hint) | PASS (auto) |
| Clear map asks first (only when there is data) | `_map.Samples/Readings/Obstacles` | Test | PASS (auto) |
| Log category filter and search, Copy (selection or shown lines). The full log is never altered | Copy of the last 5 000 lines | Test | PASS (auto) |
| RSSI colour scale with the app's quality bands (Excellent ≥ −50, Good ≥ −70), drawn with `MapView.ColorFor` | `MapView.RssiWeak/Strong/ColorFor` | Render | PASS (render) |
| RSSI colour swatch (the coloured value text was unreadable around −62 dBm) | `MapView.ColorFor` | Render | PASS (render) |
| Zoom + / − buttons (`MapView.ZoomBy`, same 0.005–5 clamp as the wheel) | — | Render; code review | PASS (render) |
| "Export heat map" toolbar button (the existing `ExportHeatmap`; refuses when there is no data) | — | File dialog | UNTESTED |
| Cursor position in metres (in the log header) | `MapView.ScreenToWorld` | Render (`dotnet run` capture) | PASS (render) |
| Link counters (telemetry lines, positions, R readings, awaiting reply, glove features) | `_telemetrySeq`, `_map`, `_pending.Count`, `_debugFeatures` | Render | PASS (render) |
| Pages: Ctrl+1…5, hide panel (‹ or click the active page), hide inspector (›), auto-hide the page panel on narrow windows | — | Renders at 1093×614 | PASS (render) |
| High-DPI: AutoScaleMode.Dpi; GrowAndShrink auto-size; MapView overlay geometry scaled by DPI (identical at 100 %) | — | Real 200 % renders and `dotnet run` capture | PASS (render) |

## Asherhouse V2 user journeys

V2 (`Legacy/RoombaSurveyWiFi_asherhouse`) is a subset of App/ in the UI: same robot row, mapping row, top bar, stats panel and log. Every V2 control has a V3 home in the tables above.

V2 behaviour that App/ dropped is **mission logic**, so V3 does not restore it:

- user-set clearance (replaced by L × W and R)
- robot-side `TRACE,ROOM/OBJ`
- `AUTOROAM` restored in a `finally`
- loading V2-format map JSON

See V3_TESTING.md, "Pre-existing observations".
