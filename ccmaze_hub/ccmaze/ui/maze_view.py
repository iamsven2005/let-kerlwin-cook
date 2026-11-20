"""Maze rendering with avatars and each player's route (section 18.4).

Used by the game screen and by the tutorial's practice mazes. Routes are drawn by route_view:
locked route solid from spawn, pending chunk dashed, open FOR block faint with "x?", pending IF
"?". Each player's overlay is offset a little so overlapping routes stay readable.
"""
from __future__ import annotations

import pygame

from .route_view import draw_route
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
            plan = game.route_plan(pl)
            if plan is not None:
                draw_route(layer, geo, game, pl, plan, col, off, now)
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
            pygame.draw.circle(surf, (255, 255, 255) if av.walking else (20, 20, 20), c, rad, 2)
            text(surf, str(pl.idx + 1), c, max(10, int(rad * 1.2)), (0, 0, 0), bold=True, anchor="center")
            if av.blocked_at and now - av.blocked_at[0] < 0.5:
                d = av.blocked_at[1]
                t = geo.tile(av.pos)
                seg = {"U": (t.topleft, t.topright), "D": (t.bottomleft, t.bottomright),
                       "L": (t.topleft, t.bottomleft), "R": (t.topright, t.bottomright)}[d]
                pygame.draw.line(surf, DANGER, seg[0], seg[1], wl + 3)
    return geo
