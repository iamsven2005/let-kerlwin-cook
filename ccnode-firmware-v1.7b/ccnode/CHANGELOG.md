# Changelog

## fw 1.7b

- Left and right tilts arm at 18° after 180 ms. Faster roll filtering lets a short, shallow wrist motion reach the threshold.
- The device suppresses vertical taps while a tilt is armed or settling, including when the raised edge lands on the table.
- The host tests cover shallow rolls in both directions, with a forward lean and a simulated landing impact.

## fw 1.7, second build: applies the v1.7 spec answers (FW-Q36 to FW-Q38)

- **FW-Q36 B:** `CAL_TILT_SETTLE_MS` = **150** by default. T:L/T:R are sent once the node has been level
  *and still* for 150 ms, so a pickup levelled in the hand and then lowered is not a tilt. The extra
  cancel rules (pitch > 20°, handling motion) are kept.
- **Fix found while applying it:** the handling-motion estimate reacted to knocks. A real tilt whose
  lifted edge lands back on the table with ≥ 2–4 g was rejected (and at 8 g even without the settle).
  The estimate now low-pass filters the acceleration *vector*, with each step clamped symmetrically
  (`TILT_MOTION_CLAMP_G` 0.5 g, `CAL_TILT_MOTION_ALPHA` 0.02), before measuring | |a| − 1 g |. Knocks
  average out; lifting, lowering and carrying don't. New `CAL_TILT_SETTLE_STILL_G` (0.08 g) is the
  stillness limit during the settle, which catches even a gentle lowering.
- **FW-Q37 A:** V suppression after a tilt unchanged. README: tilt-then-V needs ~300 ms between them.
- **FW-Q38 A:** confirmed, no change.
- `SPEC-FW-Q36..38` markers removed. `FW_VERSION` stays 1.7 (it follows the brief version; the spec
  is still v1.7). Tests: 402 (new: edge landing 1–12 g with and without the settle, gentle lowering,
  latency after level, tilt-then-V).

## fw 1.7: implements brief v1.7 (change request: tilt replaces side taps)

- **Tilt gesture (protocol meaning change):** `T:L` / `T:R` now mean "tilted left / right and back".
  The node must be face-up, held at a 25°–65° roll for ≥ 300 ms, then brought back to level (< 10°)
  within 3 s. The event is sent when it reaches level, ±50 ms (simulated); a quick gesture takes ~0.7 s.
  A tilt is cancelled by going past 65°, a flip, a timeout, pitching > 20° (not a roll), or handling
  motion. After a cancel, the node must be level again first. Face-down tilts are ignored. All
  thresholds are in the calibration block (`CAL_TILT_*`).
- **Side taps removed:** side knocks send nothing. The code stays behind `SIDE_TAPS_ENABLED 0` (1 =
  old side taps, tilt off; never both). The knock sign is now `CAL_TAP_X_SIGN`. `CAL_X_SIGN` is the
  tilt sign, and a value calibrated with knocks carries over (same physical sign).
- **V-tap counters** in the debug heartbeat (`# hb V cand= ok= rej= (supp= push= axis=)`), plus
  tilt commits per direction and cancels; `!VRESET` zeroes them. V-tap detection is unchanged.
- **Debug log:** `# tilt candidate / ARMED / COMMIT / CANCEL <L|R> <deg> pitch= motion= - <reason>`.
- **Tilt indicator** (optional in the brief, on: `TILT_INDICATOR 1`): ◄ / ► in the status area
  while a tilt is armed. `NO HUB` still has priority, and the arrow temporarily replaces ACC/battery.
- Optional `CAL_TILT_SETTLE_MS` (default 0 = brief): require stillness at level before committing
  (FW-Q36).
- Fix: the V counters were not initialised at construction (found by the new test).
- README: new Gestures section 5; calibration steps 4–7 now calibrate tilt; checklist rows 7–11;
  sections renumbered. `docs/screens.png` includes the tilt arrow (`tools/contact_sheet.py`).
- `FW_VERSION` = 1.7. Tests: 380 (43 new; the legacy side-tap tests still run with side taps on).
  Open questions FW-Q36–FW-Q38, markers `SPEC-FW-Q36..38`.

## fw 1.6: implements brief v1.6 (answers FW-Q27 to FW-Q35, and hub change X1)

- **FW-Q31 changed:** the BLE address is now a **static random address**, `C0:CC:00:00:<ID hi>:<ID lo>`
  (top bits 11, the rest from the node ID). It is set with `hci_le_set_random_address()` after GAP init
  (the module's own stored random address is read-only) and used for advertising. It is printed in the boot
  banner and `!INFO` as `addr=`, with `(NOT SET)` if the radio rejected it.
- **FW-Q33 changed (protocol):** after the accelerometer returns, a changed orientation is reported with
  the new silent **`FS:<0|1>`**, not `F:`. The HELLO sync still sends `F:<state>`.
- **FW-Q30:** `LOW_BAT_PCT` = 20 (the hub's `battery_warn_percent`).
- **X1:** `HINT:` documented maximum 14 chars (up to 16 accepted). The hint line falls back to the narrow
  pixel font when the text is too wide, so every 14-character hint fits (tested).
- **FW-Q35:** README note: don't use the debug chord mid-game (the hub sees the presses one by one).
- Answered `SPEC-FW-Q27` to `SPEC-FW-Q35` markers removed. `FW_VERSION` = 1.6. Tests: 337.


## fw 1.4: implements brief v1.4 (change request from the first hardware test)

Root causes of the observations
- **No serial output with `debugMode = true`:** `setup()` overwrote `debugMode` from the boot buttons, so
  the edit in code had no effect. The port was already `SerialUSB`. Now the buttons can only turn it on.
- **NO HUB while connected:** besides the hub not PINGing, the bundled STBLE had 2 receive buffers. A burst
  of hub writes left no room for command replies, so each notification (incl. `PONG`) waited out a 100 ms
  command timeout. STBLE patch 4 raises the pool to 8.
- **Accelerometer removal dropped the link:** the loop froze inside Arduino `Wire`. Replaced by a bit-banged
  I2C with timeouts and bus recovery, plus the watchdog.

A1 (blank screen): default **`DOFF:0`**. A 3 s **boot splash** (fw, ID, reset cause, ACC, live `zAvg` + UP/DOWN,
   BLE). Starting orientation from the **average of the first 200 ms**. **`CAL_Z_SIGN` default -1** (FW-Q27).
A2 (serial): one `NODE_SERIAL` (= `SerialUSB`) macro. No `while(!SerialUSB)` anywhere. Boot banner, **1 s
   heartbeat** in debug mode. **Hold all 4 buttons for 2 s** to toggle debug ("DEBUG ON/OFF" toast). README:
   board, port, baud, port change after upload.
A3 (NO HUB): BLE processed every pass, gaps measured (`blegap`/`maxgap` in the heartbeat). Every hub message
   logged with arrival time and gap. NO HUB is now a **steady indicator in the status area**. Display power-up
   no longer busy-waits 10 ms.
A4 (accelerometer): soft I2C with 1 ms timeouts and SDA recovery (`I2C_USE_WIRE 1` = old Wire). Non-blocking
   probe / reset / config. Missing after 3 failed polls, **re-probe every 5 s**, gestures resume. "ACC" in the
   status area instead of a full-screen error. **Watchdog ~2 s.**
A5 (advertising): advertising on the first loop pass after boot and right after a disconnect. **100 ms for 30 s,
   then 500 ms.** Fixed BLE address derived from the node ID (FW-Q31).
B1: new layout. **R L V F strip on top, never covered**; card area 60x41; right column colour block + hand,
   badge, status area; hint line at the bottom. MSG below the strip, BLINK below the strip.
B2: `CARD:IF:<family>:<steps>` and `CARD:FOR`. Square cards verified.
B3: `SEQ:<n>` (1-9), `SEQ:H<n>` (shown "H2"), plain `SEQ:H` ("HEAD").
B4: `HINT:<text>` (<= 16 chars) in the hint line; `CARD:` doesn't clear it.
Also: the low-battery icon (<= 15 %); the hand number shown as a digit in the colour block (FW-Q29);
`FW_VERSION` = 1.4. Tests: 327 (were 189), incl. an I2C bus + BMA253 simulation.

## fw 1.3.1: boot diagnostics and hang fix (no protocol change)
- STBLE patches 2/2b/3 (bounded IRQ loop, polling while waiting, IRQ pull-down).
- On-screen boot trace, `tools/DisplayTest`.


## fw 1.3.1: boot diagnostics and hang fix (no protocol change)

Reported: after a fresh flash, a blank screen and no serial output.
- **STBLE patch 2/2b/3** (`src/STBLE/PATCHES.md`): the BLE interrupt handler could loop forever if the
  module didn't answer or the IRQ line floated high, freezing the node at boot before anything was drawn.
  The loop is now bounded, command waits poll the IRQ line, and the IRQ pin has a pull-down.
- **Boot trace:** every startup step is drawn on screen and printed as `# boot: …` before it runs. A
  hang now shows where it happened.
- Debug mode waits up to 4 s for the serial monitor to attach before printing.
- The boot trace stays up for 1.5 s. The face-down power save is skipped for the first 5 s after boot
  and always in debug mode, so a node with a wrong `CAL_Z_SIGN` no longer looks dead. (This briefly
  overrides the `DOFF:1` default at boot; to be raised as FW-Q27 in the next Spec Query.)
- New `tools/DisplayTest/DisplayTest.ino`: minimal display + serial sketch to rule out board setting,
  upload or hardware problems.
- `FW_VERSION` = `1.3.1`.


## fw 1.3: implements brief v1.3 (answers FW-Q22 to FW-Q26)

All five answers confirmed the behaviour already in fw 1.2, so there are **no functional changes**.
- `SPEC-Q22` to `SPEC-Q26` markers removed; comments now cite brief v1.3 / FW-Q numbers. Questions use the
  `FW-Q<n>` prefix from now on (next: FW-Q27).
- README: licence section with the required note that the ST "guidance only" terms must be reviewed
  before any public release (FW-Q25). The NO HUB troubleshooting row explains that the hub now drops the
  link on missed PONGs (DISCONNECTED, then a fresh HELLO).
- `FW_VERSION` bumped to `1.3` (HELLO now reports `HELLO:<id>:1.3`).


## fw 1.2: implements brief v1.2 (SPEC ANSWER v1.2 + v1.1 changes)

Protocol
- `HELLO` is sent only when the hub enables TX notifications (CCCD write), on **every** (re-)subscribe,
  followed by exactly one `F:`, `B:` and `BAT:`. The v1.0 fallbacks (first write / 2 s timeout) are
  removed; a debug warning remains.
- New `BAT:<percent>` in the HELLO sync and every 60 s (battery.cpp, TinyScreen+ A4 divider + bandgap VCC).
- `DOFF:1/0` replaces `DISP`. Default `DOFF:1` (display may turn off face-down). `DISP:` is now unknown and ignored.
- `HAND:0` = unassigned (no number shown).
- CCCD disable clears the TX queue.

Display
- Shared card style: segment colours `E0E0E0` / `56CCF2` / `BB6BD9` (4th run reuses the 1st, FW-Q22),
  hollow start cell (no dot), arrow on the last block, scaled and centred to fit (max 20 px cells, FW-Q23).
- "NO HUB" overlay when the link is up but the hub has been silent for 6 s; cleared by the next hub
  message (FW-Q24). The card stays visible.

Gestures
- Pickup hardening: V taps must look like an impact (ring, or decay to ≤ 50% of the peak within 4 ms).
  Smooth pickup pushes are rejected (`CAL_TAP_V_DECAY`, FW-Q26). Simulated 20× pickup-and-flip: 13 stray
  `T:V` without the check, 0 with it.
- The Q13 tilt gate uses the brief's 0.3 g.

Other
- `src/STBLE/LICENSE-ST.txt` collects the ST / SICS licence notices (FW-Q25).
- README: hub-side connection-interval notes (Linux BlueZ `main.conf`, Windows), 20× pickup test,
  battery calibration step. Answered markers SPEC-Q1…Q14 removed; open ones were Q22–Q26 (now FW-Q22 to FW-Q26).
- Tests: 189 host tests (were 163).

## fw 1.0: implements brief v1.0
Initial version. Questions Q1–Q12 (SPEC_QUERY_1.md) and Q13–Q14 (SPEC_QUERY_2.md).
