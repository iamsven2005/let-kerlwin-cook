"""Path preview (section 14 B4): the exact path an avatar will take.

Uses the same resolution (resolve.resolve) and the same movement rule (resolve.next_action) as
the step engine, so the preview always matches real execution.

plan_player(game, player) returns:
  current : the rest of the sequence being executed now (from the avatar's position)
  queued  : one plan per queued sequence, chained
  pending : the open sequence on the table, starting where the queued ones end
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
    resolution: Optional[Resolution] = None

    @property
    def all_steps(self):
        return [s for m in self.moves for s in m.steps]


def simulate(maze, start, moves: list[Move], first_cursor: tuple[int, int, int] = (0, 0, 0),
             resolution: Optional[Resolution] = None) -> PathPlan:
    """Walk `moves` from `start`. first_cursor = (move index, seg_idx, seg_step) lets the
    current sequence resume mid-move."""
    pos = start
    out: list[PlannedMove] = []
    mi0, seg0, step0 = first_cursor
    for mi in range(mi0, len(moves)):
        mv = moves[mi]
        pm = PlannedMove(mv, mi, pos)
        seg_idx, seg_step = (seg0, step0) if mi == mi0 else (0, 0)
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
        pm.end = pos
        out.append(pm)
    return PathPlan(out, start, pos, resolution)


def plan_cards(maze, start, cards: list[Card]) -> PathPlan:
    res = resolve(cards)
    return simulate(maze, start, res.moves, resolution=res)


@dataclass
class PlayerPlans:
    current: Optional[PathPlan]
    queued: list[PathPlan]
    pending: Optional[PathPlan]
    projected_start: tuple[int, int]     # where the pending sequence starts


def plan_player(game, pl) -> PlayerPlans:
    av = pl.avatar
    pos = av.logical_pos
    current = None
    if av.current is not None:
        cursor = (av.move_idx, av.seg_idx, av.seg_step)
        current = simulate(game.maze, pos, av.current.moves, cursor, av.current.resolution)
        pos = current.end
    queued = []
    for qs in av.queue:
        p = simulate(game.maze, pos, qs.moves, resolution=qs.resolution)
        queued.append(p)
        pos = p.end
    pending = None
    if pl.sequence is not None:
        pending = plan_cards(game.maze, pos, [e.card for e in pl.sequence.entries])
    return PlayerPlans(current, queued, pending, pos)
