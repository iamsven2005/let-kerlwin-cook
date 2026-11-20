# Roomba Survey V3

A user-interface revision of the current robot console (`App/`), kept in its own folder so you can compare it side by side before anything is promoted. It is a WinForms / .NET 8 app, like App/. It adds no packages.

## Run it

From the repository root:

```powershell
dotnet run --project .\Legacy\RoombaSurveyWiFi_V3\RoombaMapper.csproj            # V3 (RoombaSurveyV3.exe)
dotnet run --project .\Legacy\RoombaSurveyWiFi_V3\Tests\App.Tests.csproj         # tests
dotnet run --project .\Legacy\RoombaSurveyWiFi_asherhouse\RoombaMapper\RoombaMapper.csproj   # V2, for comparison
dotnet run --project .\App\RoombaMapper.csproj                                   # current App
```

The apps run side by side. V3's assembly is `RoombaSurveyV3` and its title is "Roomba Survey V3 — Wi-Fi Survey Mapper".

Two robot sources cannot listen on the same UDP port (4210) at once. To compare live data, connect only one app, or give one app another port.

## Layout

| Area | What's there |
|---|---|
| Header | Source badge (REAL HARDWARE / SIMULATION / NOT CONNECTED), link status with the age of the last data, Debug pages, **STOP** |
| Connection bar | Source, port/name/IP fields, Connect, Run frontier simulation |
| Left rail (Ctrl+1…5) | **Missions**: phases 1–3, lane settings, map files. **Robot**: firmware survey, manual drive, mode/speed/sensors, set start. **Wearable**: glove. **Diagnostics**: raw command, link counters. **Settings**: robot geometry |
| Centre | The map, its toolbar (fit, zoom, exports, clear), and the event log with filter, search and copy |
| Right | Inspector: status, mission progress, robot, Wi-Fi, sensors, map layers and signal scale |

To give the map more room:

- **Page panel**: click the active page name, or ‹. It also hides automatically on narrow windows.
- **Inspector**: hide it with ›.

STOP cancels the running phase and sends `PAUSE`. It is a software stop over the link, **not** a hardware emergency stop.

## Intentionally unchanged from App/

- Robot protocol and command strings (`C,<id>,…`, HELLO, `A,<seq>`, PING, retries).
- Telemetry parsing and all of `Sources/` (USB, Bluetooth LE, UDP, simulations).
- `Core/` mapping, navigation, planning and coverage; `SimRobot`; `RobotLink`.
- File formats: survey CSV, map JSON (+ `_room_only`), PNG exports, debug CSV.
- Feature gating, phase prerequisites and every confirmation App/ already had.

Byte-identical copies are listed in V3_TESTING.md.

The only `MapView` changes are:

- a `ZoomBy` method for the toolbar buttons;
- overlay geometry (legend, key, scale bar, labels) scaled with display DPI.

At 100 % scaling the map draws exactly as before.

See **V3_FEATURE_PARITY.md** for the feature-by-feature matrix and **V3_TESTING.md** for test results, manual checks and known pre-existing issues.

## Files added or changed in V3

- `MainForm.Layout.cs`: the shell. It arranges the existing controls and adds read-only status displays.
- `MainForm.Log.cs`: log filter, search, copy and collapse.
- `Ui/Theme.cs`: colours, fonts and styling helpers.
- `Ui/Components.cs`: section card, property rows, badge, colour swatch, signal scale, navigation rail.
- `MainForm.cs`, `MainForm.Mission.cs`, `MainForm.Glove.cs`, `MainForm.Debug.cs`: layout code replaced. Event handlers and commands are kept.
- `MapView.cs`: `ZoomBy`, and DPI-scaled overlays.
- `RoombaMapper.csproj`: `AssemblyName` set to `RoombaSurveyV3`.
- `Tests/Program.cs`: V3 tests and the `--screenshots` harness.

Promotion into `App/` has not been done. It needs a separate decision after review.
