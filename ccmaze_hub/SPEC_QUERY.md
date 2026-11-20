# Spec queries for prompt_desktop_game.md

**Current spec: v1.11.** Section 21 (one button per primitive, both modes) supersedes section 20's layout. Every question so far has been answered:

| Questions / assumptions | Status |
|---|---|
| Q1–Q21 (historical) | confirmed in v1.1 |
| HUB-Q22–HUB-Q25 | confirmed in v1.3 |
| HUB-Q26–HUB-Q36 | confirmed or resolved in v1.6 |
| HUB-Q37–HUB-Q39 | confirmed in v1.7 |
| HUB-Q40–HUB-Q44 | confirmed in v1.9 |
| assumptions 1–50 | accepted |
| assumptions 51–80 | sent with v1.7–v1.10, not yet confirmed |
| assumptions 81–88 | new in v1.11, below |

There are no open `SPEC-HUB-Q` markers in the code. Below is the milestone Spec Query to relay. It has no questions, only assumptions 81–88.

```
=== SPEC QUERY ===
From: Desktop Game AI
Spec: prompt_desktop_game.md, version 1.11 (section 21)
Status: section 21 applied.
  - One layout in both modes: TL = V, TR = F, BL = L, BR = R; any 2+ buttons together = clear only.
  - Manual mode uses the same layout. Gesture mode also accepts the physical flip.
  - Remove = F twice in both modes; long-press F is retired. The lockout applies in both modes.
  - Tutorial texts, the controls test, virtual nodes, config and README are updated.
182 tests pass on Linux, including every test listed in 21.3. Not yet run against real nodes.

No open questions. Next number when one comes up: HUB-Q45.

v1.11 supersedes the v1.10-specific parts of assumptions 78–80 (the chord map, Manual chords, the F / V chord helpers).

ASSUMPTIONS ADDED IN v1.11 (51–80 not yet confirmed). Correct any that are wrong:
81. With the long press gone, the F button acts when the 100 ms chord window closes, as every other button does, rather than on release (HUB-Q30's "act on release" existed only to tell a short press from a long one). Holding F longer does nothing more.
82. manual_f_long_press_ms stays in config.json for compatibility. It is unused and isn't offered on the Settings screen (like gesture_idle_window_ms and flip_commit_window_ms since v1.9).
83. In both modes the F button toggles the logical face state, with the HUB-Q41 snap-back. manual_auto_face_up therefore only still matters when a chunk is dropped because a node was lost: the old head then lies face-up again.
84. Saved button maps equal to an old default are migrated to the v1.11 layout: the v1.7, v1.8 and v1.10 Gesture defaults, and the v1.0–v1.10 Manual default. A "chords" entry is dropped from any saved Gesture map, because chords only ever clear now. Other customised maps are kept.
85. A press made while another button is still held, but outside the 100 ms window, still counts as a chord: it clears and never acts. A chord whose buttons land one at a time (e.g. the firmware's B:F) clears too.
86. Virtual nodes: the four corner buttons (V, F, L, R), a flip toggle (only Gesture mode acts on it), and a "clear" helper, because a mouse can't press two corners at once. Keys: Q E Z C = V F L R, X = clear. The v1.10 F / V helpers and the V / B keys are gone.
87. The controls test shows TL=V, TR=F, BL=L, BR=R and "two = clear" for both schemes. Its live lights are in the order V, F, L, R, clear. The flip check is shown for Gesture nodes only.
88. The tutorial gives both schemes the same instructions and node hints. The only difference is that Gesture texts add "or turn the node face-down" for F.

PROTOCOL CHANGES PROPOSED: none.
=== END SPEC QUERY ===
```
