"""Hand = conveyor. Pure functions over a list of slots, so they're easy to test.

Model (spec HUB-Q3, "gravity"): cards in *unlocked* slots always settle into the
lowest unlocked slots, keeping their relative order. Empty slots therefore
bubble to the top. New cards enter at the top unlocked slot and fall.
Locked slots (nodes in an open sequence, or frozen while disconnected) keep
their card and are skipped over.

Slot index 0 is hand number 1 (top).
"""
from __future__ import annotations

from typing import Optional, Sequence, TypeVar

T = TypeVar("T")


def settle(slots: Sequence[Optional[T]], locked: Sequence[bool]) -> list[Optional[T]]:
    idx = [i for i in range(len(slots)) if not locked[i]]
    cards = [slots[i] for i in idx if slots[i] is not None]
    out = list(slots)
    gap = len(idx) - len(cards)
    for j, i in enumerate(idx):
        out[i] = None if j < gap else cards[j - gap]
    return out


def has_empty_unlocked(slots: Sequence[Optional[T]], locked: Sequence[bool]) -> bool:
    return any(s is None and not l for s, l in zip(slots, locked))


def deal_one(slots: Sequence[Optional[T]], locked: Sequence[bool], card: T) -> list[Optional[T]]:
    """Insert a card at the top unlocked slot and let it fall. Requires an empty unlocked slot."""
    out = settle(slots, locked)
    for i in range(len(out)):
        if not locked[i]:
            if out[i] is not None:
                raise ValueError("no empty unlocked slot to deal into")
            out[i] = card
            return settle(out, locked)
    raise ValueError("all slots locked")


def remove(slots: Sequence[Optional[T]], locked: Sequence[bool], index: int) -> tuple[Optional[T], list[Optional[T]]]:
    """Take the card out of a slot (the caller decides where it goes) and slide the conveyor."""
    out = list(slots)
    card = out[index]
    out[index] = None
    return card, settle(out, locked)
