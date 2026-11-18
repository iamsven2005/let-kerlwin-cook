# Command Card Maze Race: desktop hub

This is the hub for the maze-racing game. It owns all game logic and state. Players steer avatars through a symmetric maze toward the centre tile using Command Cards dealt onto controller **nodes**. A node is either a physical TinyScreen+ over Bluetooth LE or a virtual node on screen. The hub treats both the same way, and any mix of up to 6 works.

Implements spec `prompt_desktop_game.md` **v1.6**. That covers:
- v1.4: the 52-card shape catalog, IF/FOR control cards, sequences built as a left-to-right row, the path preview, the tutorial, and the BLE fixes from the first hardware test.
- v1.5's firmware-side items: the `FS:` sync, stable addresses, the button-chord rule and write pacing.

All spec questions so far (up to HUB-Q36) are answered. The latest Spec Query and assumptions are in `SPEC_QUERY.md`. Confirmed decisions are referenced as `spec HUB-Q<n>` comments; any future open question would be marked `SPEC-HUB-Q<n>`.

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

The nodes request a 7.5–15 ms interval so tap latency stays low and steady, which bump detection depends on.

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

209 headless tests, covering:

- the catalog (52 cards, shape families, invalid-card rejection, dominoes that can't be disabled)
- the v1.4 deck-size rule, the domino guarantee and the IF/FOR share
- every IF/FOR case in the brief's B2 table, plus provenance and discard ownership
- the new action table in Gesture and Manual mode: left/right ordering, double-flip remove, long-press F, the F lockout, R+L to discard the stash
- a check that the path preview matches real execution on random mazes and rows, including mid-hop
- conveyor sliding, movement, ties, maze fairness, the silent HELLO sync and lost-node rules
- random-event fuzzing with card-conservation invariants
- the tutorial lessons in both schemes
- the BLE worker against a fake bleak scanner and client: HELLO wait, PING/RTT, write errors, missed PONGs, fast reconnect to a stable address, fallback to scan-by-name when the address changed, backoff, the Reconnect button, stale-device removal, write pacing and the burst limit
- the `FS:` silent face-state sync, and Manual-mode button chords
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
4. **Hand numbers:** use the up/down arrows, or **Assign hand # by V-tap**.
5. **Scheme per node:** Gesture or Manual. Then press **Start game** (or Enter), or **Tutorial**.

The node table shows each physical node's battery (marked LOW below `battery_warn_percent`) and its link status. Warnings, such as cards rejected from `cards.json`, appear under the table.

## Tutorial (14 B5)

**Tutorial** in the lobby starts with a **controls test** for every player who has connected nodes. It shows each node's raw events (taps, flips, buttons), its register, face-up/face-down state, held buttons and link, so players can check their gestures register. **Start lessons** (or Enter) begins the lessons.

Every player gets their own 7×7 practice maze and scripted deals, and moves through the lessons at their own pace.

| # | Lesson | Completes when |
|---|---|---|
| 1 | Flip to discard | a discard |
| 2 | Start a sequence | a start |
| 3 | Add a card to the right, then the left | both kinds of add (multi-node only) |
| 4 | Remove a card | a remove (Gesture: double flip; Manual: hold F) (multi-node only) |
| 5 | Push and watch the path | the pushed path has finished executing |
| 6 | Stash in / use stash | a stash-in and a use-stash (players with a stash) |
| 7 | IF card combo | a push where the IF fired |
| 8 | FOR card combo | a push where the FOR took effect |
| 9 | Free practice | reaching the centre of a 9×9 maze |

- Lessons that can't apply to a player are marked `-` and skipped (see spec HUB-Q32).
- The instructions match the player's control scheme. Manual players see the long-press version.
- Each node also shows a short `HINT:`.
- **Skip lesson** and **Restart** are per player. **Quit tutorial** (or Esc) returns to the lobby.

## Game screen

- **Maze** with the **path preview** (14 B4):
  - Each player's row on the table is drawn as the exact path the avatar will take, in the player's colour. It updates live as cards are added or removed.
  - The path starts where the queued sequences end. FOR and IF are resolved exactly as at push time.
  - Each card's movement begins at a numbered marker (its row position). An IF's bonus move is dashed and labelled **IF**.
  - FOR repeats are labelled **×n**. Wall stops are short red stubs.
  - Queued and running sequences are drawn the same way but fainter, with the next step highlighted. Players' paths are offset slightly so overlaps stay readable.
- **Player panels:**
  - the hand, with each card's row badge (`HEAD 2`, `#3`), face-down marker, register letters, battery and an X for lost nodes
  - the stash
  - a one-line summary of the row (cards → moves, whether IF/FOR fire)
  - a compact queue count
- **Info bar:** deck and discard counts, deck size, maze seed, shortest path and elapsed time.
- **Node strip:** one widget per node, mirroring the node's screen in the v1.4 firmware layout:
  - the register strip with colour bar and hand number at the top
  - the card on the left and the badge on the right
  - the hint line at the bottom
  - `MSG` is shown full-screen below the register strip
- **Win overlay:** Rematch (same maze), New maze, or Lobby.

**Card style**, shared with the firmware:

| Card | How it's drawn |
|---|---|
| Movement | hollow start cell, one block per step, segment colours `E0E0E0` / `56CCF2` / `BB6BD9` (cycling), arrow on the last block |
| IF | `IF`, the condition family badge, then the action card |
| FOR | `FOR` with a loop arrow |

### Virtual node widget and keyboard shortcuts

- The corner buttons are held while the mouse is down.
- **tap L / V / R**, **Flip** (the node's physical face-down state), and **TL+TR**.
- **bump <->**: drag it onto the left or right half of another widget to bump that side. The dragged node reports `T:R` when dropped on the left half, `T:L` on the right half.

| Key | Action | Key | Action |
|---|---|---|---|
| 1–6, Tab | select node | F / Space | toggle flip |
| A / W (or S) / D | tap L / V / R | Q E Z C | hold button TL TR BL BR (key held = long press) |
| X | press TL+TR together | `[` / `]` (or B) | bump against the left / right end of the row |
| F12 | BLE debug view | H | help, Esc: back |

---

## Sequences are a row (14 B3)

A sequence is a **physical row of nodes** on the table, executed **left to right**. The first card placed is the **head**, but new cards can join at **either end**, so the head isn't necessarily first. A *free* node is face-up and not in a sequence.

| Action | Gesture mode | Manual mode | Condition |
|---|---|---|---|
| Discard | flip down, empty register | short F | free node |
| Start sequence | V, then flip down | V, then F | free node, no open sequence |
| Add **left** | V, then bump the row's left end (the node reports **T:R**: its right edge touched) | V, then **L** button | free node, open sequence |
| Add **right** | V, then bump the row's right end (reports **T:L**) | V, then **R** button | free node, open sequence |
| Remove | flip a non-head member down, then up within `remove_window_ms` (3 s) | **hold F** ≥ 1 s on the member | non-head member |
| Push | flip the head up | short F on the head | — |
| Cancel | any button on the head | 2+ buttons on the head, or **hold F** on the head | — |
| Stash in | L, then flip down | L, then F | free node, stash enabled and empty |
| Use stash | V tap on the face-down head | V on the head | the stash card joins at the **right end** |
| Discard stash | **R + L** (idle resolve) | R + L | free node, stash not in the row |

- Added nodes **stay face-up** (no flip needed), and the add resolves as soon as the side tap arrives.
- Taps on row members are ignored, since they're bump side effects. The head only reacts to V.
- The old "R + flip" add now does nothing.
- A member flipped down and left past the window stays in the row (spec HUB-Q31).
- {V} on a free node waits up to `flip_commit_window_ms` for a flip (start) or a side tap (add).
- **Manual F:** a short press acts on release, and a long press fires at the threshold (spec HUB-Q30). After a push or a discard, that node ignores F for `manual_f_lockout_ms` (600 ms), so an accidental double press can't push and then discard.
- With `manual_auto_face_up` on, manual nodes that aren't in a row snap back to face-up.
- Manual nodes always get `DOFF:0`.
- Bump correlation is no longer used (spec HUB-Q26).
- **Button chords** (v1.5 X3): in Manual mode, any mask with 3 or more buttons held is a clear only, never L/R/V/F. On the head it cancels, like any multi-button press. The firmware's 4-button debug chord (`B:F`) therefore just clears or cancels, in both modes.

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

**Control cards** (14 B2), resolved at push time (FOR first, then IF, left to right):
- **IF** (`CARD:IF:<family>:<steps>`): if the next card is a movement card of that family, in any orientation, the action card's movement runs just before it. Otherwise the IF does nothing. Each IF's condition and action are drawn at deck-build time, weighted by the catalog.
- **FOR** (`CARD:FOR`): the card right after it runs once for each card after the FOR, and the rest are consumed. If that card is a control card, the FOR does nothing.
- **Discards** (spec HUB-Q27): a card goes to the discard pile when the last move it owns finishes. A FOR goes with its last repeat. Consumed cards and ineffective control cards are discarded when their sequence starts.

## Rules as implemented (unchanged from v1.3)

- **Gravity conveyor:** locked (row) and frozen (lost) slots are skipped.
- **Exact-timeline execution:** a blocked step skips the rest of its segment at no time cost.
- **Win and ties:** the first to enter the centre wins. Ties go to the earliest arrival, then the earlier push, then a draw.
- **Maze:** 4-fold symmetric backtracker maze with braiding, always verified with BFS.
- **Lost nodes:** a node is lost on a disconnect or after 3 missed PONGs. Its slot freezes, and the row is cancelled only if the lost node was in it.

---

## Configuration

`config.json` holds every key from section 5, plus the v1.4 keys:

| Key | Default |
|---|---|
| `min_copies_per_type` | 1 |
| `min_domino_copies` | 2 |
| `control_card_share` | 0.15 |
| `remove_window_ms` | 3000 |
| `manual_f_long_press_ms` | 1000 |
| `manual_f_lockout_ms` | 600 |

Hub-only extras:

| Key | Default | Purpose |
|---|---|---|
| `ble_ping_interval_s` | 2 | ping interval |
| `ble_scan_timeout_s` | 5 | scan length |
| `ble_reconnect_min_s` / `ble_reconnect_max_s` | 1 / 10 | reconnect backoff |
| `ble_write_with_response` | false | diagnostic, see spec HUB-Q34 |
| `ble_write_gap_ms` / `ble_burst_max` / `ble_burst_window_ms` | 5 / 10 / 100 | write pacing (v1.5 X5) |

`bump_correlation_ms` is still accepted but no longer used. Everything is editable on the Settings screen.

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
| `T:R` `T:L` `T:V` | taps, R/L in the node's own frame |
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
| `MSG:<≤14>` | message |
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
  resolve.py                 push-time IF/FOR resolution + the one movement rule (next_action)
  preview.py                 path preview built on resolve.py (always matches execution)
  execution.py               avatars, queue/interrupt modes, step engine over resolved moves
  hand.py, maze.py, hooks.py conveyor, symmetric maze, optional-feature hooks
  game.py                    game state, the row model, every action
  controls.py                registers, idle/commit windows, v1.4 gesture + manual action table
  tutorial.py                lessons, per-player practice games, completion checks
ccmaze/nodes/                Node ABC + hub (diffing, debug log), VirtualNode, PhysicalNode + BleWorker
                             (shared scanner, reconnect loop), ble_platform (all OS-specific BLE), protocol
ccmaze/ui/                   lobby, game screen, maze_view (map + path overlay), tutorial screen,
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
- Adding a card only needs the new node's own side tap, so a single physical node can join a row of virtual nodes by tapping its side. The virtual row node's own tap is simply ignored.
