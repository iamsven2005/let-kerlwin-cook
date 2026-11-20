"""The v1.4 action table (section 14 B3), gesture and manual."""
from conftest import Driver, make_game

from ccmaze.sim.cards import Card, CardType


def drv(nodes=(3,), **kw):
    g, c = make_game(nodes, **kw)
    return Driver(g, c)


def actions(d):
    return [a for _, _, a in d.c.actions]


def row(d, p=0):
    """The row as (node_id or 'S', card code), left to right."""
    seq = d.g.players[p].sequence
    return [(e.node_id or "S") for e in seq.entries] if seq else None


def seqs(d, *nodes):
    return [d.g.node_display(n)["SEQ"] for n in nodes]


# ================================================================ gesture mode

def start(d, node):
    d.tap(node, "V").flip(node, True, dt=0.3)


def test_discard_is_flip_with_empty_register():
    d = drv()
    pl = d.g.players[0]
    old = pl.slots[1]
    d.flip("N2", True)
    assert actions(d) == ["discard"]
    assert old not in pl.slots and old in d.g.deck.discard_pile
    assert d.g.node_display("N2")["REG"] == "0001"
    d.flip("N2", False)
    assert actions(d) == ["discard"]


def test_start_sequence_head_badge_carries_position():
    d = drv()
    start(d, "N2")
    assert d.g.is_head("N2")
    assert seqs(d, "N1", "N2", "N3") == ["-", "H1", "-"]


def test_add_right_with_L_tap_and_left_with_R_tap():
    d = drv()
    start(d, "N2")
    d.tap("N3", "V", dt=0.3).tap("N3", "L", dt=0.3)      # its LEFT edge touched -> right end
    assert actions(d)[-1] == "add_right"
    assert row(d) == ["N2", "N3"]
    assert d.g.nodes["N3"].face_down is False             # stays face-up, no flip needed
    d.tap("N1", "V", dt=0.3).tap("N1", "R", dt=0.3)      # its RIGHT edge touched -> left end
    assert actions(d)[-1] == "add_left"
    assert row(d) == ["N1", "N2", "N3"]
    assert seqs(d, "N1", "N2", "N3") == ["1", "H2", "3"]


def test_push_runs_the_row_left_to_right():
    d = drv()
    pl = d.g.players[0]
    c = {n: d.g.card_at(n) for n in ("N1", "N2", "N3")}
    start(d, "N2")
    d.tap("N3", "V", dt=0.3).tap("N3", "L", dt=0.1)
    d.tap("N1", "V", dt=0.3).tap("N1", "R", dt=0.1)
    d.flip("N2", False, dt=0.3)                          # flip the head up: push
    assert actions(d)[-1] == "push"
    assert pl.last_push.cards == [c["N1"], c["N2"], c["N3"]]
    assert pl.sequence is None and all(pl.slots)


def test_add_needs_V_first_and_an_open_sequence():
    d = drv()
    d.tap("N1", "V").tap("N1", "L", dt=0.1)              # no sequence open: nothing
    assert "add_right" not in actions(d)
    start(d, "N2")
    d.at(1.0)
    d.tap("N3", "L", dt=0.1)                             # side tap without V
    assert row(d) == ["N2"]
    d.tap("N3", "V", dt=0.1)
    assert row(d) == ["N2"]                              # L then V is not "V then side tap"


def test_old_R_plus_flip_is_unrecognised():
    d = drv()
    start(d, "N2")
    old = d.g.card_at("N1")
    d.tap("N1", "R", dt=0.3).flip("N1", True, dt=0.1)
    assert row(d) == ["N2"] and d.g.card_at("N1") is old   # not an add, not a discard


def test_taps_on_members_and_head_side_taps_are_ignored():
    d = drv()
    start(d, "N2")
    d.tap("N3", "V", dt=0.3).tap("N3", "L", dt=0.1)
    d.tap("N3", "V", dt=0.1).tap("N3", "R", dt=0.05)     # member: ignored
    d.tap("N2", "R", dt=0.05)                            # head: side tap ignored
    assert d.g.nodes["N3"].register == set() and d.g.nodes["N2"].register == set()
    assert row(d) == ["N2", "N3"]


def test_remove_member_by_double_flip():
    d = drv()
    start(d, "N2")
    d.tap("N3", "V", dt=0.3).tap("N3", "L", dt=0.1)
    d.tap("N1", "V", dt=0.3).tap("N1", "L", dt=0.1)
    assert row(d) == ["N2", "N3", "N1"]
    d.flip("N3", True, dt=0.5).flip("N3", False, dt=1.0)
    assert actions(d)[-1] == "remove"
    assert row(d) == ["N2", "N1"]
    assert seqs(d, "N1", "N2", "N3") == ["2", "H1", "-"]


def test_member_left_down_past_window_stays():
    d = drv()
    start(d, "N2")
    d.tap("N3", "V", dt=0.3).tap("N3", "L", dt=0.1)
    d.flip("N3", True, dt=0.5)
    d.at(3.5)
    d.flip("N3", False)
    assert row(d) == ["N2", "N3"]                        # too slow: still in the sequence
    d.flip("N3", True, dt=0.5).flip("N3", False, dt=0.5)
    assert row(d) == ["N2"]                              # a new quick double flip removes it


def test_head_cannot_be_removed_flipping_it_up_pushes():
    d = drv()
    start(d, "N2")
    d.flip("N2", False, dt=0.5)
    assert actions(d)[-1] == "push"


def test_cancel_with_button_on_head():
    d = drv()
    pl = d.g.players[0]
    before = list(pl.slots)
    start(d, "N2")
    d.tap("N1", "V", dt=0.3).tap("N1", "R", dt=0.1)
    d.btn("N2", 0x1, dt=0.2).btn("N2", 0x0, dt=0.1)
    assert actions(d)[-1] == "cancel"
    assert pl.sequence is None and pl.slots == before


def test_button_clears_register_in_gesture_mode():
    d = drv()
    d.tap("N1", "V")
    d.btn("N1", 0x4).btn("N1", 0x0)
    assert d.g.nodes["N1"].register == set()


def test_v_waits_for_flip_or_side_tap():
    d = drv()
    start(d, "N2")
    d.tap("N1", "V", dt=0.3)
    d.at(1.5)                                            # past the idle window
    assert d.g.nodes["N1"].register == {"V"}
    d.tap("N1", "L")
    assert row(d) == ["N2", "N1"]


def test_lone_R_clears_at_idle_window():
    d = drv()
    d.tap("N1", "R").at(0.45)
    assert d.g.nodes["N1"].register == set()


def test_start_while_sequence_open_does_nothing():
    d = drv()
    start(d, "N2")
    d.tap("N1", "V", dt=0.2).flip("N1", True, dt=0.3)
    assert actions(d) == ["start"] and not d.g.in_sequence("N1")


def test_duplicate_flips_are_idempotent():
    d = drv()
    d.flip("N2", True).flip("N2", True).flip("N2", True)
    assert actions(d) == ["discard"]


# ---------------------------------------------------------------- stash

def test_single_node_stash_flow_stash_joins_right_end():
    d = drv(nodes=(1,))
    pl = d.g.players[0]
    first = pl.slots[0]
    d.tap("N1", "L").flip("N1", True, dt=0.3)            # stash in
    assert pl.stash is first
    d.flip("N1", False, dt=0.3)
    start(d, "N1")
    d.tap("N1", "V", dt=0.3).at(0.45)                    # V on the face-down head: use stash
    assert actions(d)[-1] == "use_stash"
    assert row(d) == ["N1", "S"]
    assert seqs(d, "N1") == ["H1"]


def test_discard_stash_is_now_R_plus_L():
    d = drv(nodes=(1,))
    pl = d.g.players[0]
    d.tap("N1", "L").flip("N1", True, dt=0.3).flip("N1", False, dt=0.3)
    stash = pl.stash
    d.tap("N1", "L", dt=0.3).tap("N1", "V", dt=0.05).at(0.45)
    assert pl.stash is stash                             # L+V no longer discards the stash
    d.tap("N1", "R", dt=2.5).tap("N1", "L", dt=0.05).at(0.45)
    assert actions(d)[-1] == "discard_stash"
    assert pl.stash is None and stash in d.g.deck.discard_pile


def test_stash_disabled_for_multi_node_by_default():
    d = drv(nodes=(2,))
    d.tap("N1", "L").flip("N1", True, dt=0.3)
    assert actions(d) == []


# ================================================================ manual mode

def mdrv(nodes=(3,), **kw):
    return drv(nodes=nodes, scheme="manual", **kw)


L_, R_, V_, F_ = 0x1, 0x2, 0x4, 0x8


def press(d, node, mask, hold=0.05):
    d.btn(node, mask, dt=0.01).at(hold).btn(node, 0, dt=0.0)
    d.at(0.15)
    return d


def long_press(d, node, mask=F_):
    d.btn(node, mask, dt=0.01).at(1.1).btn(node, 0, dt=0.0)
    d.at(0.15)
    return d


def mstart(d, node):
    press(d, node, V_)
    press(d, node, F_)


def test_manual_ignores_taps_and_flips():
    d = mdrv()
    d.tap("N1", "V").flip("N1", True)
    assert actions(d) == [] and d.g.nodes["N1"].register == set()


def test_manual_start_add_left_right_and_push():
    d = mdrv()
    pl = d.g.players[0]
    mstart(d, "N2")
    assert d.g.is_head("N2") and d.g.node_display("N2")["REG"] == "0001"
    press(d, "N3", V_)
    press(d, "N3", R_)                                   # R button = right end
    press(d, "N1", V_)
    press(d, "N1", L_)                                   # L button = left end
    assert row(d) == ["N1", "N2", "N3"]
    assert actions(d) == ["start", "add_right", "add_left"]
    press(d, "N2", F_)                                   # short F on the head: push
    assert actions(d)[-1] == "push"
    assert pl.sequence is None
    assert not any(d.g.nodes[n].face_down for n in ("N1", "N2", "N3"))
    assert d.g.node_display("N2")["DOFF"] == "0"


def test_manual_quick_tap_released_inside_two_button_window_still_works():
    d = mdrv()
    d.btn("N2", F_, dt=0.01).btn("N2", 0, dt=0.03).at(0.15)
    assert actions(d) == ["discard"]


def test_manual_lockout_stops_push_then_discard():
    d = mdrv()
    mstart(d, "N2")
    press(d, "N2", F_)                                   # push
    press(d, "N2", F_, hold=0.05)                        # accidental second press, within 600 ms
    assert actions(d) == ["start", "push"]
    d.at(0.7)
    press(d, "N2", F_)                                   # after the lockout: a normal discard
    assert actions(d)[-1] == "discard"


def test_manual_lockout_after_discard():
    d = mdrv()
    press(d, "N2", F_)
    press(d, "N2", F_)
    assert actions(d) == ["discard"]


def test_manual_long_press_member_removes_short_does_nothing():
    d = mdrv()
    mstart(d, "N2")
    press(d, "N3", V_)
    press(d, "N3", R_)
    press(d, "N3", F_)                                   # short F on a member: nothing
    assert row(d) == ["N2", "N3"]
    long_press(d, "N3")
    assert actions(d)[-1] == "remove"
    assert row(d) == ["N2"]


def test_manual_long_press_head_cancels():
    d = mdrv()
    mstart(d, "N2")
    long_press(d, "N2")
    assert actions(d)[-1] == "cancel"
    assert d.g.players[0].sequence is None and not d.g.nodes["N2"].face_down


def test_manual_two_buttons_on_head_cancel():
    d = mdrv()
    mstart(d, "N2")
    press(d, "N2", L_ | R_)
    assert actions(d)[-1] == "cancel"


def test_manual_two_buttons_clear_register():
    d = mdrv()
    press(d, "N1", V_)
    d.btn("N1", L_).btn("N1", L_ | R_, dt=0.05).btn("N1", 0, dt=0.05).at(0.2)
    assert d.g.nodes["N1"].register == set()


def test_manual_discard_stash_with_R_and_L():
    d = mdrv(nodes=(1,))
    pl = d.g.players[0]
    press(d, "N1", L_)
    press(d, "N1", F_)
    assert pl.stash is not None
    d.at(0.7)
    press(d, "N1", R_)
    press(d, "N1", L_)
    d.at(0.5)
    assert actions(d)[-1] == "discard_stash"


def test_mixed_schemes_same_player():
    d = drv(nodes=(2,), scheme=["gesture", "manual"])
    start(d, "N1")
    press(d, "N2", V_)
    press(d, "N2", L_)
    assert row(d) == ["N2", "N1"]


# ================================================================ robustness

def test_events_from_unassigned_nodes_are_ignored():
    d = drv()
    d.tap("ZZ", "V").flip("ZZ", True)
    assert actions(d) == []


def test_losing_a_sequence_node_cancels_the_sequence():
    d = drv()
    pl = d.g.players[0]
    start(d, "N3")
    d.tap("N1", "V", dt=0.3).tap("N1", "L", dt=0.1)
    held = pl.slots[0]
    d.ev("N1", "DISCONNECTED", "")
    assert pl.sequence is None
    assert pl.slots[0] is held


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
    d.tap("N1", "V")
    d.tap("N1", "R").at(0.1)
    assert d.g.node_display("N1")["REG"] == "1010"


def test_control_card_code_on_the_node():
    from ccmaze.sim.cards import ControlType
    d = drv()
    pl = d.g.players[0]
    pl.slots[0] = Card(ControlType("IF", "O", CardType("LLU", "L")))
    pl.slots[1] = Card(ControlType("FOR"))
    assert d.g.node_display("N1")["CARD"] == "IF:O:LLU"
    assert d.g.node_display("N2")["CARD"] == "FOR"


# ================================================================ v1.5 firmware items

def test_fs_is_a_silent_face_state_sync():
    d = drv()
    d.tap("N1", "V")
    d.ev("N1", "FS", "1")
    n = d.g.nodes["N1"]
    assert n.face_down and n.register == set() and actions(d) == []   # no discard, no start
    d.ev("N1", "FS", "0")
    assert not n.face_down and actions(d) == []


def test_fs_on_a_member_does_not_count_as_half_a_remove():
    d = drv()
    start(d, "N2")
    d.tap("N3", "V", dt=0.3).tap("N3", "L", dt=0.1)
    d.flip("N3", True, dt=0.3)
    d.ev("N3", "FS", "0")                              # orientation re-synced
    d.flip("N3", True, dt=0.3).flip("N3", False, dt=0.3)
    assert row(d) == ["N2"]                            # a fresh, real double flip still removes
    assert actions(d).count("remove") == 1


def test_manual_three_button_chord_is_clear_only():
    d = mdrv()
    press(d, "N1", V_)
    d.btn("N1", L_).at(0.2)                            # L alone resolves as a primitive...
    d.btn("N1", L_ | R_).at(0.2)                       # ...a second key: 2-button clear
    d.btn("N1", L_ | R_ | V_).at(0.2)                  # third key added late: still a clear, not V
    d.btn("N1", 0).at(0.2)
    assert d.g.nodes["N1"].register == set()
    assert actions(d) == []


def test_manual_debug_chord_never_acts_as_f():
    d = mdrv()
    d.btn("N2", 0x1).btn("N2", 0x3, dt=0.02).btn("N2", 0x7, dt=0.02).btn("N2", 0xF, dt=0.02)
    d.at(2.1)
    d.btn("N2", 0x0).at(0.2)
    assert actions(d) == []                            # no discard from the BR (F) key in the chord


def test_parse_fs():
    from ccmaze.nodes.protocol import parse_line
    assert parse_line("FS:1") == ("FS", "1") and parse_line("FS:2") is None
