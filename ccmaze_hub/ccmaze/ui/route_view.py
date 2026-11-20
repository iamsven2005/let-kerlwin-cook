"""Route displays (section 18.4): the route list next to a player's hand, and the route on the map.

Route list: the locked chunks in order (compact card icons, thin dividers between chunks), then the
pending chunk, highlighted. A FOR block gets a bracket labelled "x?" while open, "xN" once closed.
A transformed IF shows its action shape with an "IF" tag, a pending IF shows "IF?", a blank IF is
an empty card.

Map: the locked route solid from spawn; the pending chunk dashed and bright, continuing from the
route's end, with row numbers; an open FOR block's cards once, dashed and faint, marked "x?"; a
pending IF as a "?". A completed route flashes, then the avatar walks it (walked part brighter).
"""
from __future__ import annotations

import math

import pygame

from ..sim.preview import PathPlan, RoutePlan
from ..sim.resolve import BLANK, IF_PENDING, IF_SHAPE
from .widgets import ACCENT, DANGER, GOLD, LINE, MUTED, draw_card, draw_move, shade, text

FOR_COL = (0x6F, 0xCF, 0x97)
IF_COL = (0xF2, 0xC9, 0x4C)


# ------------------------------------------------------------------ route list
def route_items(game, pl, plan: RoutePlan):
    """[(written index, card, state, chunk index or None for the pending chunk)]"""
    res = plan.res_all or plan.res_locked
    chunk_of = []
    for ci, ch in enumerate(pl.route):
        chunk_of += [ci] * len(ch.cards)
    out = []
    for i, c in enumerate(res.cards):
        out.append((i, c, res.state[i], chunk_of[i] if i < len(chunk_of) else None))
    return out, res


def _draw_icon(surf, r, card, state, res, i, pending: bool):
    if state == BLANK:
        pygame.draw.rect(surf, (12, 13, 17), r, border_radius=3)
        pygame.draw.rect(surf, LINE, r, 1, border_radius=3)
        return
    if state == IF_SHAPE:
        pygame.draw.rect(surf, (12, 13, 17), r, border_radius=3)
        pygame.draw.rect(surf, IF_COL, r, 1, border_radius=3)
        draw_move(surf, r.inflate(-4, -4), res.if_action[i].steps, max_cell=max(4, r.h // 3))
        text(surf, "IF", (r.x + 2, r.y + 1), 9, IF_COL, bold=True)
        return
    draw_card(surf, r, card.code, max_cell=max(4, r.h // 3))
    if state == IF_PENDING:
        tag = pygame.Rect(r.x, r.bottom - 11, r.w, 11)
        pygame.draw.rect(surf, IF_COL, tag, border_bottom_left_radius=3, border_bottom_right_radius=3)
        text(surf, "IF?", tag.center, 9, (0, 0, 0), bold=True, anchor="center")


def draw_route_list(surf, rect: pygame.Rect, game, pl, plan: RoutePlan, *, icon: int = 30) -> None:
    items, res = route_items(game, pl, plan)
    gap, div = 3, 7
    if not items:
        text(surf, "route: empty - push a chunk to start it", (rect.x, rect.centery - 7), 13, MUTED)
        return
    # widths, right-aligned tail that fits
    xs = []
    x = 0
    prev_chunk = "start"
    for (_, _, _, ch) in items:
        if prev_chunk != "start" and ch != prev_chunk:
            x += div
        xs.append(x)
        x += icon + gap
        prev_chunk = ch
    total = x
    shift = max(0, total - rect.w + 26)
    first = next((k for k, xx in enumerate(xs) if xx >= shift), len(items))
    ox = rect.x + (26 if shift else 0) - shift
    y = rect.y + 14
    if shift:
        text(surf, f"+{first}", (rect.x, y + icon // 2 - 7), 12, MUTED)
    # pending chunk highlight
    pend = [k for k, it in enumerate(items) if it[3] is None]
    if pend:
        a = max(pend[0], first)
        x0, x1 = ox + xs[a] - 3, ox + xs[pend[-1]] + icon + 3
        hl = pygame.Rect(x0, y - 3, x1 - x0, icon + 6)
        pygame.draw.rect(surf, shade(ACCENT, 0.35), hl, border_radius=5)
        pygame.draw.rect(surf, ACCENT, hl, 1, border_radius=5)
    prev_chunk = "start"
    for k, (i, c, state, ch) in enumerate(items):
        if prev_chunk != "start" and ch != prev_chunk and ch is not None and k >= first:
            dx = ox + xs[k] - div // 2 - 2
            pygame.draw.line(surf, LINE, (dx, y - 2), (dx, y + icon + 2), 1)
        prev_chunk = ch
        if k < first:
            continue
        _draw_icon(surf, pygame.Rect(ox + xs[k], y, icon, icon), c, state, res, i, ch is None)
    # FOR brackets
    for b in res.blocks:
        a_k = b.open
        z_k = b.close if b.close is not None else len(items) - 1
        if z_k < first:
            continue
        xa = ox + xs[max(a_k, first)] + 2
        xz = ox + xs[z_k] + icon - 2
        by = y - 8
        colr = FOR_COL if b.close is not None else GOLD
        pygame.draw.line(surf, colr, (xa, by), (xz, by), 2)
        if a_k >= first:
            pygame.draw.line(surf, colr, (xa, by), (xa, by + 5), 2)
        if b.close is not None:
            pygame.draw.line(surf, colr, (xz, by), (xz, by + 5), 2)
        label = f"x{b.n}" if b.close is not None else "x?"
        text(surf, label, ((xa + xz) // 2, by - 1), 11, colr, bold=True, anchor="midbottom")


# ------------------------------------------------------------------ map
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


def _stubs(layer, geo, pm, off, alpha, width):
    for (p, d) in pm.blocked:
        c = geo.centre(p, off)
        dx, dy = {"U": (0, -1), "D": (0, 1), "L": (-1, 0), "R": (1, 0)}[d]
        pygame.draw.line(layer, (*DANGER, min(255, alpha + 40)), c,
                         (c[0] + dx * geo.cs * 0.42, c[1] + dy * geo.cs * 0.42), width + 1)


def _marker(layer, geo, pos, off, colour, alpha, label, extra=""):
    c = geo.centre(pos, off)
    r = max(6, geo.cs // 5)
    pygame.draw.circle(layer, (*shade(colour, 0.5), alpha), c, r)
    pygame.draw.circle(layer, (255, 255, 255, alpha), c, r, 1)
    text(layer, label, c, max(8, r + 2), (255, 255, 255), bold=True, anchor="center")
    if extra:
        text(layer, extra, (c[0] + r + 1, c[1] - r - 1), max(9, r + 3), (255, 255, 255), bold=True,
             anchor="bottomleft")


def draw_trace(layer, geo, plan: PathPlan, colour, off, *, alpha: int, width: int, style: str = "solid",
               numbers_from: int | None = None, walked_steps: int = -1):
    """style: "solid" (locked), "dash" (pending), "faint" (open FOR block, once)."""
    if plan is None:
        return
    k = 0
    seen_src = set()
    xq_done = False
    for pm in plan.moves:
        mv = pm.move
        for (a, b) in pm.steps:
            pa, pb = geo.centre(a, off), geo.centre(b, off)
            col = (*colour, alpha)
            if walked_steps >= 0 and k < walked_steps:
                col = (*shade(colour, 1.35), 255)
            if style == "solid" and mv.kind != "if":
                pygame.draw.line(layer, col, pa, pb, width)
            else:
                _dashed(layer, col, pa, pb, width, max(4, geo.cs // 6))
            k += 1
        _stubs(layer, geo, pm, off, alpha, width)
        if numbers_from is not None and mv.source not in seen_src:
            seen_src.add(mv.source)
            label = str(mv.source - numbers_from + 1) if mv.source >= numbers_from else ""
            if mv.kind == "if":
                label = "IF"
            extra = ""
            if mv.repeat_n == 0 and not xq_done:
                extra = "x?"
                xq_done = True
            elif mv.repeat_n > 1 and mv.repeat_i == 1 and mv.via not in seen_src:
                extra = f"x{mv.repeat_n}"         # once per block, on its first card
                seen_src.add(mv.via)
            if label or extra:
                _marker(layer, geo, pm.start, off, colour, alpha, label or "", extra)


def draw_route(layer, geo, game, pl, plan: RoutePlan, colour, off, now: float) -> None:
    cs = geo.cs
    w = max(2, cs // 10)
    av = pl.avatar
    locked_alpha = 200
    if pl.walking and av.walk_start is not None and now < av.walk_start:
        # 18.4: flash the completed route before the walk
        locked_alpha = 255 if int((now - (pl.complete_at or 0)) * 6) % 2 == 0 else 90
    walked = av.steps_taken if pl.walking else -1
    draw_trace(layer, geo, plan.locked, colour, off, alpha=locked_alpha, width=w, walked_steps=walked)
    # chunk boundaries on the locked route: small dots where each chunk starts
    if plan.pending is not None:
        draw_trace(layer, geo, plan.pending, colour, off, alpha=245, width=w + 1, style="dash",
                   numbers_from=plan.n_locked)
    if plan.tentative is not None:
        # the open block's cards once, faint; the first one marked "x?" (numbers for pending cards)
        draw_trace(layer, geo, plan.tentative, colour, off, alpha=120, width=w, style="faint",
                   numbers_from=plan.n_locked)
    res = plan.res_all or plan.res_locked
    if res.pending_if is not None:
        tail = plan.tentative or plan.pending or plan.locked
        c = geo.centre(tail.end, off)
        r = max(7, cs // 4)
        pygame.draw.circle(layer, (*IF_COL, 230), c, r)
        text(layer, "?", c, max(10, r + 4), (0, 0, 0), bold=True, anchor="center")
    if not pl.walking:
        end = geo.centre(plan.end, off)
        pygame.draw.circle(layer, (*colour, 235), end, max(4, cs // 6), 2)
    if plan.preview_complete:
        c = geo.centre(game.maze.centre, (0, 0))
        pygame.draw.circle(layer, (*colour, 255), c, int(cs * 0.45), 3)
