from conftest import card, make_game, open_maze

from ccmaze.config import Config
from ccmaze.sim.execution import QueuedSequence
from ccmaze.sim.game import ENDED


def push_cards(g, player, steps_list, t):
    g.exec_mode.push(g, g.players[player].avatar, QueuedSequence([card(s) for s in steps_list], t), t)


def run(g, until, dt=0.05):
    t = 0.0
    while t <= until + 1e-9:
        g.update(t)
        t += dt


def test_segment_skip_on_wall():
    # wall between (1,0) and (2,0)
    g, _ = make_game((1,), maze=open_maze(5, closed=[((1, 0), "R")]))
    av = g.players[0].avatar
    assert av.pos == (0, 0)
    push_cards(g, 0, ["RRD"], 0.0)
    g.update(0.0)
    assert av.pos == (1, 0)          # first R
    g.update(0.99)
    assert av.pos == (1, 0)
    g.update(1.0)
    assert av.pos == (1, 1)          # second R blocked & skipped, then D
    assert av.skipped_steps == {1} and av.executed_steps == {0, 2}
    assert av.blocked_at == (1.0, "R")


def test_blocked_step_never_cancels_card_or_sequence():
    g, _ = make_game((1,), maze=open_maze(5))
    av = g.players[0].avatar
    push_cards(g, 0, ["UUU", "D"], 0.0)   # all UUU steps hit the top border
    g.update(0.0)
    assert av.pos == (0, 1)               # UUU skipped instantly, D executed
    assert av.move_idx == 1 and av.current_move.type.steps == "D"


def test_border_counts_as_wall_and_skips_segment():
    g, _ = make_game((1,), maze=open_maze(5))
    av = g.players[0].avatar
    push_cards(g, 0, ["LLD"], 0.0)
    g.update(0.0)
    assert av.pos == (0, 1)
    assert av.skipped_steps == {0, 1}


def test_default_timing_one_second_per_step():
    g, _ = make_game((1,), maze=open_maze(5))
    av = g.players[0].avatar
    push_cards(g, 0, ["DDD"], 0.0)
    g.update(0.0)
    assert av.pos == (0, 1)
    g.update(1.5)
    assert av.pos == (0, 2)
    g.update(2.0)
    assert av.pos == (0, 3)
    g.update(2.9)
    assert av.current is not None      # card stays "current" through its trailing pause
    g.update(3.0)
    assert av.current is None and not av.busy


def test_tile_move_time_and_pauses():
    cfg = Config({"tile_move_time_s": 0.5, "pause_after_step_s": 1.0, "pause_between_cards_s": 2.0})
    g, _ = make_game((1,), cfg=cfg, maze=open_maze(5))
    av = g.players[0].avatar
    push_cards(g, 0, ["D", "D"], 0.0)
    g.update(0.0)
    assert av.pos == (0, 0) and av.move_to == (0, 1)
    g.update(0.5)
    assert av.pos == (0, 1)
    # card 1 ends at 1.5, then a 2 s pause between cards, 2nd step starts at 3.5, arrives 4.0
    g.update(3.4)
    assert av.move_to == (0, 1)
    g.update(3.5)
    assert av.move_to == (0, 2) and av.pos == (0, 1)
    g.update(4.0)
    assert av.pos == (0, 2)


def test_queue_is_fifo_and_never_interrupts():
    g, _ = make_game((1,), maze=open_maze(5))
    av = g.players[0].avatar
    push_cards(g, 0, ["RR"], 0.0)
    g.update(0.0)
    push_cards(g, 0, ["D"], 0.5)
    g.update(0.5)
    assert av.pos == (1, 0)
    g.update(1.0)
    assert av.pos == (2, 0)
    g.update(2.0)
    assert av.pos == (2, 1)


def test_pause_between_sequences():
    cfg = Config({"pause_between_sequences_s": 3.0})
    g, _ = make_game((1,), cfg=cfg, maze=open_maze(5))
    av = g.players[0].avatar
    push_cards(g, 0, ["R"], 0.0)
    push_cards(g, 0, ["D"], 0.0)
    g.update(0.0)
    assert av.pos == (1, 0)
    g.update(3.9)
    assert av.pos == (1, 0)
    g.update(4.0)          # first seq done at 1.0, +3 s
    assert av.pos == (1, 1)


def test_executed_cards_go_to_discard():
    g, _ = make_game((1,), maze=open_maze(5))
    n_discard = len(g.deck.discard_pile)
    push_cards(g, 0, ["R", "D"], 0.0)
    run(g, 2.5)
    assert len(g.deck.discard_pile) == n_discard + 2


def test_win_on_entering_centre_mid_card():
    g, _ = make_game((1,), maze=open_maze(5))
    av = g.players[0].avatar
    av.pos = (2, 0)
    push_cards(g, 0, ["DDD"], 0.0)
    run(g, 5)
    assert g.state == ENDED and g.winners == [0]
    assert g.win_time == 1.0
    assert av.pos == (2, 2)


def test_tie_break_earlier_step_time_independent_of_ticks():
    g, _ = make_game((1, 1), maze=open_maze(5))
    push_cards(g, 0, ["RRDD"], 0.0)
    g.players[1].avatar.pos = (4, 0)
    push_cards(g, 1, ["DDLL"], 0.2)   # same path length, pushed 0.2 s later
    g.update(10.0)                     # one huge tick: ordering must still be exact
    assert g.state == ENDED and g.winners == [0] and g.win_time == 3.0


def test_exact_tie_is_a_draw():
    g, _ = make_game((1, 1), maze=open_maze(5))
    g.players[1].avatar.pos = (4, 0)
    g.players[1].avatar.start = (4, 0)
    push_cards(g, 0, ["RRDD"], 0.0)
    push_cards(g, 1, ["LLDD"], 0.0)
    g.update(10.0)
    assert g.winners == [0, 1]
    assert g.message == "P1&P2 WIN"


def test_pushes_after_game_end_are_ignored():
    g, _ = make_game((2,), maze=open_maze(5))
    g.players[0].avatar.pos = (2, 1)
    push_cards(g, 0, ["D"], 0.0)
    g.update(0.0)
    assert g.state == ENDED
    assert not g.start_sequence("N1", 1.0)
    assert not g.push(0, 1.0)
    assert not g.discard("N1", 1.0)


def test_avatars_pass_through_each_other():
    g, _ = make_game((1, 1), maze=open_maze(5))
    a, b = g.players[0].avatar, g.players[1].avatar
    b.pos = (1, 0)
    push_cards(g, 0, ["R"], 0.0)
    g.update(0.0)
    assert a.pos == b.pos == (1, 0)


# ---------------------------------------------------------------- v1.4: resolved moves, discards

def test_for_sequence_executes_repeats_and_discards_by_ownership():
    from ccmaze.sim.cards import Card, CardType, ControlType
    g, _ = make_game((1,), maze=open_maze(7))
    av = g.players[0].avatar
    cards = [Card(ControlType("FOR")), Card(CardType("D", "D")), Card(CardType("R", "D"))]
    n0 = len(g.deck.discard_pile)
    g.exec_mode.push(g, av, QueuedSequence(cards, 0.0), 0.0)
    g.update(0.0)
    assert av.pos == (0, 1)
    assert cards[2] in g.deck.discard_pile            # consumed card: discarded at start
    assert cards[1] not in g.deck.discard_pile
    g.update(1.0)
    assert av.pos == (0, 2)
    g.update(2.0)                                     # last repeat done -> C1 and FOR discarded
    assert cards[0] in g.deck.discard_pile and cards[1] in g.deck.discard_pile
    assert len(g.deck.discard_pile) == n0 + 3


def test_if_bonus_move_runs_before_matching_card():
    from ccmaze.sim.cards import Card, CardType, ControlType
    g, _ = make_game((1,), maze=open_maze(7))
    av = g.players[0].avatar
    cards = [Card(ControlType("IF", "D", CardType("RR", "I3"))), Card(CardType("D", "D"))]
    g.exec_mode.push(g, av, QueuedSequence(cards, 0.0), 0.0)
    run(g, 5)
    assert av.trail == [((0, 0), (1, 0)), ((1, 0), (2, 0)), ((2, 0), (2, 1))]
