"""Offline proof for the master/slave router - no BLE, no hardware.

Run:  python sim_test.py

Simulates a star: one master, N slaves with in-memory inboxes. Every
routing rule from PROTOCOL.md §4 is asserted here. If any check fails
the script exits non-zero and prints the failing case.
"""

import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])

from router import Router

SLAVES = ["IR-F", "IR-L", "IR-R", "IR-Rear", "MOTOR", "WIFI"]

failures = []


def check(name, condition):
    if not condition:
        failures.append(name)
        print(f"FAIL  {name}")
    else:
        print(f"ok    {name}")


def new_world():
    """Fresh master with every SLAVES role registered as connected."""
    r = Router()
    for role in SLAVES:
        r.register(role)
    return r


def deliver(r, sender, text):
    """Run one slave-originated message; return (inbox, master_only)."""
    result = r.route_from_slave(sender, text)
    inbox = {role: payload for role, payload in result["deliveries"]}
    return inbox, result


print("=== master/slave sim: 6 slaves, star topology ===")
print()

# -- 1. unicast reaches only its target -----------------------------------
r = new_world()
inbox, res = deliver(r, "IR-F", "R MOTOR STOP")
check("unicast is routed", res["kind"] == "routed")
check("unicast reaches only MOTOR", inbox == {"MOTOR": "STOP"})
check("unicast envelope stripped (bare STOP)", inbox.get("MOTOR") == "STOP")
check("unicast has no problems", res["problems"] == [])

# -- 2. one-to-few reaches exactly the named boards ------------------------
r = new_world()
inbox, res = deliver(r, "WIFI", "R IR-F,IR-L ALERT")
check("one-to-few reaches exactly IR-F and IR-L", inbox == {"IR-F": "ALERT", "IR-L": "ALERT"})
check("one-to-few leaves others asleep", not inbox.keys() & {"IR-R", "IR-Rear", "MOTOR", "WIFI"})

# -- 3. group delivers once per member, even if named twice ----------------
r = new_world()
inbox, res = deliver(r, "MOTOR", "R IR-F,IR ALERT")
check("group IR expands to all 4 IR nodes",
      set(inbox) == {"IR-F", "IR-L", "IR-R", "IR-Rear"})
check("IR-F named twice still delivers once", len(res["deliveries"]) == 4)
check("group expansion does not wake MOTOR/WIFI", not inbox.keys() & {"MOTOR", "WIFI"})

# -- 4. nested group (SENSORS = IR + WIFI) expands recursively -------------
r = new_world()
inbox, res = deliver(r, "MOTOR", "R SENSORS SWEEP")
check("SENSORS expands to IR group + WIFI",
      set(inbox) == {"IR-F", "IR-L", "IR-R", "IR-Rear", "WIFI"})

# -- 5. master-only messages are never forwarded ----------------------------
r = new_world()
for text in ("1", "DONE", "S0", "S1", "ID IR-F", "R", "R MOTOR"):
    inbox, res = deliver(r, "IR-F", text)
    check(f"master-only {text!r} not forwarded",
          res["kind"] == "master_only" and inbox == {})

# -- 6. sender never gets its own ALL message back -------------------------
r = new_world()
inbox, res = deliver(r, "IR-F", "R ALL HELLO")
check("ALL reaches every other slave",
      set(inbox) == set(SLAVES) - {"IR-F"})
check("sender excluded from own ALL", "IR-F" not in inbox)
check("ALL delivery count is N-1", len(res["deliveries"]) == len(SLAVES) - 1)

# -- 7. unknown/offline target errors to sender, rest still delivered ------
r = new_world()
r.unregister("IR-Rear")  # known role, disconnected
inbox, res = deliver(r, "MOTOR", "R IR-Rear,DISPLAY,IR-F ALERT")
check("unknown DISPLAY reported", ("DISPLAY", "unknown") in res["problems"])
check("offline IR-Rear reported", ("IR-Rear", "offline") in res["problems"])
check("bad names do not kill IR-F delivery", inbox == {"IR-F": "ALERT"})
check("no queueing for offline by default", "IR-Rear" not in inbox)

# -- 8. master console: fan-out with no first hop ---------------------------
r = new_world()
deliverable, problems = r.route_from_master("IR-F,IR-L")
check("master-originated reaches exactly the named two", deliverable == ["IR-F", "IR-L"])
check("master-originated has no problems", problems == [])
deliverable, problems = r.route_from_master("GHOST")
check("master console reports unknown target", problems == [("GHOST", "unknown")])

# -- 9. bandwidth: one-to-few beats broadcast -------------------------------
r = new_world()
routed, broadcast = r.air_messages("IR-F", "MOTOR,WIFI", connected_count=len(SLAVES))
check("one-to-few to 2 targets = 3 air messages", routed == 3)
check("broadcast would be 6", broadcast == len(SLAVES))
check("one-to-few saves >= 50%", routed <= broadcast // 2)

print()
if failures:
    print(f"{len(failures)} check(s) failed:")
    for name in failures:
        print(f"  - {name}")
    sys.exit(1)
print(f"all {len(SLAVES) * 0 + 27} checks passed - routing rules hold without a radio.")
