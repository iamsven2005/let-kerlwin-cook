"""Command Cards: movement card types (with family codes), IF/FOR control cards, catalog loading.

v1.4 catalog (section 14 B1): cards are named after the shape the avatar traces, start cell
included (a square O is the start cell plus 3 steps). Every rotation, mirror image and travel
direction is a separate card.
"""
from __future__ import annotations

import itertools
import json
import logging
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterator, Optional, Union

log = logging.getLogger(__name__)

MAX_STEPS = 4  # node display limit
DIRS = {"U": (0, -1), "D": (0, 1), "L": (-1, 0), "R": (1, 0)}

DOMINO_FAMILY = "D"
DOMINOES = ("U", "D", "L", "R")      # the solvability guarantee (14 A3): never disabled

DEFAULT_FAMILIES: dict[str, list[str]] = {
    "D": ["U", "D", "L", "R"],
    "I3": ["UU", "DD", "LL", "RR"],
    "I4": ["UUU", "DDD", "LLL", "RRR"],
    "V": ["UR", "UL", "DR", "DL", "RU", "RD", "LU", "LD"],
    "O": ["RDL", "RUL", "LDR", "LUR", "URD", "ULD", "DRU", "DLU"],
    "L": ["UUR", "UUL", "DDR", "DDL", "RRU", "RRD", "LLU", "LLD",
          "URR", "ULL", "DRR", "DLL", "RUU", "RDD", "LUU", "LDD"],
    "S": ["RUR", "RDR", "LUL", "LDL", "URU", "ULU", "DRD", "DLD"],
}
FAMILY_CODES = tuple(DEFAULT_FAMILIES)
FAMILY_NAMES = {"D": "Domino", "I3": "I3 line", "I4": "I4 line", "V": "Corner", "O": "Square",
                "L": "L", "S": "S"}


def validate_steps(steps: str) -> str | None:
    """Return why a movement card is invalid, or None. The node display relies on these rules:
    1-4 steps of U/D/L/R, and the path never revisits a cell (e.g. UD, LRL are rejected)."""
    if not isinstance(steps, str) or not steps:
        return "no steps"
    if len(steps) > MAX_STEPS:
        return f"more than {MAX_STEPS} steps"
    if any(c not in DIRS for c in steps):
        return "steps must be U/D/L/R"
    x = y = 0
    seen = {(0, 0)}
    for c in steps:
        dx, dy = DIRS[c]
        x, y = x + dx, y + dy
        if (x, y) in seen:
            return "path revisits a cell"
        seen.add((x, y))
    return None


def segments_of(steps: str) -> list[tuple[str, int]]:
    """'UUR' -> [('U', 2), ('R', 1)]"""
    return [(d, len(list(g))) for d, g in itertools.groupby(steps)]


def cells_of(steps: str) -> list[tuple[int, int]]:
    x = y = 0
    out = [(0, 0)]
    for c in steps:
        dx, dy = DIRS[c]
        x, y = x + dx, y + dy
        out.append((x, y))
    return out


def shape_family(steps: str) -> Optional[str]:
    """Family code of the shape a path traces (start cell included), or None for other shapes."""
    if validate_steps(steps):
        return None
    cells = cells_of(steps)
    xs = [c[0] for c in cells]
    ys = [c[1] for c in cells]
    w, h = max(xs) - min(xs) + 1, max(ys) - min(ys) + 1
    n = len(cells)
    if n == 2:
        return "D"
    if n == 3:
        return "I3" if 3 in (w, h) else "V"
    if n == 4:
        if 4 in (w, h):
            return "I4"
        if w == 2 and h == 2:
            return "O"
        # 3x2 box: L/J if some line along the long axis holds 3 cells, else S/Z
        if w == 3:
            rows = {}
            for _, y in cells:
                rows[y] = rows.get(y, 0) + 1
            return "L" if 3 in rows.values() else "S"
        cols = {}
        for x, _ in cells:
            cols[x] = cols.get(x, 0) + 1
        return "L" if 3 in cols.values() else "S"
    return None


@dataclass(frozen=True)
class CardType:
    """A movement card type."""
    steps: str
    family: str
    weight: float = 1.0

    def __post_init__(self):
        problem = validate_steps(self.steps)
        if problem:
            raise ValueError(f"invalid card {self.steps!r}: {problem}")

    @property
    def segments(self) -> list[tuple[str, int]]:
        return segments_of(self.steps)

    def cells(self) -> list[tuple[int, int]]:
        """Tiles covered when drawn Tetris-style, starting at (0, 0)."""
        return cells_of(self.steps)

    @property
    def code(self) -> str:
        return self.steps


@dataclass(frozen=True)
class ControlType:
    """IF (condition family + action card) or FOR (14 B2)."""
    kind: str                         # "IF" | "FOR"
    cond: Optional[str] = None        # IF: condition family code
    action: Optional[CardType] = None  # IF: the movement it adds

    def __post_init__(self):
        if self.kind not in ("IF", "FOR"):
            raise ValueError(f"unknown control card {self.kind!r}")
        if self.kind == "IF" and (not self.cond or self.action is None):
            raise ValueError("IF needs a condition family and an action card")

    @property
    def code(self) -> str:
        """Protocol payload after CARD: (CARD:IF:O:LLU, CARD:FOR)."""
        if self.kind == "FOR":
            return "FOR"
        return f"IF:{self.cond}:{self.action.steps}"


_uid_counter = itertools.count(1)


@dataclass(eq=False)
class Card:
    """A physical card instance in the deck (two instances of 'UUR' are distinct)."""

    type: Union[CardType, ControlType]
    uid: int = field(default_factory=lambda: next(_uid_counter))

    @property
    def kind(self) -> str:
        return self.type.kind if isinstance(self.type, ControlType) else "MOVE"

    @property
    def is_control(self) -> bool:
        return isinstance(self.type, ControlType)

    @property
    def is_move(self) -> bool:
        return isinstance(self.type, CardType)

    @property
    def code(self) -> str:
        return self.type.code

    @property
    def steps(self) -> str:
        """Movement steps ('' for control cards)."""
        return self.type.steps if isinstance(self.type, CardType) else ""

    @property
    def family(self) -> Optional[str]:
        return self.type.family if isinstance(self.type, CardType) else None

    def __repr__(self) -> str:  # pragma: no cover - debug helper
        return f"Card({self.code}#{self.uid})"


def parse_code(code: str) -> tuple[str, Optional[str], Optional[str]]:
    """'UUR' -> ('MOVE', None, 'UUR'); 'IF:O:LLU' -> ('IF', 'O', 'LLU'); 'FOR' -> ('FOR', None, None);
    '-' or '' -> ('NONE', None, None)."""
    if not code or code == "-":
        return ("NONE", None, None)
    if code == "FOR":
        return ("FOR", None, None)
    if code.startswith("IF:"):
        parts = code.split(":")
        if len(parts) == 3:
            return ("IF", parts[1], parts[2])
        return ("BAD", None, None)
    return ("MOVE", None, code)


# ------------------------------------------------------------------ catalog
@dataclass
class Catalog:
    movement: list[CardType]
    if_enabled: bool = True
    for_enabled: bool = True

    def __iter__(self) -> Iterator[CardType]:
        return iter(self.movement)

    def __len__(self) -> int:
        return len(self.movement)

    def families(self) -> list[str]:
        return sorted({c.family for c in self.movement})


def default_catalog_dict() -> dict:
    return {
        "_comment": "Each family and each card type has an enable switch and a weight. Effective weight of "
                    "a card type = family weight x card weight. The dominoes U/D/L/R can't be disabled "
                    "(solvability guarantee). control_cards: IF/FOR on/off; their share of the deck is "
                    "control_card_share in config.json.",
        "families": {
            fam: {
                "enabled": True,
                "weight": 1.0,
                "cards": {s: {"enabled": True, "weight": 1.0} for s in cards},
            }
            for fam, cards in DEFAULT_FAMILIES.items()
        },
        "control_cards": {"IF": {"enabled": True}, "FOR": {"enabled": True}},
    }


def catalog_from_dict(d: dict, warnings: list[str] | None = None) -> Catalog:
    """Build the enabled catalog. Invalid cards are skipped and reported in `warnings` (spec 2.2,
    HUB-Q24). Dominoes that are disabled or missing are put back (spec HUB-Q29)."""
    out: list[CardType] = []
    warnings = warnings if warnings is not None else []

    def warn(msg):
        warnings.append(msg)
        log.warning(msg)

    seen: set[str] = set()
    for fam, fd in d.get("families", {}).items():
        fam_on = bool(fd.get("enabled", True))
        fw = float(fd.get("weight", 1.0))
        for steps, cd in fd.get("cards", {}).items():
            problem = validate_steps(steps)
            if problem:
                warn(f"cards.json: rejected {fam}/{steps!r} ({problem})")
                continue
            if steps in seen:
                warn(f"cards.json: {steps!r} listed twice; keeping the first")
                continue
            w = fw * float(cd.get("weight", 1.0))
            on = fam_on and bool(cd.get("enabled", True)) and w > 0
            if steps in DOMINOES and not on:
                warn(f"cards.json: domino {steps!r} can't be disabled (solvability guarantee); re-enabled")
                on, w = True, max(w, 1.0)
            if not on:
                continue
            shape = shape_family(steps)
            if shape and fam in FAMILY_CODES and shape != fam:
                warn(f"cards.json: {steps!r} is listed in family {fam} but traces a {shape} shape")
            seen.add(steps)
            out.append(CardType(steps, fam, w))
    for s in DOMINOES:
        if s not in seen:
            warn(f"cards.json: domino {s!r} missing; added (solvability guarantee)")
            out.append(CardType(s, DOMINO_FAMILY, 1.0))
    cc = d.get("control_cards", {})
    return Catalog(
        movement=out,
        if_enabled=bool(cc.get("IF", {}).get("enabled", True)),
        for_enabled=bool(cc.get("FOR", {}).get("enabled", True)),
    )


def load_catalog(path: str | Path | None, warnings: list[str] | None = None) -> Catalog:
    if path is None or not Path(path).exists():
        return catalog_from_dict(default_catalog_dict(), warnings)
    with Path(path).open("r", encoding="utf-8") as f:
        return catalog_from_dict(json.load(f), warnings)


def write_default_catalog(path: str | Path) -> None:
    """Write the default catalog, one card type per line so it's easy to hand-edit."""
    d = default_catalog_dict()
    lines = ["{", f'  "_comment": {json.dumps(d["_comment"])},', '  "families": {']
    fams = list(d["families"].items())
    for i, (fam, fd) in enumerate(fams):
        lines.append(f'    {json.dumps(fam)}: {{"enabled": true, "weight": 1.0, "cards": {{')
        cards = list(fd["cards"].items())
        for j, (steps, cd) in enumerate(cards):
            comma = "," if j < len(cards) - 1 else ""
            lines.append(f'      {json.dumps(steps)}: {{"enabled": true, "weight": 1.0}}{comma}')
        lines.append("    }}" + ("," if i < len(fams) - 1 else ""))
    lines += ["  },",
              '  "control_cards": {"IF": {"enabled": true}, "FOR": {"enabled": true}}',
              "}"]
    text = "\n".join(lines) + "\n"
    json.loads(text)  # sanity check
    Path(path).write_text(text, encoding="utf-8")
