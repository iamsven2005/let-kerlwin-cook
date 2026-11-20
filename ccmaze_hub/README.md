# Command Card Maze Race: desktop hub

This is the hub for the maze-racing game. It owns all game logic and state. Each player **builds a route** from their corner to the centre tile of a symmetric maze out of Command Cards dealt onto controller **nodes**, one chunk at a time; when the route reaches the centre the avatar walks it, and the first avatar to arrive wins. A node is either a physical TinyScreen+ over Bluetooth LE or a virtual node on screen. The hub treats both the same way, and any mix of up to 6 works.

Implements spec `prompt_desktop_game.md` **v1.11**. That covers:
- v1.4: the 52-card shape catalog, sequences built as a left-to-right row, the tutorial, and the BLE fixes from the first hardware test.
- v1.5's firmware-side items: the `FS:` sync, stable addresses, the button-chord rule and write pacing.
- **v1.8 (section 18):**
  - **route building** replaces push-by-push steering: pushed chunks are locked into a route, and the avatar walks it once it reaches the centre
  - new **FOR … FOR** blocks and the **IF** transform
  - Gesture mode is now **buttons plus the physical flip**: no tilt, no V tap, and **no cancel** in either mode
- **v1.9 (section 19):** a single press **latches** its bit for `latch_ms` (3 s) after the most recent press, with a countdown bar on the hub
- **v1.11 (section 21):** **one button per primitive, the same in both modes**: top-left = V, top-right = F, bottom-left = L, bottom-right = R, and any two or more buttons together clear the latched register. Gesture mode also accepts the physical flip; Manual mode's long-press F is retired (remove = F twice in both modes). This replaces the v1.10 layout (V = both lower, F = both top).

Every spec question so far (up to HUB-Q44) is answered, and there are no open `SPEC-HUB-Q` markers in the code. The latest Spec Query and assumptions are in `SPEC_QUERY.md`. Answered decisions are referenced as `spec HUB-Q<n>` comments.

Runs on **Linux (BlueZ, primary)** and **Windows (WinRT)** from the same code base.

---

## Setup

Requires Python **3.10–3.13**. pygame 2.6.1 has no wheels for newer versions yet.

Run the setup script on each machine. **Never copy `.venv` between machines or operating systems.** The setup scripts detect a venv that came from another OS and rebuild it.

**Linux**
```bash
./setup.sh              # creates .venv, installs requirements.txt, runs doctor.py
./run.sh                # start the hub
./run.sh --no-ble --virtual 4 --players 2 --start    # quick play-test without Bluetooth
```

**Windows**
```bat
setup.bat               :: or: powershell -ExecutionPolicy Bypass -File setup.ps1
run.bat
run.bat --no-ble --virtual 4 --players 2 --start
```

Both setup scripts accept a flag that always rebuilds the venv: `--recreate` for `setup.sh`, `-Recreate` for `setup.ps1`.

### `doctor.py` (environment check)

`python doctor.py` (or `.venv/bin/python doctor.py`) prints `[ OK ] / [WARN] / [FAIL]` lines with a fix for each problem. Add `--no-scan` to skip the scan, or `--scan-seconds N` to change its length.

| Checked on | What it checks |
|---|---|
| All | Python version; whether it's running in a venv; the pinned packages; a BLE scan that lists any `CCNODE-` nodes |
| Linux | adapter present (`/sys/class/bluetooth`); rfkill blocks; BlueZ version (≥ 5.55); `bluetooth` service running; adapter powered; `bluetooth` group (informational only; the scan is the real permission test); the `[LE]` connection-interval setting |
| Windows | build ≥ 16299 (Windows 10 1709+); Bluetooth radio present and on |

### BLE connection interval (hub side)

The nodes request a 7.5–15 ms interval so tap latency stays low and steady, which keeps button presses responsive.

- **Linux:** BlueZ grants the request only within the limits in `/etc/bluetooth/main.conf`. `setup.sh` **offers** to set `MinConnectionInterval=6` and `MaxConnectionInterval=12` under `[LE]` (units of 1.25 ms). It asks first, uses sudo, backs up the original file to `main.conf.ccmaze-backup-<timestamp>`, and restarts `bluetooth`. You can also run the tool by hand:
  ```bash
  python tools/bluez_conn_interval.py status
  sudo .venv/bin/python tools/bluez_conn_interval.py apply
  sudo .venv/bin/python tools/bluez_conn_interval.py undo    # restores the newest backup, restarts bluetooth
  ```
  To undo manually instead, copy the backup over `/etc/bluetooth/main.conf` and run `sudo systemctl restart bluetooth`.
- **Windows:** the OS chooses the interval and it can't be configured. Use the node's debug mode to print the negotiated interval.

### Moving between Linux and Windows

- Source files use LF line endings. `.gitattributes` keeps `.bat`/`.ps1` as CRLF.
- `config.json` stores no absolute paths. `card_catalog_file` is relative to the config file's folder.
- All paths in the code use `pathlib`. All OS-specific BLE handling is in `ccmaze/nodes/ble_platform.py`.
- After moving the project, just run that OS's setup script.

## Running

```
run.sh / run.bat [--config PATH] [--virtual N] [--players N] [--start] [--no-ble] [--window WxH] [-v]
```

By default `config.json` is read from the project folder, and it is created with the defaults if it's missing. The window is resizable: everything is drawn on a 1600×960 canvas and scaled to fit.

## Tests

```bash
.venv/bin/python -m pytest -q        # Windows: .venv\Scripts\python -m pytest -q
```

182 headless tests, covering:

- the catalog (52 cards, shape families, invalid-card rejection, dominoes that can't be disabled)
- the deck-size rule, the domino guarantee and the IF/FOR share
- **every row of the 18.3 table**, IF waiting for the next chunk, FOR blocks spanning chunks, non-nesting blocks, and the locked-route prefix property
- **route completion**: mid-chunk (the rest ignored), held back by an open FOR block or a pending IF and completing when it closes / resolves, walls on the trace
- **the walk**: the flash delay, timing equal to the estimate, walls, first to arrive wins (a later builder with a shorter route wins), ties and draws, one huge tick vs many small ones, discard at push
- the 3 s latch: holds then clears with no action, restarts on every press, the flip commits what is latched, V then L at 2.9 s adds but at 3.1 s only L is latched, two buttons clear early, `REG:` is sent on the timeout clear
- the v1.11 layout in both modes: each single button maps correctly, every 2-button and 3+ combo only clears, a chord landing one button at a time never acts, F twice removes (Manual too), a long F is just an F, the lockout in both modes
- the action table in both modes: start, literal left/right adds, push, F-button snap-back after a discard, stash in / use stash / discard stash, no cancel anywhere; mixed schemes in one player
- Gesture's physical flip: mixed with the F button, the double-F remove, the lockout keeping state in step, switching it off; tilt and V tap ignored; nodes inactive while walking
- config: the removed keys are dropped and the v1.7 Gesture button map is migrated
- conveyor sliding, maze fairness, the silent HELLO / `FS:` syncs and lost-node rules
- random-event fuzzing with card-conservation invariants
- the tutorial: controls test checks and all nine lessons in both schemes
- the BLE worker against a fake bleak scanner and client: HELLO wait, PING/RTT, write errors, missed PONGs, fast reconnect to a stable address, fallback to scan-by-name, backoff, the Reconnect button, stale-device removal, write pacing and the burst limit
- the BlueZ config tool, `doctor.py`, and a Pygame smoke test

---

## Lobby (setup)

1. **Physical nodes.**
   - **Scan BLE** lists nodes advertising as `CCNODE-<id>`, with their ID and RSSI.
   - **Connect** a node, and use **Blink** to find out which device it is.
   - Once a node is known, the hub keeps it connected by itself.
   - **Reconnect** forces an immediate scan-and-connect.
2. **Virtual nodes:** **+ Virtual node** (or Ctrl+V). There's a limit of 6 nodes in total.
3. **Players:** set the player count (1–4) and assign each node to a player with the left/right arrows.
4. **Hand numbers:** use the up/down arrows, or **Assign hand # by press** (press any button on each node, top to bottom).
5. **Scheme per node:** Gesture or Manual. Then press **Start game** (or Enter), or **Tutorial**.

The node table shows each physical node's battery (marked LOW below `battery_warn_percent`) and its link status. Warnings, such as cards rejected from `cards.json`, appear under the table.

## Tutorial (18.8)

**Tutorial** in the lobby starts with a **controls test** for every player who has connected nodes:
- **Button check:** each button shows what it means and ticks green once pressed: `TL=V`, `TR=F`, `BL=L`, `BR=R`, and `2+=clear` for any two or more together (21.1, both modes).
- **Flip check** (Gesture nodes): `flip down` and `flip up` tick green once the node reports `F:1` and `F:0`.
- **Live lights** `L R V F clear` show what each input actually resolved to, through the same controller the game uses, with the latch countdown under the register.
- The node's register, held buttons, face state, link and its last raw events.

**Start lessons** (or Enter) begins the lessons. Every player gets their own 7×7 practice maze and scripted deals, and moves at their own pace.

| # | Lesson | Completes when |
|---|---|---|
| 1 | Discard | a discard |
| 2 | Start a sequence (V, then F) | a start |
| 3 | Add right, push, start again, add left | an add-right, a push, then an add-left (multi-node only) |
| 4 | Remove a card (F twice on the member) | a remove (multi-node only) |
| 5 | Push chunks and watch the route grow | two chunks in the route |
| 6 | Stash in / use stash / discard stash | all three (players with a stash) |
| 7 | IF: transform and blank | the route has one IF that transformed and one that became a blank |
| 8 | FOR … FOR block | a closed block whose two FORs were pushed in different chunks |
| 9 | Build a route to the centre (9×9 maze) | the avatar arrives |

- The instructions say plainly that a started sequence can't be cancelled; there are no cancel steps.
- Lessons 1 and 6 teach the latch (19.3): a press stays lit for 3 s and a flip does what is lit (L lit + flip = stash in, not discard). "Made a mistake? Press any two buttons together", or wait 3 s.
- Gesture and Manual players get the same instructions, except that Gesture texts add "or turn the node face-down" for F.
- Single-node players do lessons 7 and 8 across pushes (an IF at the end of a chunk waits for the next one; a FOR block stays open across pushes).
- If a practice route reaches the centre before a lesson is done, the lesson starts a fresh route and keeps what the player has already done.
- Lessons that can't apply to a player are marked `-` and skipped (spec HUB-Q32). The instructions match the player's scheme, and each node shows a short `HINT:`.
- **Skip lesson** and **Restart** are per player. **Quit tutorial** (or Esc) returns to the lobby.

## Game screen

- **Maze** with every player's route (18.4), in the player's colour, offset slightly so overlaps stay readable:
  - the **locked route** solid from the spawn corner
  - the **pending chunk** (the row on the table) dashed and bright, continuing from the route's end, with row-position markers; it updates live as cards are added or removed
  - an **open FOR block**: its cards drawn once, faint and dashed, marked **x?**; recomputed when the block closes. A closed block in the pending chunk is marked **xN**
  - a **pending IF** as a gold **?** where the trace currently ends
  - wall stops as short red stubs; a gold ring on the centre when pushing the pending chunk would complete the route
  - completion only counts on the resolved trace, never inside an open block or past a pending IF
  - a completed route **flashes** for `route_flash_s`, then the avatar walks it; the walked part is drawn brighter
- **Player panels:**
  - status: `building`, route length in tiles, number of chunks and the estimated walk time; while walking, `WALKING` with a progress bar and the time to go
  - the hand, with each card's row badge (`HEAD 2`, `#3`), face-down marker, battery and an X for lost nodes, and the **latched bits with a countdown bar** (19.2)
  - the stash
  - the **route list**: the locked chunks in order as compact card icons with thin dividers between chunks, then the pending chunk highlighted. FOR blocks get a bracket labelled `x?` / `xN`; a transformed IF shows its new shape with an `IF` tag, a pending IF shows `IF?`, a blank is an empty card
- **Info bar:** deck and discard counts, deck size, maze seed, shortest path and elapsed time.
- **Node strip:** one widget per node, mirroring the node's screen (register strip with colour bar and hand number, card, badge, hint line; `MSG` such as `WALKING` full-screen).
- **Win overlay:** Rematch (same maze), New maze, or Lobby.

**Card style**, shared with the firmware:

| Card | How it's drawn |
|---|---|
| Movement | hollow start cell, one block per step, segment colours `E0E0E0` / `56CCF2` / `BB6BD9` (cycling), arrow on the last block |
| IF | `IF`, the condition family badge, then the action card |
| FOR | `FOR` with a loop arrow |

### Virtual node widget and keyboard shortcuts

- Four corner buttons, held while the mouse is down: TL = V, TR = F, BL = L, BR = R in both modes (21.1).
- **Flip** turns the node over (its physical face state; only Gesture mode acts on it). **clear** presses two buttons together, since a mouse can't press two corners at once.
- A countdown bar under the screen shows how long the latched bits have left (19.2).
- No tilt, tap or bump controls (18.8).

| Key | Action | Key | Action |
|---|---|---|---|
| 1–6, Tab | select node | F / Space | turn the node over |
| Q E Z C | hold button TL TR BL BR = V F L R | X | two buttons together (clear) |
| F12 | BLE debug view | H | help, Esc: back |

---

## Route building (18.1)

- Every player builds at the same time. The avatar **waits at spawn** while its player builds.
- **Push** resolves the chunk and **appends it to the end of the route**. A pushed chunk is locked for good. Its cards go to the **discard pile at push time** (18.1.8): the route keeps its own copy of the moves, so long routes don't drain the deck.
- The route is traced from the spawn corner with the usual wall rule: a move follows each segment until it's blocked, then continues with the next segment.
- As soon as the definite trace **enters the centre**, the route is complete; the rest of that chunk is ignored. Not inside an open FOR block, not past a pending IF: those are rechecked when the block closes or the IF resolves.
- On completion the route flashes for `route_flash_s` (1 s, spec HUB-Q42), then the avatar walks it using `tile_move_time_s`, `pause_after_step_s` and `pause_between_cards_s`. The flash is the same for everyone, so it never changes who arrives first.
- **The first avatar to reach the centre wins**, even if its player finished building later. Ties: the earlier arrival, then the earlier completing push, then a draw (spec HUB-Q5).
- While a player's avatar walks, that player's nodes do nothing and show `MSG:WALKING`.
- Removed (18.1): the per-avatar queue, live execution while building, `pause_between_sequences_s`, the HUB-Q27 discard timing and the optional interrupt mode.

## The row and the action table (18.5-18.7)

A chunk is built as a **physical row of nodes** on the table, executed **left to right**. The first card placed is the **head**; new cards join at **either end**, so the head can sit anywhere. A *free* node is face-up and not in the row. `SEQ:` badges: `H<n>` for the head at row position n, `<n>` for a member (1 = leftmost).

**Inputs (21.1, both modes):**
- **One button per primitive:** top-left = **V**, top-right = **F**, bottom-left = **L**, bottom-right = **R** (`gesture_button_map` and `manual_button_map`, separate keys with the same default). **Any two or more buttons together only clear** the latched register.
- Every press is resolved when the 100 ms chord window (`manual_clear_window_ms`) from its first press closes, using every button pressed in it (spec HUB-Q37, HUB-Q44), so a two-finger clear never fires an action first. A press while another button is still held, a chord landing one button at a time and the firmware's `B:F` all just clear. Releases mean nothing: there is no long press any more.
- The **F button toggles the logical face state**. After an action that leaves the node free (discard, stash in, discard stash, or nothing recognised) it snaps back face-up (spec HUB-Q41); a start leaves the head logically face-down.
- **Gesture mode** also accepts the **physical flip** (`F:1` / `F:0`), which sets the state directly (`physical_flip_enabled`); the two can be mixed. That is the only difference between the modes. `DOFF` follows only the physical orientation; Manual nodes always get `DOFF:0`. Tilt and the V tap are ignored (`accept_tilt` / `accept_tap_v`, both false).
- **The latch (19):** a single L, R or V sets its bit and keeps it **latched until `latch_ms` (3000) after the most recent press** on that node; every press restarts the timer. When it runs out the register clears silently, with no action, and the node gets the new `REG:`. This replaces `gesture_idle_window_ms` and `flip_commit_window_ms` (kept in `config.json`, unused). After any action the register clears; a node that joins the row loses its latched bits.

| Action | Input (both modes) | Condition |
|---|---|---|
| Discard | F, empty register | free node |
| Start sequence | V, then F | free node, no open sequence; the head is logically face-down |
| Add **left** / **right** | V, then **L** / **R** | free node, open sequence; it stays face-up (place it at that end of the row too) |
| Push | F on the head | appends the chunk to the route |
| Remove | **F twice** on a member within `remove_window_ms` (3 s) (Gesture: the button or physically down then up, mixed is fine) | non-head member |
| Stash in | L, then F | free node, stash enabled and empty |
| Discard stash | R, then F | free node, stash not empty |
| Use stash | V on the face-down head (immediately) | the stash card joins at the **right end** |
| Clear latched bits | any 2+ buttons together | any node |

- **No cancel, in any mode** (18.6): the head can't be removed, and a started chunk only ends by being pushed. The one exception is a system event, not a gesture: if a node in the open chunk is lost, the chunk is dropped and its cards stay in the hand (spec HUB-Q22 kept, spec HUB-Q43).
- Inputs on row members are ignored, except the remove gesture. The head only reacts to V (use stash) and F (push).
- **Lockout (both modes):** after a push or a discard, that node ignores F (button or flip) for `manual_f_lockout_ms` (600 ms), so a double press can't push and then discard. A physical flip in that time still updates the logical face state, silently.
- F commits whatever is latched, so a stray press changes its meaning for 3 s (L latched + F = stash in, not discard). The strip shows the latched bits; clear them with any two buttons together.
- With `manual_auto_face_up` on, a Manual node whose chunk was dropped lies face-up again.

---

## Cards and deck

**Catalog** (`cards.json`, 52 movement cards, 14 B1). Cards are named after the shape the avatar traces, start cell included. Every rotation, mirror and direction of travel is its own card.

| Family | Code | Shape | Steps |
|---|---|---|---|
| Domino | `D` | 1×2 | 1 |
| I3 | `I3` | 1×3 line | 2 |
| I4 | `I4` | 1×4 line | 3 |
| Corner | `V` | 2×2 minus one | 2 |
| Square | `O` | 2×2 | 3 |
| L | `L` | 3×2 L/J | 3 |
| S | `S` | 3×2 S/Z | 3 |

How `cards.json` is loaded:
- Each family and card has `enabled` and `weight`. Each card's family code is stored with it.
- Cards with more than 4 steps, or a path that revisits a cell, are rejected and shown as a lobby warning.
- A card listed under a family whose shape it doesn't trace gets a warning.
- The dominoes `U D L R` can't be disabled. If they're disabled or missing they are put back, with a warning (spec HUB-Q29).
- `"control_cards": {"IF": {"enabled": …}, "FOR": {"enabled": …}}` switches the control cards (spec HUB-Q28).

**Deck size** (14 A3):
- Movement cards = `max(deck_multiplier × total hand size, min_copies_per_type × enabled movement types)`.
- There are never fewer than `min_copies_per_type` of each type, or fewer than `min_domino_copies` of each domino.
- Control cards: each enabled kind gets `max(1, round(control_card_share × movement / 2))`.

With the defaults, one player with one node gets 52 + 4 extra dominoes + 4 IF + 4 FOR = **64 cards**, so every maze stays solvable.

**Control cards** (18.2-18.3), resolved over the whole route as written: IFs first, then FOR blocks, then the movement is traced.
- **FOR** (`CARD:FOR`): the first FOR opens a block and the next FOR closes it. With N cards between them the block runs N times, in order (`FOR A B C FOR` → A B C ×3, `FOR A FOR` → A, `FOR FOR` → nothing). Both FORs produce no movement. Blocks don't nest, and a block can span chunks: it stays open until a closing FOR is pushed.
- **IF** (`CARD:IF:<family>:<steps>`): resolved once, against the card written directly after it in the route. A movement card of the IF's family (any orientation) → the IF **becomes its action shape** at that position. Anything else, including IF and FOR → a **blank** (no movement). Inside a FOR block the transformed shape or the blank repeats with the block. An IF that is the last card of a chunk waits for the next chunk's first card.
- N counts every card between the FORs, a blank IF included (spec HUB-Q40).
- Each IF's condition and action are drawn at deck-build time, weighted by the catalog.

## Rules as implemented (unchanged)

- **Gravity conveyor:** locked (row) and frozen (lost) slots are skipped.
- **Exact-timeline walk:** a blocked step skips the rest of its segment at no time cost; events are processed in time order across avatars, independent of frame rate.
- **Maze:** 4-fold symmetric backtracker maze with braiding, always verified with BFS.
- **Lost nodes:** a node is lost on a disconnect or after 3 missed PONGs. Its slot freezes, and the open chunk is dropped only if the lost node was in it.

---

## Configuration

`config.json` holds every key from section 5, plus the v1.4 keys:

| Key | Default |
|---|---|
| `min_copies_per_type` | 1 |
| `min_domino_copies` | 2 |
| `control_card_share` | 0.15 |
| `remove_window_ms` | 3000 |
| `manual_f_lockout_ms` | 600 (both modes since v1.11) |
| `gesture_button_map` / `manual_button_map` (21.1) | `{"bit0": "V", "bit1": "F", "bit2": "L", "bit3": "R"}` each |
| `latch_ms` (19) | 3000 |
| `physical_flip_enabled` (18.5) | true |
| `accept_tilt` / `accept_tap_v` (18.5) | false / false |

Hub-only extras:

| Key | Default | Purpose |
|---|---|---|
| `ble_ping_interval_s` | 2 | ping interval |
| `ble_scan_timeout_s` | 5 | scan length |
| `ble_reconnect_min_s` / `ble_reconnect_max_s` | 1 / 10 | reconnect backoff |
| `ble_write_with_response` | false | diagnostic, see spec HUB-Q34 |
| `ble_write_gap_ms` / `ble_burst_max` / `ble_burst_window_ms` | 5 / 10 / 100 | write pacing (v1.5 X5) |
| `route_flash_s` | 1.0 | flash a completed route before the walk (spec HUB-Q42) |

Removed in v1.8 (18.9): `pause_between_sequences_s`, `optional_features.interrupt_mode` and `bump_correlation_ms`. Old config files still load; those keys are dropped, and a saved `gesture_button_map` / `manual_button_map` equal to an old default is migrated to the v1.11 layout (a `chords` entry from v1.10 is dropped: chords only ever clear now). `gesture_idle_window_ms` and `flip_commit_window_ms` (deprecated by `latch_ms`, 19.1) and `manual_f_long_press_ms` (retired, 21.1) stay in `config.json` for compatibility, unused, and hidden from Settings. Everything is editable on the Settings screen.

---

## BLE

**Connection life cycle** (14 A1/A2). Every known node runs this loop until it is removed from the lobby, in every state (lobby, tutorial, game, win screen, settings):

1. **Fast first attempt** (v1.5 X2): firmware 1.4+ keeps a fixed address, so the hub first connects straight to the remembered address, without a scan.
2. If that fails (e.g. the one-time address change when the firmware goes from 1.3 to 1.4), the hub scans for the node's own name (`CCNODE-<id>`) and connects to the freshly discovered device. It keeps doing this until the next good session.
3. Subscribe to notifications, then wait for HELLO (up to 5 s). Only after HELLO does the hub write display state.
4. Run the session: PING every 2 s, and every write is awaited.
5. When the link is lost, tear everything down (stop notifications, disconnect, drop references) and start again.

Further details:
- Failed attempts back off 1, 2, 4, 8 s, then every 10 s. **Reconnect** in the lobby skips the wait.
- One shared scanner serves the lobby scan and all reconnect loops. It pauses while a connect is in progress.
- On Linux, a connect failure that looks like a stale BlueZ device makes the hub remove that device over D-Bus (`org.bluez.Adapter1.RemoveDevice`) and retry once. This lives in `ble_platform`.
- After 3 missed PONGs the hub drops the link and reconnects (HUB-Q23). PING jumps the write queue, and its PONG clock starts only once it has actually been sent.
- **Write pacing** (v1.5 X5): every write is at least `ble_write_gap_ms` (5 ms) after the previous one, and at most `ble_burst_max` (10) messages go out in any `ble_burst_window_ms` (100 ms). A full resend after HELLO (8 fields plus a PING) takes about 60 ms.
- A failed write is logged and ends the session, so recovery always goes through a fresh connect.

**BLE debug view (F12)**, on any screen:
- Per node: link status and address, last PING sent, last PONG received, round-trip time, write counts (ok/failed) and missed PONGs.
- A live, timestamped log of every message sent (`>`, logged when the write completed), every message received (`<`), errors (`!`) and connection events (`*`).
- **Save log** writes a snapshot to `logs/ble_debug_<time>.txt`.
- Every line also goes to the rolling `logs/ble_debug.log` while the hub runs. Please send that file after the next hardware test.

### Protocol summary (contract with the firmware)

The protocol is newline-terminated ASCII, at most 20 bytes per message, over the Nordic UART Service:

| | UUID |
|---|---|
| Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| RX (hub writes without response) | `6E400002-…` |
| TX (notify) | `6E400003-…` |

Nodes are discovered by the name prefix `CCNODE-`.

| Node → hub | Meaning |
|---|---|
| `HELLO:<id>:<fw>` | on subscribe, followed by one `F:`, one `B:` and one `BAT:`: a silent state sync. The hub then resends the full display state. |
| `F:1` / `F:0` | flip |
| `FS:1` / `FS:0` | silent face-state sync after the node's accelerometer recovers (v1.5 X1): Gesture nodes take the new state and clear their register; never an action |
| `T:L` `T:R` `T:V` | tilt / tap; ignored since v1.8 unless `accept_tilt` / `accept_tap_v` |
| `B:<hex>` | buttons |
| `BAT:<0-100>` | battery |
| `PONG` | ping reply |

| Hub → node | Meaning |
|---|---|
| `CARD:<steps>` / `CARD:IF:<fam>:<steps>` / `CARD:FOR` / `CARD:-` | the card; clears `MSG` |
| `SEQ:H<n>` / `SEQ:<n>` / `SEQ:-` | head at row position n / member at position n (1 = leftmost) / none |
| `REG:<RLVF>` | register |
| `COL:<RRGGBB>` | player colour |
| `HAND:<n>` | hand number (0 = unassigned) |
| `DOFF:0/1` | display-off policy |
| `MSG:<≤14>` | message (`WALKING` while the avatar walks, the win message at the end) |
| `HINT:<≤14>` | hint, capped at 14 chars so it fits 20 bytes (spec HUB-Q35); not cleared by `CARD:` |
| `BLINK` | flash |
| `PING` | ping |

Display fields are diffed and sent in the order `COL HAND CARD SEQ REG DOFF MSG HINT`.

---

## Architecture

```
main.py, doctor.py, setup.sh / setup.ps1 / setup.bat, run.sh / run.bat, tools/bluez_conn_interval.py
ccmaze/config.py             defaults, load/save, key migration
ccmaze/sim/                  headless simulation (no pygame imports)
  cards.py                   movement cards + families, IF/FOR control cards, catalog loading
  deck.py                    v1.4 deck rules, weighted round-robin, scripted tutorial deck
  resolve.py                 route IF/FOR resolution (18.2-18.3) + the one movement rule (next_action)
  preview.py                 route trace, completion, pending-chunk preview, walk-time estimate
  execution.py               avatars and the walk (exact-timeline step engine)
  hand.py, maze.py, hooks.py conveyor, symmetric maze, optional-feature hooks
  game.py                    game state, the row model, routes, every action
  controls.py                registers, 3 s latch, chord window, logical face state, the action table
  tutorial.py                lessons, per-player practice games, completion checks
ccmaze/nodes/                Node ABC + hub (diffing, debug log), VirtualNode, PhysicalNode + BleWorker
                             (shared scanner, reconnect loop), ble_platform (all OS-specific BLE), protocol
ccmaze/ui/                   lobby, game screen, maze_view (map), route_view (route list + map trace), tutorial screen,
                             settings, node widgets, BLE debug view
tests/                       pytest suite
```

## Troubleshooting

- **Start with `doctor.py`.** Every `[FAIL]` line comes with its fix.
- **A node shows "NO HUB" while the lobby says it's connected.** Open the debug view (F12) and look at that node:
  - `>` PING lines every 2 s with `<` PONG replies and an RTT: the hub side is fine, and the node isn't processing what it receives.
  - `>` PING lines but no PONG: the writes complete on the hub, so they're being lost on the way or on the node. Try `ble_write_with_response: true` (spec HUB-Q34).
  - Writes that take long to complete (shown as "write took … ms") point to a slow link. The interval tuning in `setup.sh` helps.
  - No `>` lines at all, or `!` write errors: the problem is on the hub or link side.
  - Save the log and send it.
- **A node doesn't come back after a power cycle.** The debug view shows `scanning for CCNODE-<id>` and any `!` attempt failures. Reconnect forces an immediate retry.
- **No Bluetooth:** use virtual nodes (`--no-ble`).

## Known limitations

- The new BLE life cycle has only been exercised against a fake bleak scanner and client so far. It still needs a run with the real node.
- `setup.ps1` and `doctor.py` haven't yet been run on a real Windows PC.
- The new route and control rules have only run headless and with virtual nodes so far.
- Physical and virtual nodes can be mixed in one row: an add only needs the new node's own V and L / R.
