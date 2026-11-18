import pytest

from ccmaze.nodes.base import NodeHub
from ccmaze.nodes.physical import PhysicalNode
from ccmaze.nodes.protocol import LineBuffer, ProtocolError, encode, parse_line
from ccmaze.nodes.virtual import VirtualNode
from ccmaze.sim import events as E


class FakeWorker:
    def __init__(self):
        from ccmaze.config import Config
        self.cfg = Config({})
        self.written = []

    def write(self, node, data):
        self.written.append(data)

    def disconnect(self, node):
        pass


def test_encode_fields_and_limits():
    assert encode("CARD", "UUR") == b"CARD:UUR\n"
    assert encode("CARD", "-") == b"CARD:-\n"
    assert encode("REG", "1010") == b"REG:1010\n"
    assert encode("COL", "E8474C") == b"COL:E8474C\n"
    assert encode("BLINK") == b"BLINK\n"
    assert encode("PING") == b"PING\n"
    assert encode("MSG", "P1&P2 WIN") == b"MSG:P1&P2 WIN\n"
    assert encode("MSG", "X" * 30) == b"MSG:" + b"X" * 14 + b"\n"   # truncated to 14 chars
    assert encode("MSG", "") == b"MSG:\n"
    assert encode("HINT", "V THEN FLIP DOWN!") == b"HINT:V THEN FLIP DO\n"   # capped at 14 chars = 20 bytes
    assert encode("CARD", "IF:I3:URUR") == b"CARD:IF:I3:URUR\n"
    for f, p in [("CARD", "UUUU"), ("SEQ", "H6"), ("HAND", "6"), ("DOFF", "0"), ("CARD", "FOR")]:
        assert len(encode(f, p)) <= 20
    with pytest.raises(ProtocolError):
        encode("CARD", "U" * 20)


def test_parse_lines():
    assert parse_line("HELLO:07:1.2") == (E.HELLO, "07:1.2")
    assert parse_line("F:1") == (E.FLIP, "1")
    assert parse_line("T:V") == (E.TAP, "V")
    assert parse_line("B:a") == (E.BUTTONS, "A")
    assert parse_line("PONG") == (E.PONG, "")
    assert parse_line("BAT:87") == (E.BATTERY, "87")
    assert parse_line("BAT:101") is None
    assert parse_line("BAT:x") is None
    assert parse_line("T:X") is None
    assert parse_line("B:1F") is None
    assert parse_line("garbage") is None


def test_line_buffer_reassembles_chunks():
    lb = LineBuffer()
    assert lb.feed(b"T:") == []
    assert lb.feed(b"R\nF:1\r\nB") == ["T:R", "F:1"]
    assert lb.feed(b":3\n") == ["B:3"]


def test_physical_node_hello_then_state_sync_then_flip():
    hub = NodeHub()
    w = FakeWorker()
    n = PhysicalNode(hub, "07", "AA:BB", w)
    hub.add(n)
    n.connected = True
    n.on_data(b"HELLO:07:1.0\nF:1\nB:2\nBAT:76\n", ts=5.0)
    n.on_data(b"F:0\nB:0\nT:V\nPONG\nBAT:75\n", ts=6.0)
    evs = hub.drain()
    assert [(e.kind, e.value) for e in evs] == [
        (E.HELLO, "07:1.0"), (E.FSYNC, "1"), (E.BSYNC, "2"), (E.FLIP, "0"), (E.BUTTONS, "0"), (E.TAP, "V")]
    assert evs[0].ts == 5.0 and evs[-1].ts == 6.0
    assert n.fw_version == "1.0" and n.battery == 75


def test_hub_sync_sends_only_changes_and_resends_after_invalidate():
    hub = NodeHub()
    w = FakeWorker()
    n = PhysicalNode(hub, "07", "AA", w)
    hub.add(n)
    n.connected = True
    d = {"COL": "E8474C", "HAND": "1", "CARD": "UUR", "SEQ": "-", "REG": "0000", "DOFF": "1", "MSG": ""}
    hub.sync("07", d)
    # MSG "" isn't sent: the CARD: just before it already cleared the node's message
    assert w.written == [b"COL:E8474C\n", b"HAND:1\n", b"CARD:UUR\n", b"SEQ:-\n", b"REG:0000\n", b"DOFF:1\n"]
    hub.sync("07", d)
    assert len(w.written) == 6
    hub.sync("07", {**d, "CARD": "D"})
    assert w.written[-1] == b"CARD:D\n"
    n.invalidate()
    hub.sync("07", d)
    assert len(w.written) == 13


def test_msg_is_resent_after_card_clears_it():
    hub = NodeHub()
    w = FakeWorker()
    n = PhysicalNode(hub, "07", "AA", w)
    hub.add(n)
    n.connected = True
    d = {"CARD": "-", "MSG": "P1 WINS"}
    hub.sync("07", d)
    assert w.written == [b"CARD:-\n", b"MSG:P1 WINS\n"]
    hub.sync("07", {"CARD": "UU", "MSG": "P1 WINS"})
    assert w.written[-2:] == [b"CARD:UU\n", b"MSG:P1 WINS\n"]   # CARD cleared it on the node
    hub.sync("07", {"CARD": "UU", "MSG": ""})
    assert w.written[-1] == b"MSG:\n"


def test_virtual_node_emits_same_events():
    hub = NodeHub()
    v = VirtualNode(hub, "V9")
    hub.add(v)
    v.tap("R")
    v.toggle_flip()
    v.press(0x1)
    v.press(0x2)
    v.release(0x3)
    v.toggle_flip()
    got = [(e.kind, e.value) for e in hub.drain()]
    assert got == [(E.TAP, "R"), (E.FLIP, "1"), (E.BUTTONS, "1"), (E.BUTTONS, "3"), (E.BUTTONS, "0"), (E.FLIP, "0")]
    hub.sync("V9", {"CARD": "UU", "SEQ": "H"})
    assert v.display["CARD"] == "UU" and v.display["SEQ"] == "H"
