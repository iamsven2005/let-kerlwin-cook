"""Route tracing and preview (sections 18.1, 18.4).

Uses the same resolution (resolve.resolve) and the same movement rule (resolve.next_action) as the
walk, so every trace on the map is exactly what the avatar will do.

trace(maze, start, moves, stop_at)  -> PathPlan, stopping the moment the centre is entered
plan_route(maze, start, centre, locked_cards, pending_cards) -> RoutePlan:
  locked    : the locked route's definite moves, traced from spawn (solid, player colour)
  pending   : what the pending chunk adds, continuing from the locked route's end (live preview)
  tentative : the cards of a still-open FOR block, traced once from there (dashed, "x?")
Completion only counts on the definite trace (18.4): never inside an open block or past a
pending IF.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional

from .cards import Card
from .resolve import Move, Resolution, next_action, resolve


@dataclass
class PlannedMove:
    move: Move
    move_index: int
    start: tuple[int, int]
    steps: list[tuple[tuple[int, int], tuple[int, int]]] = field(default_factory=list)
    blocked: list[tuple[tuple[int, int], str]] = field(default_factory=list)   # (pos, dir) stubs
    end: tuple[int, int] = (0, 0)


@dataclass
class PathPlan:
    moves: list[PlannedMove]
    start: tuple[int, int]
    end: tuple[int, int]
    reached: bool = False              # entered stop_at (the centre)
    reach_move: Optional[int] = None   # index (into the traced list) of the move that got there

    @property
    def all_steps(self):
        return [s for m in self.moves for s in m.steps]

    @property
    def length(self) -> int:
        return len(self.all_steps)

    @property
    def move_list(self) -> list[Move]:
        return [pm.move for pm in self.moves]


def trace(maze, start, moves: list[Move], stop_at=None) -> PathPlan:
    """Walk `moves` from `start` with the wall rule. If stop_at is given (the centre), stop on the
    step that enters it: the rest of that move and every later move are ignored (18.1.4)."""
    pos = start
    out: list[PlannedMove] = []
    if stop_at is not None and pos == stop_at:
        return PathPlan(out, start, pos, True, None)
    for mi, mv in enumerate(moves):
        pm = PlannedMove(mv, mi, pos)
        seg_idx, seg_step = 0, 0
        segs = mv.type.segments
        while True:
            act = next_action(maze, pos, segs, seg_idx, seg_step)
            if act[0] == "done":
                break
            if act[0] == "blocked":
                _, d, seg_idx, seg_step, _skipped = act
                pm.blocked.append((pos, d))
                continue
            _, to, seg_idx, seg_step, _idx = act
            pm.steps.append((pos, to))
            pos = to
            if stop_at is not None and pos == stop_at:
                pm.end = pos
                out.append(pm)
                return PathPlan(out, start, pos, True, mi)
        pm.end = pos
        out.append(pm)
    return PathPlan(out, start, pos)


def walk_time(cfg, plan: PathPlan) -> float:
    """Seconds from the start of the walk to arriving on the last traced tile, using the same
    timeline as the walk engine (execution.py): each step = tile_move_time_s, then
    pause_after_step_s before the next step; pause_between_cards_s between moves; a blocked step
    costs nothing."""
    tile, pause = cfg.tile_move_time_s, cfg.pause_after_step_s
    between = cfg.pause_between_cards_s
    t = 0.0
    arrival = 0.0
    for k, pm in enumerate(plan.moves):
        for _ in pm.steps:
            arrival = t + tile
            t = arrival + pause
        if k < len(plan.moves) - 1:
            t += between
    return arrival


@dataclass
class RoutePlan:
    locked: PathPlan
    pending: Optional[PathPlan]
    tentative: Optional[PathPlan]
    res_locked: Resolution
    res_all: Optional[Resolution]       # locked + pending chunk (None without a pending chunk)
    n_locked: int                       # written cards in the locked route

    @property
    def complete(self) -> bool:
        return self.locked.reached

    @property
    def preview_complete(self) -> bool:
        """The pending chunk, if pushed now, would complete the route."""
        return self.pending is not None and self.pending.reached

    @property
    def end(self) -> tuple[int, int]:
        return self.pending.end if self.pending is not None else self.locked.end


def plan_route(maze, start, centre, locked_cards: list[Card],
               pending_cards: Optional[list[Card]] = None) -> RoutePlan:
    res_l = resolve(locked_cards)
    locked = trace(maze, start, res_l.moves, centre)
    pending = None
    res_a = None
    res_open = res_l
    if pending_cards:
        res_a = resolve(list(locked_cards) + list(pending_cards))
        res_open = res_a
        assert res_a.moves[:len(res_l.moves)] == res_l.moves, "locked moves must be a prefix"
        if not locked.reached:
            pending = trace(maze, locked.end, res_a.moves[len(res_l.moves):], centre)
    tentative = None
    tail = pending if pending is not None else locked
    if res_open.tentative and not tail.reached:
        tentative = trace(maze, tail.end, res_open.tentative, None)
    return RoutePlan(locked, pending, tentative, res_l, res_a, len(locked_cards))
