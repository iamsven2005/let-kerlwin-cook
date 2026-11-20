"""Tutorial (18.8): controls test and nine lessons, completed by doing them, per player and scheme."""
from ccmaze.config import Config
from ccmaze.sim.cards import load_catalog
from ccmaze.sim.events import NodeEvent
from ccmaze.sim.game import NodeSetup
from ccmaze.sim.tutorial import CONTROLS, DONE_PAUSE_S, LESSONS, Tutorial

Vb, Fb, Lb, Rb = 0x1, 0x2, 0x4, 0x8          # 21.1: TL = V, TR = F, BL = L, BR = R


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

    def flip(self, n, down):
        return self.ev(n, "F", "1" if down else "0", dt=0.3)

    def press(self, n, mask, hold=0.05):
        self.ev(n, "B", format(mask, "X"), dt=0.05)
        self.at(hold)
        self.ev(n, "B", "0", dt=0.0)
        return self.at(0.2)

    def F(self, n):
        return self.press(n, Fb)

    def V(self, n):
        return self.press(n, Vb)

    def start(self, n):
        return self.V(n).F(n)

    def lockout(self):
        return self.at(0.7)

    def next_lesson(self):
        return self.at(DONE_PAUSE_S + 0.1)


def make(players, cfg=None):
    cfg = cfg or Config({"pause_after_step_s": 0.05, "route_flash_s": 0.1})
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
    tut.start_lessons(d.t)
    assert status(pt)["stash"] == "n/a"                  # multi-node: no stash by default
    # 1 discard
    assert key(pt) == "discard" and pt.node_display("A")["HINT"] == "F = DISCARD"
    d.F("A").next_lesson()
    # 2 start: lower button, then flip
    assert key(pt) == "start"
    d.V("A").flip("A", True).next_lesson()                # (turning it over works too)
    # 3 add right, push, start again, add left
    assert key(pt) == "add"
    g = lambda: pt.game.players[0]
    d.start("A").V("B").press("B", Rb)
    assert [e.node_id for e in g().sequence.entries] == ["A", "B"]
    d.press("A", Lb | Rb)                                # no cancel: two buttons only clear
    assert g().sequence is not None
    d.F("A").lockout()                               # push
    assert key(pt) == "add" and len(g().route) == 1
    d.start("A").V("B").press("B", Lb)                   # left end
    d.next_lesson()
    # 4 remove: F twice on the member
    assert key(pt) == "remove"
    d.start("A").V("B").press("B", Rb).F("B").F("B")
    d.next_lesson()
    # 5 push two chunks
    assert key(pt) == "push"
    d.start("A").F("A").lockout()
    assert key(pt) == "push"
    d.start("A").F("A")
    d.next_lesson()
    # 7 IF (6 is n/a): hand slot 1 = IF, slot 2 = square -> transform; then IF before a domino -> blank
    assert key(pt) == "if"
    assert pt.game.card_at("A").kind == "IF" and pt.game.card_at("B").family == "O"
    d.start("A").V("B").press("B", Rb).F("A").lockout()
    assert key(pt) == "if"                               # transform done, blank still to do
    for _ in range(6):                                   # deal until an IF sits above another card
        if pt.game.card_at("A").kind == "IF" and pt.game.card_at("B").is_move \
                and pt.game.card_at("B").family != "O":
            break
        d.F("A").lockout()
    d.start("A").V("B").press("B", Rb).F("A")
    d.next_lesson()
    # 8 FOR ... FOR spanning two pushes: slot 1 = FOR, slot 2 = DD
    assert key(pt) == "for"
    assert pt.game.card_at("A").kind == "FOR"
    d.start("A").V("B").press("B", Rb).F("A").lockout()      # FOR DD (open)
    assert key(pt) == "for"
    for _ in range(6):
        if pt.game.card_at("A").kind == "FOR":
            break
        d.F("A").lockout()
    d.start("A").F("A")                                      # FOR closes it in the next push
    d.next_lesson()
    assert key(pt) == "practice"
    assert all(status(pt)[k] == "done" for k in ("discard", "start", "add", "remove", "push", "if", "for"))
    pt.skip(d.t)
    assert pt.finished and tut.all_finished
    assert pt.node_display("A")["HINT"] == "ALL DONE"


def test_manual_single_node_player():
    tut, d = make({1: ("2F80ED", [NodeSetup("M", 1, 1, "manual")])})
    pt = tut.players[1]
    tut.start_lessons(0.0)
    st = status(pt)
    assert st["add"] == "n/a" and st["remove"] == "n/a" and st["stash"] == "todo" and st["if"] == "todo"
    assert "F (top-right)" in pt.instruction() and pt.node_display("M")["HINT"] == "F = DISCARD"
    V, L, R, F = Vb, Lb, Rb, Fb
    mstart = lambda: d.press("M", V).press("M", F)
    d.press("M", F).next_lesson()                                    # 1 discard
    assert key(pt) == "start"
    mstart()
    d.next_lesson()
    assert key(pt) == "push"                                         # 5: two chunks
    mstart(); d.press("M", F).lockout()
    mstart(); d.press("M", F)
    d.next_lesson()
    assert key(pt) == "stash"                                        # 6
    d.press("M", L).press("M", F).lockout()                          # stash in
    mstart(); d.press("M", V).at(0.5)                                # use stash
    d.press("M", F).lockout()                                        # push
    d.press("M", L).press("M", F).lockout()                          # stash again
    d.press("M", R).press("M", F)                                    # R, F: discard stash
    d.next_lesson()
    assert key(pt) == "if" and "IF card on its own" in pt.instruction()   # 7: across pushes
    assert pt.game.card_at("M").kind == "IF"
    mstart(); d.press("M", F).lockout()                              # IF alone: waits
    assert pt.game.card_at("M").family == "O"
    mstart(); d.press("M", F).lockout()                              # square: IF -> RR
    assert pt.game.card_at("M").kind == "IF"
    mstart(); d.press("M", F).lockout()                              # IF, then D next: blank
    mstart(); d.press("M", F)
    d.next_lesson()
    assert key(pt) == "for"                                          # 8: FOR / card / FOR
    for _ in range(3):
        mstart(); d.press("M", F).lockout()
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
    for l in LESSONS + [CONTROLS]:
        assert len(l.hint_g) <= 14 and len(l.hint_m) <= 14


def test_texts_have_no_tilt_bump_or_cancel_steps_and_say_no_cancel():
    text = " ".join(l.gesture + l.manual + l.single + l.single_manual + l.hint_g for l in LESSONS + [CONTROLS])
    low = text.lower()
    assert "tilt" not in low and "bump" not in low
    assert "cancel" not in low.replace("there is no cancel", "")
    start = next(l for l in LESSONS if l.key == "start")
    assert "no cancel" in start.gesture and "no cancel" in start.manual
    for key in ("discard", "stash"):                     # 19.3: latch consequences are taught
        l = next(x for x in LESSONS if x.key == key)
        assert "stash" in l.gesture and "two buttons together" in l.gesture
        assert "two buttons together" in l.manual
    assert "V (top-left)" in start.gesture and "turn the node" in start.gesture
    assert "turn the node" not in start.manual            # Manual mode has no physical flip
    assert [l.key for l in LESSONS] == ["discard", "start", "add", "remove", "push", "stash", "if", "for",
                                        "practice"]


def test_controls_test_button_and_flip_checks():
    tut, d = make({0: ("E8474C", [NodeSetup("A", 0, 1)])})
    assert tut.check_done("A") == (False, False)
    d.press("A", Lb)
    assert tut.lit("A", "L", d.t) and not tut.lit("A", "R", d.t)
    d.at(1.0)
    assert not tut.lit("A", "L", d.t)
    d.press("A", Rb)
    assert tut.lit("A", "R", d.t)
    d.V("A")
    assert tut.lit("A", "V", d.t)
    d.press("A", Lb | Rb)
    assert tut.lit("A", "clear", d.t)
    assert tut.check_done("A") == (False, False)         # F is still missing
    d.F("A")
    assert tut.lit("A", "F", d.t)
    assert tut.check_done("A") == (True, False)
    d.flip("A", True).flip("A", False)
    assert tut.check_done("A") == (True, True)
    d.ev("A", "B", "5")
    assert tut.buttons["A"] == 0x5 and tut.button_seen["A"] == d.t


def test_button_meanings_per_scheme():
    from ccmaze.sim.tutorial import button_meanings
    cfg = Config({})
    for scheme in ("gesture", "manual"):                 # 21.1: the same layout in both modes
        assert button_meanings(cfg, scheme) == {"TL": "V", "TR": "F", "BL": "L", "BR": "R", "2+": "clear"}


def test_a_route_that_reaches_the_centre_early_restarts_the_lesson_route():
    tut, d = make({0: ("E8474C", [NodeSetup("A", 0, 1)])})
    pt = tut.players[0]
    tut.start_lessons(0.0)
    d.F("A").next_lesson()                           # 1 done
    d.V("A")                                             # lesson 2 not done yet ...
    from conftest import open_maze
    from routeutil import push_chunk
    pt.game.maze = open_maze(7)
    push_chunk(pt.game, 0, ["I4:RRR", "I4:DDD"], d.t)    # ... a route reaches the centre (3, 3)
    assert pt.game.players[0].walking
    for _ in range(60):
        d.at(0.1)
    assert key(pt) == "start" and not pt.game.players[0].route      # fresh route, same lesson
    assert [a for _, _, a in pt.controller.actions] == []           # (nothing was done yet)
