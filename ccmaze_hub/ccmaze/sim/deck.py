"""The single shared deck: draw pile + discard pile, and the v1.4 deck-building rules.

Deck contents (section 14 A3 / B2):
  movement = max(deck_multiplier x total hand size, min_copies_per_type x enabled movement types)
             but never less than the guaranteed copies: min_copies_per_type of every type, and at
             least min_domino_copies of each domino U/D/L/R (any grid path can be walked with
             single steps, so every maze stays solvable)
  control  = per enabled kind (IF, FOR): max(1, round(control_card_share x movement / 2))
Each IF gets its condition family and action card at build time, weighted by the catalog.
"""
from __future__ import annotations

import math
import random
from dataclasses import dataclass

from .cards import DOMINOES, Card, CardType, Catalog, ControlType


def weighted_round_robin(types: list[CardType], n: int, rng: random.Random) -> list[CardType]:
    """Pick n card types so counts track the weights as evenly as possible.

    The type order is shuffled first, so a partial fill is a random weighted subset rather than
    the first types in catalog order. Smooth weighted round-robin (nginx style).
    """
    if n <= 0 or not types:
        return []
    order = list(types)
    rng.shuffle(order)
    total = sum(t.weight for t in order)
    current = [0.0] * len(order)
    picks: list[CardType] = []
    for _ in range(n):
        for i, t in enumerate(order):
            current[i] += t.weight
        best = max(range(len(order)), key=lambda i: current[i])
        current[best] -= total
        picks.append(order[best])
    return picks


def _round_half_up(x: float) -> int:
    return int(math.floor(x + 0.5))


@dataclass
class DeckPlan:
    movement_target: int     # max(multiplier x hand size, min copies x types)
    guaranteed: int          # copies that must exist (min copies, domino minimum)
    movement: int            # actual movement cards
    n_if: int
    n_for: int

    @property
    def total(self) -> int:
        return self.movement + self.n_if + self.n_for


def plan_deck(catalog: Catalog, total_hand_size: int, cfg) -> DeckPlan:
    types = list(catalog.movement)
    min_copies = max(0, int(cfg.get("min_copies_per_type", 1)))
    min_dom = max(1, int(cfg.get("min_domino_copies", 2)))
    target = max(int(cfg.deck_multiplier * total_hand_size), min_copies * len(types))
    guaranteed = sum(max(min_copies, min_dom) if t.steps in DOMINOES else min_copies for t in types)
    movement = max(target, guaranteed)
    # spec HUB-Q28: IF/FOR switches live in cards.json; each enabled kind gets half the share
    # (minimum 1); a disabled kind's half is not given to the other.
    share = float(cfg.get("control_card_share", 0.15))
    half = share * movement / 2
    n_if = max(1, _round_half_up(half)) if (catalog.if_enabled and share > 0) else 0
    n_for = max(1, _round_half_up(half)) if (catalog.for_enabled and share > 0) else 0
    return DeckPlan(target, guaranteed, movement, n_if, n_for)


def build_cards(catalog: Catalog, total_hand_size: int, cfg, rng: random.Random) -> list[Card]:
    plan = plan_deck(catalog, total_hand_size, cfg)
    types = list(catalog.movement)
    min_copies = max(0, int(cfg.get("min_copies_per_type", 1)))
    min_dom = max(1, int(cfg.get("min_domino_copies", 2)))
    picks: list[CardType] = []
    for t in types:
        picks += [t] * (max(min_copies, min_dom) if t.steps in DOMINOES else min_copies)
    picks += weighted_round_robin(types, plan.movement - len(picks), rng)
    cards = [Card(t) for t in picks]
    weights = [t.weight for t in types]
    for _ in range(plan.n_if):
        # spec HUB-Q36: condition family weighted by its cards' total weight (= how common it is)
        cond = rng.choices(types, weights)[0].family
        action = rng.choices(types, weights)[0]
        cards.append(Card(ControlType("IF", cond, action)))
    for _ in range(plan.n_for):
        cards.append(Card(ControlType("FOR")))
    return cards


class Deck:
    def __init__(self, cards: list[Card], rng: random.Random):
        if not cards:
            raise ValueError("deck needs at least one card")
        self.rng = rng
        self.draw_pile: list[Card] = list(cards)
        self.rng.shuffle(self.draw_pile)
        self.discard_pile: list[Card] = []
        self.reshuffles = 0
        self.size = len(cards)

    @classmethod
    def build(cls, catalog: Catalog, total_hand_size: int, cfg, rng: random.Random) -> "Deck":
        return cls(build_cards(catalog, total_hand_size, cfg, rng), rng)

    def draw(self) -> Card | None:
        """Top card, reshuffling the discard pile if needed. None if both piles are empty."""
        if not self.draw_pile and self.discard_pile:
            self.draw_pile = self.discard_pile
            self.discard_pile = []
            self.rng.shuffle(self.draw_pile)
            self.reshuffles += 1
        if not self.draw_pile:
            return None
        return self.draw_pile.pop()

    def discard(self, card: Card) -> None:
        self.discard_pile.append(card)

    @property
    def can_draw(self) -> bool:
        return bool(self.draw_pile or self.discard_pile)


class ScriptedDeck(Deck):
    """Tutorial deck: deals a fixed list of card types in order, cycling; discards are dropped."""

    def __init__(self, script: list, rng: random.Random | None = None):
        if not script:
            raise ValueError("empty script")
        self.rng = rng or random.Random(0)
        self.script = list(script)
        self.pos = 0
        self.draw_pile = []
        self.discard_pile = []
        self.reshuffles = 0
        self.size = len(script)

    def draw(self) -> Card | None:
        t = self.script[self.pos % len(self.script)]
        self.pos += 1
        return Card(t)

    def discard(self, card: Card) -> None:
        pass

    @property
    def can_draw(self) -> bool:
        return True
