"""The one node interface, and the hub that owns every node.

Game logic never touches Node objects: it reads events from NodeHub.events and
the hub pushes display state to nodes by diffing against what was last sent.
"""
from __future__ import annotations

import logging
import queue
import threading
import time
from abc import ABC, abstractmethod
from collections import deque
from typing import Optional

from ..sim.events import NodeEvent
from .protocol import DISPLAY_FIELDS

ble_log = logging.getLogger("ccmaze.ble")   # every debug-view line also goes here (logs/ble_debug.log)
DEBUG_LINES = 400


class Node(ABC):
    kind = "abstract"

    def __init__(self, node_id: str, hub: "NodeHub"):
        self.node_id = node_id
        self.hub = hub
        self.connected = False
        self.sent: dict[str, str] = {}          # last display payload sent, per field
        self.blink_until = 0.0
        self.battery: Optional[int] = None      # last BAT:<percent>; None = unknown / virtual
        self.phys_face_down = False             # last F: / sync seen from the node (for the mirror widget)
        # BLE debug view (14 A1): (monotonic ts, direction, text); direction: > sent, < received,
        # ! error, * event. Written from the BLE thread, read by the UI, so guarded by a lock.
        self.debug: deque = deque(maxlen=DEBUG_LINES)
        self._debug_lock = threading.Lock()
        self.last_ping_sent: Optional[float] = None
        self.last_pong: Optional[float] = None
        self.rtt_ms: Optional[float] = None
        self.writes_ok = 0
        self.write_errors = 0

    # --- debug log ------------------------------------------------------------
    def log_debug(self, direction: str, text: str, ts: Optional[float] = None) -> None:
        ts = time.monotonic() if ts is None else ts
        with self._debug_lock:
            self.debug.append((ts, direction, text))
        ble_log.debug("%s %s %s", self.node_id, direction, text)

    def debug_lines(self) -> list[tuple[float, str, str]]:
        with self._debug_lock:
            return list(self.debug)

    # --- hub -> node ----------------------------------------------------------
    @abstractmethod
    def _transmit(self, field: str, payload: str = "") -> None:
        """Deliver one command to the device (or widget)."""

    def send(self, field: str, payload: str = "") -> None:
        self._transmit(field, payload)
        if field in DISPLAY_FIELDS:
            self.sent[field] = payload
        if field == "CARD":
            self.sent["MSG"] = ""   # the node clears its message on every CARD:

    def blink(self) -> None:
        self.blink_until = time.monotonic() + 1.0
        self.send("BLINK")

    def invalidate(self) -> None:
        """Forget what was sent so the next sync resends the full display state."""
        self.sent.clear()

    # --- node -> hub ----------------------------------------------------------
    def emit(self, kind: str, value: str = "", ts: Optional[float] = None) -> None:
        self.hub.events.put(NodeEvent(self.node_id, kind, value, time.monotonic() if ts is None else ts))

    def close(self) -> None:
        pass

    @property
    def label(self) -> str:
        return self.node_id


class NodeHub:
    def __init__(self):
        self.nodes: dict[str, Node] = {}
        self.events: "queue.Queue[NodeEvent]" = queue.Queue()

    def add(self, node: Node) -> Node:
        self.nodes[node.node_id] = node
        return node

    def remove(self, node_id: str) -> None:
        n = self.nodes.pop(node_id, None)
        if n:
            n.close()

    def drain(self) -> list[NodeEvent]:
        out = []
        while True:
            try:
                out.append(self.events.get_nowait())
            except queue.Empty:
                return out

    def sync(self, node_id: str, desired: dict[str, str]) -> None:
        n = self.nodes.get(node_id)
        if n is None or not n.connected:
            return
        for field in DISPLAY_FIELDS:
            if field in desired and n.sent.get(field) != desired[field]:
                n.send(field, desired[field])

    def ordered(self) -> list[Node]:
        return list(self.nodes.values())
