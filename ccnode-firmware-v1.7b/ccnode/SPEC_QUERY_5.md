```
=== SPEC QUERY ===
From: Firmware AI
Spec: prompt_node_firmware.md, version 1.4
Status: Milestone "v1.4 implemented, untested on hardware". A1-A5 fixed, B1-B4 done, FW_VERSION = 1.4. 327/327 host tests pass (incl. I2C/BMA253 simulation, IF/FOR cards, SEQ:H<n>, HINT:, square cards, default DOFF:0, strip never covered). SAMD21 compile/link OK (~87 KB flash, ~9.5 KB RAM), both I2C backends. Root causes found for the v1.4 observations: (1) "no serial output with debugMode = true": setup() overwrote debugMode from the buttons, so the edit in code had no effect (fixed; port was already SerialUSB). (2) NO HUB while connected: besides the hub not PINGing, the bundled STBLE had only 2 receive buffers, so a burst of hub writes made each notification (incl. PONG) wait out a 100 ms command timeout (pool raised to 8, STBLE patch 4). (3) Removing the accelerometer froze the loop inside Arduino Wire (replaced by a bit-banged I2C with timeouts, plus the watchdog). Layout mock-up and designer checklist follow this block.

FW-Q27 [NON-BLOCKING] [missing]
Section: 14 A1 (Z-axis sign on the ASD2511 Rev5)
Issue: The first hardware test (blank only with DOFF:1, display fine with DOFF:0, node face-up) means the node read face-up as face-down, i.e. raw Z is about -1 g face-up on this stack. I can't measure the mounting myself.
Options:
  A) Default CAL_Z_SIGN = -1 (inverted), confirmed by checklist item 1: face-up must show "zAvg +1.00 UP" on the splash.
  B) Keep +1 and leave it to calibration.
Recommendation: A. The observation points to inverted Z, and with DOFF:0 as the default a wrong sign can no longer blank the screen; the splash shows it at once. CAL_X_SIGN is left at +1: if the chip is rotated about the Y axis, X is inverted too, and calibration step 5 will show it.
Meanwhile: A implemented (config.h, SPEC-FW-Q27).
Affects other component: no

FW-Q28 [NON-BLOCKING] [contradiction]
Section: 13 FW-Q23 vs 14 B1 (card scaling)
Issue: The FW-Q23 rule floor(62 / larger grid side) assumed a 64x64 card area. The v1.4 layout leaves 60x41 for the card, so a 1x4 card (I4 vertical) would overflow with that rule.
Options:
  A) cell = min(floor((W-2)/gridW), floor((H-2)/gridH), CARD_MAX_CELL = 20), centred in the card area (W=60, H=41). The IF action card uses the same rule in its 29x41 sub-area.
  B) Keep floor(62/side) and shrink only when it doesn't fit.
Recommendation: A. It is the same idea (fit with a 1 px margin) applied to a non-square area. All 152 valid cards stay inside and centred (tested).
Meanwhile: A implemented (display.cpp, SPEC-FW-Q28).
Affects other component: no

FW-Q29 [NON-BLOCKING] [ambiguity]
Section: 14 B3 (badge "H2") vs section 7 (hand number)
Issue: v1.2 showed the hand as "H3". Now the badge shows the head as "H2", so "H3" next to "H2" would be confusing.
Options:
  A) Hand number as a bare digit inside the player-colour block ("3"); the badge alone uses H<n>, #<n> and HEAD.
  B) Hand as "P3".
  C) Keep "H3".
Recommendation: A. It is the shortest and can't be confused with the badge. The colour block doubles as the colour bar.
Meanwhile: A implemented (display.cpp, SPEC-FW-Q29).
Affects other component: no

FW-Q30 [NON-BLOCKING] [ambiguity]
Section: 14 B1 (status area)
Issue: The right column has room for one row of status (35x11 px), but there are three possible indicators.
Options:
  A) NO HUB (red text) takes the whole row while active, as the most urgent. Otherwise "ACC" (orange) on the left and a battery icon on the right, both at once if needed. Low battery = BAT <= 15 %.
  B) Show only the single highest-priority indicator.
Recommendation: A. It is steady, never blinks, and hides at most ACC/BAT during the few seconds before the hub drops a silent link anyway (FW-Q24 note).
Meanwhile: A implemented (display.cpp, config.h LOW_BAT_PCT, SPEC-FW-Q30).
Affects other component: no

FW-Q31 [NON-BLOCKING] [missing]
Section: 14 A5 / observation 6 (rejoin after power-cycle)
Issue: The node advertised with the BlueNRG-MS "static random" address, which ST's stack may regenerate at reset. A hub that remembers nodes by address would then not find a power-cycled node without a fresh scan.
Options:
  A) Advertise with a fixed address derived from the node ID, written to the module at every boot (identical after every power cycle). It uses the "public" address type, but isn't IEEE-registered.
  B) Keep the module's static random address.
Recommendation: A. The address can never change, which helps the hub rejoin on its own. The hub needs one fresh scan after updating to fw 1.4, because the address differs from the 1.3 one.
Meanwhile: A implemented (ble_link.cpp, SPEC-FW-Q31).
Affects other component: yes (the hub can rely on a stable address per node)

FW-Q32 [NON-BLOCKING] [infeasible]
Section: 14 A4 ("every I2C call needs a timeout")
Issue: Arduino's SAMD Wire library can't be given timeouts (its busy-waits have none), so the requirement can't be met with Wire.
Options:
  A) CCNode's own bit-banged I2C on the same SDA/SCL pins (~250 kHz). Every wait times out after 1 ms, a slave holding SDA is clocked free, and NACKs end a transfer cleanly. Cost: about 30 % CPU while sampling at 1 kHz (~1 ms per 3 ms poll). Wire stays selectable with I2C_USE_WIRE 1 (watchdog only).
  B) Keep Wire and rely on the watchdog alone (a reset, and a dropped link, every time the accelerometer is pulled).
Recommendation: A. It is the only option where checklist item 4 ("link stays up") can pass. It is tested against a simulated BMA253: unplug, SDA stuck, SCL stuck, all bounded. Please report the heartbeat's loop rate and maxgap so the CPU cost is confirmed on hardware.
Meanwhile: A implemented (soft_i2c.h, i2c_bus.cpp, SPEC-FW-Q32).
Affects other component: no

FW-Q33 [NON-BLOCKING] [ambiguity]
Section: 14 A4 (orientation while the accelerometer is missing)
Issue: The brief doesn't say what F the node reports while the accelerometer is missing, or when it returns and the node was turned over meanwhile.
Options:
  A) While missing: the HELLO sync sends the last known F. When the accelerometer returns, the orientation is averaged over 200 ms, and if it differs from what the hub was last told, one F:<new> is sent (like a flip commit).
  B) Send nothing extra; the hub keeps the old F until the next real flip.
Recommendation: A. The hub's F state is never left wrong, and a correction costs at most one message.
Meanwhile: A implemented (CCNode.ino, SPEC-FW-Q33).
Affects other component: yes (the hub may see one F: right after an accelerometer recovery)

FW-Q34 [NON-BLOCKING] [ambiguity]
Section: 14 B1 / B4 (MSG vs HINT and status)
Issue: "MSG fills the area below the strip", but HINT and the status area also live below the strip.
Options:
  A) MSG text uses y12-41 full width; the status area (NO HUB / ACC / battery) and the hint line stay visible under it. So a tutorial can show MSG:WELCOME with HINT:press any key.
  B) MSG covers everything below the strip, hiding HINT and the status while it is shown.
Recommendation: A. Nothing important disappears behind a MSG.
Meanwhile: A implemented (display.cpp, SPEC-FW-Q34).
Affects other component: no

FW-Q35 [NON-BLOCKING] [ambiguity]
Section: 14 A2 (debug toggle) vs section 4 (buttons are never interpreted)
Issue: Holding all 4 buttons for 2 s toggles debug, but the chord also reaches the hub as B:F (then B:0 on release), because the node reports buttons raw.
Options:
  A) Keep reporting raw. The hub should treat B:F as "no action" (or know it's the debug chord).
  B) Suppress B: messages while all 4 buttons are held.
Recommendation: A. It keeps section 4 ("do not interpret the buttons") intact.
Meanwhile: A implemented (CCNode.ino, SPEC-FW-Q35).
Affects other component: yes (the hub must not act on B:F)
=== END SPEC QUERY ===
```

---

## Layout mock-up (brief v1.4 B1)

Rendered screens from the host tests are in `docs/screens.png`.

```
  x 0        24        48        72      95
 y0 +---------+---------+---------+---------+
    |    R    |    L    |  [ V ]  |    F    |   register strip: filled = set. Always visible
y10 +---------+---------+---------+---------+
y12 +---------------------------+ +---------+
    |                           | |    1    |   colour block (player colour) + hand number
    |  card area 60 x 41        | +---------+
    |  start = hollow square,   | |  HEAD   |   sequence badge: #3 / H2 / HEAD
    |  blocks in run colours,   | +---------+
    |  arrow on the last block  | | NO HUB  |   status: NO HUB, or ACC and a low-battery icon
y52 +---------------------------+ +---------+
y53 |            tap R to start              |   hint line (HINT:, "DEBUG ON/OFF")
y63 +----------------------------------------+

IF card (in the card area):          FOR card:          MSG:
 IF                                    FOR               +----------------------------+
 [][]      ->    [start][][]          +-----+>           |         P1 WINS            |
 [][]            (action, small)      |     |            |                  [NO HUB]  |
 (family O as grey cells)             +-----+            | press any key (hint)       |
                                                         +----------------------------+
```

- **BLINK:** everything below the strip flashes white/black for 1 s.
- **Boot splash (3 s, the only screen without the strip):**
  - `CCNODE fw 1.4`
  - `ID 3F2A   rst PWR|WDT|RST|SW|BOD`
  - `ACC OK | ACC not found`
  - `zAvg +0.98 UP | DOWN`, live every 200 ms
  - `BLE OK adv-fast | BLE FAIL`

## Designer test checklist (brief v1.4 Part C; also README section 6)

| # | Do this | Expect |
|---|---|---|
| 1 | Fresh flash, node face-up | Splash shows `ACC OK`, `zAvg` about `+1.00`, `UP`. The serial monitor prints the `# CCNODE …` banner and, in debug mode, a `# hb …` line every second. |
| 2 | Turn it face-down for 2 s, then back | Splash / heartbeat shows `DOWN`, then `UP`. The display **stays on** (default `DOFF:0`). |
| 3 | Connect to the hub in the lobby | No `NO HUB` indicator for 60 s. Heartbeat `hub_age` stays under 3 s. |
| 4 | Remove the accelerometer while connected | `ACC` appears in the status area. The link **stays up**, and buttons still work in Manual mode. Re-attach it: gestures resume within about 5 s. |
| 5 | Power-cycle while connected | Advertising again within about 1 s (splash `BLE OK adv-fast`), with the same name and address. The hub should rejoin on its own (one fresh scan is needed after updating from 1.3, FW-Q31). |
| 6 | README calibration steps 8 and 10 (FW-Q26) | 20/20 lift-and-taps reported as `T:V`, 0 stray `T:V` in 20 pickups. Please include the `# spike …` lines. |

Please also send back a few `# hb …` heartbeat lines from item 3 (loop rate, `maxgap`, `blegap`). They
confirm the loop-starvation fix (A3) and the soft-I2C CPU cost (FW-Q32).

## Proposed protocol changes

None. FW-Q31, FW-Q33 and FW-Q35 affect how the hub uses existing messages, but don't change the wire
protocol.

## Assumptions not stated in brief v1.4

**Hardware / platform**
1. The accelerometer is a BMA253 read through its 32-frame FIFO in stream mode (500 Hz bandwidth → 1000 Hz data rate, ±8 g), drained every 3 ms. Sample times are reconstructed at 1 ms spacing.
2. Calibration is compile-time and shared by all nodes, so every node needs the same shield orientation. `CAL_SIDE_AXIS` lets raw Y be the left/right axis.
3. Node ID: 16-bit FNV-1a hash of the SAMD21 128-bit serial, written as 4 uppercase hex digits (~0.02 % collision chance for 6 nodes). `NODE_ID_OVERRIDE` can force one.
4. `fw_version` is `1.4` and follows the brief version implemented.
5. "Up" and the button positions are relative to the screen in the TinyScreen library's default orientation. `DISPLAY_FLIP` rotates the display and buttons together.
6. Battery: TinyScreen+ divider on A4 (ratio 0.5) against VCC from the 1.1 V bandgap. Coarse LiPo curve 3.40 V = 0 %, 3.80 V = 45 %, 4.15 V = 100 %. It reads ~100 % on USB / while charging. It is measured every 60 s, connected or not, for the low-battery icon.
7. The soft I2C runs on the variant's `PIN_WIRE_SDA` / `PIN_WIRE_SCL` with the internal pull-ups enabled as a backup to the shield's.
8. Watchdog: 2 s from the 32 kHz ultra-low-power oscillator via GCLK2. It is enabled at the very start of setup() and fed in every boot step and loop pass. The reset cause is shown on the splash and printed in the banner.

**BLE / protocol**
9. HELLO is sent only on a CCCD enable. The HELLO sync uses the debounced buttons, a fresh battery reading, and the averaged orientation (or the last known one if the accelerometer is missing, FW-Q33).
10. Unsubscribing stops notifications and clears the TX queue. Events while unsubscribed or disconnected are dropped, not queued. TX queue of 16; on overflow the oldest message is dropped. Messages are never merged into one notification.
11. Advertising: 100 ms for the first 30 s after boot or a disconnect, then 500 ms. If starting it fails, it is retried every 250 ms. TX power as in TinyCircuits' example.
12. Hub→node validation is strict and case-sensitive. Malformed or out-of-range values make the whole message ignored: `SEQ:0`, `SEQ:10`, `SEQ:H0`, `HAND:7`, `REG:10a1`, `CARD:uu`, `CARD:IF:X:L`, `CARD:IF:O:`, `CARD:FOR:2`, `DOFF:2`. `MSG` is truncated to 14 chars and `HINT` to 16, and non-printable characters become `?`.
13. SEQ positions are one digit, 1–9.
14. Cards that revisit a cell are still drawn (the later step on top) rather than rejected.
15. The connection-parameter request is sent once, 1 s after connecting, and only if the interval is above 15 ms.
16. A reconnect needs no reset: the node advertises again on the next loop pass, and on the new CCCD enable it sends a fresh HELLO sync.
17. Hub messages that arrive during the 3 s splash are applied to the stored state and shown when the splash ends.

**Gestures**
18. Tap classification: a 4 ms capture window after the first threshold crossing picks the dominant axis (peak ÷ threshold). The R/L sign comes from that axis's first crossing. Y-dominant spikes send no event.
19. The refractory period (150 ms) counts from the start of the spike. Suppressed or rejected spikes also start a refractory period.
20. Suppressed taps are dropped, never delayed. Suppression covers: tilt (horizontal gravity > 0.5 g for 8 ms); zAvg off its side while tilted > 0.3 g; 300 ms after those end; 300 ms after a flip commit; 300 ms after the start-up averaging.
21. The V-tap shape check (FW-Q26) applies to V taps only.
22. The starting orientation and filter baselines come from the average of the first 200 ms of samples (v1.4 A1), after boot and after every accelerometer recovery. No events are produced during those 200 ms.
23. A knock before any lift or tilt is indistinguishable from a real tap, and is reported.

**Display**
24. 16-bit colour at brightness 10/15. No idle dimming.
25. Card: hollow start = white outline (2 px when blocks are ≥ 12 px). The arrow is black, ~30 % of the block. There is a 1–2 px gap between blocks. IF family icons use 5 px cells in one fixed orientation (families have no direction), in grey `A0A0A0`. "IF"/"FOR" are in yellow.
26. Hand-number text is black on light player colours and white on dark ones. The badge is black on white. Strip boxes are white with a black letter when set, and dark outline with a grey letter when clear.
27. "Waiting" = grey outline box with the word `waiting`. The disconnected and serial-test screens keep the strip, then show the title, node name, fw or `BLE ERROR`, and a bottom line with `UP`/`DOWN` + last event (or `ACC not found`).
28. MSG uses the largest library font that fits in 94 × 30 px, otherwise two lines split at the space nearest the middle.
29. Display power: off only with `DOFF:1`, the node face-down and its orientation known. Never during the splash, a blink or debug mode. Switching on is non-blocking (10 ms boost settle in the loop), followed by a full redraw.
30. The "DEBUG ON/OFF" toast shows in the hint line for 1.5 s, then the hint returns.

**Modes / build**
31. Debug mode can be enabled by `DEBUG_DEFAULT`, by `debugMode = true` in code, by 4 buttons at power-on, by 4 buttons held 2 s at run time (armed only after all buttons were released once, so the boot combo doesn't immediately toggle it back), or by `!DEBUG`. Both bottom buttons at power-on = serial test mode.
32. The boot trace and the boot banner are printed in every mode. The 1 s heartbeat is printed in debug mode only. Raw accelerometer lines are off by default (`DEBUG_ACCEL_PRINT_MS`).
33. STBLE is bundled with 4 patches: IRQ masking, a bounded interrupt loop + polling while waiting for replies, an IRQ pull-down, and 8 receive buffers. No other external interrupts may be attached (PATCHES.md).
