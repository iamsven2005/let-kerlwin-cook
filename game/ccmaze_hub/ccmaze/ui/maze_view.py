"""Maze rendering with avatars and the live path preview (section 14 B4).

Used by the game screen and by the tutorial's practice mazes.

Path overlay, per player, in the player's colour, offset a little per player so overlapping
paths stay readable:
  - pending sequence (the row on the table): bright, starting where the queued sequences end
  - queued sequences and the rest of the running one: fainter; the next step is highlighted
  - a numbered marker where each card's movement begins (the card's row position)
  - IF bonus moves dashed, FOR repeats labelled "xn", wall stops as short red stubs
"""
from __future__ import annotations

import math

import pygame

from ..sim.preview import PathPlan, plan_player
from .widgets import DANGER, GOLD, WALL, hex_rgb, shade, star, text

PLAYER_OFFSETS = [(-1, -1), (1, 1), (1, -1), (-1, 1), (0, -1), (0, 1)]


class MazeGeometry:
    def __init__(self, rect: pygame.Rect, size: int):
        self.cs = rect.w // size
        self.ox = rect.x + (rect.w - self.cs * size) // 2
        self.oy = rect.y + (rect.h - self.cs * size) // 2
        self.size = size

    def tile(self, p) -> pygame.Rect:
        return pygame.Rect(self.ox + p[0] * self.cs, self.oy + p[1] * self.cs, self.cs, self.cs)

    def centre(self, p, off=(0.0, 0.0)):
        return (self.ox + (p[0] + 0.5) * self.cs + off[0], self.oy + (p[1] + 0.5) * self.cs + off[1])


def _dashed(surf, colour, a, b, width, dash):
    dx, dy = b[0] - a[0], b[1] - a[1]
    length = math.hypot(dx, dy)
    if length < 1:
        return
    n = max(1, int(length // dash))
    for i in range(0, n, 2):
        s = (a[0] + dx * i / n, a[1] + dy * i / n)
        e = (a[0] + dx * min(i + 1, n) / n, a[1] + dy * min(i + 1, n) / n)
        pygame.draw.line(surf, colour, s, e, width)


def draw_plan(layer, geo: MazeGeometry, plan: PathPlan, colour, off, *, alpha: int, width: int,
              highlight_first: bool = False, numbers: bool = True):
    if plan is None:
        return
    col = (*colour, alpha)
    first = True
    for pm in plan.moves:
        mv = pm.move
        for (a, b) in pm.steps:
            pa, pb = geo.centre(a, off), geo.centre(b, off)
            if mv.kind == "if":
                _dashed(layer, col, pa, pb, width, max(4, geo.cs // 6))
            else:
                pygame.draw.line(layer, col, pa, pb, width)
            if first and highlight_first:
                pygame.draw.line(layer, (255, 255, 255, 235), pa, pb, width + 2)
            first = False
        for (p, d) in pm.blocked:
            c = geo.centre(p, off)
            dx, dy = {"U": (0, -1), "D": (0, 1), "L": (-1, 0), "R": (1, 0)}[d]
            pygame.draw.line(layer, (*DANGER, min(255, alpha + 40)), c,
                             (c[0] + dx * geo.cs * 0.42, c[1] + dy * geo.cs * 0.42), width + 1)
        if numbers and (mv.kind != "repeat" or mv.repeat_i == 1):
            c = geo.centre(pm.start, off)
            r = max(6, geo.cs // 5)
            pygame.draw.circle(layer, (*shade(colour, 0.5), alpha), c, r)
            pygame.draw.circle(layer, (255, 255, 255, alpha), c, r, 1)
            label = str(mv.source + 1) if mv.kind != "if" else "IF"
            text(layer, label, c, max(8, r + 2), (255, 255, 255), bold=True, anchor="center")
            if mv.kind == "repeat" and mv.repeat_n > 1:
                text(layer, f"x{mv.repeat_n}", (c[0] + r + 1, c[1] - r - 1), max(9, r + 3), (255, 255, 255),
                     bold=True, anchor="bottomleft")


def draw_maze(surf, rect: pygame.Rect, game, now: float, *, paths: bool = True):
    m = game.maze
    geo = MazeGeometry(rect, m.size)
    cs = geo.cs
    board = pygame.Rect(geo.ox, geo.oy, cs * m.size, cs * m.size)
    pygame.draw.rect(surf, (24, 27, 35), board)
    for pl in game.players:
        pygame.draw.rect(surf, shade(hex_rgb(pl.colour), 0.35), geo.tile(pl.avatar.start))
    cr = geo.tile(m.centre)
    pulse = 0.75 + 0.25 * abs(((now * 1.5) % 2) - 1)
    pygame.draw.rect(surf, shade(GOLD, pulse), cr.inflate(-4, -4), border_radius=4)
    star(surf, cr.center, cs * 0.3, (60, 40, 0))
    wl = max(2, cs // 12)
    for y in range(m.size):
        for x in range(m.size):
            px, py = geo.ox + x * cs, geo.oy + y * cs
            if not m.can_move((x, y), "U"):
                pygame.draw.line(surf, WALL, (px, py), (px + cs, py), wl)
            if not m.can_move((x, y), "L"):
                pygame.draw.line(surf, WALL, (px, py), (px, py + cs), wl)
            if x == m.size - 1:
                pygame.draw.line(surf, WALL, (px + cs, py), (px + cs, py + cs), wl)
            if y == m.size - 1:
                pygame.draw.line(surf, WALL, (px, py + cs), (px + cs, py + cs), wl)

    if paths:
        layer = pygame.Surface(surf.get_size(), pygame.SRCALPHA)
        for pl in game.players:
            col = hex_rgb(pl.colour)
            o = PLAYER_OFFSETS[pl.idx % len(PLAYER_OFFSETS)]
            off = (o[0] * cs * 0.11, o[1] * cs * 0.11)
            plans = plan_player(game, pl)
            w = max(2, cs // 10)
            draw_plan(layer, geo, plans.current, col, off, alpha=110, width=w, highlight_first=True)
            for q in plans.queued:
                draw_plan(layer, geo, q, col, off, alpha=80, width=w)
            if plans.pending is not None:
                draw_plan(layer, geo, plans.pending, col, off, alpha=235, width=w + 2)
                end = geo.centre(plans.pending.end, off)
                pygame.draw.circle(layer, (*col, 235), end, max(4, cs // 6), 2)
        surf.blit(layer, (0, 0))

    # avatars (offset when sharing a tile)
    tiles: dict = {}
    for pl in game.players:
        tiles.setdefault(pl.avatar.pos, []).append(pl)
    offsets = {1: [(0, 0)], 2: [(-0.18, 0), (0.18, 0)], 3: [(-0.18, -0.15), (0.18, -0.15), (0, 0.18)],
               4: [(-0.18, -0.18), (0.18, -0.18), (-0.18, 0.18), (0.18, 0.18)]}
    for pos, pls in tiles.items():
        offs = offsets.get(len(pls), [(0, 0)] * len(pls))
        for pl, (dx, dy) in zip(pls, offs):
            av = pl.avatar
            rx, ry = av.render_pos(now)
            c = (geo.ox + (rx + 0.5 + dx) * cs, geo.oy + (ry + 0.5 + dy) * cs)
            rad = int(cs * (0.33 if len(pls) == 1 else 0.24))
            col = hex_rgb(pl.colour)
            pygame.draw.circle(surf, col, c, rad)
            pygame.draw.circle(surf, (255, 255, 255) if av.busy else (20, 20, 20), c, rad, 2)
            text(surf, str(pl.idx + 1), c, max(10, int(rad * 1.2)), (0, 0, 0), bold=True, anchor="center")
            if av.blocked_at and now - av.blocked_at[0] < 0.5:
                d = av.blocked_at[1]
                t = geo.tile(av.pos)
                seg = {"U": (t.topleft, t.topright), "D": (t.bottomleft, t.bottomright),
                       "L": (t.topleft, t.bottomleft), "R": (t.topright, t.bottomright)}[d]
                pygame.draw.line(surf, DANGER, seg[0], seg[1], wl + 3)
    return geo
