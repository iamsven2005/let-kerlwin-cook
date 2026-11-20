"""Shared card style (spec 4): segment colours, cycling for 4+ runs, start ring, arrow."""
import os

import pytest

os.environ.setdefault("SDL_VIDEODRIVER", "dummy")
pygame = pytest.importorskip("pygame")

from ccmaze.sim.cards import CardType, validate_steps  # noqa: E402
from ccmaze.ui.widgets import SEGMENT_COLOURS, draw_card, segment_colour  # noqa: E402

E0, BLUE, PURPLE = (0xE0, 0xE0, 0xE0), (0x56, 0xCC, 0xF2), (0xBB, 0x6B, 0xD9)


def test_segment_colours_cycle_mod_3():
    assert SEGMENT_COLOURS == [E0, BLUE, PURPLE]
    assert [segment_colour(i) for i in range(5)] == [E0, BLUE, PURPLE, E0, BLUE]


def test_four_run_card_is_valid_and_adjacent_runs_differ():
    assert validate_steps("URUR") is None
    segs = CardType("URUR", "custom").segments
    assert len(segs) == 4
    cols = [segment_colour(i) for i in range(len(segs))]
    assert cols[3] == E0
    assert all(a != b for a, b in zip(cols, cols[1:]))


def _pixel_colours(steps):
    pygame.init()
    surf = pygame.Surface((120, 120))
    surf.fill((0, 0, 0))
    draw_card(surf, (0, 0, 120, 120), steps, border=False, max_cell=20)
    return {tuple(surf.get_at((x, y)))[:3] for x in range(120) for y in range(120)}


def test_drawn_four_run_card_uses_all_three_colours():
    cols = _pixel_colours("URUR")
    assert E0 in cols and BLUE in cols and PURPLE in cols


def test_single_run_card_uses_only_first_colour():
    cols = _pixel_colours("RRR")
    assert E0 in cols and BLUE not in cols and PURPLE not in cols
