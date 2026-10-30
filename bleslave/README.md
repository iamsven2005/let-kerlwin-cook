# Master / slave: send from one to few

A general master/slave layer for the multi-BLE setup, alongside the
robot brain in `../` (which is a specialised master that consumes
messages itself instead of forwarding them).

## The problem it fixes

The old `Bluetooth/hub.py` broadcasts: every message from one board
goes to *every* other board. If a display board only needs to stop
the motor board, 4 other boards still wake up, read it, and ignore it.

This master is a star:

```
              MASTER (central)
             /   |    \     \
         IR-F  MOTOR DISPLAY WIFI ...
```

Slaves cannot talk directly to each other — BLE central/peripheral
connections don't work that way here — so a slave-to-slave message
always goes slave → master → selected slaves. "One to few" is a
fan-out at the master, not a BLE multicast (there is none on this
UART service).

## How to send

| From | To | How |
|---|---|---|
| Master | one | `send MOTOR STOP` |
| Master | a few | `send IR-F,IR-L ALERT` |
| Master | a group | `send IR ALERT` (group expands in the master) |
| Master | all | `send ALL HELLO` — explicit only, never the default |
| Slave | one / a few / a group | `R MOTOR STOP`, `R IR-F,IR-L ALERT`, `R IR ALERT` |
| Slave | master only | anything without `R `, e.g. `HELLO`, `1`, `DONE` |

On delivery the master strips the `R <targets> ` envelope, so
recipients get just the payload (`STOP`, `ALERT`). That is what
stops loops: a delivered message echoed back is master-only, not
a new routing request.

Groups by default: `IR` (all IR nodes), `SENSORS` (IR + WIFI),
`ALL`. Edit `GROUPS` in `router.py` for your build.

## Files

| File | What it is |
|---|---|
| `PROTOCOL.md` | Full spec: star topology, wire format, routing rules, offline/unknown handling, bandwidth, merge path into `brain.py` |
| `router.py` | Routing logic only, no BLE. Group expansion, dedup, sender exclusion, unknown/offline reporting |
| `master.py` | BLE master (needs `bleak`). Discovery, `ID <role>` registration, routing, console: `list`, `groups`, `send <targets> <payload>` |
| `Slave_Node/Slave_Node.ino` | Generic slave board. Change `ROLE` + `ID_REPLY` per board. Buttons test master-only / ALL / one / group. `UART.ino` beside it is a verbatim copy of `Bluetooth/UART.ino` |
| `sim_test.py` | Offline proof, no hardware: `python sim_test.py` |

## Try it without hardware

```bash
cd MultiDevice/MasterSlave
python sim_test.py
```

All checks pass, including: unicast reaches only its target,
one-to-few reaches exactly the named boards, a group delivers
once per member even if named twice, master-only messages
(`1`, `DONE`, `S0`) are never forwarded, the sender never gets
its own `ALL` message back, and an unknown/offline target errors
to the sender without killing delivery to the rest.

Example saving, 6 slaves, one board messaging 2 others:
slave-originated one-to-few = 3 air messages (1 in + 2 out),
old broadcast = 6. Master-originated is cheaper still: 2, no first hop.

## Not proven here

* Arduino compile: `Slave_Node.ino` needs the TinyScreen+ / STBLE
  libraries from the original sketches; no Arduino toolchain here.
* BLE central on a board: like `brain.py`, `master.py` is a
  laptop/central master. Moving the master onto a TinyScreen+ as
  a BLE central is a separate port, not a rename.
* Delivery guarantee: this UART service is write-without-response.
  The router reports unknown/offline targets, but for a
  safety-critical `STOP` have the recipient ACK and retry on
  timeout, as in `../PROTOCOL.md` §3.
