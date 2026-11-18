The whole block below is meant to be relayed as one piece. The mock-up, the checklist and the
assumptions are inside it as appendices, because last time they sat outside the block and didn't
come through.

```
=== SPEC QUERY ===
From: Firmware AI
Spec: prompt_node_firmware.md, version 1.6
Status: Milestone "v1.5 + v1.6 applied". FW-Q27-Q30, Q32, Q34, Q35 applied as confirmed; markers removed. FW-Q31 (changed): BLE address is now a static random address C0:CC:00:00:<ID hi>:<ID lo> (top bits 11, rest from the node ID), set in the radio at every boot with hci_le_set_random_address() and used for advertising; printed in the boot banner as addr=... (with "(NOT SET)" if the radio rejected it). FW-Q33 (changed): after the accelerometer returns, a changed orientation is sent as the new silent FS:<0|1>; the HELLO sync still sends F:. LOW_BAT_PCT = 20. X1: HINT laid out and tested for 14 chars (up to 16 accepted); every 14-char hint fits the line (the narrow pixel font is used when needed). README: "don't use the debug chord mid-game". FW_VERSION = 1.6. 337/337 host tests pass; SAMD21 compile/link OK (~87 KB flash, ~9.5 KB RAM). Still not run on hardware. One item to verify on hardware: the address the hub sees must equal the banner's addr= (checklist item 5); the BlueNRG-MS can't be tested here.

No open questions. Next question number: FW-Q36.

APPENDIX A - SCREEN LAYOUT MOCK-UP (96 x 64 px)

  x 0        24        48        72      95
 y0 +---------+---------+---------+---------+
    |    R    |    L    |  [ V ]  |    F    |  register strip: box filled = bit set.
y10 +---------+---------+---------+---------+  Always visible except during the 3 s boot splash.
y12 +---------------------------+ +---------+
    |                           | |    1    |  colour block (player colour) + hand digit
    |  card area 60 x 41        | +---------+
    |  start = hollow square,   | |  HEAD   |  sequence badge: #3 / H2 / HEAD
    |  blocks in run colours,   | +---------+
    |  arrow on the last block  | | NO HUB  |  status: NO HUB, or ACC + battery icon
y52 +---------------------------+ +---------+
y53 |            FLIP TO START!              |  hint line (HINT:, "DEBUG ON/OFF" toast)
y63 +----------------------------------------+

  IF card (card area):           FOR card:          MSG screen:
   IF                             FOR                +----------------------------+
   [][]   ->   [S][ ][>]         +------+>           | R  | L  | V  | F  (strip)   |
   [][]        (action, small)   |      |            |         P1 WINS            |
   (family O, grey cells)        +------+            |                   [NO HUB] |
                                                     | press any key (hint)       |
                                                     +----------------------------+
  BLINK: everything below the strip flashes white/black for 1 s.
  Boot splash (3 s, the only screen without the strip):
    CCNODE fw 1.6 / ID 3F2A  rst PWR|WDT|RST|SW|BOD / ACC OK|ACC not found /
    zAvg +0.98 UP|DOWN (live) / BLE OK adv-fast|BLE FAIL
  Rendered screenshots: docs/screens.png in the firmware package.

APPENDIX B - DESIGNER TEST CHECKLIST (brief v1.4 Part C; also README section 6)
  1. Fresh flash, node face-up -> splash shows ACC OK, zAvg about +1.00, UP; serial prints the
     "# CCNODE ..." banner and, in debug mode, a "# hb ..." heartbeat every second.
  2. Turn it face-down for 2 s and back -> splash/heartbeat shows DOWN then UP; the display stays
     on (default DOFF:0).
  3. Connect to the hub in the lobby -> no NO HUB for 60 s; heartbeat hub_age stays under 3 s.
  4. Remove the accelerometer while connected -> ACC appears in the status area, the link stays up,
     buttons still work in Manual mode. Re-attach -> gestures resume within about 5 s (if the node
     was turned over meanwhile, the hub gets one FS:).
  5. Power-cycle while connected -> advertising again within about 1 s, same name and same address;
     the hub's view of the address must equal the banner's addr=C0:CC:00:00:<ID>. The hub rejoins
     on its own.
  6. README calibration steps 8 and 10 -> 20/20 lift-and-taps reported as T:V; 0 stray T:V in 20
     pickups. Please include the "# spike ..." lines.
  Please also send a few "# hb ..." lines from item 3 (loop rate, maxgap, blegap).

APPENDIX C - ASSUMPTIONS NOT STATED IN BRIEF v1.6
  Hardware / platform
  1. BMA253 read through its 32-frame FIFO in stream mode (500 Hz bw -> 1 kHz data, +-8 g), drained
     every 3 ms; sample times reconstructed at 1 ms spacing.
  2. Calibration is compile-time and shared by all nodes (same shield orientation on every node).
     CAL_SIDE_AXIS lets raw Y be the left/right axis.
  3. Node ID: 16-bit FNV-1a hash of the SAMD21 serial, 4 uppercase hex digits (~0.02 % collision
     chance for 6 nodes); NODE_ID_OVERRIDE can force one.
  4. fw_version "1.6" follows the brief version implemented.
  5. "Up" and button positions are screen-relative in the library's default orientation;
     DISPLAY_FLIP rotates display and buttons together.
  6. Battery: A4 divider (0.5) vs. VCC from the 1.1 V bandgap; curve 3.40 V = 0 %, 3.80 V = 45 %,
     4.15 V = 100 %; reads ~100 % on USB / charging. Measured every 60 s, connected or not, for the
     low-battery icon (<= 20 %).
  7. Soft I2C on the variant's SDA/SCL pins (~250 kHz, 1 ms timeouts), internal pull-ups on as a
     backup to the shield's.
  8. Watchdog 2 s (OSCULP32K via GCLK2), enabled first thing in setup(), fed in every boot step and
     loop pass; reset cause on the splash and in the banner.
  BLE / protocol
  9. HELLO only on a CCCD enable. The sync uses the debounced buttons, a fresh battery reading and
     the averaged orientation (or the last known one if the accelerometer is missing).
  10. FS:<0|1> is sent only after an accelerometer recovery, only if the orientation differs from
      the last F:/FS: the hub got, and only while subscribed (otherwise the next HELLO sync covers it).
  11. Static random address C0:CC:00:00:<ID>; the 46 non-type bits are the ID padded with the fixed
      bytes 00 00 CC (never all-0 or all-1), so the hub can also derive it from the ID.
  12. Unsubscribing stops notifications and clears the TX queue; events while unsubscribed or
      disconnected are dropped, not queued. TX queue 16, oldest dropped on overflow; one message per
      notification, never merged.
  13. Advertising 100 ms for 30 s after boot or a disconnect, then 500 ms; retried every 250 ms if
      starting it fails.
  14. Hub->node validation is strict and case-sensitive; malformed or out-of-range values make the
      whole message ignored (SEQ:0, SEQ:10, SEQ:H0, HAND:7, REG:10a1, CARD:uu, CARD:IF:X:L,
      CARD:IF:O:, CARD:FOR:2, DOFF:2). MSG cut at 14 chars, HINT at 16 (documented 14);
      non-printable characters become '?'.
  15. SEQ positions are one digit, 1-9.
  16. Cards that revisit a cell are still drawn (later step on top) rather than rejected.
  17. Connection-parameter request once, 1 s after connecting, only if the interval is > 15 ms.
  18. Reconnects need no reset; hub messages arriving during the 3 s splash are shown when it ends.
  Gestures
  19. Tap classification: 4 ms window after the first threshold crossing; dominant axis by
      peak / threshold; R/L sign from that axis's first crossing; Y-dominant spikes send nothing.
  20. Refractory 150 ms from the spike start; suppressed or rejected spikes also start one.
  21. Suppressed taps are dropped, never delayed (tilt > 0.5 g for 8 ms; zAvg off its side while
      tilted > 0.3 g; 300 ms after those end, after a flip commit, and after start-up averaging).
  22. The V-tap shape check (FW-Q26) applies to V taps only.
  23. Starting orientation and baselines = average of the first 200 ms of samples, after boot and
      after every accelerometer recovery; no events during those 200 ms.
  24. A knock before any lift or tilt is indistinguishable from a real tap, and is reported.
  Display
  25. 16-bit colour, brightness 10/15, no idle dimming.
  26. Hollow start = white outline (2 px when blocks >= 12 px); black arrow ~30 % of the block;
      1-2 px gaps. IF family icons: 5 px grey (A0A0A0) cells in one fixed orientation; IF/FOR in
      yellow.
  27. Hand digit black on light player colours, white on dark ones; badge black on white; strip
      boxes white + black letter when set, dark outline + grey letter when clear.
  28. "Waiting" = grey outline box + "waiting". Disconnected / serial-test screens keep the strip
      and show title, node name, fw or BLE ERROR, and UP/DOWN + last event (or "ACC not found").
  29. MSG: largest library font that fits 94 x 30 px, else two lines split at the space nearest the
      middle. HINT: normal font if <= 95 px, else the narrow pixel font.
  30. Display off only with DOFF:1 + face-down + orientation known; never during the splash, a blink
      or debug mode. Switching on is non-blocking (10 ms settle in the loop), then a full redraw.
  31. "DEBUG ON/OFF" toast in the hint line for 1.5 s, then the hint returns.
  Modes / build
  32. Debug on via DEBUG_DEFAULT, debugMode = true in code, 4 buttons at power-on, 4 buttons held
      2 s at run time (armed after all buttons were released once), or !DEBUG. Both bottom buttons
      at power-on = serial test mode. Boot trace and banner always printed; heartbeat in debug only.
  33. STBLE bundled with 4 patches (IRQ masking; bounded IRQ loop + polling while waiting; IRQ
      pull-down; 8 receive buffers). No other external interrupts may be attached.

Proposed protocol changes: none beyond FS: (already confirmed in v1.5).
=== END SPEC QUERY ===
```
