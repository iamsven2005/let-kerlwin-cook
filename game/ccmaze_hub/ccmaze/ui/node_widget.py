"""On-screen node widget: shows exactly what a node shows; virtual nodes get controls."""
from __future__ import annotations

import time

import pygame

from ..nodes.base import Node
from ..nodes.virtual import VirtualNode
from .widgets import DANGER, LINE, MUTED, OK, PANEL, TEXT, ButtonGroup, draw_node_screen, hex_rgb, mouse_pos, text

W, H = 252, 262
SCREEN_W, SCREEN_H = 192, 128


class NodeWidget:
    def __init__(self, node: Node, pos, on_bump_drop=None, warn_percent=lambda: 20):
        self.node = node
        self.warn_percent = warn_percent
        self.rect = pygame.Rect(pos[0], pos[1], W, H)
        self.on_bump_drop = on_bump_drop
        self.dragging = False
        self.flash_until = 0.0
        self.group = ButtonGroup()
        self._build()

    @property
    def screen_rect(self) -> pygame.Rect:
        return pygame.Rect(self.rect.x + 30, self.rect.y + 24, SCREEN_W, SCREEN_H)

    def _build(self):
        g = self.group
        g.clear()
        x, y = self.rect.x, self.rect.y
        sr = self.screen_rect
        n = self.node
        if isinstance(n, VirtualNode):
            corners = [("TL", 0x1, (x + 3, sr.y)), ("TR", 0x2, (sr.right + 4, sr.y)),
                       ("BL", 0x4, (x + 3, sr.bottom - 22)), ("BR", 0x8, (sr.right + 4, sr.bottom - 22))]
            for lab, mask, p in corners:
                g.add((p[0], p[1], 24, 22), lab, size=10,
                      on_press=lambda m=mask: n.press(m), on_release=lambda m=mask: n.release(m),
                      tooltip=f"button {lab}")
            by = sr.bottom + 8
            g.add((x + 6, by, 78, 26), "< tap L", lambda: n.tap("L"), size=13)
            g.add((x + 87, by, 78, 26), "tap V", lambda: n.tap("V"), size=13)
            g.add((x + 168, by, 78, 26), "tap R >", lambda: n.tap("R"), size=13)
            by += 31
            self.flip_btn = g.add((x + 6, by, 104, 26), "Flip", lambda: n.toggle_flip(), size=13)
            g.add((x + 113, by, 64, 26), "TL+TR", lambda: n.click_buttons(0x3), size=12)
            self.bump_rect = pygame.Rect(x + 180, by, 66, 26)
        else:
            by = sr.bottom + 8
            g.add((x + 6, by, 78, 26), "BLINK", lambda: n.blink(), size=13)
            self.bump_rect = None

    def move(self, pos):
        self.rect.topleft = pos
        self._build()

    # ------------------------------------------------------------------ draw
    def draw(self, surf, selected: bool, info: str = "", hotkey: str = ""):
        n = self.node
        disp = getattr(n, "display", None) or dict(n.sent)
        col = hex_rgb(disp.get("COL", "808080"))
        pygame.draw.rect(surf, PANEL, self.rect, border_radius=8)
        pygame.draw.rect(surf, col if selected else LINE, self.rect, 3 if selected else 1, border_radius=8)
        if time.monotonic() < self.flash_until:
            pygame.draw.rect(surf, (255, 255, 255), self.rect, 2, border_radius=8)
        title = f"{hotkey + ' ' if hotkey else ''}{n.node_id}"
        text(surf, title, (self.rect.x + 8, self.rect.y + 4), 14, TEXT, bold=True)
        kind = "virtual" if isinstance(n, VirtualNode) else "BLE"
        status_col = OK if n.connected else DANGER
        label = kind if n.connected else f"{kind} offline"
        if n.battery is not None:
            low = n.battery < self.warn_percent()
            text(surf, f"{n.battery}%", (self.rect.right - 70, self.rect.y + 5), 12, DANGER if low else MUTED,
                 bold=low, anchor="topright")
        text(surf, label, (self.rect.right - 8, self.rect.y + 5), 12, status_col, anchor="topright")
        blink = time.monotonic() < n.blink_until and int(time.monotonic() * 6) % 2 == 0
        face_down = n.face_down if isinstance(n, VirtualNode) else getattr(n, "phys_face_down", False)
        draw_node_screen(surf, self.screen_rect, disp, connected=n.connected, blink=blink, face_down=face_down)
        if isinstance(n, VirtualNode):
            self.flip_btn.label = "Flip (is down)" if n.face_down else "Flip (is up)"
            self.flip_btn.toggled = n.face_down
            for b, mask in zip(self.group.buttons[:4], (1, 2, 4, 8)):
                b.toggled = bool(n.buttons & mask)
        self.group.draw(surf)
        if self.bump_rect is not None:
            r = self.bump_rect
            pygame.draw.rect(surf, (60, 52, 30) if not self.dragging else (120, 100, 40), r, border_radius=5)
            pygame.draw.rect(surf, LINE, r, 1, border_radius=5)
            text(surf, "bump <->", r.center, 12, TEXT, anchor="center")
        if info:
            text(surf, info, (self.rect.x + 8, self.rect.bottom - 22), 12, MUTED)
        if not isinstance(n, VirtualNode):
            extra = getattr(n, "status", "")
            fw = getattr(n, "fw_version", "")
            rssi = getattr(n, "rssi", None)
            line = f"{extra}  fw {fw or '?'}  RSSI {rssi if rssi is not None else '?'}"
            text(surf, line, (self.rect.x + 8, self.screen_rect.bottom + 40), 12, MUTED)

    def draw_drag(self, surf):
        if self.dragging and self.bump_rect is not None:
            pygame.draw.line(surf, (240, 200, 80), self.bump_rect.center, mouse_pos(), 3)

    # ------------------------------------------------------------------ input
    def handle(self, ev, widgets) -> bool:
        if self.group.handle(ev):
            return True
        if self.bump_rect is not None:
            if ev.type == pygame.MOUSEBUTTONDOWN and ev.button == 1 and self.bump_rect.collidepoint(ev.pos):
                self.dragging = True
                return True
            if ev.type == pygame.MOUSEBUTTONUP and ev.button == 1 and self.dragging:
                self.dragging = False
                for w in widgets:
                    if w is not self and w.rect.collidepoint(ev.pos) and self.on_bump_drop:
                        self.on_bump_drop(self, w, ev.pos)   # left/right half = which end it bumped
                        break
                return True
        return False
