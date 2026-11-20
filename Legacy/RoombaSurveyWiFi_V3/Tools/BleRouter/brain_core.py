"""Brain core: pure-Python, no BLE, no hardware.

This is the testable heart of the multi-device brain from PROTOCOL.md.
Transport (brain.py, BLE) only calls these methods; it never toggles
has_wall itself, so the toggle rule lives in exactly one place.

The brief, implemented literally, per sensor:
    has_wall[sensor] starts 0
    on receiving "1" from that sensor: has_wall[sensor] ^= 1

Multi-device note: has_wall is a dict, not a single int. A left-wall
event must not flip the front-wall memory. With one sensor fitted,
has_wall["F"] is the variable from the brief.
"""

import random

SENSORS = ("F", "L", "R", "Rear")


class Brain:
    def __init__(self, rng=None):
        self.has_wall = {s: 0 for s in SENSORS}
        self.wall_stale = {s: False for s in SENSORS}
        self.motor_online = False
        self.motor_busy = False          # a DRIVE/TURN is in progress
        self.motor_seq = 0               # next motor command sequence
        self.outbox = []                 # (target, message) in send order
        self.rng = rng or random.Random()

    # -- outbound helpers -------------------------------------------------
    def _send_motor(self, command):
        """One high-level route command. Brain sends it once; the motor
        owns execution until DONE/STALLED. Sequence makes a resend safe."""
        self.motor_seq = (self.motor_seq + 1) % 100
        msg = f"M{self.motor_seq} {command}"
        self.outbox.append(("MOTOR", msg))
        self.motor_busy = True
        return msg

    def _send_ir(self, sensor, message):
        self.outbox.append((f"IR-{sensor}", message))
        return message

    def drain_outbox(self):
        out, self.outbox = self.outbox, []
        return out

    # -- IR events --------------------------------------------------------
    def on_ir_toggle(self, sensor):
        """IR node sent '1': a confirmed proximity edge occurred.

        Returns (new_has_wall, action_taken). The toggle is the memory:
        first '1' -> wall remembered, second '1' -> wall forgotten.
        """
        if sensor not in self.has_wall:
            return None, "unknown sensor, ignored"
        self.has_wall[sensor] ^= 1
        self.wall_stale[sensor] = False
        new = self.has_wall[sensor]

        if sensor == "F":
            if new == 1:
                # Wall appeared in front: interrupt the motor once.
                # Direction: away from a known side wall, else random.
                direction = self._turn_direction()
                degrees = self.rng.randint(90, 180)
                self._send_motor(f"TURN {degrees} {direction}")
                return new, f"wall front: TURN {degrees} {direction}"
            # Wall gone: resume driving, but only if motor is idle.
            # If a turn is still running, its DONE handler will drive.
            if not self.motor_busy:
                self._send_motor("DRIVE")
                return new, "wall front gone: DRIVE"
            return new, "wall front gone: turn still running, DRIVE on DONE"
        return new, f"wall {sensor} now {new}, no motor action (side/rear only)"

    def on_ir_sync(self, sensor, absolute):
        """IR node sent 'S0'/'S1': absolute state, NOT a toggle.

        This heals a lost '1' (see PROTOCOL.md §2). It overwrites
        has_wall instead of flipping it.
        """
        if sensor not in self.has_wall:
            return None, "unknown sensor, ignored"
        old = self.has_wall[sensor]
        self.has_wall[sensor] = 1 if absolute else 0
        self.wall_stale[sensor] = False
        if old == self.has_wall[sensor]:
            return self.has_wall[sensor], "sync: no change"
        # A healed state is treated like an edge for the front sensor only.
        if sensor == "F":
            if self.has_wall[sensor] == 1 and not self.motor_busy:
                direction = self._turn_direction()
                degrees = self.rng.randint(90, 180)
                self._send_motor(f"TURN {degrees} {direction}")
                return 1, f"sync healed to wall: TURN {degrees} {direction}"
            if self.has_wall[sensor] == 0 and not self.motor_busy:
                self._send_motor("DRIVE")
                return 0, "sync healed to clear: DRIVE"
        return self.has_wall[sensor], "sync: state corrected"

    def on_ir_ack_missing(self, sensor):
        """Brain got no ACK for its 'A' reply, or node reconnected stale.

        We do NOT resend a toggle (that would double-flip). We ask for
        an absolute sync instead; the next S0/S1 fixes the state."""
        return self._send_ir(sensor, "SYNC?")

    # -- motor events -----------------------------------------------------
    def on_motor_done(self):
        """Motor finished a DRIVE/TURN/STOP step. Brain decides the next
        step once, it does not stream wheel speeds."""
        self.motor_busy = False
        if self.has_wall["F"] == 0:
            self._send_motor("DRIVE")
            return "DONE: front clear, DRIVE"
        direction = self._turn_direction()
        degrees = self.rng.randint(90, 180)
        self._send_motor(f"TURN {degrees} {direction}")
        return f"DONE: front still blocked, TURN {degrees} {direction}"

    def on_motor_stalled(self):
        self.motor_busy = False
        return "STALLED: motor stopped, brain takes no further action until operator/motor recovers"

    # -- connection events --------------------------------------------------
    def on_ir_disconnect(self, sensor):
        if sensor in self.wall_stale:
            self.wall_stale[sensor] = True
        return f"IR-{sensor} offline, has_wall kept ({self.has_wall.get(sensor)}) but stale"

    def on_motor_connect(self):
        self.motor_online = True
        self.motor_busy = False
        if self.has_wall["F"] == 0:
            self._send_motor("DRIVE")
            return "motor online, front clear: DRIVE"
        return "motor online, front blocked: waiting for turn decision"

    # -- helpers ------------------------------------------------------------
    def _turn_direction(self):
        """Turn away from a known side wall. Deterministic given walls;
        random only when both sides are equal (both clear / both blocked)."""
        left, right = self.has_wall["L"], self.has_wall["R"]
        if left and not right:
            return "R"
        if right and not left:
            return "L"
        return self.rng.choice(["L", "R"])


def bandwidth_comparison(seconds, poll_interval_s=1.0, edges=2, sync_interval_s=30.0):
    """Messages for one IR node over `seconds`, old polling vs new.

    Old: one message per poll, for the whole period.
    New: one '1' per confirmed edge, plus one S0/S1 sync per sync interval.
    """
    import math
    old = int(seconds / poll_interval_s)
    new = edges + int(math.ceil(seconds / sync_interval_s))
    saved_pct = 100.0 * (1 - new / old) if old else 0.0
    return {"old_polling": old, "new_interrupt": new, "saved_pct": round(saved_pct, 1)}
