"""Section 18.1-18.4: route resolution (FOR ... FOR blocks, IF transform), completion, preview."""
import pytest
from conftest import make_game, open_maze
from routeutil import cards, mk, push_chunk, steps_of

from ccmaze.config import Config
from ccmaze.sim.preview import plan_route, trace
from ccmaze.sim.resolve import BLANK, FOR_OPEN_PENDING, IF_PENDING, IF_SHAPE, resolve

IFO = "IF:O:LLU"       # IF next is a square (O) -> becomes the L card LLU
IFD = "IF:D:UU"        # IF next is a domino (D) -> becomes the I3 card UU


# ------------------------------------------------------------------ the 18.3 table, row by row
TABLE = [
    (["FOR", "L:UUR", "O:RDL", "S:RUR", "FOR"], ["UUR", "RDL", "RUR"] * 3),
    (["FOR", "D:U", "FOR"], ["U"]),
    (["FOR", "FOR"], []),
    ([IFO, "O:RDL"], ["LLU", "RDL"]),
    ([IFO, "S:RUR"], ["RUR"]),
    # spec rows 6 and 7 say x3; 2 cards between the FORs -> x2 (18.2, spec HUB-Q40)
    (["FOR", IFO, "O:RDL", "FOR"], ["LLU", "RDL"] * 2),
    (["FOR", IFO, "S:RUR", "FOR"], ["RUR"] * 2),
    ([IFO, "FOR", "O:RDL", "FOR"], ["RDL"]),
    (["FOR", "D:U", IFO, "FOR"], ["U", "U"]),
]


@pytest.mark.parametrize("written,expected", TABLE)
def test_section_3_table(written, expected):
    res = resolve(cards(*written))
    assert steps_of(res.moves) == expected
    assert res.settled


def test_table_if_waits_for_the_next_chunk():
    chunk1 = cards("D:R", IFD)
    r1 = resolve(chunk1)
    assert steps_of(r1.moves) == ["R"] and r1.state[1] == IF_PENDING and not r1.settled
    r2 = resolve(chunk1 + cards("D:L", "D:D"))
    assert steps_of(r2.moves) == ["R", "UU", "L", "D"]
    assert r2.state[1] == IF_SHAPE


def test_table_for_block_spans_chunks():
    chunk1 = cards("FOR", "D:R")
    r1 = resolve(chunk1)
    assert r1.moves == [] and r1.state[0] == FOR_OPEN_PENDING
    assert steps_of(r1.tentative) == ["R"] and r1.tentative[0].repeat_n == 0      # drawn once, "x?"
    r2 = resolve(chunk1 + cards("D:U", "FOR"))
    assert steps_of(r2.moves) == ["R", "U"] * 2


def test_blocks_dont_nest_and_moves_keep_provenance():
    res = resolve(cards("FOR", "D:U", "FOR", "D:L", "FOR", "D:R", "D:D", "FOR"))
    assert steps_of(res.moves) == ["U", "L", "R", "D", "R", "D"]
    assert [(b.open, b.close, b.n) for b in res.blocks] == [(0, 2, 1), (4, 7, 2)]
    m = res.moves[2]
    assert (m.source, m.repeat_i, m.repeat_n, m.via) == (5, 1, 2, 4)


def test_if_any_orientation_and_control_cards_never_fit():
    assert steps_of(resolve(cards(IFO, "O:DRU")).moves) == ["LLU", "DRU"]
    res = resolve(cards(IFO, IFO, "O:RDL"))
    assert res.state[:2] == [BLANK, IF_SHAPE] and steps_of(res.moves) == ["LLU", "RDL"]


def test_locked_moves_are_a_prefix_of_locked_plus_pending():
    locked = cards("D:R", "FOR", "D:U", IFO)
    for extra in (["O:RDL"], ["FOR"], ["S:RUR", "FOR", "D:D"], ["D:L"]):
        a, b = resolve(locked), resolve(locked + cards(*extra))
        assert b.moves[:len(a.moves)] == a.moves


# ------------------------------------------------------------------ completion (maze 5: centre (2, 2))
def test_completion_mid_chunk_ignores_the_rest():
    g, _ = make_game((1,), maze=open_maze(5))
    pl = g.players[0]
    push_chunk(g, 0, ["I3:RR"], 0.0)
    assert not pl.walking and pl.avatar.pos == (0, 0)           # waits at spawn while building
    push_chunk(g, 0, ["I3:DD", "D:R", "D:R"], 1.0)
    plan = pl.locked_plan.locked
    assert plan.reached and plan.end == (2, 2)
    assert plan.length == 4 and plan.reach_move == 1              # the two R cards are ignored
    assert pl.walking and pl.complete_at == 1.0


def test_completion_waits_for_an_open_for_block():
    g, _ = make_game((1,), maze=open_maze(5))
    pl = g.players[0]
    push_chunk(g, 0, ["I3:RR"], 0.0)
    push_chunk(g, 0, ["FOR", "D:D", "D:D"], 1.0)                  # DD would reach (2, 2) ...
    assert not pl.walking                                         # ... but the block is open
    rp = pl.locked_plan
    assert rp.tentative is not None and rp.tentative.end == (2, 2) and not rp.complete
    push_chunk(g, 0, ["FOR"], 2.0)                                # closes: D D D D -> centre
    assert pl.walking and pl.locked_plan.locked.length == 4


def test_completion_waits_for_a_pending_if():
    g, _ = make_game((1,), maze=open_maze(5))
    pl = g.players[0]
    push_chunk(g, 0, ["I3:RR", "D:D", IFD], 0.0)                  # at (2, 1), IF pending
    assert not pl.walking and pl.locked_plan.res_locked.pending_if == 2
    push_chunk(g, 0, ["S:RUR"], 1.0)                              # the IF sees S -> blank
    assert not pl.walking
    assert steps_of(pl.locked_plan.locked.move_list) == ["RR", "D", "RUR"]


def test_completion_after_if_resolves_into_the_centre():
    g, _ = make_game((1,), maze=open_maze(5))
    pl = g.players[0]
    push_chunk(g, 0, ["I3:RR", "IF:D:DD"], 0.0)                    # IF next is a domino -> DD
    assert not pl.walking
    push_chunk(g, 0, ["D:L"], 1.0)                                # RR, DD -> (2, 2)
    assert pl.walking and pl.locked_plan.locked.end == (2, 2)


def test_walls_follow_the_segment_rule_on_the_trace():
    maze = open_maze(5, closed=[((1, 0), "R")])
    plan = trace(maze, (0, 0), resolve(cards("L:RRD")).moves)
    assert plan.end == (1, 1) and plan.moves[0].blocked == [((1, 0), "R")]


def test_preview_continues_from_the_locked_end():
    g, _ = make_game((1,), maze=open_maze(5))
    pl = g.players[0]
    push_chunk(g, 0, ["I3:RR"], 0.0)
    rp = plan_route(g.maze, (0, 0), g.maze.centre, pl.route_cards, cards("D:D", "D:D"))
    assert rp.pending.start == (2, 0) and rp.preview_complete and not rp.complete


def test_discard_at_push():
    g, _ = make_game((1,), maze=open_maze(5))
    n0 = len(g.deck.discard_pile)
    cs = push_chunk(g, 0, ["D:R", "FOR", "D:D"], 0.0)
    assert all(c in g.deck.discard_pile for c in cs) and len(g.deck.discard_pile) == n0 + 3
