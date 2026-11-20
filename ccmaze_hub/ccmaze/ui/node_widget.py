"""On-screen node widget: shows exactly what a node shows; virtual nodes get controls."""
from __future__ import annotations

import time

import pygame

from ..nodes.base import Node
from ..nodes.virtual import VirtualNode
from .widgets import (DANGER, LINE, MUTED, OK, PANEL, TEXT, ButtonGroup, draw_latch_bar, draw_node_screen,
                      hex_rgb, mouse_pos, text)

W, H = 252, 262
SCREEN_W, SCREEN_H = 192, 128


# default meanings of the corner buttons (gesture_button_map / manual_button_map, 18.5 / 18.7)
# 21.1: one button per primitive, the same in both modes
BUTTON_TIPS = {"TL": "button TL: V  (Q)", "TR": "button TR: F, a flip  (E)",
               "BL": "button BL: L  (Z)", "BR": "button BR: R  (C)"}


class NodeWidget:
    def __init__(self, node: Node, pos, warn_percent=lambda: 20):
        self.node = node
        self.warn_percent = warn_percent
        self.rect = pygame.Rect(pos[0], pos[1], W, H)
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
                      tooltip=BUTTON_TIPS[lab])
            by = sr.bottom + 8
            # 18.8: four buttons (corners) plus a flip toggle; no tilt or bump controls
            self.flip_btn = g.add((x + 6, by, 150, 26), "Flip", lambda: n.toggle_flip(), size=12,
                                  tooltip="turn the node over (Gesture mode only)  (F / Space)")
            # 21.1: a mouse can't press two corners at once, so a helper for the clear
            g.add((x + 160, by, 86, 26), "clear", lambda: n.click_buttons(0xC), size=12,
                  tooltip="two buttons together (BL+BR): clear the latched register  (X)")
        else:
            by = sr.bottom + 8
            g.add((x + 6, by, 78, 26), "BLINK", lambda: n.blink(), size=13)

    def move(self, pos):
        self.rect.topleft = pos
        self._build()

    # ------------------------------------------------------------------ draw
    def draw(self, surf, selected: bool, info: str = "", hotkey: str = "", latch=None):
        """latch: (bits, seconds left, latch seconds) for the 19.2 countdown bar, or None."""
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
        if latch and latch[0] and latch[1] > 0:
            draw_latch_bar(surf, pygame.Rect(self.screen_rect.x, self.screen_rect.bottom + 2,
                                             self.screen_rect.w, 5), latch)
        if info:
            text(surf, info, (self.rect.x + 8, self.rect.bottom - 22), 12, MUTED)
        if not isinstance(n, VirtualNode):
            extra = getattr(n, "status", "")
            fw = getattr(n, "fw_version", "")
            rssi = getattr(n, "rssi", None)
            line = f"{extra}  fw {fw or '?'}  RSSI {rssi if rssi is not None else '?'}"
            text(surf, line, (self.rect.x + 8, self.screen_rect.bottom + 40), 12, MUTED)

    # ------------------------------------------------------------------ input
    def handle(self, ev, widgets=None) -> bool:
        return self.group.handle(ev)
