# Spec queries for prompt_desktop_game.md

**Current spec: v1.6.** Every question so far has been answered:

| Questions / assumptions | Status |
|---|---|
| Q1–Q21 (historical) | confirmed in v1.1 |
| HUB-Q22–HUB-Q25 | confirmed in v1.3 |
| HUB-Q26–HUB-Q36 | confirmed or resolved in v1.6 |
| assumptions 1–45 | accepted |

v1.5 (firmware items X1–X5) and the v1.6 answers are applied. There are no open `SPEC-HUB-Q` markers in the code.

Below is the v1.6 milestone Spec Query to relay. It has no questions, only five new assumptions (46–50) about how the v1.5 items were implemented.

```
=== SPEC QUERY ===
From: Desktop Game AI
Spec: prompt_desktop_game.md, version 1.6
Status: v1.6 applied: HUB-Q26–HUB-Q36 answers, and v1.5 X1–X5 (FS: silent face-state sync, fast reconnect to the stable address with scan-by-name fallback, 3+ button chord = clear in Manual mode, write pacing and burst limit). 209 tests pass on Linux, including the BLE worker against a fake bleak scanner and client. Not yet run against the real node or a real Windows PC.

No open questions. Next number when one comes up: HUB-Q37.

ASSUMPTIONS ADDED IN v1.6 (1–45 were accepted). Correct any that are wrong:
46. FS:<0|1> on a Gesture-mode node sets its face-down state, clears its register, and drops a half-finished remove double flip, so a re-sync can never complete a remove or trigger any action. On a Manual-mode node it only updates the physical state shown in the node mirror; the virtual face state and the register are left alone, because manual nodes don't use orientation.
47. 3+ button chord (v1.5 X3): in Manual mode it is a clear only, never L/R/V/F, even when the third button is added after the first two were already handled. On the head it also cancels, the same rule as 2+ buttons, which section 15 X3 says is acceptable. In Gesture mode chords follow the normal rule (clear; cancel on the head).
48. Fast reconnect (v1.5 X2): the remembered address is the last one seen in a good session or in a fresh lobby scan. It is tried once per outage; if that fails, the hub scans by name until the next good session, with the v1.4 backoff.
49. Write pacing (v1.5 X5) applies to every write, not only after full resends. Writes are at least 5 ms apart, with at most 10 in any 100 ms (hub-only config keys ble_write_gap_ms, ble_burst_max, ble_burst_window_ms). A full resend after HELLO (8 fields plus a PING) takes about 60 ms.
50. PING goes to the front of the paced write queue, and the missed-PONG count only starts once the PING write has completed. Pacing therefore can't cause a false "lost" after a burst of display updates.

X4 (firmware low-battery threshold 20 %) needs no hub change: battery_warn_percent is already 20.

PROTOCOL CHANGES PROPOSED: none. HINT text is capped at 14 characters as confirmed (HUB-Q35), and FS: is handled as specified.
=== END SPEC QUERY ===
```
