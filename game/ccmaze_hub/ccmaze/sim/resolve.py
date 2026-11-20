"""Push-time resolution of IF / FOR cards (section 14 B2), and the single movement rule.

resolve(cards) turns the pushed row (left to right) into a flat list of movement Moves, keeping
provenance: which physical card each move came from, and why (normal, IF bonus, FOR repeat).
Order: FOR is expanded first, then IFs are resolved left to right against the expanded list.

next_action() is the one implementation of the movement rule (segment skip on walls). Both the
step engine (execution.py) and the path preview (preview.py) use it, so the preview always
matches what really happens.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional

from .cards import DIRS, Card, CardType


@dataclass(frozen=True)
class Move:
    type: CardType
    source: int                 # index (in the pushed row) of the card whose movement this is
    kind: str = "card"          # "card" | "if" (IF bonus move) | "repeat" (FOR repeat)
    repeat_i: int = 1           # FOR repeats: 1..repeat_n
    repeat_n: int = 1
    via: Optional[int] = None   # index of the IF / FOR card that caused this move


@dataclass
class Resolution:
    moves: list[Move]
    responsible: dict[int, int] = field(default_factory=dict)   # card index -> last move index it owns
    if_effective: list[int] = field(default_factory=list)       # indices of IFs that fired
    for_effective: list[tuple[int, int]] = field(default_factory=list)  # (FOR index, n repeats)
    consumed: list[int] = field(default_factory=list)           # cards eaten by a FOR

    @property
    def idle_cards(self) -> list[int]:
        """Card indices that own no move (discarded when the sequence starts, spec HUB-Q27)."""
        return [i for i in range(self._n) if i not in self.responsible]

    _n: int = 0


@dataclass(frozen=True)
class _Item:
    src: int
    kind: str = "card"          # "card" | "repeat" | "noop" (an ineffective FOR)
    repeat_i: int = 1
    repeat_n: int = 1
    via: Optional[int] = None


def resolve(cards: list[Card]) -> Resolution:
    res = Resolution(moves=[])
    res._n = len(cards)
    # 1. expand FOR
    items: list[_Item] = []
    i = 0
    while i < len(cards):
        c = cards[i]
        if c.kind == "FOR":
            rest = len(cards) - i - 1
            if rest == 0 or cards[i + 1].is_control:
                items.append(_Item(i, "noop"))        # no effect; following cards run normally
                i += 1
                continue
            for k in range(rest):
                items.append(_Item(i + 1, "repeat", k + 1, rest, via=i))
            res.consumed = list(range(i + 2, len(cards)))
            res.for_effective.append((i, rest))
            break                                     # everything after C1 is consumed
        items.append(_Item(i))
        i += 1
    # 2. resolve IFs left to right against the expanded list, emit moves
    for j, it in enumerate(items):
        c = cards[it.src]
        if it.kind == "noop":
            continue
        if c.kind == "IF":
            nxt = items[j + 1] if j + 1 < len(items) else None
            if nxt is not None and nxt.kind != "noop":
                n = cards[nxt.src]
                if n.is_move and n.family == c.type.cond:
                    res.moves.append(Move(c.type.action, it.src, "if", via=it.src))
                    res.if_effective.append(it.src)
            continue
        if c.is_move:
            res.moves.append(Move(c.type, it.src, it.kind, it.repeat_i, it.repeat_n, it.via))
    # 3. ownership for discard timing: a card goes when the last move it owns has finished
    for m, mv in enumerate(res.moves):
        res.responsible[mv.source] = m
        if mv.via is not None:
            res.responsible[mv.via] = m          # the FOR goes with its last repeat
    return res


# ------------------------------------------------------------------ the movement rule
def next_action(maze, pos, segments: list[tuple[str, int]], seg_idx: int, seg_step: int):
    """One decision of the movement rule.

    Returns one of:
      ("move", to_pos, seg_idx, seg_step, step_index)   -> take this step; new cursor
      ("blocked", direction, seg_idx, seg_step, skipped_step_indices)
                                                        -> wall/border: rest of segment skipped
      ("done",)                                         -> card finished
    """
    while seg_idx < len(segments):
        d, count = segments[seg_idx]
        base = sum(n for _, n in segments[:seg_idx])
        if seg_step >= count:
            seg_idx, seg_step = seg_idx + 1, 0
            continue
        if not maze.can_move(pos, d):
            skipped = list(range(base + seg_step, base + count))
            return ("blocked", d, seg_idx + 1, 0, skipped)
        dx, dy = DIRS[d]
        return ("move", (pos[0] + dx, pos[1] + dy), seg_idx, seg_step + 1, base + seg_step)
    return ("done",)
