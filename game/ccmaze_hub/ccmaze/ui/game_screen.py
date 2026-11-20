"""Shared game screen: maze, player panels, deck counts, node strip, win overlay."""
from __future__ import annotations

import time
from typing import TYPE_CHECKING

import pygame

from ..sim.game import ENDED
from ..sim.preview import plan_player
from .maze_view import draw_maze
from .widgets import (ACCENT, BG, DANGER, GOLD, LINE, MUTED, PANEL, PANEL_HI, TEXT, ButtonGroup, draw_card,
                      hex_rgb, seq_badge_text, shade, text, triangle)

if TYPE_CHECKING:  # pragma: no cover
    from .app import App

MAZE_RECT = pygame.Rect(16, 16, 600, 600)
PANELS_RECT = pygame.Rect(636, 16, 948, 600)

HELP = [
    "Virtual node keyboard shortcuts (act on the selected node):",
    "  1-6 / Tab   select node            A / W(or S) / D   tap L / V / R",
    "  F or Space  toggle flip            Q E Z C   press button TL TR BL BR (hold = long press)",
    "  X           press TL+TR together   [ / ]   bump against the LEFT / RIGHT end of the row",
    "Mouse: drag 'bump <->' onto the left or right half of another node to bump that side.",
    "Gesture: flip down = discard | V, flip down = start | V, then bump an end = add there",
    "         member: flip down + up = remove | flip head up = push | button on head = cancel",
    "         L, flip down = stash | V on head = use stash | R + L = discard stash",
    "Manual:  V, F = start | V, L/R button = add left/right | F on head = push",
    "         hold F on member = remove | hold F on head (or 2 buttons) = cancel",
    "F12: BLE debug view    Esc: back to lobby    H: toggle this help",
]


class GameScreen:
    def __init__(self, app: "App"):
        self.app = app
        self.buttons = ButtonGroup()
        self.overlay = ButtonGroup()
        self.show_help = False
        self._build()

    def _build(self):
        b = self.buttons
        b.clear()
        b.add((1430, 630, 72, 28), "Lobby", self.app.to_lobby, size=13)
        b.add((1508, 630, 76, 28), "Settings", lambda: self.app.open_settings(back="game"), size=13)
        b.add((1352, 630, 72, 28), "Help (H)", self._toggle_help, size=13)
        o = self.overlay
        o.clear()
        o.add((560, 470, 200, 44), "Rematch (same maze)", self.app.rematch, size=15)
        o.add((780, 470, 140, 44), "New maze", self.app.new_maze, size=15)
        o.add((940, 470, 120, 44), "Lobby", self.app.to_lobby, size=15)

    def _toggle_help(self):
        self.show_help = not self.show_help

    # ------------------------------------------------------------------ input
    def handle(self, ev) -> None:
        g = self.app.game
        if g and g.state == ENDED and self.overlay.handle(ev):
            return
        if self.buttons.handle(ev):
            return
        if self.app.handle_node_strip(ev):
            return
        if ev.type == pygame.KEYDOWN:
            if ev.key == pygame.K_ESCAPE:
                self.app.to_lobby()
            elif ev.key == pygame.K_h:
                self._toggle_help()
            else:
                self.app.node_hotkey(ev)

    # ------------------------------------------------------------------ draw
    def draw(self, surf, now: float):
        g = self.app.game
        surf.fill(BG)
        if g is None:
            return
        self._draw_maze(surf, g, now)
        self._draw_panels(surf, g, now)
        # info bar
        y = 630
        el = (g.win_time if g.state == ENDED and g.win_time else now) - self.app.game_started_at
        info = (f"Deck {len(g.deck.draw_pile)}   Discard {len(g.deck.discard_pile)}   "
                f"Deck size {g.deck.size}   Maze seed {g.maze.seed}   "
                f"Shortest path {g.path_length}   Time {int(el // 60)}:{int(el % 60):02d}")
        text(surf, info, (MAZE_RECT.x, y + 4), 15, TEXT)
        last = g.log[-1].text if g.log else ""
        text(surf, last, (MAZE_RECT.x, y + 28), 13, MUTED)
        text(surf, "1-6 select · A/W/D tap · F flip · Q E Z C buttons (hold) · X two buttons · [ ] bump an end · "
                   "F12 BLE · H help", (640, y + 28), 13, MUTED)
        self.buttons.draw(surf)
        self.app.draw_node_strip(surf, info_for_node=self._node_info)
        if self.show_help:
            self._draw_help(surf)
        if g.state == ENDED:
            self._draw_win(surf, g)

    def _node_info(self, node_id: str) -> str:
        g = self.app.game
        n = g.nodes.get(node_id) if g else None
        if n is None:
            return "not in this game"
        return f"P{n.player + 1} · hand #{n.hand_no} · {n.scheme}"

    def _draw_maze(self, surf, g, now):
        draw_maze(surf, MAZE_RECT, g, now, paths=True)

    def _draw_panels(self, surf, g, now):
        n = len(g.players)
        ph = min(196, (PANELS_RECT.h - (n - 1) * 6) // n)
        for i, pl in enumerate(g.players):
            r = pygame.Rect(PANELS_RECT.x, PANELS_RECT.y + i * (ph + 6), PANELS_RECT.w, ph)
            self._draw_panel(surf, g, pl, r, now)

    def _draw_panel(self, surf, g, pl, r, now):
        """Hand (with row badges) and stash; the sequence itself is drawn on the map (14 B4)."""
        col = hex_rgb(pl.colour)
        win = g.state == ENDED and pl.idx in g.winners
        pygame.draw.rect(surf, PANEL if not win else shade(col, 0.4), r, border_radius=8)
        pygame.draw.rect(surf, col, (r.x, r.y, 8, r.h), border_top_left_radius=8, border_bottom_left_radius=8)
        av = pl.avatar
        text(surf, pl.name, (r.x + 16, r.y + 5), 18, col, bold=True)
        state = "finished" if g.state == ENDED else ("moving" if av.busy else "idle")
        n_seq = (1 if av.current else 0) + len(av.queue)
        status = f"at {av.pos}   steps {av.steps_taken}   {state}   queue: {n_seq} seq / {av.queued_card_count()} cards"
        text(surf, status, (r.x + 60, r.y + 8), 13, MUTED)
        low = [nid for nid in pl.node_ids if (hn := self.app.hub.nodes.get(nid)) is not None
               and hn.battery is not None and hn.battery < self.app.cfg.battery_warn_percent]
        if low:
            text(surf, "LOW BATTERY: " + ", ".join(low), (r.right - 12, r.y + 26), 13, DANGER, bold=True,
                 anchor="topright")
        if win:
            text(surf, "WINNER", (r.right - 12, r.y + 5), 18, GOLD, bold=True, anchor="topright")

        big = r.h >= 170
        cs = 84 if big else 70
        mc = 20 if big else 16
        y1 = r.y + 34
        text(surf, "Hand", (r.x + 16, y1 + cs // 2 - 8), 13, MUTED)
        x = r.x + 60
        seq = pl.sequence
        for slot, nid in enumerate(pl.node_ids):
            card = pl.slots[slot]
            node = g.nodes[nid]
            cr = pygame.Rect(x, y1, cs, cs)
            draw_card(surf, cr, card.code if card else "-", max_cell=mc)
            badge = seq_badge_text(g.node_display(nid)["SEQ"])
            if badge:
                head = badge.startswith("HEAD")
                pygame.draw.rect(surf, GOLD if head else ACCENT, cr, 2, border_radius=4)
                bw = 52 if head else 26
                bb = pygame.Rect(cr.right - bw + 4, cr.y - 7, bw, 15)
                pygame.draw.rect(surf, GOLD if head else ACCENT, bb, border_radius=3)
                text(surf, badge, bb.center, 11, (0, 0, 0), bold=True, anchor="center")
            text(surf, f"#{node.hand_no}", (cr.x + 3, cr.y + 2), 10, MUTED)
            if node.face_down:
                triangle(surf, (cr.x + 8, cr.bottom - 8), "D", 4, MUTED)
            if not node.connected:
                pygame.draw.line(surf, DANGER, cr.topleft, cr.bottomright, 3)
                pygame.draw.line(surf, DANGER, cr.topright, cr.bottomleft, 3)
            hub_node = self.app.hub.nodes.get(nid)
            if hub_node is not None and hub_node.battery is not None:
                lowb = hub_node.battery < self.app.cfg.battery_warn_percent
                text(surf, f"{hub_node.battery}%", (cr.centerx, cr.bottom + 1), 10, DANGER if lowb else MUTED,
                     bold=lowb, anchor="midtop")
            if node.register:
                regs = "".join(b for b in "RLV" if b in node.register)
                text(surf, regs, (cr.right - 3, cr.bottom - 1), 11, ACCENT, bold=True, anchor="bottomright")
            x += cs + 8
        if pl.stash_enabled:
            x += 14
            sr = pygame.Rect(x, y1, cs, cs)
            used = bool(seq and seq.has_stash)
            pygame.draw.rect(surf, PANEL_HI, sr.inflate(6, 6), border_radius=6)
            draw_card(surf, sr, pl.stash.code if pl.stash else "-", max_cell=mc, dim=used)
            text(surf, "stash", (sr.centerx, sr.bottom + 3), 10, MUTED, anchor="midtop")
            if used:
                pygame.draw.rect(surf, ACCENT, sr, 2, border_radius=4)
            x += cs + 10
        # short summary of the row on the table (its path is on the map)
        if seq:
            plan = plan_player(g, pl).pending
            res = plan.resolution if plan else None
            parts = [f"row: {len(seq.entries)} card(s) -> {len(res.moves) if res else 0} move(s)"]
            if res and res.if_effective:
                parts.append(f"IF fires x{len(res.if_effective)}")
            if res and res.for_effective:
                parts.append(f"FOR x{res.for_effective[0][1]}")
            text(surf, "   ".join(parts), (x + 16, y1 + cs // 2 - 8), 13, TEXT)

    def _draw_help(self, surf):
        r = pygame.Rect(260, 140, 1080, 330)
        pygame.draw.rect(surf, (10, 12, 18), r, border_radius=10)
        pygame.draw.rect(surf, ACCENT, r, 2, border_radius=10)
        for i, line in enumerate(HELP):
            text(surf, line, (r.x + 24, r.y + 20 + i * 29), 16, TEXT if i else ACCENT, bold=(i == 0))

    def _draw_win(self, surf, g):
        shadeov = pygame.Surface((1600, 960), pygame.SRCALPHA)
        shadeov.fill((0, 0, 0, 150))
        surf.blit(shadeov, (0, 0))
        r = pygame.Rect(500, 300, 620, 240)
        pygame.draw.rect(surf, PANEL, r, border_radius=12)
        col = hex_rgb(g.players[g.winners[0]].colour) if g.winners else GOLD
        pygame.draw.rect(surf, col, r, 3, border_radius=12)
        names = " & ".join(f"Player {w + 1}" for w in g.winners)
        title = f"{names} win{'s' if len(g.winners) == 1 else ''}!"
        if len(g.winners) > 1:
            title = f"Draw: {names}!"
        text(surf, title, (r.centerx, r.y + 40), 34, col, bold=True, anchor="midtop")
        el = g.win_time - self.app.game_started_at if g.win_time else 0
        text(surf, f"reached the centre after {el:.1f} s", (r.centerx, r.y + 100), 16, MUTED, anchor="midtop")
        self.overlay.draw(surf)
