"""Tutorial screen (section 14 B5): controls test page, then guided lessons per player."""
from __future__ import annotations

import time
from typing import TYPE_CHECKING

import pygame

from ..nodes.physical import PhysicalNode
from ..sim.tutorial import LESSONS
from .maze_view import draw_maze
from .widgets import (ACCENT, BG, DANGER, GOLD, LINE, MUTED, OK, PANEL, TEXT, ButtonGroup, draw_card, hex_rgb,
                      seq_badge_text, text, wrap_text)

if TYPE_CHECKING:  # pragma: no cover
    from .app import App

TOP = 60
BOTTOM = 680
STATUS_COL = {"done": OK, "skipped": MUTED, "n/a": (70, 70, 80), "active": GOLD, "todo": LINE}


class TutorialScreen:
    def __init__(self, app: "App"):
        self.app = app
        self.buttons = ButtonGroup()
        self._sig = None

    @property
    def tut(self):
        return self.app.tutorial

    def _cols(self):
        n = max(1, len(self.tut.players))
        w = (1600 - 16 - 12 * (n - 1)) // n
        return [(pygame.Rect(8 + i * (w + 12), TOP, w, BOTTOM - TOP), p)
                for i, p in enumerate(sorted(self.tut.players))]

    def _rebuild(self):
        b = self.buttons
        b.clear()
        b.add((1450, 14, 134, 34), "Quit tutorial", self.app.quit_tutorial, size=15)
        if self.tut.phase == "controls":
            b.add((1270, 14, 170, 34), "Start lessons >", lambda: self.tut.start_lessons(time.monotonic()),
                  size=15, colour=(40, 90, 60))
        for r, p in self._cols():
            pt = self.tut.players[p]
            if self.tut.phase == "lessons" and not pt.finished:
                b.add((r.right - 236, r.bottom - 40, 110, 30), "Skip lesson",
                      lambda pt=pt: pt.skip(time.monotonic()), size=13)
                b.add((r.right - 120, r.bottom - 40, 110, 30), "Restart",
                      lambda pt=pt: pt.restart_lesson(time.monotonic()), size=13)

    def _signature(self):
        return (self.tut.phase, tuple((p, pt.index, pt.finished) for p, pt in self.tut.players.items()))

    def handle(self, ev):
        if self._signature() != self._sig:
            self._rebuild()
            self._sig = self._signature()
        if self.buttons.handle(ev):
            return
        if self.app.handle_node_strip(ev):
            return
        if ev.type == pygame.KEYDOWN:
            if ev.key == pygame.K_ESCAPE:
                self.app.quit_tutorial()
            elif ev.key == pygame.K_RETURN and self.tut.phase == "controls":
                self.tut.start_lessons(time.monotonic())
            else:
                self.app.node_hotkey(ev)

    # ------------------------------------------------------------------ draw
    def draw(self, surf, now):
        if self._signature() != self._sig:
            self._rebuild()
            self._sig = self._signature()
        surf.fill(BG)
        title = "Tutorial - controls test" if self.tut.phase == "controls" else "Tutorial"
        text(surf, title, (16, 14), 26, TEXT, bold=True)
        sub = ("Try every gesture: each node's raw events, register and link appear below. "
               "Press Start lessons (or Enter) when ready." if self.tut.phase == "controls" else
               "Each player follows their own lessons with their own nodes. Skip or restart any time.")
        text(surf, sub, (16, 44), 13, MUTED)
        for r, p in self._cols():
            if self.tut.phase == "controls":
                self._draw_controls_col(surf, r, p, now)
            else:
                self._draw_lesson_col(surf, r, p, now)
        self.buttons.draw(surf)
        self.app.draw_node_strip(surf, info_for_node=self._node_info)

    def _node_info(self, nid):
        p = self.tut.node_owner.get(nid)
        if p is None:
            return "not in the tutorial"
        n = self.tut.players[p].game.nodes[nid]
        return f"P{p + 1} · hand #{n.hand_no} · {n.scheme}"

    def _header(self, surf, r, p):
        pt = self.tut.players[p]
        col = hex_rgb(pt.colour)
        pygame.draw.rect(surf, PANEL, r, border_radius=10)
        pygame.draw.rect(surf, col, (r.x, r.y, r.w, 6), border_top_left_radius=10, border_top_right_radius=10)
        scheme = "Manual" if pt.manual else "Gesture"
        text(surf, f"Player {p + 1}", (r.x + 12, r.y + 12), 20, col, bold=True)
        text(surf, f"{len(pt.nodes)} node(s) · {scheme}", (r.x + 120, r.y + 17), 13, MUTED)
        return pt

    def _draw_controls_col(self, surf, r, p, now):
        pt = self._header(surf, r, p)
        y = r.y + 46
        for ns in pt.nodes:
            nid = ns.node_id
            hn = self.app.hub.nodes.get(nid)
            n = pt.game.nodes[nid]
            box = pygame.Rect(r.x + 10, y, r.w - 20, 180)
            pygame.draw.rect(surf, (22, 25, 32), box, border_radius=8)
            link = "connected" if hn and hn.connected else "OFFLINE"
            if isinstance(hn, PhysicalNode):
                link += f" · {hn.status}"
                if hn.rtt_ms is not None:
                    link += f" · RTT {hn.rtt_ms:.0f} ms"
            text(surf, f"{nid}  #{ns.hand_no}", (box.x + 10, box.y + 8), 16, TEXT, bold=True)
            text(surf, link, (box.x + 110, box.y + 10), 12, OK if hn and hn.connected else DANGER)
            # register + flip + buttons
            regs = [("R", "R" in n.register), ("L", "L" in n.register), ("V", "V" in n.register),
                    ("F", n.face_down)]
            for i, (ch, on) in enumerate(regs):
                bx = pygame.Rect(box.x + 10 + i * 34, box.y + 34, 30, 24)
                pygame.draw.rect(surf, ACCENT if on else (50, 52, 60), bx, border_radius=4)
                text(surf, ch, bx.center, 14, (0, 0, 0) if on else MUTED, bold=True, anchor="center")
            mask = self.tut.players[p].controller.prev_mask.get(nid, 0)
            for i, lab in enumerate(("TL", "TR", "BL", "BR")):
                bx = pygame.Rect(box.x + 160 + i * 38, box.y + 34, 34, 24)
                on = bool(mask & (1 << i))
                pygame.draw.rect(surf, GOLD if on else (50, 52, 60), bx, border_radius=4)
                text(surf, lab, bx.center, 12, (0, 0, 0) if on else MUTED, bold=True, anchor="center")
            text(surf, "face-down" if (hn and hn.phys_face_down) else "face-up", (box.x + 320, box.y + 38), 13,
                 MUTED)
            evs = self.tut.raw.get(nid, [])[-6:]
            for i, (ts, kind, val) in enumerate(reversed(evs)):
                age = now - ts
                c = TEXT if age < 1.5 else MUTED
                text(surf, f"{kind}:{val}" if val else kind, (box.x + 12, box.y + 68 + i * 18), 14, c, bold=age < 0.5)
                text(surf, f"{age:4.1f} s ago", (box.x + 140, box.y + 68 + i * 18), 12, MUTED)
            if not evs:
                text(surf, "no events yet - tap, flip or press a button", (box.x + 12, box.y + 70), 13, MUTED)
            y += 190
            if y > r.bottom - 180:
                break

    def _draw_lesson_col(self, surf, r, p, now):
        pt = self._header(surf, r, p)
        # progress ticks
        x = r.x + 12
        for i, st in enumerate(pt.states):
            c = STATUS_COL[st.status]
            box = pygame.Rect(x, r.y + 42, 24, 18)
            pygame.draw.rect(surf, c, box, 0 if st.status in ("done", "active") else 1, border_radius=4)
            label = "v" if st.status == "done" else ("-" if st.status in ("n/a", "skipped") else str(i + 1))
            text(surf, label, box.center, 11, (0, 0, 0) if st.status in ("done", "active") else MUTED,
                 bold=True, anchor="center")
            x += 28
        if pt.finished:
            text(surf, "Tutorial complete!", (r.centerx, r.y + 160), 26, OK, bold=True, anchor="center")
            done = sum(1 for s in pt.states if s.status == "done")
            text(surf, f"{done} lesson(s) done", (r.centerx, r.y + 196), 15, MUTED, anchor="center")
            return
        lesson = pt.lesson
        text(surf, lesson.title, (r.x + 12, r.y + 70), 19, GOLD, bold=True)
        y = wrap_text(surf, pt.instruction(), (r.x + 12, r.y + 98), r.w - 24, 17, TEXT, max_lines=5, line_h=23)
        if pt.completed_at is not None:
            text(surf, "Well done!", (r.right - 14, r.y + 70), 19, OK, bold=True, anchor="topright")
        # practice maze + hand
        avail_h = r.bottom - 50 - (y + 8) - 96
        side = max(120, min(r.w - 24, avail_h))
        mrect = pygame.Rect(r.x + (r.w - side) // 2, y + 8, side, side)
        draw_maze(surf, mrect, pt.game, now, paths=True)
        hy = mrect.bottom + 10
        pl = pt.game.players[0]
        cx = r.x + 12
        cs = 70
        for slot, nid in enumerate(pl.node_ids):
            card = pl.slots[slot]
            cr = pygame.Rect(cx, hy, cs, cs)
            draw_card(surf, cr, card.code if card else "-", max_cell=16)
            badge = seq_badge_text(pt.game.node_display(nid)["SEQ"])
            if badge:
                pygame.draw.rect(surf, GOLD if badge.startswith("HEAD") else ACCENT, cr, 2, border_radius=4)
                text(surf, badge, (cr.centerx, cr.y - 2), 11, GOLD, bold=True, anchor="midbottom")
            text(surf, nid, (cr.centerx, cr.bottom + 2), 11, MUTED, anchor="midtop")
            cx += cs + 8
        if pl.stash_enabled:
            sr = pygame.Rect(cx + 12, hy, cs, cs)
            draw_card(surf, sr, pl.stash.code if pl.stash else "-", max_cell=16)
            text(surf, "stash", (sr.centerx, sr.bottom + 2), 11, MUTED, anchor="midtop")
