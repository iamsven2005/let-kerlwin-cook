"""Tutorial lessons complete by doing them, per player and per control scheme (14 B5)."""
from ccmaze.config import Config
from ccmaze.sim.cards import load_catalog
from ccmaze.sim.events import NodeEvent
from ccmaze.sim.game import NodeSetup
from ccmaze.sim.tutorial import DONE_PAUSE_S, LESSONS, Tutorial


class TDriver:
    def __init__(self, tut):
        self.tut, self.t = tut, 0.0

    def at(self, dt):
        self.t += dt
        self.tut.tick(self.t)
        return self

    def ev(self, node, kind, value, dt=0.05):
        self.t += dt
        self.tut.handle(NodeEvent(node, kind, value, self.t))
        self.tut.tick(self.t)
        return self

    def tap(self, n, side):
        return self.ev(n, "T", side)

    def flip(self, n, down):
        return self.ev(n, "F", "1" if down else "0", dt=0.3)

    def press(self, n, mask, hold=0.05):
        self.ev(n, "B", format(mask, "X"), dt=0.05)
        self.at(hold)
        self.ev(n, "B", "0", dt=0.0)
        return self.at(0.2)

    def long_press(self, n, mask=0x8):
        self.ev(n, "B", format(mask, "X"))
        self.at(1.1)
        self.ev(n, "B", "0", dt=0.0)
        return self.at(0.2)

    def next_lesson(self):
        return self.at(DONE_PAUSE_S + 0.1)


def make(players, cfg=None):
    cfg = cfg or Config({"pause_after_step_s": 0.05})
    tut = Tutorial(cfg, load_catalog(None), players)
    return tut, TDriver(tut)


def status(pt):
    return {s.lesson.key: s.status for s in pt.states}


def key(pt):
    return pt.lesson.key


def test_gesture_two_node_player_goes_through_every_lesson():
    tut, d = make({0: ("E8474C", [NodeSetup("A", 0, 1), NodeSetup("B", 0, 2)])})
    pt = tut.players[0]
    assert tut.phase == "controls" and key(pt) == "controls"
    d.tap("A", "V")
    assert tut.raw["A"][-1][1:] == ("T", "V")
    tut.start_lessons(d.t)
    assert status(pt)["stash"] == "n/a"           # multi-node: no stash by default
    # 1 discard
    assert key(pt) == "discard" and pt.node_display("A")["HINT"] == "FLIP=DISCARD"
    d.flip("A", True).flip("A", False).next_lesson()
    # 2 start
    assert key(pt) == "start"
    d.tap("A", "V").flip("A", True).next_lesson()
    # 3 add right, cancel, add left
    assert key(pt) == "add"
    d.tap("A", "V").flip("A", True)
    d.tap("B", "V").tap("B", "L")                 # right end
    d.ev("A", "B", "1").ev("A", "B", "0")         # cancel
    d.flip("A", False)
    d.tap("A", "V").flip("A", True)
    d.tap("B", "V").tap("B", "R")                 # left end
    d.next_lesson()
    # 4 remove
    assert key(pt) == "remove"
    d.tap("A", "V").flip("A", True).tap("B", "V").tap("B", "L")
    d.flip("B", True).flip("B", False).next_lesson()
    # 5 push and watch
    assert key(pt) == "push"
    d.tap("A", "V").flip("A", True).flip("A", False)
    for _ in range(40):
        d.at(0.1)
    # 7 IF combo (6 is n/a): hand slot 1 = IF, slot 2 = square
    assert key(pt) == "if"
    assert pt.game.card_at("A").kind == "IF" and pt.game.card_at("B").family == "O"
    d.tap("A", "V").flip("A", True).tap("B", "V").tap("B", "L").flip("A", False)
    d.next_lesson()
    # 8 FOR combo
    assert key(pt) == "for"
    assert pt.game.card_at("A").kind == "FOR"
    d.tap("A", "V").flip("A", True).tap("B", "V").tap("B", "L").flip("A", False)
    d.next_lesson()
    assert key(pt) == "practice"
    assert all(status(pt)[k] == "done" for k in ("discard", "start", "add", "remove", "push", "if", "for"))
    pt.skip(d.t)
    assert pt.finished and tut.all_finished
    assert pt.node_display("A")["HINT"] == "ALL DONE"


def test_manual_single_node_player_uses_stash_and_long_press_lessons():
    tut, d = make({1: ("2F80ED", [NodeSetup("M", 1, 1, "manual")])})
    pt = tut.players[1]
    tut.start_lessons(0.0)
    st = status(pt)
    assert st["add"] == "n/a" and st["remove"] == "n/a" and st["stash"] == "todo"
    assert "Press F" in pt.instruction() and pt.node_display("M")["HINT"] == "PRESS F"
    V, L, F = 0x4, 0x1, 0x8
    d.press("M", F).next_lesson()                                   # discard
    assert key(pt) == "start"
    d.press("M", V).press("M", F).next_lesson()
    assert key(pt) == "push"
    d.press("M", V).press("M", F).press("M", F)
    for _ in range(40):
        d.at(0.1)
    assert key(pt) == "stash"
    d.press("M", L).press("M", F)                                   # stash in
    d.at(0.7).press("M", V).press("M", F).press("M", V).at(0.5)      # start, use stash
    d.next_lesson()
    assert key(pt) == "if"
    assert "Stash the square" in pt.instruction()
    d.press("M", L).press("M", F).at(0.7)                           # stash the square
    assert pt.game.card_at("M").kind == "IF"
    d.press("M", V).press("M", F).press("M", V).at(0.5).press("M", F)
    d.next_lesson()
    assert key(pt) == "for"
    d.press("M", L).press("M", F).at(0.7)
    d.press("M", V).press("M", F).press("M", V).at(0.5).press("M", F)
    d.next_lesson()
    assert key(pt) == "practice"


def test_skip_and_players_progress_independently():
    tut, d = make({0: ("E8474C", [NodeSetup("A", 0, 1)]), 1: ("2F80ED", [NodeSetup("B", 1, 1)])})
    tut.start_lessons(0.0)
    a, b = tut.players[0], tut.players[1]
    d.flip("A", True).next_lesson()
    assert key(a) == "start" and key(b) == "discard"
    b.skip(d.t)
    assert status(b)["discard"] == "skipped" and key(b) == "start"
    d.flip("B", True)                      # B's events never touch A's game
    assert key(a) == "start"


def test_every_hint_fits_the_protocol():
    for l in LESSONS:
        assert len(l.hint_g) <= 14 and len(l.hint_m) <= 14
