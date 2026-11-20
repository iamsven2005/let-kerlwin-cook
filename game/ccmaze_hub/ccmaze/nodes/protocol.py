"""BLE protocol (section 6 of the brief): newline-terminated ASCII, <= 20 bytes.

Hub -> node fields: COL HAND CARD SEQ REG DOFF MSG, plus bare BLINK / PING.
Node -> hub: HELLO:<id>:<fw>, F:1/0, FS:1/0, T:R/L/V, B:<hex>, BAT:<percent>, PONG.

A node clears its MSG whenever it receives CARD:, so MSG is sent last.
"""
from __future__ import annotations

from typing import Optional

from ..sim import events as E

MAX_MSG_BYTES = 20
MAX_MSG_TEXT = 14
# send order; MSG must follow CARD (a node clears MSG on CARD:). HINT is not cleared by CARD:.
DISPLAY_FIELDS = ("COL", "HAND", "CARD", "SEQ", "REG", "DOFF", "MSG", "HINT")
# spec HUB-Q35: the brief allows 16 hint chars, but "HINT:" + 16 + "\n" is 22 bytes > 20, so hints
# are capped at 14 characters (like MSG) until the spec says otherwise.
MAX_HINT_TEXT = 14
BARE_COMMANDS = ("BLINK", "PING")


class ProtocolError(ValueError):
    pass


def encode(field: str, payload: str = "") -> bytes:
    if field in BARE_COMMANDS:
        line = field
    else:
        if field == "MSG":
            payload = payload[:MAX_MSG_TEXT]   # "MSG:" with empty text clears
        elif field == "HINT":
            payload = payload[:MAX_HINT_TEXT]  # "HINT:" with empty text clears
        line = f"{field}:{payload}"
    data = (line + "\n").encode("ascii", errors="replace")
    if len(data) > MAX_MSG_BYTES:
        raise ProtocolError(f"message too long ({len(data)} bytes): {line!r}")
    return data


def parse_line(line: str) -> Optional[tuple[str, str]]:
    """Parse one node->hub line. Returns (event kind, value) or None if unrecognised."""
    line = line.strip()
    if not line:
        return None
    if line == "PONG":
        return (E.PONG, "")
    head, sep, rest = line.partition(":")
    if not sep:
        return None
    if head == "HELLO":
        return (E.HELLO, rest)
    if head == "F" and rest in ("0", "1"):
        return (E.FLIP, rest)
    if head == "FS" and rest in ("0", "1"):
        return (E.FSTATE, rest)
    if head == "T" and rest in ("R", "L", "V"):
        return (E.TAP, rest)
    if head == "BAT":
        if rest.isdigit() and 0 <= int(rest) <= 100:
            return (E.BATTERY, str(int(rest)))
        return None
    if head == "B":
        try:
            v = int(rest, 16)
        except ValueError:
            return None
        if 0 <= v <= 0xF:
            return (E.BUTTONS, format(v, "X"))
    return None


class LineBuffer:
    """Reassembles newline-terminated lines from arbitrary BLE notification chunks."""

    def __init__(self, limit: int = 256):
        self.buf = bytearray()
        self.limit = limit

    def feed(self, data: bytes) -> list[str]:
        self.buf.extend(data)
        out = []
        while True:
            i = self.buf.find(b"\n")
            if i < 0:
                break
            raw = bytes(self.buf[:i]).rstrip(b"\r")
            del self.buf[: i + 1]
            out.append(raw.decode("ascii", errors="replace"))
        if len(self.buf) > self.limit:   # garbage without newline: drop it
            self.buf.clear()
        return out
