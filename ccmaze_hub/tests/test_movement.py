"""The walk (18.1.5-18.1.6): timing, walls, arrival, first-to-arrive wins, ties."""
from conftest import make_game, open_maze
from routeutil import push_chunk

from ccmaze.config import Config
from ccmaze.sim.game import ENDED
from ccmaze.sim.preview import walk_time


def cfg(**kw):
    base = {"route_flash_s": 0.0}
    base.update(kw)
    return Config(base)


def test_avatar_waits_at_spawn_then_walks_after_the_flash():
    g, _ = make_game((1,), cfg=Config({"route_flash_s": 1.0}), maze=open_maze(5))
    av = g.players[0].avatar
    push_chunk(g, 0, ["I3:RR"], 0.0)
    g.update(5.0)
    assert av.pos == (0, 0) and not av.busy                       # building: stays at spawn
    push_chunk(g, 0, ["I3:DD"], 5.0)
    g.update(5.9)
    assert av.pos == (0, 0)                                       # flashing
    g.update(6.0)
    assert av.pos == (1, 0)                                       # walk starts 1 s after the push
    g.update(9.0)
    assert av.pos == (2, 2) and g.state == ENDED and g.win_time == 9.0


def test_walls_skip_the_rest_of_the_segment_on_the_walk():
    g, _ = make_game((1,), cfg=cfg(), maze=open_maze(5, closed=[((1, 0), "R")]))
    av = g.players[0].avatar
    push_chunk(g, 0, ["L:RRD", "D:R", "D:D"], 0.0)               # (1,0) blocked R, D, R, D -> (2,2)
    assert g.players[0].walking
    g.update(0.0)
    assert av.pos == (1, 0)
    g.update(1.0)
    assert av.pos == (1, 1) and av.skipped_steps == {1}
    assert av.blocked_at == (1.0, "R")
    g.update(10.0)
    assert av.pos == (2, 2) and g.winners == [0]


def test_tile_move_time_and_pauses_match_walk_time():
    c = cfg(tile_move_time_s=0.5, pause_after_step_s=1.0, pause_between_cards_s=2.0)
    g, _ = make_game((1,), cfg=c, maze=open_maze(5))
    pl = g.players[0]
    av = pl.avatar
    push_chunk(g, 0, ["I3:RR", "D:D", "D:D"], 0.0)
    g.update(0.0)
    assert av.pos == (0, 0) and av.move_to == (1, 0)
    g.update(0.5)
    assert av.pos == (1, 0)
    # RR: arrive 0.5, step 2 at 1.5 -> arrive 2.0, card done 3.0, +2 s -> D at 5.0 (arr 5.5),
    # card done 6.5, +2 s -> D at 8.5, arrives 9.0
    g.update(8.9)
    assert av.pos == (2, 1)
    g.update(9.0)
    assert av.pos == (2, 2) and g.win_time == 9.0
    assert walk_time(c, pl.locked_plan.locked) == 9.0


def test_first_to_arrive_wins_even_if_it_finished_building_later():
    g, _ = make_game((1, 1), cfg=Config({"route_flash_s": 1.0}), maze=open_maze(5))
    p0, p1 = g.players
    assert p1.avatar.start == (4, 4)
    # P1 completes first (t = 0) with a long route: 10 steps, arrives 1 + 9 = 10.0
    push_chunk(g, 0, ["I4:RRRR", "I3:DD", "I3:LL", "D:D", "D:D"], 0.0)
    assert p0.walking
    g.update(2.9)
    # P2 completes later (t = 3) with a 4-step route: arrives 4 + 3 = 7.0
    push_chunk(g, 1, ["I3:UU", "I3:LL"], 3.0)
    g.update(20.0)
    assert g.state == ENDED and g.winners == [1] and g.win_time == 7.0


def test_tie_on_arrival_goes_to_the_earlier_completing_push():
    g, _ = make_game((1, 1), cfg=cfg(), maze=open_maze(5))
    push_chunk(g, 0, ["I3:RR", "I3:DD"], 0.0)                     # arrives 0 + 3 = 3.0
    g.update(0.5)
    g.players[1].avatar.pos = g.players[1].avatar.start = (4, 3)
    g._replan(g.players[1])
    push_chunk(g, 1, ["D:U", "I3:LL"], 0.5)                       # 3 steps: 0.5 + 2 = 2.5
    g.update(10.0)
    assert g.winners == [1] and g.win_time == 2.5                  # earlier arrival beats earlier push
    g, _ = make_game((1, 1), cfg=cfg(), maze=open_maze(5))
    push_chunk(g, 0, ["I3:RR", "I3:DD"], 0.0)                     # 3.0
    push_chunk(g, 1, ["I3:UU", "D:L"], 1.0)                       # 3 steps: 1 + 2 = 3.0, later push
    g.update(10.0)
    assert g.win_time == 3.0 and g.winners == [0]


def test_exact_tie_is_a_draw():
    g, _ = make_game((1, 1), cfg=cfg(), maze=open_maze(5))
    push_chunk(g, 0, ["I3:RR", "I3:DD"], 0.0)
    push_chunk(g, 1, ["I3:LL", "I3:UU"], 0.0)
    g.update(10.0)
    assert g.winners == [0, 1] and g.message == "P1&P2 WIN"


def test_one_huge_tick_gives_the_same_result():
    for ticks in ([10.0], [x * 0.05 for x in range(201)]):
        g, _ = make_game((1, 1), cfg=Config({"route_flash_s": 1.0}), maze=open_maze(5))
        push_chunk(g, 0, ["I3:RR", "I3:DD"], 0.0)
        push_chunk(g, 1, ["I3:LL", "I3:UU"], 0.2)
        for t in ticks:
            g.update(t)
        assert g.winners == [0] and g.win_time == 4.0


def test_nothing_happens_after_the_game_ends():
    g, _ = make_game((2,), cfg=cfg(), maze=open_maze(5))
    push_chunk(g, 0, ["I3:RR", "I3:DD"], 0.0)
    g.update(5.0)
    assert g.state == ENDED
    assert not g.start_sequence("N1", 6.0)
    assert not g.push(0, 6.0)
    assert not g.discard("N1", 6.0)
