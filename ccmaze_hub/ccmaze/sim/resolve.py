"""Route resolution of IF / FOR cards (section 18.2-18.3), and the single movement rule.

resolve(cards) takes a route *as written* (every locked chunk in order, optionally followed by the
pending chunk) and turns it into a flat list of movement Moves, keeping provenance: which written
card each move came from, and why (normal card, transformed IF, FOR repeat).

Order (18.3): IFs first, then FOR blocks expanded, then (elsewhere) the movement is traced.

IF   resolved once against the card written directly after it. A movement card of the IF's
     condition family -> the IF becomes its action shape at the same position; anything else
     (a different family, IF, FOR) -> a blank (no movement). An IF with nothing written after it
     is *pending*: it waits for the next chunk.
FOR  the first FOR opens a block, the next FOR closes it. N cards between them (a blank or a
     pending IF counts as a card) -> the block runs N times (spec HUB-Q40: so the 18.3 table's rows
     6 and 7, which have 2 cards between the FORs, run x2, not x3). Both FORs produce no movement.
     Blocks don't nest. A block still open at the end of the route is *open*: its cards are
     reported once as `tentative` (drawn dashed, "x?") and produce no definite moves yet.

`moves` are the definite moves. Appending more written cards never changes them (only a pending
IF or an open block can change), so a locked route's moves are always a prefix of the moves of
the same route plus a pending chunk.

next_action() is the one implementation of the movement rule (segment skip on walls). The walk
(execution.py) and every trace / preview (preview.py) use it, so previews match the walk.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional

from .cards import DIRS, Card, CardType

# per written card
MOVE, IF_SHAPE, BLANK, IF_PENDING, FOR_OPEN, FOR_CLOSE, FOR_OPEN_PENDING = (
    "move", "if", "blank", "if?", "for", "for_end", "for?")


@dataclass(frozen=True)
class Move:
    type: CardType
    source: int                 # index (in the written route) of the card this move comes from
    kind: str = "card"          # "card" | "if" (a transformed IF)
    repeat_i: int = 1           # FOR block pass 1..repeat_n
    repeat_n: int = 1           # 0 = inside a still-open block ("x?")
    via: Optional[int] = None   # index of the opening FOR, for moves inside a block


@dataclass
class ForBlock:
    open: int
    close: Optional[int]        # None = still open
    members: list[int]          # written indices between the two FORs

    @property
    def n(self) -> Optional[int]:
        return None if self.close is None else len(self.members)


@dataclass
class Resolution:
    cards: list[Card]
    state: list[str]                                   # per written card: MOVE, IF_SHAPE, ...
    if_action: dict[int, CardType] = field(default_factory=dict)   # transformed IFs
    blocks: list[ForBlock] = field(default_factory=list)
    moves: list[Move] = field(default_factory=list)                # definite
    tentative: list[Move] = field(default_factory=list)            # open block, once, repeat_n = 0

    @property
    def open_block(self) -> Optional[ForBlock]:
        return self.blocks[-1] if self.blocks and self.blocks[-1].close is None else None

    @property
    def pending_if(self) -> Optional[int]:
        return next((i for i, s in enumerate(self.state) if s == IF_PENDING), None)

    @property
    def settled(self) -> bool:
        """True when nothing is waiting on a later chunk (no open FOR, no pending IF)."""
        return self.open_block is None and self.pending_if is None

    def block_of(self, i: int) -> Optional[ForBlock]:
        for b in self.blocks:
            if i == b.open or i == b.close or i in b.members:
                return b
        return None

    # tutorial / UI helpers
    @property
    def if_effective(self) -> list[int]:
        return sorted(self.if_action)

    @property
    def for_effective(self) -> list[tuple[int, int]]:
        return [(b.open, b.n) for b in self.blocks if b.close is not None]


def _member_moves(res: Resolution, i: int, repeat_i: int, repeat_n: int, via: Optional[int]) -> list[Move]:
    s = res.state[i]
    if s == MOVE:
        return [Move(res.cards[i].type, i, "card", repeat_i, repeat_n, via)]
    if s == IF_SHAPE:
        return [Move(res.if_action[i], i, "if", repeat_i, repeat_n, via)]
    return []          # blank, pending IF


def resolve(cards: list[Card]) -> Resolution:
    n = len(cards)
    res = Resolution(cards=list(cards), state=[""] * n)
    # 1. IFs, against the card written directly after each one
    for i, c in enumerate(cards):
        if c.kind == "IF":
            if i + 1 >= n:
                res.state[i] = IF_PENDING
                continue
            nxt = cards[i + 1]
            if nxt.is_move and nxt.family == c.type.cond:
                res.state[i] = IF_SHAPE
                res.if_action[i] = c.type.action
            else:
                res.state[i] = BLANK
        elif c.kind == "FOR":
            res.state[i] = FOR_OPEN          # fixed up below
        else:
            res.state[i] = MOVE
    # 2. FOR blocks
    block: Optional[ForBlock] = None
    for i, c in enumerate(cards):
        if c.kind == "FOR":
            if block is None:
                block = ForBlock(i, None, [])
                res.blocks.append(block)
            else:
                block.close = i
                res.state[i] = FOR_CLOSE
                k = len(block.members)
                for r in range(1, k + 1):
                    for m in block.members:
                        res.moves.extend(_member_moves(res, m, r, k, block.open))
                block = None
            continue
        if block is not None:
            block.members.append(i)
        else:
            res.moves.extend(_member_moves(res, i, 1, 1, None))
    if block is not None:
        res.state[block.open] = FOR_OPEN_PENDING
        for m in block.members:
            res.tentative.extend(_member_moves(res, m, 1, 0, block.open))
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
