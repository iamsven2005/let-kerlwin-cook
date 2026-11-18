"""Node events. Both PhysicalNode and VirtualNode produce exactly these."""
from __future__ import annotations

from dataclasses import dataclass

# kinds
TAP = "T"          # value: "R" | "L" | "V"
FLIP = "F"         # value: "1" (face-down) | "0" (face-up)
FSYNC = "FSYNC"    # value like FLIP; state report right after HELLO, never a commit
BUTTONS = "B"      # value: hex bitmask string
FSTATE = "FS"      # value "0"/"1": silent face-state sync after an accelerometer recovery (v1.5 X1)
BSYNC = "BSYNC"    # value like BUTTONS; state report right after HELLO, never an action
BATTERY = "BAT"    # value: "0".."100"
HELLO = "HELLO"    # value: "<node_id>:<fw_version>"
PONG = "PONG"
CONNECTED = "CONNECTED"
DISCONNECTED = "DISCONNECTED"


@dataclass(frozen=True)
class NodeEvent:
    node_id: str
    kind: str
    value: str
    ts: float   # stamped on arrival at the hub (monotonic seconds)
