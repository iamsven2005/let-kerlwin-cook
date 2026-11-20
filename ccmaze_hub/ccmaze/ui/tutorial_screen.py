"""Tutorial screen (section 14 B5): controls test page, then guided lessons per player."""
from __future__ import annotations

import time
from typing import TYPE_CHECKING

import pygame

from ..nodes.physical import PhysicalNode
from ..sim.tutorial import BUTTON_ITEMS, LESSONS, button_meanings
from .maze_view import draw_maze
from .route_view import draw_route_list
from .widgets import (ACCENT, BG, DANGER, GOLD, LINE, MUTED, OK, PANEL, TEXT, ButtonGroup, draw_card, hex_rgb,
                      draw_latch_bar, seq_badge_text, shade, text, wrap_text)

if TYPE_CHECKING:  # pragma: no cover
    from .app import App

TOP = 60
BOTTOM = 680
NODE_BOX_H = 182
NODE_BOX_STEP = 190
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
        sub = ("Press each button alone and two together: they show V, F, L, R and clear. "
               "Turn each node over for the flip check. "
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

    @staticmethod
    def _node_boxes(r, pt):
        out = []
        y = r.y + 46
        for ns in pt.nodes:
            if y + NODE_BOX_H > r.bottom:
                break
            out.append((ns.node_id, pygame.Rect(r.x + 10, y, r.w - 20, NODE_BOX_H)))
            y += NODE_BOX_STEP
        return out

    def _draw_controls_col(self, surf, r, p, now):
        pt = self._header(surf, r, p)
        hands = {ns.node_id: ns.hand_no for ns in pt.nodes}
        for nid, box in self._node_boxes(r, pt):
            hn = self.app.hub.nodes.get(nid)
            n = pt.game.nodes[nid]
            pygame.draw.rect(surf, (22, 25, 32), box, border_radius=8)
            link = "connected" if hn and hn.connected else "OFFLINE"
            if isinstance(hn, PhysicalNode):
                link += f" · {hn.status}"
                if hn.rtt_ms is not None:
                    link += f" · RTT {hn.rtt_ms:.0f} ms"
            text(surf, f"{nid}  #{hands[nid]}", (box.x + 10, box.y + 6), 16, TEXT, bold=True)
            text(surf, link, (box.x + 110, box.y + 8), 12, OK if hn and hn.connected else DANGER)
            # register + flip + buttons
            regs = [("R", "R" in n.register), ("L", "L" in n.register), ("V", "V" in n.register),
                    ("F", n.face_down)]
            for i, (ch, on) in enumerate(regs):
                bx = pygame.Rect(box.x + 10 + i * 34, box.y + 30, 30, 22)
                pygame.draw.rect(surf, ACCENT if on else (50, 52, 60), bx, border_radius=4)
                text(surf, ch, bx.center, 14, (0, 0, 0) if on else MUTED, bold=True, anchor="center")
            mask = self.tut.players[p].controller.prev_mask.get(nid, 0)
            for i, lab in enumerate(("TL", "TR", "BL", "BR")):
                bx = pygame.Rect(box.x + 160 + i * 38, box.y + 30, 34, 22)
                on = bool(mask & (1 << i))
                pygame.draw.rect(surf, GOLD if on else (50, 52, 60), bx, border_radius=4)
                text(surf, lab, bx.center, 12, (0, 0, 0) if on else MUTED, bold=True, anchor="center")
            text(surf, "face-down" if (hn and hn.phys_face_down) else "face-up", (box.x + 320, box.y + 33), 13,
                 MUTED)
            # 18.8 live indicators: what the input resolved to (L / R / V / F / clear)
            lights = [(lab, self.tut.lit(nid, lab, now)) for lab in ("V", "F", "L", "R", "clear")]   # 21.1 order
            for i, (lab, on) in enumerate(lights):
                bx = pygame.Rect(box.x + 10 + i * 58, box.y + 57, 54, 20)
                pygame.draw.rect(surf, OK if on else (44, 46, 54), bx, border_radius=10)
                text(surf, lab, bx.center, 11, (0, 0, 0) if on else MUTED, bold=True, anchor="center")
            # 18.8 button check (each button and the top chord) and physical flip check
            meanings = button_meanings(self.app.cfg, n.scheme)
            chk = self.tut.checks.get(nid, set())
            cx = box.x + 10
            for lab in BUTTON_ITEMS:                         # 21.1: V, F, L, R, clear
                ok = lab in chk
                w = 72 if lab == "2+" else 50
                bx = pygame.Rect(cx, box.y + 84, w, 22)
                pygame.draw.rect(surf, shade(OK, 0.5) if ok else (36, 38, 46), bx, border_radius=4)
                pygame.draw.rect(surf, OK if ok else LINE, bx, 1, border_radius=4)
                chip = "two = clear" if lab == "2+" else f"{lab}={meanings[lab]}"
                text(surf, chip, bx.center, 11, TEXT if ok else MUTED, bold=ok, anchor="center")
                cx += w + 3
            fx = box.x + 10 + 5 * 58 + 8
            if n.scheme == "gesture":
                for lab, txt_ in (("F:1", "down"), ("F:0", "up")):
                    ok = lab in chk
                    bx = pygame.Rect(fx, box.y + 57, 66, 20)
                    pygame.draw.rect(surf, shade(OK, 0.5) if ok else (36, 38, 46), bx, border_radius=4)
                    pygame.draw.rect(surf, OK if ok else LINE, bx, 1, border_radius=4)
                    text(surf, f"flip {txt_}", bx.center, 11, TEXT if ok else MUTED, bold=ok, anchor="center")
                    fx += 70
            latch = self.app.latch_info(nid, now)
            if latch:                                        # 19.2 countdown
                draw_latch_bar(surf, pygame.Rect(box.x + 10, box.y + 54, 4 * 34 - 4, 3), latch)
            done_b, done_f = self.tut.check_done(nid)
            if done_b and (done_f or n.scheme == "manual"):
                text(surf, "all checks OK", (fx + 10, box.y + 59), 13, OK, bold=True)
            evs = self.tut.raw.get(nid, [])[-3:]
            for i, (ts, kind, val) in enumerate(reversed(evs)):
                age = now - ts
                c = TEXT if age < 1.5 else MUTED
                ey = box.y + 114 + i * 20
                text(surf, f"{kind}:{val}" if val else kind, (box.x + 12, ey), 14, c, bold=age < 0.5)
                text(surf, f"{age:4.1f} s ago", (box.x + 140, ey), 12, MUTED)
            if not evs:
                text(surf, "no events yet - press each button, then two together, and turn the node over",
                     (box.x + 12, box.y + 116), 13, MUTED)

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
            cx = sr.right + 8
        # 18.4: the route so far, then the pending chunk
        plan = pt.game.route_plan(pl)
        rw = r.right - 12 - (cx + 14)
        if plan is not None and rw > 80:
            text(surf, "Route", (cx + 14, hy - 2), 11, MUTED)
            draw_route_list(surf, pygame.Rect(cx + 14, hy + 12, rw, 42), pt.game, pl, plan, icon=26)
