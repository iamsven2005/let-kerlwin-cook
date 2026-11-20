"""The action table (18.6, 21: one button per primitive, both modes), the 3 s latch (19), and the
Gesture-only physical flip."""
import itertools

import pytest
from conftest import Driver, make_game
from routeutil import push_chunk

from ccmaze.config import Config
from ccmaze.sim.cards import Card, CardType

V_, F_, L_, R_ = 0x1, 0x2, 0x4, 0x8          # 21.1: TL = V, TR = F, BL = L, BR = R
MODES = ("gesture", "manual")


def drv(nodes=(3,), **kw):
    g, c = make_game(nodes, **kw)
    return Driver(g, c)


def actions(d):
    return [a for _, _, a in d.c.actions]


def row(d, p=0):
    seq = d.g.players[p].sequence
    return [(e.node_id or "S") for e in seq.entries] if seq else None


def seqs(d, *nodes):
    return [d.g.node_display(n)["SEQ"] for n in nodes]


def press(d, node, mask, hold=0.05):
    """Press (all bits at once), hold, release, then let the 100 ms chord window close."""
    d.btn(node, mask, dt=0.01).at(hold).btn(node, 0, dt=0.0)
    d.at(0.15)
    return d


def V(d, node):
    return press(d, node, V_)


def F(d, node):
    return press(d, node, F_)


def L(d, node):
    return press(d, node, L_)


def R(d, node):
    return press(d, node, R_)


def start(d, node):
    V(d, node)
    F(d, node)


# ================================================================ the layout (21.1), both modes

@pytest.mark.parametrize("scheme", MODES)
def test_each_single_button_maps_correctly(scheme):
    d = drv(scheme=scheme)
    n = d.g.nodes["N1"]
    for mask, bit in ((V_, "V"), (L_, "L"), (R_, "R")):
        press(d, "N1", mask)
        assert n.register == {bit}, (scheme, mask, n.register)
        d.at(3.1)                                         # let the latch run out
    assert actions(d) == []
    F(d, "N1")                                            # F with an empty register: discard
    assert actions(d) == ["discard"]


@pytest.mark.parametrize("scheme", MODES)
def test_every_two_and_three_plus_combo_only_clears(scheme):
    d = drv(scheme=scheme)
    n = d.g.nodes["N1"]
    combos = [a | b for a, b in itertools.combinations((1, 2, 4, 8), 2)] + [0x7, 0xB, 0xD, 0xE, 0xF]
    for mask in combos:
        V(d, "N1")
        assert n.register == {"V"}
        press(d, "N1", mask)
        assert n.register == set() and not n.face_down, (scheme, hex(mask))
        d.at(0.7)
    assert actions(d) == []                               # never a discard, start or stash


@pytest.mark.parametrize("scheme", MODES)
def test_chords_landing_one_button_at_a_time_never_act(scheme):
    d = drv(scheme=scheme)
    d.btn("N2", 0x2).btn("N2", 0x3, dt=0.02).btn("N2", 0x7, dt=0.02).btn("N2", 0xF, dt=0.02)
    d.at(0.3).btn("N2", 0).at(0.2)
    assert actions(d) == [] and not d.g.nodes["N2"].face_down


@pytest.mark.parametrize("scheme", MODES)
def test_late_second_button_while_holding_is_a_clear(scheme):
    d = drv(scheme=scheme)
    n = d.g.nodes["N1"]
    d.btn("N1", V_).at(0.2)                               # V resolves
    d.btn("N1", V_ | F_).at(0.2)                          # F added late, V still held: clear, not F
    d.btn("N1", 0).at(0.2)
    assert n.register == set() and actions(d) == []


def test_a_single_press_waits_for_the_chord_window():
    d = drv()
    d.btn("N1", L_)
    d.at(0.05)
    assert d.g.nodes["N1"].register == set()              # not yet: a second button may still come
    d.at(0.06)
    assert d.g.nodes["N1"].register == {"L"}


@pytest.mark.parametrize("scheme", MODES)
def test_long_press_F_is_nothing_special(scheme):
    d = drv(scheme=scheme)
    start(d, "N2")
    V(d, "N3")
    R(d, "N3")
    d.btn("N3", F_).at(1.5).btn("N3", 0).at(0.2)          # held 1.5 s on a member: one F only
    assert row(d) == ["N2", "N3"] and d.g.nodes["N3"].face_down
    d.btn("N3", F_).at(1.5).btn("N3", 0).at(0.2)          # a second F (held again): the remove
    assert actions(d)[-1] == "remove" and row(d) == ["N2"]


def test_tilt_and_v_tap_are_ignored_by_default():
    d = drv()
    for side in "LRV":
        d.tap("N1", side)
    d.at(0.2)
    assert d.g.nodes["N1"].register == set()
    d = drv(accept_tilt=True, accept_tap_v=True)         # both can be switched back on
    d.tap("N1", "V").tap("N1", "L")
    assert d.g.nodes["N1"].register == {"V", "L"}


# ================================================================ the action table, both modes

@pytest.mark.parametrize("scheme", MODES)
def test_start_add_left_right_and_push(scheme):
    d = drv(scheme=scheme)
    pl = d.g.players[0]
    c = {n: d.g.card_at(n) for n in ("N1", "N2", "N3")}
    start(d, "N2")
    assert d.g.is_head("N2") and d.g.nodes["N2"].face_down
    assert seqs(d, "N1", "N2", "N3") == ["-", "H1", "-"]
    V(d, "N3")
    R(d, "N3")
    V(d, "N1")
    L(d, "N1")
    assert row(d) == ["N1", "N2", "N3"] and actions(d) == ["start", "add_right", "add_left"]
    assert seqs(d, "N1", "N2", "N3") == ["1", "H2", "3"]
    assert not d.g.nodes["N1"].face_down                  # added nodes stay face-up
    F(d, "N2")                                            # F on the head: push
    assert actions(d)[-1] == "push" and pl.sequence is None
    assert pl.route[-1].cards == [c["N1"], c["N2"], c["N3"]]
    assert not any(d.g.nodes[n].face_down for n in ("N1", "N2", "N3"))


@pytest.mark.parametrize("scheme", MODES)
def test_f_twice_removes_a_member_and_too_slow_stays(scheme):
    d = drv(scheme=scheme)
    start(d, "N2")
    V(d, "N3")
    R(d, "N3")
    F(d, "N3")
    d.at(3.2)
    F(d, "N3")                                            # second F after remove_window_ms: stays
    assert row(d) == ["N2", "N3"]
    F(d, "N3")
    F(d, "N3")
    assert actions(d)[-1] == "remove" and row(d) == ["N2"]


@pytest.mark.parametrize("scheme", MODES)
def test_f_button_discard_snaps_back_face_up(scheme):
    d = drv(scheme=scheme)
    pl = d.g.players[0]
    old = pl.slots[1]
    F(d, "N2")
    assert actions(d) == ["discard"] and old in d.g.deck.discard_pile
    assert not d.g.nodes["N2"].face_down                  # spec HUB-Q41
    assert d.g.node_display("N2")["REG"] == "0000"
    d.at(0.7)
    F(d, "N2")
    assert actions(d) == ["discard", "discard"]


@pytest.mark.parametrize("scheme", MODES)
def test_lockout_after_push_and_after_discard(scheme):
    d = drv(scheme=scheme)
    start(d, "N2")
    F(d, "N2")                                            # push
    F(d, "N2")                                            # bounce inside 600 ms: ignored
    assert actions(d) == ["start", "push"]
    d.at(0.7)
    F(d, "N2")                                            # after the lockout: a normal discard
    assert actions(d)[-1] == "discard"
    F(d, "N2")
    assert actions(d).count("discard") == 1


@pytest.mark.parametrize("scheme", MODES)
def test_stash_in_use_stash_and_discard_stash(scheme):
    d = drv(nodes=(1,), scheme=scheme)
    pl = d.g.players[0]
    L(d, "N1")
    F(d, "N1")                                            # L, F: stash in
    assert actions(d) == ["stash_in"] and pl.stash is not None
    stashed = pl.stash
    d.at(0.7)
    start(d, "N1")
    V(d, "N1")                                            # V on the face-down head: use stash, now
    assert actions(d)[-1] == "use_stash" and row(d) == ["N1", "S"]
    F(d, "N1")                                            # push
    assert pl.route[-1].cards[-1] is stashed and pl.stash is None
    d.at(0.7)
    L(d, "N1")
    F(d, "N1")
    d.at(0.7)
    R(d, "N1")
    F(d, "N1")                                            # R, F: discard stash
    assert actions(d)[-1] == "discard_stash" and pl.stash is None


@pytest.mark.parametrize("scheme", MODES)
def test_no_cancel_anywhere(scheme):
    d = drv(scheme=scheme)
    pl = d.g.players[0]
    start(d, "N2")
    for mask in (V_ | F_, L_ | R_, 0xF, 0x7):
        press(d, "N2", mask)
    d.btn("N2", F_).at(1.5).btn("N2", 0).at(0.2)          # a long F pushes (it's just an F)
    assert "cancel" not in actions(d) and not hasattr(d.g, "cancel_sequence")
    assert actions(d) == ["start", "push"]


def test_add_needs_V_first_and_an_open_sequence():
    d = drv()
    R(d, "N1")
    F(d, "N1")                                            # R, F with an empty stash: nothing
    assert actions(d) == []
    V(d, "N1")
    R(d, "N1")                                            # no open sequence: just V + R latched
    assert d.g.nodes["N1"].register == {"V", "R"}


def test_members_ignore_buttons_except_the_remove_gesture():
    d = drv()
    start(d, "N2")
    V(d, "N3")
    R(d, "N3")
    for mask in (V_, L_, R_, L_ | R_):
        press(d, "N3", mask)
    assert d.g.nodes["N3"].register == set() and row(d) == ["N2", "N3"]


def test_start_while_sequence_open_does_nothing():
    d = drv()
    start(d, "N2")
    start(d, "N1")
    assert row(d) == ["N2"] and actions(d) == ["start"]


def test_mixed_schemes_same_player():
    d = drv(nodes=(2,), scheme=["gesture", "manual"])
    start(d, "N1")
    V(d, "N2")
    L(d, "N2")
    assert row(d) == ["N2", "N1"]


# ================================================================ Gesture only: the physical flip

def test_physical_discard_stays_down_until_flipped_up():
    d = drv()
    d.flip("N2", True)
    assert actions(d) == ["discard"] and d.g.node_display("N2")["REG"] == "0001"
    d.flip("N2", False)
    assert actions(d) == ["discard"]                      # turning it back over does nothing


def test_mixed_f_button_and_physical_flips():
    d = drv()
    V(d, "N2")
    d.flip("N2", True)                                    # physical start
    F(d, "N2")                                            # the F button toggles down -> up: push
    assert actions(d) == ["start", "push"] and not d.g.nodes["N2"].face_down
    d.at(0.7)
    d.flip("N2", False)                                   # physically back up: already up
    assert actions(d) == ["start", "push"]
    V(d, "N1")
    F(d, "N1")                                            # button start ...
    d.flip("N1", True)                                    # ... a physical down is a duplicate ...
    d.flip("N1", False)                                   # ... and a physical up pushes
    assert actions(d) == ["start", "push", "start", "push"]


def test_mixed_double_F_removes():
    d = drv()
    start(d, "N2")
    V(d, "N1")
    L(d, "N1")
    d.flip("N1", True)                                    # physical down ...
    F(d, "N1")                                            # ... F button toggles back up: remove
    assert actions(d)[-1] == "remove"


def test_physical_lockout_keeps_state_in_step():
    d = drv()
    start(d, "N2")
    F(d, "N2")                                            # push
    d.flip("N2", True, dt=0.05)                           # physical, inside 600 ms
    assert actions(d) == ["start", "push"] and d.g.nodes["N2"].face_down
    d.flip("N2", False, dt=0.7)
    assert actions(d) == ["start", "push"]


def test_manual_ignores_taps_and_flips():
    d = drv(scheme="manual", accept_tilt=True, accept_tap_v=True)
    d.tap("N1", "V").flip("N1", True)
    assert actions(d) == [] and d.g.nodes["N1"].register == set()
    assert d.g.node_display("N1")["DOFF"] == "0"


def test_physical_flips_can_be_switched_off():
    d = drv(physical_flip_enabled=False)
    d.flip("N1", True)
    assert actions(d) == [] and not d.g.nodes["N1"].face_down
    F(d, "N1")
    assert actions(d) == ["discard"]


def test_duplicate_flips_are_idempotent():
    d = drv()
    d.flip("N1", True).flip("N1", True)
    assert actions(d) == ["discard"]


# ================================================================ the 3 s latch (section 19)

def test_latch_holds_for_3_s_then_clears_with_no_action():
    d = drv()
    n = d.g.nodes["N1"]
    L(d, "N1")
    t0 = d.t - 0.15 - 0.05                               # the press (resolved after the window)
    d.at(2.7)
    assert n.register == {"L"} and d.c.latch_remaining("N1", d.t) > 0
    d.at(t0 + 3.05 - d.t)
    assert n.register == set() and actions(d) == []
    assert d.g.node_display("N1")["REG"] == "0000"


def test_a_new_press_restarts_the_timer():
    d = drv()
    n = d.g.nodes["N1"]
    L(d, "N1")
    d.at(2.5)
    L(d, "N1")                                           # same bit again: just restarts
    d.at(2.5)
    assert n.register == {"L"}
    R(d, "N1")
    d.at(2.6)
    assert n.register == {"L", "R"}
    d.at(0.4)
    assert n.register == set()


def test_the_flip_commits_the_latched_register():
    d = drv(nodes=(1,))
    pl = d.g.players[0]
    L(d, "N1")
    d.at(2.5)
    F(d, "N1")                                           # stray L still latched: stash in, not discard
    assert actions(d) == ["stash_in"] and pl.stash is not None
    assert d.g.nodes["N1"].register == set()


def test_v_then_L_at_2_9_s_adds_at_3_1_s_only_L_is_latched():
    for gap, added in ((2.9, True), (3.1, False)):
        d = drv()
        start(d, "N2")
        d.btn("N1", V_).at(0.05).btn("N1", 0)            # V pressed at t0 ...
        t0 = d.t - 0.05
        d.at(t0 + gap - d.t)
        d.btn("N1", L_)                                  # ... L pressed at t0 + gap
        d.at(0.15).btn("N1", 0)
        if added:
            assert row(d) == ["N1", "N2"] and actions(d)[-1] == "add_left"
        else:
            assert row(d) == ["N2"] and d.g.nodes["N1"].register == {"L"}


def test_two_buttons_clear_early():
    d = drv()
    n = d.g.nodes["N1"]
    V(d, "N1")
    R(d, "N1")
    assert n.register == {"V", "R"}
    press(d, "N1", L_ | R_)
    assert n.register == set()
    F(d, "N1")                                           # nothing latched: a plain discard
    assert actions(d) == ["discard"]


def test_bits_latched_on_a_node_are_cleared_when_it_joins():
    d = drv()
    start(d, "N2")
    R(d, "N1")
    V(d, "N1")
    L(d, "N1")                                           # V latched (with R): L adds at the left
    assert row(d) == ["N1", "N2"] and d.g.nodes["N1"].register == set()


def test_reg_is_sent_on_the_timeout_clear():
    from ccmaze.nodes.base import NodeHub
    from ccmaze.nodes.virtual import VirtualNode
    d = drv()
    hub = NodeHub()
    node = hub.add(VirtualNode(hub, "N1"))
    hub.sync("N1", d.g.node_display("N1"))
    L(d, "N1")
    hub.sync("N1", d.g.node_display("N1"))
    assert node.display["REG"] == "0100"
    n_writes = node.writes_ok
    d.at(3.1)                                           # the latch runs out
    hub.sync("N1", d.g.node_display("N1"))
    assert node.display["REG"] == "0000" and node.writes_ok == n_writes + 1
    assert node.last_command == "REG"


# ================================================================ walking, robustness, config

def test_nodes_do_nothing_while_the_avatar_walks():
    d = drv()
    pl = d.g.players[0]
    push_chunk(d.g, 0, ["I3:RR", "I3:DD"], d.t)          # open_maze(5): (0,0) -> (2,2)
    assert pl.walking
    assert all(d.g.node_display(n)["MSG"] == "WALKING" for n in pl.node_ids)
    slots = list(pl.slots)
    F(d, "N1")
    start(d, "N2")
    d.flip("N3", True)
    assert actions(d) == [] and pl.slots == slots and pl.sequence is None
    assert d.g.nodes["N3"].face_down                     # state still follows the real node


def test_events_from_unassigned_nodes_are_ignored():
    d = drv()
    d.btn("ZZ", V_).flip("ZZ", True)
    assert actions(d) == []


@pytest.mark.parametrize("scheme", MODES)
def test_losing_a_chunk_node_drops_the_chunk(scheme):
    d = drv(scheme=scheme)
    pl = d.g.players[0]
    start(d, "N3")
    V(d, "N1")
    R(d, "N1")
    held = pl.slots[0]
    d.ev("N1", "DISCONNECTED", "")
    assert pl.sequence is None and pl.slots[0] is held   # spec HUB-Q43: a system event
    if scheme == "manual":
        assert not d.g.nodes["N3"].face_down             # the old head lies face-up again


def test_losing_a_bystander_node_keeps_the_sequence():
    d = drv()
    pl = d.g.players[0]
    start(d, "N3")
    d.ev("N2", "DISCONNECTED", "")
    assert pl.sequence is not None and pl.sequence.head == "N3"
    frozen = pl.slots[1]
    d.flip("N2", True)
    assert pl.slots[1] is frozen
    d.ev("N2", "CONNECTED", "")
    d.ev("N2", "FSYNC", "1")
    d.ev("N2", "BSYNC", "1")
    assert d.g.nodes["N2"].face_down and pl.slots[1] is frozen
    d.btn("N2", 0x1)
    assert d.c.prev_mask["N2"] == 1


def test_register_display_format():
    d = drv()
    V(d, "N1")
    R(d, "N1")
    assert d.g.node_display("N1")["REG"] == "1010"


def test_control_card_code_on_the_node():
    from ccmaze.sim.cards import ControlType
    d = drv()
    pl = d.g.players[0]
    pl.slots[0] = Card(ControlType("IF", "O", CardType("LLU", "L")))
    pl.slots[1] = Card(ControlType("FOR"))
    assert d.g.node_display("N1")["CARD"] == "IF:O:LLU"
    assert d.g.node_display("N2")["CARD"] == "FOR"


def test_fs_is_a_silent_face_state_sync():
    d = drv()
    V(d, "N1")
    d.ev("N1", "FS", "1")
    n = d.g.nodes["N1"]
    assert n.face_down and n.register == set() and actions(d) == []
    d.ev("N1", "FS", "0")
    assert not n.face_down and actions(d) == []


def test_fs_on_a_member_does_not_count_as_half_a_remove():
    d = drv()
    start(d, "N2")
    V(d, "N3")
    L(d, "N3")
    d.flip("N3", True, dt=0.3)
    d.ev("N3", "FS", "0")
    d.flip("N3", True, dt=0.3).flip("N3", False, dt=0.3)
    assert row(d) == ["N2"] and actions(d).count("remove") == 1


def test_parse_fs():
    from ccmaze.nodes.protocol import parse_line
    assert parse_line("FS:1") == ("FS", "1") and parse_line("FS:2") is None


def test_deprecated_config_keys_are_dropped():
    c = Config({"pause_between_sequences_s": 3.0, "bump_correlation_ms": 80,
                "optional_features": {"interrupt_mode": True, "coop_mode": True}})
    assert c.get("pause_between_sequences_s") is None and c.get("bump_correlation_ms") is None
    assert "interrupt_mode" not in c.optional_features and c.feature("coop_mode")
    layout = {"bit0": "V", "bit1": "F", "bit2": "L", "bit3": "R"}
    assert c.gesture_button_map == layout and c.manual_button_map == layout and c.latch_ms == 3000
    assert c.physical_flip_enabled and not c.accept_tilt and not c.accept_tap_v


def test_old_default_button_maps_are_migrated():
    new = {"bit0": "V", "bit1": "F", "bit2": "L", "bit3": "R"}
    for old in ({"bit0": "L", "bit1": "R", "bit2": "L", "bit3": "R"},                     # v1.7
                {"bit0": "L", "bit1": "R", "bit2": "V", "bit3": "V"},                     # v1.8-1.9
                {"bit0": "L", "bit1": "R", "bit2": "-", "bit3": "-",
                 "chords": {"3": "F", "C": "V"}}):                                        # v1.10
        assert Config({"gesture_button_map": old}).gesture_button_map == new
    old_manual = {"bit0": "L", "bit1": "R", "bit2": "V", "bit3": "F"}
    assert Config({"manual_button_map": old_manual}).manual_button_map == new
    custom = {"bit0": "F", "bit1": "V", "bit2": "R", "bit3": "L", "chords": {"3": "F"}}
    assert Config({"gesture_button_map": custom}).gesture_button_map == {
        "bit0": "F", "bit1": "V", "bit2": "R", "bit3": "L"}                                # chords dropped
