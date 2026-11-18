"""The path preview (14 B4) must match real execution exactly."""
import random

import pytest
from conftest import make_game

from ccmaze.sim.cards import Card, ControlType, load_catalog
from ccmaze.sim.execution import QueuedSequence
from ccmaze.sim.maze import generate_maze
from ccmaze.sim.preview import plan_cards, plan_player


def random_row(rng, cat, n):
    out = []
    types = list(cat)
    for _ in range(n):
        r = rng.random()
        if r < 0.15:
            out.append(Card(ControlType("FOR")))
        elif r < 0.3:
            out.append(Card(ControlType("IF", rng.choice(cat.families()), rng.choice(types))))
        else:
            out.append(Card(rng.choice(types)))
    return out


def run_until_idle(g, av, t=0.0):
    while True:
        g.update(t)
        if not av.busy:
            return t
        t += 0.25


@pytest.mark.parametrize("seed", range(25))
def test_preview_matches_execution(seed):
    rng = random.Random(seed)
    cat = load_catalog(None)
    maze = generate_maze(9, 0.3, seed)
    g, _ = make_game((1,), maze=maze, pause_after_step_s=0.25)
    av = g.players[0].avatar
    rows = [random_row(rng, cat, rng.randint(1, 5)) for _ in range(3)]
    # predicted: chain the plans
    pos, predicted = av.pos, []
    for r in rows:
        p = plan_cards(maze, pos, r)
        predicted += p.all_steps
        pos = p.end
    for i, r in enumerate(rows):
        g.exec_mode.push(g, av, QueuedSequence(r, 0.0), 0.0)
    run_until_idle(g, av)
    assert av.trail == predicted
    assert av.pos == pos


@pytest.mark.parametrize("move_time", [0.0, 0.3])
@pytest.mark.parametrize("seed", range(10))
def test_mid_execution_plan_predicts_the_rest(seed, move_time):
    rng = random.Random(100 + seed)
    cat = load_catalog(None)
    maze = generate_maze(9, 0.3, seed)
    g, _ = make_game((1,), maze=maze, pause_after_step_s=0.5, tile_move_time_s=move_time)
    av = g.players[0].avatar
    pl = g.players[0]
    for _ in range(2):
        g.exec_mode.push(g, av, QueuedSequence(random_row(rng, cat, 4), 0.0), 0.0)
    t = 0.0
    for _ in range(rng.randint(0, 9)):
        g.update(t)
        t += 0.35
    done = len(av.trail)
    plans = plan_player(g, pl)
    rest = (plans.current.all_steps if plans.current else []) + [s for p in plans.queued for s in p.all_steps]
    run_until_idle(g, av, t)
    assert av.trail[done:] == rest
    assert plans.projected_start == av.pos


def test_pending_sequence_starts_where_queue_ends():
    g, c = make_game((2,), maze=generate_maze(9, 0.2, 3))
    pl = g.players[0]
    av = pl.avatar
    g.exec_mode.push(g, av, QueuedSequence([Card(t) for t in load_catalog(None)][:2], 0.0), 0.0)
    g.start_sequence("N1", 0.0)
    plans = plan_player(g, pl)
    assert plans.pending is not None
    assert plans.pending.start == plans.projected_start == plans.queued[0].end


def test_blocked_segments_are_reported():
    from conftest import open_maze
    from ccmaze.sim.cards import CardType
    maze = open_maze(5, closed=[((1, 0), "R")])
    p = plan_cards(maze, (0, 0), [Card(CardType("RRD", "L"))])
    assert p.moves[0].steps == [((0, 0), (1, 0)), ((1, 0), (1, 1))]
    assert p.moves[0].blocked == [((1, 0), "R")]
