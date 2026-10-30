# Master / slave protocol: send from one to few

This is a second, general layer on top of `MultiDevice/` — it does not
replace `brain.py`. Use `brain.py` when the master *is* the robot
brain (it owns `has_wall` and route decisions). Use this master/slave
system when any board needs to send a message to one, a few, or all
other boards, e.g. one sensor board alerting only the motor board
and the display board, not every board on the robot.

## 1. Topology: a star, not a mesh

```
                MASTER (central, e.g. laptop / brain board)
               /      |      \      \
          Slave A  Slave B  Slave C  Slave D
          (IR-F)   (MOTOR)  (DISPLAY)(WIFI)
```

* Only the master is a BLE central. Every slave is a BLE peripheral,
  exactly like the original `Bluetooth/BleTest.ino` boards and the
  `MultiDevice/IR_Node` / `Motor_Node` boards (same Nordic UART
  service, same 20-byte packet limit).
* Slaves **cannot** talk directly to each other over BLE in this
  setup. A slave-to-slave message always goes slave -> master ->
  selected slaves. That is not a limitation of this protocol, it is
  how BLE central/peripheral connections work here.
* Consequence: "one to few" is implemented as a *fan-out at the
  master*, not as a BLE multicast. There is no multicast packet on
  this UART service. Sending to 2 slaves costs 2 air messages, to
  all 6 costs 6 — but crucially, sending to 2 does **not** cost 6,
  which is what the old `Bluetooth/hub.py` did (it broadcast every
  message to every other board).

## 2. Identities and groups

A slave is known to the master by the role it reports to `ID?`,
e.g. `ID IR-F`, `ID MOTOR`, `ID DISPLAY`. That role is its address.
There is no separate numeric address to remember, and a board that
reconnects keeps its role.

Groups are expanded by the master only, never by the slaves:

| Group | Members (default) | Use |
|---|---|---|
| `IR` | `IR-F`, `IR-L`, `IR-R`, `IR-Rear` | all proximity nodes |
| `SENSORS` | `IR` + `WIFI` | every sensing node |
| `ALL` | every connected slave | broadcast — opt-in only, never the default |
| any single role, e.g. `MOTOR` | that one slave | unicast |

Edit `GROUPS` in `router.py` to add a group for your build (e.g.
`ARMS`, `LIGHTS`). A group that names a role with no board
connected is not an error by itself — see §5 for the offline rule.

## 3. Wire format

All messages are still ≤ 20 bytes, like the old hub. That forces
short roles and short payloads; if a payload does not fit, split
it at the application layer, do not silently truncate it in the
router (the master transport truncates only with a warning in
`master.py`, and the sim never truncates).

### 3a. Master -> slave: no header needed

The master writes to *only* the selected slaves' BLE connections.
The connection itself is the address, so the payload goes through
unchanged:

```
master console:  send MOTOR STOP
  -> master writes "STOP" to the MOTOR connection only
master console:  send IR-F,IR-L ALERT
  -> master writes "ALERT" to IR-F and to IR-L, nobody else
master console:  send IR ALERT
  -> master expands IR, writes "ALERT" to each connected IR node
```

This is the cheapest direction: zero addressing bytes on the air.

### 3b. Slave -> master -> few: envelope, stripped on delivery

A slave has only one connection (to the master), so it *must* say
who the message is for. It does that with a one-line envelope:

```
R <targets> <payload>
```

* `R` = route this (as opposed to a message for the master only).
* `<targets>` = one role, a comma list with no spaces, or a group:
  `MOTOR`, `IR-F,IR-L`, `IR`, `ALL`.
* `<payload>` = the rest of the line, delivered unchanged.

Examples (all ≤ 20 bytes on the air from the slave):

```
R MOTOR STOP        slave asks master to stop the motor only
R IR-F,IR-L ALERT   slave alerts front + left IR nodes only
R IR ALERT          slave alerts the whole IR group
R ALL HELLO         slave broadcasts (explicit, not default)
```

On delivery the master **strips the envelope**: recipients receive
just `STOP`, `ALERT`, `HELLO`. That is what prevents loops — a
recipient cannot mistake a delivered message for a new routing
request and bounce it back. A slave that wants to forward something
it received must deliberately send a new `R …` message.

### 3c. Slave -> master only: no envelope

Anything that does **not** start with `R ` is for the master and
is never forwarded. This keeps the previous layer working
unchanged alongside this one:

```
1        IR edge toggle -> brain only (MultiDevice/PROTOCOL.md §2)
DONE     motor step finished -> brain only
S0 / S1  IR absolute sync -> brain only
ID IR-F  slave identity, handled by the master, never forwarded
```

If you mix the two systems, run `brain.py` for brain traffic and
`master.py` for routed traffic on *separate* masters, or merge
them deliberately (see §6) — do not run both masters against the
same boards at once; a board can only hold one BLE connection.

## 4. Routing rules (in `router.py`, tested in `sim_test.py`)

1. Resolve targets: expand groups, deduplicate, preserve the order
   the sender listed them. `IR-F,IR` delivers to `IR-F` once, not twice.
2. Never deliver back to the sender. A slave in its own target list
   or group (`R ALL …` from `IR-F`) is excluded from delivery.
3. Never forward a message that has already been stripped. Only
   the master routes, only once.
4. Unknown target, e.g. a role that was never registered: the
   master reports it to the sender (`ERR <target> unknown`) and
   still delivers to the known targets. One bad name does not kill
   the rest of the few.
5. Offline target (known role, currently disconnected): same
   treatment — `ERR <target> offline` to the sender, deliver to
   the rest. The master does **not** queue for later by default:
   a queued `STOP` delivered minutes later could be dangerous,
   and a queued `ALERT` could be stale. If you need queuing, add
   it per-message-type with an expiry, do not turn it on globally.
6. Broadcast (`ALL`) is explicit. There is no "send to everyone
   unless you say otherwise" path, which is the old hub's behaviour
   and the thing this system replaces.

## 5. Bandwidth: one-to-few vs broadcast

With N slaves connected (excluding the sender), a slave-originated
message costs:

```
unicast (1 target):   1 slave->master + 1 master->target  = 2 air messages
one-to-few (k):       1 + k air messages
old broadcast:        1 + (N-1) air messages, always
```

Example, 6 slaves total, sender + 5 others, message to 2 of them:

```
one-to-few:  1 + 2 = 3 air messages
broadcast:   1 + 5 = 6 air messages   -> 50% saved, and 3 boards
                                        never woke up to read it
```

Master-originated messages are cheaper still (no first hop):
k air messages for k targets, vs N-1 for broadcast.

## 6. Relationship to brain.py

* `brain.py` is a *specialised* master: it consumes IR toggles and
  motor DONE itself and never forwards them. In router terms, every
  brain message is master-only.
* `master.py` is a *general* master: it forwards `R …` messages
  and otherwise just logs master-only messages.
* To combine them (recommended next step if you want one master
  for the robot): import `Router` into `brain.py`, and in its
  `on_message`, first try `router.route_from_slave(...)`; if the
  result is "routed", forward it and return; if it is
  "master-only", run the existing brain logic. The router is
  deliberately transport-free so that merge is a ~15-line change
  and does not need a second BLE connection. Do not merge by
  running both masters at once.

## 7. What this does not do

* No slave-to-slave direct BLE link, no mesh, no BLE advertising
  multicast — see §1.
* No encryption or authentication beyond BLE pairing: a board
  that can connect and answer `ID?` can send `R ALL …`. If that
  matters for your build, add an allow-list of roles in the master
  before enabling routing.
* No guaranteed delivery: BLE UART here is write-without-response
  (same as the old hub). The router reports unknown/offline
  targets, but it cannot prove a connected slave read the message.
  For a safety-critical `STOP`, have the recipient ACK to the
  master (`A`) and let the sender retry with a timeout — the same
  pattern as the motor sequence in `MultiDevice/PROTOCOL.md` §3.
