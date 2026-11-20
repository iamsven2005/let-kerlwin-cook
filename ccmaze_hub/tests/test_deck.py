import random
from collections import Counter

from conftest import make_game

from ccmaze.config import Config
from ccmaze.sim.cards import (DEFAULT_FAMILIES, DOMINOES, Card, CardType, Catalog, catalog_from_dict,
                              default_catalog_dict, load_catalog, segments_of, shape_family)
from ccmaze.sim.deck import Deck, build_cards, plan_deck, weighted_round_robin


# ---------------------------------------------------------------- catalog (14 B1)

def test_default_catalog_has_52_cards_in_7_families():
    cat = load_catalog(None)
    assert len(cat) == 52
    assert len({c.steps for c in cat}) == 52
    assert Counter(c.family for c in cat) == {"D": 4, "I3": 4, "I4": 4, "V": 8, "O": 8, "L": 16, "S": 8}
    assert all(len(c.steps) <= 4 for c in cat)
    assert cat.if_enabled and cat.for_enabled


def test_every_card_traces_its_family_shape():
    for fam, cards in DEFAULT_FAMILIES.items():
        for s in cards:
            assert shape_family(s) == fam, (fam, s)
    assert shape_family("RDL") == "O" and shape_family("UUR") == "L" and shape_family("RUR") == "S"
    assert shape_family("UUUR") is None    # 4 steps, no family


def test_segments():
    assert segments_of("UUR") == [("U", 2), ("R", 1)]
    assert segments_of("RDL") == [("R", 1), ("D", 1), ("L", 1)]


def test_catalog_enable_and_weights():
    d = default_catalog_dict()
    d["families"]["I4"]["enabled"] = False
    d["families"]["V"]["cards"]["UR"]["enabled"] = False
    d["families"]["L"]["weight"] = 2.0
    d["families"]["L"]["cards"]["UUR"]["weight"] = 3.0
    d["control_cards"]["FOR"]["enabled"] = False
    cat = catalog_from_dict(d)
    by = {c.steps: c for c in cat}
    assert "UUU" not in by and "UR" not in by
    assert by["UUR"].weight == 6.0 and by["UUL"].weight == 2.0
    assert cat.if_enabled and not cat.for_enabled


def test_dominoes_cannot_be_disabled():
    d = default_catalog_dict()
    d["families"]["D"]["enabled"] = False
    w = []
    cat = catalog_from_dict(d, w)
    assert {c.steps for c in cat} >= set(DOMINOES)
    assert len([x for x in w if "domino" in x]) == 4
    # missing entirely -> added back
    d = {"families": {"L": {"cards": {"UUR": {}}}}}
    w = []
    cat = catalog_from_dict(d, w)
    assert {c.steps for c in cat} == {"UUR", *DOMINOES}
    assert all(c.family == "D" for c in cat if c.steps in DOMINOES)


def test_invalid_cards_are_rejected_not_fatal():
    d = default_catalog_dict()
    d["families"]["X"] = {"enabled": True, "weight": 1.0, "cards": {
        "UD": {}, "LRL": {}, "UUUUR": {}, "UX": {}, "URUR": {},
    }}
    warnings = []
    cat = {c.steps for c in catalog_from_dict(d, warnings)}
    assert "URUR" in cat and not cat & {"UD", "LRL", "UUUUR", "UX"}
    assert len([w for w in warnings if "rejected" in w]) == 4


def test_family_mismatch_is_warned():
    d = {"families": {"O": {"cards": {"UUR": {}}}}}
    w = []
    catalog_from_dict(d, w)
    assert any("traces a L shape" in x for x in w)


# ---------------------------------------------------------------- deck size (14 A3, B2)

def counts(cards):
    return Counter(c.code if c.is_move else c.kind for c in cards)


def test_single_node_player_gets_every_card_plus_domino_guarantee():
    cfg = Config({})
    cat = load_catalog(None)
    plan = plan_deck(cat, 1, cfg)
    # max(2 x 1, 1 x 52) = 52, but dominoes need 2 each: 52 + 4 = 56 movement cards
    assert plan.movement_target == 52 and plan.movement == 56
    # 0.15 x 56 / 2 = 4.2 -> 4 IF and 4 FOR
    assert (plan.n_if, plan.n_for) == (4, 4) and plan.total == 64
    cards = build_cards(cat, 1, cfg, random.Random(1))
    c = counts(cards)
    assert len(cards) == 64
    assert all(c[t.steps] >= 1 for t in cat)
    assert all(c[d] >= 2 for d in DOMINOES)
    assert c["IF"] == 4 and c["FOR"] == 4


def test_big_group_uses_the_multiplier():
    cfg = Config({"deck_multiplier": 12})
    plan = plan_deck(load_catalog(None), 6, cfg)    # 72 > 52
    assert plan.movement == 72


def test_min_copies_and_domino_config():
    cfg = Config({"min_copies_per_type": 2, "min_domino_copies": 5, "control_card_share": 0.0})
    cards = build_cards(load_catalog(None), 1, cfg, random.Random(2))
    c = counts(cards)
    assert all(c[d] == 5 for d in DOMINOES)
    assert all(v >= 2 for v in c.values())
    assert "IF" not in c and "FOR" not in c
    assert len(cards) == 2 * 52 + 4 * 3


def test_control_minimum_one_each_and_disable():
    cfg = Config({"min_copies_per_type": 0, "deck_multiplier": 2, "control_card_share": 0.15})
    cat = Catalog([CardType(s, "D") for s in DOMINOES])
    p = plan_deck(cat, 1, cfg)            # movement 8 (domino minimum) -> 0.6 each -> min 1
    assert (p.n_if, p.n_for) == (1, 1)
    cat.for_enabled = False
    p = plan_deck(cat, 1, cfg)
    assert (p.n_if, p.n_for) == (1, 0)


def test_if_cards_get_condition_and_action_from_catalog():
    cards = build_cards(load_catalog(None), 2, Config({}), random.Random(5))
    ifs = [c for c in cards if c.kind == "IF"]
    fams = {t.family for t in load_catalog(None)}
    assert ifs and all(c.type.cond in fams and c.type.action.steps for c in ifs)
    assert all(len(f"CARD:{c.code}\n") <= 20 for c in cards)
    assert {c.code for c in cards if c.kind == "FOR"} == {"FOR"}


def test_game_deck_matches_plan_and_conserves_cards():
    g, _ = make_game((2, 1))
    assert g.deck.size == plan_deck(g.catalog, 3, g.cfg).total
    assert g.total_cards() == g.deck.size


def test_round_robin_counts_are_even():
    cat = list(load_catalog(None))
    picks = weighted_round_robin(cat, 104, random.Random(3))
    assert set(Counter(p.steps for p in picks).values()) == {2}


def test_weights_are_respected():
    cat = [CardType("U", "D", 3.0), CardType("D", "D", 1.0)]
    counts_ = Counter(p.steps for p in weighted_round_robin(cat, 40, random.Random(0)))
    assert counts_ == {"U": 30, "D": 10}


def test_reshuffle_and_empty():
    d = Deck([Card(CardType(s, "D")) for s in "UDL"], random.Random(0))
    cards = [d.draw() for _ in range(3)]
    assert d.draw() is None
    d.discard(cards[0])
    d.discard(cards[1])
    got = {d.draw().uid, d.draw().uid}
    assert got == {cards[0].uid, cards[1].uid}
    assert d.reshuffles == 1
    assert d.draw() is None


def test_pushed_cards_are_discarded_at_once_so_the_hand_refills():
    from ccmaze.sim.game import Game, NodeSetup
    from conftest import open_maze
    cfg = Config({})
    deck = Deck([Card(CardType(s, "D")) for s in "URD"], random.Random(0))   # 3 cards, 3 slots
    g = Game(cfg, load_catalog(None), [NodeSetup(f"N{i}", 0, i) for i in (1, 2, 3)], maze=open_maze(5),
             deck=deck, rng_seed=1)
    pl = g.players[0]
    assert g.deck.draw_pile == [] and all(pl.slots)
    pushed = pl.slots[2]
    g.start_sequence("N3", 0.0)
    g.push(0, 0.0)
    # 18.1.8: the pushed card goes to the discard pile at push time, which is reshuffled at once
    assert pl.slots[0] is pushed and g.deck.reshuffles == 1
    assert g.node_display("N1")["CARD"] == pushed.code
