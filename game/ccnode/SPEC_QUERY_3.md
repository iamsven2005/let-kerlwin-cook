```
=== SPEC QUERY ===
From: Firmware AI
Spec: prompt_node_firmware.md, version 1.2
Status: v1.2 applied in full: SPEC ANSWER v1.2 (Q1, Q11) and the v1.1 changes. Added: HELLO sync (HELLO + F + B + BAT) on every CCCD enable; BAT every 60 s; DOFF replaces DISP; HAND:0; shared card style; NO HUB overlay; README hub-side notes and the 20x pickup test. Answered markers SPEC-Q1..Q14 removed. 189/189 host tests pass; SAMD21 compile/link OK (~80 KB flash). Still not run on real hardware. Numbering continues at Q22 because Q15-Q21 were used on the hub side.

Q22 [NON-BLOCKING] [missing]
Section: 7. Display (shared card style)
Issue: The shared style defines 3 segment colours, but 24 of the 152 cards the hub can send (no revisited cell, max 4 steps) have 4 straight runs, e.g. URUR, ULDL.
Options:
  A) Segment 4 reuses segment 1's colour (E0E0E0), i.e. colour = segment index mod 3. Adjacent runs still always differ.
  B) Add a 4th shared colour (e.g. F2C94C), used by both hub and node.
Recommendation: A. It needs no new colour, adjacent runs always differ, and the arrow fixes the order.
Meanwhile: A implemented (display.cpp, SPEC-Q22).
Affects other component: yes (the hub must draw 4-run cards the same way)

Q23 [NON-BLOCKING] [ambiguity]
Section: 7. Display ("scaled and centred to fit")
Issue: The brief doesn't give a scaling rule. Scaling a 1-step card (1x2 cells) to fill 64 px gives 31 px blocks, while a 5-cell card gets 12 px blocks.
Options:
  A) Cell = floor(62 / larger grid side), capped at 20 px, centred.
  B) Fixed 12 px cells for every card (consistent scale, more empty space).
  C) Scale to fill with no cap.
Recommendation: A. Small cards are bigger and easier to read, but not cartoonish. The pixel geometry doesn't need to match the hub's.
Meanwhile: A implemented (display.cpp, SPEC-Q23; cap = CARD_MAX_CELL).
Affects other component: no

Q24 [NON-BLOCKING] [ambiguity]
Section: 7. Display ("Hub silent")
Issue: The trigger is "no PING for ~6 s", but the overlay clears "on the next hub message". If only PING reset the timer, a CARD arriving without a PING would clear the overlay and it would come straight back.
Options:
  A) Any hub message resets the 6 s timer and clears the overlay. The timer starts at the HELLO sync.
  B) Only PING resets the timer; other messages clear the overlay until the next check.
Recommendation: A. Any traffic proves the hub is alive. With the hub PINGing every ~2 s, A and B behave the same in practice.
Meanwhile: A implemented (CCNode.ino, SPEC-Q24).
Affects other component: no

Q25 [NON-BLOCKING] [missing]
Section: 11. Q14 ("keep ST's original licence file next to it")
Issue: STBLE as TinyCircuits ships it has no licence file. Its terms are only in the file headers, and there are three different notices: ST BSD-3-Clause (a few files), ST's "for guidance only" disclaimer (most STBlueNRG files), and a Swedish Institute of Computer Science BSD notice (gp_timer.c).
Options:
  A) src/STBLE/LICENSE-ST.txt collects all three notices verbatim from the headers, and the headers stay unchanged.
  B) Someone downloads ST's X-CUBE-BLE1 package and adds its licence file instead.
Recommendation: A. It is what the bundled code actually carries. If the project is ever published, someone should check that the "guidance only" notice suits that use.
Meanwhile: A done.
Affects other component: no

Q26 [NON-BLOCKING] [ambiguity]
Section: 3. Tap detection (pickup suppression; v1.1 Q9 says it is now important)
Issue: Since the hub can't filter stray V taps, I added a shape check for V taps. The vertical spike must ring (change sign) or decay below 50% of its peak within the 4 ms window. A fast pickup is a smooth push that is still near its peak at that point. In simulation, 20 rough pickups and flips gave 13 stray T:V without the check and 0 with it, and lift-and-taps are still detected. The risk is that real table impacts on this hardware decay more slowly than modelled, so some real V taps get rejected.
Options:
  A) Keep the check, tunable as CAL_TAP_V_DECAY, and tune on hardware with README calibration steps 8 and 10 (the debug log prints end=% for every spike).
  B) Remove it and rely on tilt suppression only.
Recommendation: A, and please have the designer run steps 8 and 10 on the first node.
Meanwhile: A implemented (gestures.cpp, SPEC-Q26; CAL_TAP_V_DECAY 1.0 = check off).
Affects other component: no
=== END SPEC QUERY ===
```

---

## Proposed protocol changes

None. Q22 asks the hub to render 4-run cards consistently but doesn't change the wire protocol.

## Assumptions not stated in brief v1.2

**Hardware / platform**
1. The accelerometer is a BMA253 read through its 32-frame FIFO in stream mode: 500 Hz bandwidth → 1000 Hz data rate, filtered data, ±8 g. Sample times are reconstructed at 1 ms spacing.
2. Calibration is compile-time and shared by all nodes, so every node needs the same shield orientation. An extra constant `CAL_SIDE_AXIS` lets raw Y be the left/right axis.
3. The node ID is a 16-bit FNV-1a hash of the SAMD21 128-bit serial, written as 4 uppercase hex digits. The collision chance among 6 nodes is ~0.02%. `NODE_ID_OVERRIDE` can force an ID.
4. `fw_version` is `1.2` and follows the brief version implemented.
5. "Up" and the button positions are relative to the screen in the TinyScreen library's default orientation. `DISPLAY_FLIP` rotates the display and buttons together.
6. Battery: TinyScreen+ divider on A4 (ratio 0.5) against VCC measured with the 1.1 V bandgap (TinyCircuits' method). Coarse LiPo curve: 3.40 V = 0%, 3.80 V = 45%, 4.15 V = 100%, linear between points. On USB or while charging it reads ~100%.

**BLE / protocol**
7. HELLO is sent **only** on a CCCD enable; the v1.0 fallbacks (first hub write, 2 s timeout) are removed. Debug mode warns if the hub connects but doesn't subscribe within 2 s.
8. The HELLO sync uses the current debounced button state for `B:` and a fresh battery reading for `BAT:`. The 60 s BAT period restarts at each sync.
9. Unsubscribing (CCCD disable) stops all notifications and clears the TX queue. Events while unsubscribed or disconnected are dropped, not queued.
10. TX queue of 16 messages. If the link stalls, the oldest message is dropped. Messages are never merged into one notification.
11. Advertising interval 50-100 ms. Address: the BlueNRG-MS per-chip static random address. TX power as in TinyCircuits' example.
12. Hub→node validation is strict and case-sensitive. Malformed or out-of-range values ignore the whole message (`SEQ:1`, `SEQ:10`, `HAND:7`, `REG:10a1`, `CARD:uu`, `CARD:UUUUU`, `COL:12345`, `DOFF:2`). `MSG` text is truncated to 14 chars, and non-printable characters become `?`. The old `DISP:` is now an unknown message.
13. Cards that revisit a cell are still drawn (the later step on top) rather than rejected, even though the hub guarantees it won't send them.
14. The connection-parameter request is sent once, 1 s after connecting, and only if the current interval is above 15 ms.

**Gestures**
15. Tap classification: a 4 ms capture window after the first threshold crossing picks the dominant axis by peak ÷ threshold. The R/L sign comes from that axis's first crossing. Side and V thresholds are separate constants (both 1.5 g). Spikes dominated by the Y axis (top/bottom edge) send no event.
16. The refractory period (150 ms) counts from the start of the spike. Suppressed or rejected spikes also start a refractory period.
17. Suppressed taps are dropped, never delayed. Suppression covers: tilt (horizontal gravity > 0.5 g for 8 ms, ~10 ms filter, inputs clamped to ±1 g); zAvg off its committed side while tilted > 0.3 g; 300 ms after either of those ends; 300 ms after a flip commit; 300 ms after boot.
18. The V-tap shape check (Q26) applies to V taps only, not to side taps, so bump timing is unaffected.
19. Initial orientation and filter baselines come from the first sample: a node that boots face-down reports `F:1` in the sync without sending a flip event.
20. A knock before any lift or tilt is indistinguishable from a real tap, and is reported.

**Display**
21. 16-bit colour at brightness 10/15. Idle dimming is not implemented (it is optional, and must not affect latency).
22. Card: the start cell is a white outline (2 px when blocks are ≥ 12 px) with no dot, to keep it "hollow". The arrow is black, sized to ~30% of the block. There is a 1-2 px gap between blocks.
23. Right column: player-colour bar 6 px; hand `H<n>` in the player colour, or white if its luminance is too low; badge `HEAD` / `#n` black on white; REG boxes white with a black letter when set, dark outline when clear.
24. "Waiting" = grey outline box with the word `waiting`. The disconnected screen shows `DISCONNECTED`, the node name, the fw version or an error line, and `F:x  last <event>` for hub-less calibration.
25. NO HUB overlay: a red panel over the badge and REG area on the game screen (the card stays fully visible), or a red bottom banner on a MSG screen. It is cleared with a full redraw.
26. MSG uses the largest library font that fits on one line, otherwise two lines split at the space nearest the middle.
27. BLINK alternates full white and full black every 125 ms for 1 s, and turns the display on even in face-down power save.
28. The face-down display-off also applies while disconnected (`DOFF:1` is the default then).

**Modes / build**
29. Boot combos: all 4 buttons = debug log; both bottom buttons = serial test mode (BLE off). The same modes are available as compile-time flags. In serial test mode, typed commands count as hub messages for the NO HUB timer.
30. Debug output is in milli-g integers and percentages (newlib-nano on SAMD has no `%f`), with lines prefixed `#`.
31. STBLE is bundled with the IRQ-masking patch. No other external interrupts may be attached (see PATCHES.md).
