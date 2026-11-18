```
=== SPEC QUERY ===
From: Firmware AI
Spec: prompt_node_firmware.md, version 1.0
Status: Milestone "firmware complete, untested on hardware". All of sections 1-9 implemented. 163/163 host tests pass (gesture simulations, parser, renderer). The sketch compiles and links for the SAMD21 (~78 KB flash, ~7 KB RAM). Q1-Q12 (first query) are still unanswered and implemented with their recommendations. Two new questions follow.

Q13 [NON-BLOCKING] [contradiction]
Section: 3. Tap detection (suppression) vs "V taps must work"
Issue: Lifting a node ~1 cm and tapping it onto the table gives ~45 ms of near free-fall, which pulls zAvg from +1.0 to ~0.4 g. Read literally, "suppress from the moment zAvg starts crossing the hysteresis band" would therefore suppress most V taps.
Options:
  A) zAvg leaving its side triggers suppression only while the node is also tilted (horizontal gravity > 0.3 g). Pure vertical motion never suppresses; real flips always pass through a tilt.
  B) Follow the brief literally and accept missed V taps after a lift.
Recommendation: A. In simulation it keeps drop-taps (1/1 detected) and still suppresses every handling knock during a flip.
Meanwhile: A implemented (gestures.cpp, SPEC-Q13). Setting FAST_TILT_ALPHA 0 gives the literal behaviour (B).
Affects other component: no

Q14 [NON-BLOCKING] [infeasible]
Section: 1. Hardware ("start from TinyCircuits' official BLE UART example")
Issue: The official STBLE library (0.1.2) leaves Enable_SPI_IRQ()/Disable_SPI_IRQ() empty. ST's HCI code relies on them, so the BLE interrupt can fire in the middle of a main-loop SPI write and corrupt it. That would show up as rare, random BLE hangs or dropped messages.
Options:
  A) Bundle STBLE inside the sketch (src/STBLE) with a one-function patch that masks only the BLE IRQ pin's interrupt line. Everything else in the library and example is unchanged.
  B) Use the installed library unmodified and accept the race.
Recommendation: A. The patch is small and documented (src/STBLE/PATCHES.md), and the build no longer depends on which STBLE copy is installed.
Meanwhile: A implemented (ble_link.cpp, SPEC-Q14).
Affects other component: no
=== END SPEC QUERY ===
```

---

## Proposed protocol changes (none applied; each waits for a SPEC ANSWER)

| Ref | Change | Why | Status in code |
|---|---|---|---|
| Q3 A | Send `HELLO` + `F:` when the hub **enables notifications**, instead of "right after connect". The wire format is unchanged. | A notification sent before the hub subscribes is lost. | Implemented. This is only a timing clarification, so the hub needs no change. Fallbacks: first hub write, or 2 s after connecting. |
| Q3 B | New hub→node `HELLO?`: the node resends HELLO + state. | Lets the hub resync without reconnecting. | Not implemented. |
| Q3 C | Also send `B:<hex>` after HELLO + `F:`. | Gives the hub the full input state from the start (a button held across a reconnect). | Ready behind `SEND_B_AFTER_HELLO` (off). |
| Q12 | New node→hub `BAT:<percent>` (or `BAT:<mV>`) after HELLO and every 60 s. | Six battery nodes; a node dying mid-game is otherwise invisible. | Not implemented. |

## Assumptions not stated in the brief

**Hardware / platform**
1. The BLE shield is the ST ASD2116 (BlueNRG-MS); the Nordic nRF8001 shield isn't supported (Q1).
2. The accelerometer is BMA253 (chip ID 0xFA) at 0x18. Older BMA250 shields are reported as `ACCEL ERROR`.
3. Sampling uses the BMA253's 32-frame FIFO in stream mode: 500 Hz bandwidth → 1000 Hz data rate, filtered data, ±8 g. Sample timestamps are reconstructed at 1 ms spacing from the moment the FIFO is read.
4. Calibration is compile-time and shared by all nodes, which assumes every node is assembled with the same shield orientation. Nothing is stored in flash.
5. An extra calibration constant, `CAL_SIDE_AXIS`, lets raw Y be the left/right axis in case the shield is mounted rotated 90°.
6. The node ID is a 16-bit FNV-1a hash of the SAMD21's 128-bit serial number, written as 4 uppercase hex digits. The collision chance among 6 nodes is ~0.02%. `NODE_ID_OVERRIDE` can force an ID.
7. `fw_version` is `1.0`.
8. "Up" and the button positions are relative to the screen in the TinyScreen library's default orientation. `DISPLAY_FLIP` rotates the display and buttons together, so `B:` bits and `U` stay screen-relative.

**BLE / protocol**
9. Nordic UART Service UUIDs. The RX characteristic accepts write with and without response; TX uses notify (Q2).
10. Advertising data carries the name; the 128-bit service UUID is in the scan response. Advertising interval 50-100 ms. Address: the BlueNRG-MS per-chip static random address.
11. Each node→hub message is sent as exactly one notification ending in `\n`. Hub→node bytes are parsed as a stream, and the end of a write also ends a message. Lines over 40 bytes are dropped (Q4).
12. Events detected while no hub is subscribed are dropped, not queued. HELLO + `F:` resynchronises the state.
13. TX queue of 16 messages. If the link stalls, the oldest message is dropped. Messages are never merged.
14. `B:<hex>` is a single uppercase hex digit (`B:0` … `B:F`).
15. Hub→node validation is strict and case-sensitive. Out-of-range or malformed values ignore the whole message (`SEQ:1`, `SEQ:10`, `HAND:7`, `REG:10a1`, `CARD:uu`, `CARD:UUUUU`, `COL:12345`). `MSG` text is truncated to 14 chars, and non-printable characters become `?`.
16. `MSG` stays up while SEQ/REG/COL/HAND update silently. An empty `MSG:` clears it. `BLINK` returns to whatever was on screen (Q8).
17. On disconnect, all hub-supplied state resets to defaults: no card, no badge, REG 0000, grey colour, no hand, no MSG, `DISP:0` (Q7). The hub must resend state after every HELLO.
18. A connection-parameter update to 7.5-15 ms (latency 0, timeout 4 s) is requested 1 s after connecting (Q11).
19. Hub commands are also accepted over USB serial in every mode, for bench testing. `!INFO` and `!DEBUG` are local serial-only commands.

**Gestures**
20. R/L are in the device frame (the node's own right edge, relative to its screen), face-up or face-down. Spikes dominated by the Y axis (knocks on the top/bottom edge) send no event (Q10).
21. Tap classification: a 4 ms capture window after the first threshold crossing picks the dominant axis by peak ÷ threshold. The R/L sign comes from that axis's first threshold crossing. Side and V thresholds are separate constants (both 1.5 g).
22. The refractory period (150 ms) counts from the start of the spike. A spike that is suppressed or ignored still starts a refractory period.
23. Suppressed taps are dropped, never delayed. Suppression covers: tilt (horizontal gravity > 0.5 g for 8 ms, ~10 ms filter, inputs clamped to ±1 g so knocks can't fake a tilt); zAvg off its committed side while tilted > 0.3 g (Q13); 300 ms after either of those ends; 300 ms after a flip commit; 300 ms after boot (Q9 option C).
24. Initial orientation and filter baselines come from the first sample, so a node that boots face-down sends no flip event and reports `F:1` in HELLO.
25. The brief's `flipSince == 0` sentinel is replaced by an explicit flag. The behaviour is identical, and it avoids a corner case at `millis() == 0`.
26. A stray `T:V` can still happen from a knock while lifting, before any tilt. This is a known limitation (Q9).

**Display**
27. The display uses 16-bit colour at brightness 10/15. Idle dimming is not implemented (the brief makes it optional, and it must not affect latency).
28. Card: fixed 12 px cells, centred. The 4-colour segment palette (cyan, orange, magenta, lime) is independent of the player colour. The start cell is a white outline with a dot. A black arrow on the last block shows the final direction. In a reversal, the later step is drawn on top (Q6).
29. Right column: player-colour bar (6 px); hand as `H<n>` in the player colour, or white if the colour is too dark to read; badge `HEAD` / `#n` black on white; REG boxes white with a black letter when set, dark outline when clear.
30. "Waiting" = grey outline box with the word `waiting`. Disconnected screen: `DISCONNECTED`, node name, fw or error line, and `F:x  last <event>` for hub-less calibration.
31. MSG uses the largest of the library's fonts that fits on one line, otherwise two lines split at the space nearest the middle.
32. BLINK alternates full white and full black every 125 ms for 1 s, and turns the display on even if it is in face-down power save.
33. The face-down display-off only happens while `DISP:0` (the default) and `DISPLAY_OFF_FACE_DOWN` are both set. It also applies while disconnected.

**Modes / build**
34. Boot combos: all 4 buttons = debug log; both bottom buttons = serial test mode (BLE off). The same modes are available as compile-time flags.
35. Debug output is in milli-g integers (newlib-nano on SAMD has no `%f`) and is prefixed `#` so scripts can separate it from protocol lines in test mode.
36. STBLE is bundled with the IRQ-masking patch (Q14). No other external interrupts may be attached (see PATCHES.md).
