"""The walk (section 18.1.5): once a route is complete the avatar walks it from its spawn corner.

The per-avatar queue of pushed sequences and live execution while building are gone (18.1); the
avatar waits at spawn until its route reaches the centre, then walks the whole route.

Steps run on an exact timeline (spec HUB-Q5): each event carries a scheduled time, and the game
processes events across all avatars in time order, independent of frame rate.

Timeline of one step: start at t -> arrive at t + tile_move_time_s -> next step due at
arrival + pause_after_step_s. A move finishes when the pause after its last step ends (spec
HUB-Q4), then pause_between_cards_s. A blocked step costs no time: the rest of that segment is
skipped. The walk ends the moment the avatar enters the centre (the rest of the route is ignored).
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import TYPE_CHECKING, Optional

from .resolve import Move, next_action

if TYPE_CHECKING:  # pragma: no cover
    from .game import Game
    from .maze import Pos


@dataclass
class Avatar:
    player: int
    pos: "Pos"
    start: "Pos"
    moves: list[Move] = field(default_factory=list)   # the route being walked
    push_time: Optional[float] = None                 # the push that completed the route
    walk_start: Optional[float] = None
    total_steps: int = 0                              # steps in the route, for the progress bar
    arrived: bool = False
    move_idx: int = 0
    seg_idx: int = 0
    seg_step: int = 0
    next_time: Optional[float] = None
    phase: str = "step"  # "step" | "arrive"
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
    def walking(self) -> bool:
        """The route is complete and the avatar is (about to start) walking it."""
        return self.push_time is not None and not self.arrived

    @property
    def busy(self) -> bool:
        return self.next_time is not None

    @property
    def current_move(self) -> Optional[Move]:
        if self.walking and self.move_idx < len(self.moves):
            return self.moves[self.move_idx]
        return None

    def progress(self) -> float:
        if self.arrived:
            return 1.0
        return self.steps_taken / self.total_steps if self.total_steps else 0.0

    def render_pos(self, now: float) -> tuple[float, float]:
        if self.move_to is not None and self.move_from is not None and now < self.move_end:
            span = max(self.move_end - self.move_start, 1e-9)
            f = min(max((now - self.move_start) / span, 0.0), 1.0)
            return (self.move_from[0] + (self.move_to[0] - self.move_from[0]) * f,
                    self.move_from[1] + (self.move_to[1] - self.move_from[1]) * f)
        return (float(self.pos[0]), float(self.pos[1]))

    def begin_walk(self, moves: list[Move], push_time: float, start_at: float, total_steps: int) -> None:
        self.moves = list(moves)
        self.push_time = push_time
        self.walk_start = start_at
        self.total_steps = total_steps
        self.move_idx = 0
        _start_move(self)
        self.phase = "step"
        self.next_time = start_at


def _start_move(av: Avatar) -> None:
    av.seg_idx = 0
    av.seg_step = 0
    av.executed_steps = set()
    av.skipped_steps = set()
    av.last_step_index = None


def advance(game: "Game", av: Avatar) -> None:
    """Process exactly one scheduled event for this avatar (av.next_time is its time)."""
    t = av.next_time
    assert t is not None
    cfg = game.cfg

    if av.phase == "arrive":
        av.pos = av.move_to
        av.phase = "step"
        av.next_time = t + cfg.pause_after_step_s
        game.on_avatar_enter(av, t)
        return

    while True:
        if av.move_idx >= len(av.moves):
            av.next_time = None        # route used up without reaching the centre (shouldn't happen)
            return
        mv = av.moves[av.move_idx]
        act = next_action(game.maze, av.pos, mv.type.segments, av.seg_idx, av.seg_step)
        if act[0] == "done":
            av.move_idx += 1
            _start_move(av)
            if av.move_idx < len(av.moves) and cfg.pause_between_cards_s > 0:
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
        av.next_time = t + cfg.pause_after_step_s
        game.on_avatar_enter(av, t)
        return
