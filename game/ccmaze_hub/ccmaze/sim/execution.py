"""Avatar queues and step execution.

A pushed sequence is resolved once at push time (resolve.py: FOR, then IF) into a flat list of
movement Moves. The avatar executes those moves.

Steps run on an exact timeline (spec HUB-Q5): each event carries a scheduled time, and the game
processes events across all avatars in time order, independent of frame rate.

Timeline of one step: start at t -> arrive at t + tile_move_time_s -> next step due at
arrival + pause_after_step_s. A move finishes when the pause after its last step ends (spec
HUB-Q4), then pause_between_cards_s; a sequence finishes the same way, then
pause_between_sequences_s. A blocked step costs no time: the rest of that segment is skipped.

Discards (spec HUB-Q27): a physical card goes to the discard pile when the last move it owns has
finished; cards that own no move (FOR-consumed cards, ineffective IF/FOR) go when the sequence
starts executing.
"""
from __future__ import annotations

import itertools
from collections import deque
from dataclasses import dataclass, field
from typing import TYPE_CHECKING, Optional

from .cards import Card
from .resolve import Move, Resolution, next_action, resolve

if TYPE_CHECKING:  # pragma: no cover
    from .game import Game
    from .maze import Pos

_seq_ids = itertools.count(1)


@dataclass
class QueuedSequence:
    cards: list[Card]                 # the pushed row, left to right
    push_time: float
    resolution: Resolution = None     # type: ignore[assignment]
    seq_id: int = field(default_factory=lambda: next(_seq_ids))
    discarded: set = field(default_factory=set)   # card indices already in the discard pile
    started: bool = False

    def __post_init__(self):
        if self.resolution is None:
            self.resolution = resolve(self.cards)

    @property
    def moves(self) -> list[Move]:
        return self.resolution.moves

    def live_cards(self) -> list[Card]:
        return [c for i, c in enumerate(self.cards) if i not in self.discarded]


@dataclass
class Avatar:
    player: int
    pos: "Pos"
    start: "Pos"
    queue: deque = field(default_factory=deque)
    current: Optional[QueuedSequence] = None
    move_idx: int = 0
    seg_idx: int = 0
    seg_step: int = 0
    next_time: Optional[float] = None
    phase: str = "step"  # "step" | "arrive"
    idle_since: float = float("-inf")
    # for rendering
    move_from: Optional["Pos"] = None
    move_to: Optional["Pos"] = None
    move_start: float = 0.0
    move_end: float = 0.0
    executed_steps: set = field(default_factory=set)   # step indices in the current move that moved
    skipped_steps: set = field(default_factory=set)    # step indices skipped because of a wall
    last_step_index: Optional[int] = None
    blocked_at: Optional[tuple[float, str]] = None     # (time, direction) of last blocked step
    steps_taken: int = 0
    trail: list = field(default_factory=list)          # (from, to) of executed steps, for tests

    @property
    def busy(self) -> bool:
        return self.current is not None or bool(self.queue) or self.next_time is not None

    @property
    def current_move(self) -> Optional[Move]:
        if self.current and self.move_idx < len(self.current.moves):
            return self.current.moves[self.move_idx]
        return None

    @property
    def logical_pos(self) -> "Pos":
        """Where the avatar is (or is arriving), for path projection."""
        if self.phase == "arrive" and self.move_to is not None:
            return self.move_to
        return self.pos

    def render_pos(self, now: float) -> tuple[float, float]:
        if self.move_to is not None and self.move_from is not None and now < self.move_end:
            span = max(self.move_end - self.move_start, 1e-9)
            f = min(max((now - self.move_start) / span, 0.0), 1.0)
            return (self.move_from[0] + (self.move_to[0] - self.move_from[0]) * f,
                    self.move_from[1] + (self.move_to[1] - self.move_from[1]) * f)
        return (float(self.pos[0]), float(self.pos[1]))

    def all_cards(self) -> list[Card]:
        out = []
        if self.current:
            out.extend(self.current.live_cards())
        for q in self.queue:
            out.extend(q.live_cards())
        return out

    def queued_card_count(self) -> int:
        return len(self.all_cards())


# --- execution-mode strategy (interrupt_mode is [OPTIONAL]) -----------------------

class QueueMode:
    """Base game: pushes append to the FIFO queue, nothing is interrupted."""

    def push(self, game: "Game", av: Avatar, qs: QueuedSequence, now: float) -> None:
        av.queue.append(qs)
        if av.next_time is None:
            av.next_time = max(now, av.idle_since + game.cfg.pause_between_sequences_s)
            av.phase = "step"


class InterruptMode:
    """[OPTIONAL] A new push discards the running and queued sequences."""

    def push(self, game: "Game", av: Avatar, qs: QueuedSequence, now: float) -> None:
        for c in av.all_cards():
            game.deck.discard(c)
        av.queue.clear()
        av.current = None
        if av.phase == "arrive" and av.move_to is not None:
            av.pos = av.move_to  # finish the hop in progress
        av.phase = "step"
        av.queue.append(qs)
        av.next_time = now
        game.on_cards_discarded(now)


# --- the step engine ----------------------------------------------------------------

def _start_move(av: Avatar) -> None:
    av.seg_idx = 0
    av.seg_step = 0
    av.executed_steps = set()
    av.skipped_steps = set()
    av.last_step_index = None


def _discard(game: "Game", qs: QueuedSequence, indices, t: float) -> None:
    any_ = False
    for i in indices:
        if i not in qs.discarded:
            qs.discarded.add(i)
            game.deck.discard(qs.cards[i])
            any_ = True
    if any_:
        game.on_cards_discarded(t)


def advance(game: "Game", av: Avatar) -> None:
    """Process exactly one scheduled event for this avatar (av.next_time is its time)."""
    t = av.next_time
    assert t is not None
    cfg = game.cfg

    if av.phase == "arrive":
        av.pos = av.move_to
        av.phase = "step"
        game.on_avatar_enter(av, t)
        av.next_time = t + cfg.pause_after_step_s
        return

    while True:
        if av.current is None:
            if not av.queue:
                av.next_time = None
                av.idle_since = t
                return
            av.current = av.queue.popleft()
            av.move_idx = 0
            _start_move(av)
            qs = av.current
            qs.started = True
            _discard(game, qs, qs.resolution.idle_cards, t)

        qs = av.current
        if av.move_idx >= len(qs.moves):
            _discard(game, qs, range(len(qs.cards)), t)   # safety: nothing may stay behind
            av.current = None
            if av.queue and cfg.pause_between_sequences_s > 0:
                av.next_time = t + cfg.pause_between_sequences_s
                av.idle_since = t
                return
            continue

        mv = qs.moves[av.move_idx]
        act = next_action(game.maze, av.pos, mv.type.segments, av.seg_idx, av.seg_step)
        if act[0] == "done":
            done = av.move_idx
            _discard(game, qs, [i for i, m in qs.resolution.responsible.items() if m == done], t)
            av.move_idx += 1
            _start_move(av)
            if av.move_idx < len(qs.moves) and cfg.pause_between_cards_s > 0:
                av.next_time = t + cfg.pause_between_cards_s
                return
            continue

        if act[0] == "blocked":
            _, d, av.seg_idx, av.seg_step, skipped = act
            av.skipped_steps.update(skipped)
            av.blocked_at = (t, d)
            delay = game.collision.on_blocked(game, av, d, t)
            if delay > 0:
                av.next_time = t + delay
                return
            continue

        _, to, av.seg_idx, av.seg_step, idx = act
        av.executed_steps.add(idx)
        av.last_step_index = idx
        av.steps_taken += 1
        av.move_from = av.pos
        av.move_to = to
        av.trail.append((av.pos, to))
        av.move_start = t
        av.move_end = t + cfg.tile_move_time_s
        if cfg.tile_move_time_s > 0:
            av.phase = "arrive"
            av.next_time = av.move_end
            return
        av.pos = av.move_to
        game.on_avatar_enter(av, t)
        av.next_time = t + cfg.pause_after_step_s
        return
