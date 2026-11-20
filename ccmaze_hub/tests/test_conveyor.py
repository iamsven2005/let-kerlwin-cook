from conftest import card, make_game

from ccmaze.sim import hand


def ids(slots):
    return [s.steps if s else None for s in slots]


def test_settle_gravity_keeps_order():
    a, b = card("U"), card("D")
    out = hand.settle([a, None, b, None], [False] * 4)
    assert out == [None, None, a, b]


def test_remove_slides_cards_above_down():
    a, b, c, d = (card(s) for s in ("U", "D", "L", "R"))
    taken, out = hand.remove([a, b, c, d], [False] * 4, 2)
    assert taken is c
    assert out == [None, a, b, d]


def test_deal_enters_top_and_falls():
    a = card("U")
    n = card("D")
    out = hand.deal_one([None, None, a], [False] * 3, n)
    assert out == [None, n, a]


def test_locked_slots_are_skipped():
    a, x, b, c = (card(s) for s in ("U", "UU", "L", "R"))
    locked = [False, True, False, False]
    taken, out = hand.remove([a, x, b, c], locked, 3)
    assert taken is c
    # a slides over the locked x into slot 3
    assert out == [None, x, a, b]


def test_deal_with_locked_top_goes_to_topmost_unlocked():
    x, a = card("U"), card("D")
    n = card("L")
    out = hand.deal_one([x, None, a], [True, False, False], n)
    assert out == [x, n, a]


def test_game_discard_slides_and_refills_top():
    g, c = make_game((4,))
    pl = g.players[0]
    before = list(pl.slots)
    assert all(before)
    assert g.discard("N3", 0.0)
    after = pl.slots
    assert after[1] is before[0] and after[2] is before[1] and after[3] is before[3]
    assert after[0] is not None and after[0] not in before


def test_game_locked_nodes_keep_cards_during_discard():
    g, c = make_game((4,))
    pl = g.players[0]
    before = list(pl.slots)
    assert g.start_sequence("N2", 0.0)
    assert g.discard("N4", 0.0)
    after = pl.slots
    assert after[1] is before[1]                 # locked head keeps its card
    assert after[2] is before[0]                 # N1's card jumped over the locked N2
    assert after[3] is before[2]
    assert after[0] not in before


def test_push_removes_cards_and_slides():
    g, c = make_game((4,))
    pl = g.players[0]
    before = list(pl.slots)
    g.start_sequence("N3", 0.0)
    g.add_to_sequence("N1", 0.0)
    assert g.push(0, 0.0)
    after = pl.slots
    assert after[3] is before[3]
    assert after[2] is before[1]
    assert all(after)
    assert before[2] not in after and before[0] not in after
    assert [c.uid for c in pl.route[0].cards] == [before[2].uid, before[0].uid]
    assert before[2] in g.deck.discard_pile or before[2] in pl.slots    # 18.1.8: discarded at push


def test_remove_from_sequence_keeps_card_in_hand():
    g, c = make_game((3,))
    pl = g.players[0]
    before = list(pl.slots)
    g.start_sequence("N1", 0.0)
    g.add_to_sequence("N2", 0.0)
    g.add_to_sequence("N3", 0.0)
    assert g.remove_from_sequence("N2", 0.0)
    assert pl.slots == before
    assert pl.sequence.position_of("N3") == 2
    assert not g.remove_from_sequence("N1", 0.0)   # head can't be removed this way


def test_card_conservation():
    g, c = make_game((2, 1))
    total = g.total_cards()
    g.discard("N1", 0)
    g.start_sequence("N2", 0)
    g.push(0, 0)
    g.stash_in("N3", 0)
    for t in range(1, 30):
        g.update(float(t))
        assert g.total_cards() == total


def test_deal_interval_refills_one_card_per_interval():
    g, c = make_game((3,), deal_interval_s=1.0)
    pl = g.players[0]
    assert all(pl.slots)               # initial deal is instant (spec HUB-Q15)
    g.start_sequence("N3", 0.0)
    g.add_to_sequence("N2", 0.0)
    g.push(0, 0.0)
    assert sum(1 for s in pl.slots if s) == 1
    g.update(0.5)
    assert sum(1 for s in pl.slots if s) == 1
    g.update(1.0)
    assert sum(1 for s in pl.slots if s) == 2
    g.update(1.5)
    assert sum(1 for s in pl.slots if s) == 2
    g.update(2.0)
    assert sum(1 for s in pl.slots if s) == 3
