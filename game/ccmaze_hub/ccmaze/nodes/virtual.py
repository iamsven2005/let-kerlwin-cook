"""VirtualNode: an on-screen node. Emits exactly the events a physical node would."""
from __future__ import annotations

import itertools

import time

from ..sim import events as E
from .base import Node, NodeHub

_ids = itertools.count(1)


class VirtualNode(Node):
    kind = "virtual"

    def __init__(self, hub: NodeHub, node_id: str | None = None):
        super().__init__(node_id or f"V{next(_ids)}", hub)
        self.connected = True
        self.display: dict[str, str] = {"CARD": "-", "SEQ": "-", "REG": "0000", "COL": "808080",
                                        "HAND": "0", "MSG": "", "DOFF": "0", "HINT": ""}
        self.face_down = False     # the widget's own physical-flip state
        self.buttons = 0           # currently held buttons (bitmask)
        self.last_command = ""

    def _transmit(self, field: str, payload: str = "") -> None:
        self.last_command = field
        self.writes_ok += 1
        if field == "PING":
            self.last_ping_sent = time.monotonic()
        self.log_debug(">", f"{field}:{payload}" if field not in ("BLINK", "PING") else field)
        if field in self.display:
            self.display[field] = payload
        if field == "CARD":
            self.display["MSG"] = ""   # same as the firmware: CARD: clears the message

    def emit(self, kind: str, value: str = "", ts=None) -> None:
        self.log_debug("<", f"{kind}:{value}" if value else kind, ts)
        super().emit(kind, value, ts)

    # --- user actions on the widget ---------------------------------------------
    def tap(self, side: str) -> None:
        self.emit(E.TAP, side)

    def toggle_flip(self) -> None:
        self.face_down = not self.face_down
        self.emit(E.FLIP, "1" if self.face_down else "0")

    def press(self, mask: int) -> None:
        self.buttons |= mask
        self.emit(E.BUTTONS, format(self.buttons, "X"))

    def release(self, mask: int) -> None:
        self.buttons &= ~mask
        self.emit(E.BUTTONS, format(self.buttons, "X"))

    def click_buttons(self, mask: int) -> None:
        """Press and release (several bits = pressed together)."""
        self.press(mask)
        self.release(mask)


def bump(a: VirtualNode, b: VirtualNode) -> None:
    """Simulate knocking two nodes together: both report a side tap, a few ms apart."""
    import time
    t = time.monotonic()
    a.emit(E.TAP, "R", ts=t)
    b.emit(E.TAP, "L", ts=t + 0.01)
