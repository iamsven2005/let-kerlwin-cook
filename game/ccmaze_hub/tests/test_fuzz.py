"""Random event storms: no exceptions, cards are conserved, sequences stay consistent."""
import random

import pytest
from conftest import make_game

from ccmaze.sim.events import NodeEvent
from ccmaze.sim.maze import generate_maze


@pytest.mark.parametrize("seed", range(12))
def test_random_event_storm(seed):
    rng = random.Random(seed)
    layout = rng.choice([(1,), (3,), (2, 2), (1, 1, 1, 1), (3, 1, 2), (2, 1, 1, 2)])
    schemes = [rng.choice(["gesture", "manual"]) for _ in range(sum(layout))]
    g, c = make_game(layout, maze=generate_maze(9, 0.3, seed), scheme=schemes,
                     pause_after_step_s=0.05, deal_interval_s=rng.choice([0.0, 0.2]),
                     deck_multiplier=rng.choice([1, 2, 3]))
    total = g.total_cards()
    ids = list(g.nodes)
    t = 0.0
    face = {n: False for n in ids}
    for _ in range(3000):
        t += rng.choice([0.005, 0.02, 0.05, 0.1, 0.3, 0.5])
        n = rng.choice(ids)
        r = rng.random()
        if r < 0.35:
            ev = NodeEvent(n, "T", rng.choice("RLV"), t)
        elif r < 0.6:
            face[n] = not face[n]
            ev = NodeEvent(n, "F", "1" if face[n] else "0", t)
        elif r < 0.85:
            ev = NodeEvent(n, "B", format(rng.choice([0, 1, 2, 4, 8, 3, 0, 0]), "X"), t)
        elif r < 0.9:
            ev = NodeEvent(n, "DISCONNECTED", "", t)
        else:
            ev = NodeEvent(n, "CONNECTED", "", t)
        c.handle(ev)
        c.tick(t)
        g.update(t)
        assert g.total_cards() == total
        for pl in g.players:
            seen = set()
            for card in pl.slots + [pl.stash] + pl.avatar.all_cards():
                if card is not None:
                    assert card.uid not in seen
                    seen.add(card.uid)
            if pl.sequence:
                head = pl.sequence.entries[0]
                assert head.node_id == pl.sequence.head
                for e in pl.sequence.entries:
                    if e.node_id is None:
                        assert e.card is pl.stash
                    else:
                        assert g.card_at(e.node_id) is e.card
                        assert g.nodes[e.node_id].connected
        for nid in ids:
            d = g.node_display(nid)
            assert len(d["REG"]) == 4 and d["CARD"]
        if g.state != "playing":
            break
