# Multi-device protocol: interrupt, don't poll

This is the change to the diagram in the brief:

```
Before (polling)
  Brain --polls every 1 s--> IR sensor
  Brain --wheel speeds, every loop--> Motor controller
  Brain --polls every 1 s--> WiFi module

After (interrupt + offload)
  IR node --only on a confirmed change--> Brain : "1"
  Brain  --only on a decision change--> Motor : "M<n> DRIVE / TURN / STOP"
  Motor  --only on completion--> Brain : "DONE" / "STALLED"
  Brain --still polls every 1 s--> WiFi module (unchanged, see below)
```

## 1. Roles and identities

The old `Bluetooth/hub.py` is a laptop hub that broadcasts every message
to every board. That is not a brain. The new brain (`brain.py`) is
role-aware: it does **not** broadcast IR/motor traffic to unrelated boards.

| Role | Old BOARD_ID idea | New ID reply (to `ID?`) | What it owns |
|---|---|---|---|
| Brain / master | — (the hub) | — | `has_wall[]`, route decisions, WiFi poll |
| IR node, front | e.g. `3` | `ID IR-F` | One emitter+receiver pair, edge detection |
| IR node, left | e.g. `4` | `ID IR-L` | Same, left side |
| IR node, right | e.g. `5` | `ID IR-R` | Same, right side |
| IR node, rear | e.g. `6` | `ID IR-Rear` | Optional, same |
| Motor controller | e.g. `2` | `ID MOTOR` | Wheel speeds, encoders, turn execution |
| WiFi node | — | `ID WIFI` | RSSI, polled (see §5) |

The brain identifies a sender by its BLE connection + its `ID …` reply,
not by trusting a payload. That is what makes a 1-byte IR event work:
the brain already knows *which* sensor sent it, so the payload does not
need to repeat the sensor id.

## 2. IR -> Brain: the toggle, exactly as specified

Brain state, one variable **per sensor**, not one global:

```python
has_wall = {"F": 0, "L": 0, "R": 0, "Rear": 0}
# on receiving "1" from sensor S:
has_wall[S] ^= 1          # 0 -> 1, 1 -> 0, exactly the brief
```

Rules on the IR node (`IR_Node/IR_Node.ino`):

1. Sample like the old code (38 kHz bursts, `IR_CONFIRM_COUNT = 2`
   consecutive reads, as in `Roomba_v1/TinyScreenRoomba`).
2. Keep a local `present` state. Only when the *confirmed* state
   differs from the last state you reported, send one byte: `"1"`.
   No message on every sample, no level stream, no "still there" repeat.
3. Do **not** send "0" for "wall gone" and "1" for "wall here". Both
   edges are the same `"1"`. The brain's toggle is the memory.
   That is the whole bandwidth saving: a wall that sits in front of
   the robot for 60 s costs 1 message on arrival, not 60 (1 s poll)
   or 1,200 (50 ms poll in the old Roomba sketch).
4. Debounce in the node, not the brain: the 2-confirm rule stops a
   flickering reflection from toggling the brain twice.

Worked example (front sensor only):

```
t=0   no wall,  has_wall[F]=0,  IR sends nothing
t=5s  wall appears, confirmed -> IR sends "1"  -> brain: 0 -> 1, brain interrupts motor: TURN
t=65s wall still there,          IR sends nothing -> brain still knows 1, no traffic
t=70s wall gone, confirmed   -> IR sends "1"  -> brain: 1 -> 0, brain interrupts motor: DRIVE
Total: 2 bytes of IR traffic for 70 s, vs 70 at 1 s polling.
```

### Why a global `has_wall` would break multi-device

With one sensor, a single `has_wall` is fine. With front + left +
right, it is not: a left-wall arrival must not flip the front-wall
memory. Hence `has_wall[F/L/R/Rear]`, each toggled only by its own
node's `"1"`. If you only ever fit one IR node, `has_wall["F"]` is
the variable from the brief.

### The one honest weakness of a bare toggle, and the fix

A toggle is only as reliable as its delivery. If one `"1"` is lost
over BLE, the brain is inverted until the *next* edge — it will think
a wall is there when it is not, or drive into one it thinks is gone.
Retransmitting the same `"1"` is **not** a fix: if the first copy did
arrive and only the ACK was lost, a retransmit toggles the brain a
second time and re-inverts it.

So, deliberately, the IR node does **not** retransmit a toggle.
Instead it has two cheap, rare safeguards that cost nothing in the
normal case:

* Brain ACK: on a `"1"`, the brain replies `"A"` (1 byte, unicast to
  that node only, never broadcast). The node logs a missed ACK, but
  does not resend the toggle.
* Absolute resync: on (re)connect, and every 30 s, and whenever the
  brain sends `"SYNC?"`, the IR node sends its *absolute* state
  `"S0"` or `"S1"` (2 bytes). On `"S0"/"S1"` the brain **sets**
  `has_wall[S] = 0/1` instead of toggling. This heals any single lost
  toggle within 30 s, at 1/30th the cost of 1 s polling, and it is
  the only periodic IR traffic left.

If you want zero desync risk and can spare 1 more byte, the next step
is to add a sequence bit (`"10"` / `"11"`, brain ignores a repeated
sequence). That is not implemented here, to keep the wire format
exactly the `"1"` from the brief — say so before changing it.

## 3. Brain -> Motor: one route command, motor does the driving

Old behaviour (`TinyScreenRoomba.updateBehaviour` + `setWheelSpeeds`):
the brain decided forward/turn every 50 ms and owned wheel speeds.
New behaviour: the brain sends **one** high-level command and then
gets out of the way. The motor controller owns the control loop:
encoder counting, turn angle, straight driving, and stopping.

Wire format (all fit in one 20-byte BLE packet):

```
Brain -> Motor:
  M<n> DRIVE          drive straight until interrupted (n = sequence, see below)
  M<n> DRIVE <mm>     drive straight for <mm>, then report DONE
  M<n> TURN <deg> L   turn in place <deg> degrees left, then DONE
  M<n> TURN <deg> R   turn in place <deg> degrees right, then DONE
  M<n> STOP           stop now, report DONE

Motor -> Brain:
  A<n>                ACK: command <n> received (unicast, not broadcast)
  DONE                route step finished (turn complete / distance reached / stopped)
  STALLED             wheels commanded but encoders not moving (timeout in Motor_Node)
```

Sequence numbers on motor commands only (not on IR toggles): a motor
command is *not* idempotent — resending `TURN 90 L` after a lost ACK
would turn twice. So the motor ignores a command whose `<n>` it has
already executed, and the brain may safely resend an un-ACKed command
with the same `<n>`. Sequence wraps at 99.

Brain route logic (in `brain_core.py`, deliberately small):

```
on IR "1" from F:
    has_wall[F] ^= 1
    if has_wall[F] == 1:  TURN 90-180 deg, away from a side wall if one is known
    else:                 DRIVE (if motor is not already driving)
on motor DONE:
    if has_wall[F] == 0:  DRIVE
    else:                 TURN again (do not resume driving into the wall)
on disconnect of motor:   brain marks motor offline, sends nothing until it returns
on disconnect of IR node: brain keeps last has_wall[S] but marks it stale;
                          the node's SYNC on reconnect overwrites it absolutely
```

Fail-safe, in the motor node: if BLE disconnects, the motor stops.
A route command must never outlive its brain connection.

What the brain no longer does: wheel-speed updates, encoder math,
turn-angle integration, or a drive loop. What it still does: decide
*when* to drive vs turn, and in which direction — that decision needs
`has_wall` from all sensors, so it stays in the brain.

## 4. What is NOT changed

* Original files are untouched: `Bluetooth/`, `IR Proximity/`,
  `Roomba_v1/` still work as single-board / laptop-hub code.
  This folder is the multi-device layer on top of the same BLE UART
  service (Nordic UART, 20-byte packets) and the same IR burst method.
* `UART.ino` in `IR_Node/` and `Motor_Node/` is a verbatim copy of
  `Bluetooth/UART.ino` — Arduino sketches need it as a tab in the
  same folder. Do not edit it per-node.
* The 20-byte BLE limit still applies: every message above is
  ≤ 20 bytes, most are 1–2 bytes.

## 5. WiFi: why it stays polled

The diagram says "continuous WiFi strength testing" and the brain
polls it every second. That one stays polling, on purpose: RSSI is
a continuously varying analogue value, not an edge. There is no
"wall appeared / wall gone" event to toggle, and a threshold-crossing
event would still need a value attached. Keep the 1 s WiFi poll;
the saving comes from IR (edge-only) and motor (command-only),
which were the chatty links.
