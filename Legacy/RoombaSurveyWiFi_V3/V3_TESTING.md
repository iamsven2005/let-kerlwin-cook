# V3 testing record

Machine: Windows 11, .NET SDK 8.0.427. Display: 2880×1800 at **200 %** scaling, which is 1440×900 logical. Date: 12 Nov 2026. Branch: `feat/roomba-v3-ui` at `9b7d37f`, with V3 untracked.

## Commands run (from the repository root) and their results

```text
$ dotnet restore .\Legacy\RoombaSurveyWiFi_V3\RoombaMapper.csproj
  All projects are up-to-date for restore.
$ dotnet build .\Legacy\RoombaSurveyWiFi_V3\RoombaMapper.csproj -c Debug
Build succeeded.
    0 Warning(s)
    0 Error(s)
$ dotnet run --project .\Legacy\RoombaSurveyWiFi_V3\Tests\App.Tests.csproj
Glove feature parsing, control gating and debug routing tests passed.
Frontier simulation passed: 390 cells, 455 steps, 0 collisions.
Single-gap simulation passed: 451 cells, 512 steps.
WinForms frontier simulation action and telemetry passed.
V3 shell: navigation, command wiring, gating, labels, clear-map guard and log filter passed.
V3 shell: closing while a simulated phase stops passed.
exit code: 0
$ dotnet run --project .\Legacy\RoombaSurveyWiFi_V3\RoombaMapper.csproj
  Process RoombaSurveyV3 started; window title "Roomba Survey V3 — Wi-Fi Survey Mapper".
  Captured with PrintWindow. Closed through CloseMainWindow and exited cleanly.
```

Baseline before any change (the unmodified copy): build succeeded with 0 warnings, and the first four test lines above passed.

The new assertions were mutation-checked: expecting `RUNX` for Start survey fails with `_runBtn must send RUNX; sent: C,59249001,RUN`.

### Repository checks

```text
$ git status --short
?? Legacy/RoombaSurveyWiFi_V3/
$ git diff --check
(clean; no tracked file changed)
```

`diff -rq App Legacy/RoombaSurveyWiFi_V3` lists only these files as different:

- `MainForm*.cs`
- `MapView.cs`
- `RoombaMapper.csproj`
- `Tests/Program.cs`

These were added: `MainForm.Layout.cs`, `MainForm.Log.cs`, `Ui/`, and the three Markdown files.

`Core/`, `Sources/`, `Telemetry.cs`, `RobotLink.cs`, `SimRobot.cs`, `SimRooms.cs`, `GloveFeatures.cs`, `Program.cs` and `Tools/` are identical to App/.

Nothing under these paths was modified:

- `App/`
- `SurveyFirmware/`
- `Legacy/RoombaSurveyWiFi_asherhouse/`
- other `Legacy/` projects
- the STL files
- the root README or `.gitignore`

## What the automated V3 tests cover (`Tests/Program.cs`)

- **Visible controls and badge**: Connect, frontier simulation, Debug pages and STOP are visible on the landing page. The title contains "V3". The badge reads NOT CONNECTED, REAL HARDWARE or SIMULATION as appropriate.
- **Pages**: each of the 5 pages shows alone. The single map instance stays visible. Navigation never connects and never sends a `C,` command.
- **Command literals**: with a capturing source that can send, every robot control sends exactly its original literal:
  - `RUN`, `PAUSE`, `HOME`, `MEASURE`, `ROAM`
  - `STEP,FWD`, `STEP,BACK`, `STEP,LEFT`, `STEP,RIGHT`
  - `MODE,SLOW`, `SPEED,120`, `SENSORS,FL`, `SENSORS,F`, `PROBE,ON`
  - `GOTO,1234,-57`, plus the target marker
  - STOP → `PAUSE`
  - a typed command with its `C,` prefix removed
  - a password command, which stays masked in the log
  - glove `TURN,90`
- **Probe gating**: the probe checkbox is disabled with side sensors.
- **Data age**: older than 3 s shows STALE; fresh data shows receiving.
- **Clear map**: cancelling keeps the data; confirming clears it and sends nothing.
- **Layers**: the heat map and path toggles change the `MapView` properties.
- **Log**: the category filter, case-insensitive search and live appends to the filtered view all work, while the full log keeps every line.
- **Source fields**: each source shows only its own fields. USB Serial with no port reports "No COM port selected" and doesn't connect.
- **Honest labels**: Bluetooth LE is shown as receive-only with robot controls disabled. Simulation is labelled SIMULATION. After disconnecting, the badge reads NOT CONNECTED, the phase hint says to connect, and the phases are disabled.
- **Closing mid-phase**: closing the window while a simulated phase is stopping raises no UI-thread exception. This was a real V3 regression, found and fixed (see below).

## Visual checks with a real renderer

Run `dotnet run --project .\Legacy\RoombaSurveyWiFi_V3\Tests\App.Tests.csproj -- --screenshots <folder>`.

The harness is DPI-aware like the app. It renders the real form with `DrawToBitmap` at **logical** sizes. It connects Simulation, visits each page, filters the log, disconnects, then runs the simulated robot's Phase 1 and STOP.

The 15 images from the final run were reviewed by eye. Results on this 200 % display:

| Logical size | Stands in for | Result |
|---|---|---|
| 1366×768 | 1366×768 at 100 % | Fits. The map is about 52 % of the width; nothing clips. |
| 1280×720 | 1920×1080 at 150 % | Fits. |
| 1455×864 | 1920×1080 at 125 % is 1536×864; capped by this screen | Fits. |
| 1093×614 | 1366×768 at 125 % | The page panel auto-hides and the map keeps most of the width. Reopen it from the rail. |

At 96 dpi (earlier runs in a DPI-unaware process, which is the 100 % layout), all pages rendered correctly after the fixes listed below.

### Problems found by these checks and fixed

- Inspector values were clipped. They now wrap, using `PropertyTable` with its own layout.
- Drive-pad text wrapped, and the Wi-Fi mode combo was too narrow.
- The map toolbar wrapped. The inspector hide button moved into the inspector.
- Number inputs collapsed when laid out on a hidden page.
- The header link text was squashed by TableLayoutPanel. It now uses explicit layout.
- **At 200 %**: auto-size buttons and panels came out about 3–4× too large. Cause: they defaulted to `GrowOnly`, which keeps the size DPI scaling gave them. They now use `GrowAndShrink`.
- At 200 %, the badge was scaled twice.
- At 200 %, splitter limits were not scaled.
- At 200 %, `MapView`'s legend, map key, scale bar and labels overlapped, because the geometry was in fixed pixels while the fonts scaled. The geometry is now multiplied by DPI/96, so output at 100 % is unchanged. App/ has the same overlap at high DPI.
- On hover, the cursor readout made the toolbar wrap and moved the map. It now lives in the log header.
- After the window closed mid-phase, a late telemetry update hit disposed controls (NullReferenceException). Fixed with a guard and by stopping the UI clock on close. This is now covered by a test, and 10 repeated runs were clean.

## Not tested: needs hardware or a person

| Check | Why not tested |
|---|---|
| Real USB Serial / Bluetooth LE / Wi-Fi UDP robot: HELLO, `A,<seq>`, ACKs, retries, motors | No TinyScreen+ robot attached. The transport code is unchanged (byte-identical). |
| Connecting UDP or BLE with no device present | Not automated: binding a UDP port can raise a Windows Firewall prompt, and BLE starts a real radio scan. Manual check 3. |
| Glove calibration, walks and survey guide with a real glove | No glove attached. Gating and literals are tested. |
| File dialogs: Export CSV, Save image, Export heat map, Save map, Load map, debug Record to CSV | Modal dialogs. The code is unchanged. CSV quoting is tested through the writer. |
| Set start here confirmation; phase completion prompts | Modal dialogs. In 30 s (and 150 s in a separate timing run), the simulated robot's Phase 1 did not finish (see below), so no prompt appeared. |
| True 125 % and 150 % Windows scaling; 1920×1080 at 100 % | This screen is 200 % and too small for a 1920×1080 logical window. Covered by the logical-size renders above. |

## Manual checklist (please run before promotion)

1. Launch V2 (`Legacy/RoombaSurveyWiFi_asherhouse/RoombaMapper`) and V3 side by side. The titles differ, and the V3 header says "Roomba Survey V3".
2. **Simulation**: Connect. The badge reads SIMULATION, and the heat map and path grow. Do Export CSV and Save image and open the files. Disconnect: the inspector says the values are not live. Reconnect.
3. **Sources**: select USB Serial (check Refresh), Bluetooth LE (edit the name) and Wi-Fi UDP (port and IP). Connect with no device: the status is honest, nothing crashes, and Diagnostics → Awaiting reply stays 0.
4. **Run frontier simulation**: telemetry arrives and the badge reads SIMULATION.
5. **Simulated robot (no hardware)**: run Phase 1 to the end (slow, see below). The outline prompt appears if the outline is incomplete. Phases 2 and 3 unlock. STOP cancels. Save map, then Load map.
6. **Real robot over Wi-Fi**: the badge reads REAL HARDWARE, data age is shown, and the command path opens after the first data. Check each Robot-page button, right-click "Send robot here", and "Set start here" (confirm/cancel). Unplug the robot: the header turns STALE after 3 s.
7. **Glove**: the buttons follow its F line. Do I'm here, calibration, room walk and guide.
8. **Debug pages**: open, cards update, Record to CSV, close (hides; reopening keeps the cards).
9. **Layout**: resize to the minimum window size. The map stays visible, the page panel auto-hides, and ‹ › bring the panels back. Also try Ctrl+1…5, and the Tab key through the controls.

## Pre-existing observations (not changed: outside UI scope)

1. **The simulated robot is very slow, and STOP takes seconds to settle.** I ran the same flow against an unmodified copy of App/ in a temporary folder (App/ itself was not touched):
   - after 150 s, Phase 1 was still running, with 34 telemetry updates (baseline) and 103 (V3);
   - STOP took 18.4 s to settle in the baseline and 7.5 s in V3.

   The simulation runs on the UI thread in `SimRobot`/`Core`. This is not a V3 regression.
2. **AUTOROAM after a stopped phase.** Phases 1 and 2 send `AUTOROAM,OFF` (`Core/Phases.cs:29`, `:187`). Only Phase 3 sends `AUTOROAM,ON` (`:674`), at its start and not in a `finally`. After Phase 1 or 2 alone, or a stopped or failed phase, the robot's automatic roaming stays off until Phase 3 runs. V2 restored it in a `finally`. Fixing this would change Core logic, so it needs separate approval.
3. **V2 map files** (`RoomShapeId`/`Shapes`) are rejected by App/ and V3 ("old map format?"). There is no migration path.
4. **`MapView` takes keyboard focus whenever the mouse enters it**, so moving the mouse over the map while typing in the command box moves focus. Behaviour kept as in App/.
5. **Starting the frontier simulation or the simulated robot clears the map without asking.** This is App/ behaviour, kept. Only the explicit Clear map button gained a confirmation.

## Future ideas (not shipped: need data, tests or approval)

- Confirm before a source switch discards an unsaved survey (observation 5).
- Export the event log to a file. Today there is only Copy, because no export existed before.
- A keyboard shortcut for STOP. It was left out to avoid surprise sends.
