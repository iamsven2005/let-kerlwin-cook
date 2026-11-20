```
=== SPEC QUERY ===
From: Firmware AI
Spec: prompt_node_firmware.md, version 1.3
Status: Milestone "v1.3 applied; spec complete". FW-Q22 to FW-Q26 were all confirmed as already implemented, so there are no functional changes. All SPEC-FW-Q markers removed (none open). FW_VERSION = 1.3. README licence note added (FW-Q25). 189/189 host tests pass; SAMD21 compile/link OK (~80 KB flash, ~7 KB RAM). Not yet run on real hardware: waiting for the designer's results from README calibration steps 8 and 10 on the first physical node (FW-Q26). Those results may lead to retuning CAL_TAP_V_DECAY / CAL_TAP_V_G, which are calibration constants, not spec changes.

No open questions. Next question number: FW-Q27.
=== END SPEC QUERY ===
```

---

## Proposed protocol changes

None.

## What to send back after the hardware test (FW-Q26)

For calibration steps 8 and 10, the debug log lines starting `# spike` (they include `end=NN%` and the
result), plus the counts: real lift-and-taps detected out of 20, and stray `T:V` out of 20 pickups.
That is enough to set `CAL_TAP_V_DECAY` / `CAL_TAP_V_G` for all nodes.

## Assumptions not stated in brief v1.3

**Hardware / platform**
1. The accelerometer is a BMA253 read through its 32-frame FIFO in stream mode: 500 Hz bandwidth → 1000 Hz data rate, filtered data, ±8 g. Sample times are reconstructed at 1 ms spacing.
2. Calibration is compile-time and shared by all nodes, so every node needs the same shield orientation. An extra constant `CAL_SIDE_AXIS` lets raw Y be the left/right axis.
3. The node ID is a 16-bit FNV-1a hash of the SAMD21 128-bit serial, written as 4 uppercase hex digits. The collision chance among 6 nodes is ~0.02%. `NODE_ID_OVERRIDE` can force an ID.
4. `fw_version` is `1.3` and follows the brief version implemented.
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
14a. A dropped link and reconnect (the hub's response to missed PONGs) needs no reset: the node re-advertises at once, and on the new CCCD enable sends a fresh HELLO sync.

**Gestures**
15. Tap classification: a 4 ms capture window after the first threshold crossing picks the dominant axis by peak ÷ threshold. The R/L sign comes from that axis's first crossing. Side and V thresholds are separate constants (both 1.5 g). Spikes dominated by the Y axis (top/bottom edge) send no event.
16. The refractory period (150 ms) counts from the start of the spike. Suppressed or rejected spikes also start a refractory period.
17. Suppressed taps are dropped, never delayed. Suppression covers: tilt (horizontal gravity > 0.5 g for 8 ms, ~10 ms filter, inputs clamped to ±1 g); zAvg off its committed side while tilted > 0.3 g; 300 ms after either of those ends; 300 ms after a flip commit; 300 ms after boot.
18. The V-tap shape check (FW-Q26) applies to V taps only, not to side taps, so bump timing is unaffected.
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
