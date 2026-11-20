"""Small immediate-ish widget kit for Pygame plus shared drawing helpers."""
from __future__ import annotations

from typing import Callable, Optional

import pygame

from ..sim.cards import DIRS, CardType, parse_code

# ---------------------------------------------------------------- theme
BG = (18, 20, 26)
PANEL = (30, 33, 42)
PANEL_HI = (42, 46, 58)
LINE = (70, 76, 92)
TEXT = (226, 230, 238)
MUTED = (140, 148, 165)
ACCENT = (110, 170, 255)
DANGER = (235, 87, 87)
OK = (80, 200, 120)
GOLD = (245, 200, 66)
WALL = (205, 210, 222)

_fonts: dict[tuple[int, bool], pygame.font.Font] = {}

# window -> canvas mapping (offset x, offset y, scale), set by the app every frame
MOUSE_XFORM = (0, 0, 1.0)


def mouse_pos() -> tuple[int, int]:
    ox, oy, s = MOUSE_XFORM
    x, y = pygame.mouse.get_pos()
    return int((x - ox) / s), int((y - oy) / s)


def font(size: int, bold: bool = False) -> pygame.font.Font:
    key = (size, bold)
    if key not in _fonts:
        f = None
        # Linux: DejaVu; Windows: Segoe UI / Arial. UI text sticks to glyphs all of them have
        # (Latin-1 plus dashes and the ellipsis); arrows and the star are drawn as polygons.
        for name in ("dejavusans", "segoeui", "arial", "helvetica", "freesans"):
            try:
                path = pygame.font.match_font(name, bold=bold)
            except Exception:
                path = None
            if path:
                f = pygame.font.Font(path, size)
                break
        _fonts[key] = f or pygame.font.Font(None, int(size * 1.3))
    return _fonts[key]


def hex_rgb(h: str, default=(128, 128, 128)) -> tuple[int, int, int]:
    try:
        return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16))
    except (ValueError, IndexError, TypeError):
        return default


def shade(c, f: float):
    return tuple(max(0, min(255, int(v * f))) for v in c)


def text(surf, s: str, pos, size=16, colour=TEXT, bold=False, anchor="topleft"):
    img = font(size, bold).render(str(s), True, colour)
    r = img.get_rect(**{anchor: pos})
    surf.blit(img, r)
    return r


def wrap_text(surf, s: str, pos, width: int, size=14, colour=TEXT, max_lines=4, line_h=None):
    """Draw word-wrapped text; returns the y below the last line."""
    f = font(size)
    words, lines, cur = s.split(), [], ""
    for w in words:
        trial = (cur + " " + w).strip()
        if f.size(trial)[0] <= width or not cur:
            cur = trial
        else:
            lines.append(cur)
            cur = w
    if cur:
        lines.append(cur)
    line_h = line_h or int(size * 1.35)
    x, y = pos
    for ln in lines[:max_lines]:
        surf.blit(f.render(ln, True, colour), (x, y))
        y += line_h
    return y


# ---------------------------------------------------------------- icons
ICONS = {"@L": "L", "@R": "R", "@U": "U", "@D": "D"}   # button labels drawn as triangles


def triangle(surf, centre, direction: str, size: float, colour):
    """Solid triangle pointing U/D/L/R (font-independent arrow)."""
    dx, dy = DIRS[direction]
    cx, cy = centre
    tip = (cx + dx * size, cy + dy * size)
    a = (cx - dx * size * 0.7 + dy * size * 0.85, cy - dy * size * 0.7 - dx * size * 0.85)
    b = (cx - dx * size * 0.7 - dy * size * 0.85, cy - dy * size * 0.7 + dx * size * 0.85)
    pygame.draw.polygon(surf, colour, [tip, a, b])


def star(surf, centre, r: float, colour):
    import math
    pts = []
    for i in range(10):
        ang = -math.pi / 2 + i * math.pi / 5
        rr = r if i % 2 == 0 else r * 0.45
        pts.append((centre[0] + rr * math.cos(ang), centre[1] + rr * math.sin(ang)))
    pygame.draw.polygon(surf, colour, pts)


# ---------------------------------------------------------------- buttons
class Button:
    def __init__(self, rect, label: str, on_click: Callable[[], None] | None = None,
                 size: int = 15, toggled: bool = False, enabled: bool = True,
                 on_press: Callable[[], None] | None = None, on_release: Callable[[], None] | None = None,
                 colour=None, tooltip: str = ""):
        self.rect = pygame.Rect(rect)
        self.label = label
        self.on_click = on_click
        self.on_press = on_press
        self.on_release = on_release
        self.size = size
        self.toggled = toggled
        self.enabled = enabled
        self.pressed = False
        self.colour = colour
        self.tooltip = tooltip

    def draw(self, surf, mouse=None):
        mouse = mouse or mouse_pos()
        hover = self.rect.collidepoint(mouse) and self.enabled
        base = self.colour or PANEL_HI
        if self.toggled:
            base = ACCENT if self.colour is None else shade(self.colour, 1.3)
        if self.pressed:
            base = shade(base, 1.4)
        elif hover:
            base = shade(base, 1.2)
        if not self.enabled:
            base = shade(PANEL, 0.9)
        pygame.draw.rect(surf, base, self.rect, border_radius=5)
        pygame.draw.rect(surf, LINE, self.rect, 1, border_radius=5)
        col = TEXT if self.enabled else MUTED
        if self.toggled and self.colour is None:
            col = (10, 14, 22)
        if self.label in ICONS:
            triangle(surf, self.rect.center, ICONS[self.label], self.size * 0.45, col)
        else:
            text(surf, self.label, self.rect.center, self.size, col, anchor="center")

    def handle(self, ev) -> bool:
        if not self.enabled:
            return False
        if ev.type == pygame.MOUSEBUTTONDOWN and ev.button == 1 and self.rect.collidepoint(ev.pos):
            self.pressed = True
            if self.on_press:
                self.on_press()
            return True
        if ev.type == pygame.MOUSEBUTTONUP and ev.button == 1 and self.pressed:
            self.pressed = False
            if self.on_release:
                self.on_release()
            if self.rect.collidepoint(ev.pos) and self.on_click:
                self.on_click()
            return True
        return False


class ButtonGroup:
    def __init__(self):
        self.buttons: list[Button] = []

    def clear(self):
        self.buttons = []

    def add(self, *a, **kw) -> Button:
        b = Button(*a, **kw)
        self.buttons.append(b)
        return b

    def draw(self, surf):
        m = mouse_pos()
        for b in self.buttons:
            b.draw(surf, m)

    def handle(self, ev) -> bool:
        for b in reversed(self.buttons):
            if b.handle(ev):
                return True
        return False


# ---------------------------------------------------------------- cards
SEGMENT_COLOURS = [(0xE0, 0xE0, 0xE0), (0x56, 0xCC, 0xF2), (0xBB, 0x6B, 0xD9)]   # shared with the firmware


def segment_colour(index: int):
    """Colour of the segment with 0-based `index`. A 4th or later run reuses the colours in order
    (segment n uses colour ((n-1) mod 3) + 1, spec 4 / FW-Q22), so adjacent runs always differ."""
    return SEGMENT_COLOURS[index % len(SEGMENT_COLOURS)]


def draw_card(surf, rect, code: Optional[str], *, executed=(), skipped=(), current=None,
              dim: bool = False, max_cell: int = 18, border: bool = True):
    """Draw a card from its protocol code: movement ('UUR'), 'IF:<fam>:<steps>', 'FOR', or '-'."""
    rect = pygame.Rect(rect)
    if border:
        pygame.draw.rect(surf, (12, 13, 17), rect, border_radius=4)
        pygame.draw.rect(surf, LINE, rect, 1, border_radius=4)
    kind, cond, steps = parse_code(code or "-")
    if kind == "NONE":
        text(surf, "…", rect.center, max(10, rect.h // 3), MUTED, anchor="center")
    elif kind == "FOR":
        _draw_for(surf, rect, dim)
    elif kind == "IF":
        _draw_if(surf, rect, cond, steps, dim)
    elif kind == "MOVE":
        draw_move(surf, rect, steps, executed=executed, skipped=skipped, current=current, dim=dim,
                  max_cell=max_cell)
    else:
        text(surf, str(code), rect.center, 12, DANGER, anchor="center")


IF_COLOUR = (0xF2, 0xC9, 0x4C)
FOR_COLOUR = (0x6F, 0xCF, 0x97)


def _draw_if(surf, rect, cond, steps, dim):
    """IF card: 'IF' + condition family code on top, the action card below."""
    f = 0.55 if dim else 1.0
    size = max(9, rect.h // 4)
    top = pygame.Rect(rect.x, rect.y + 2, rect.w, size + 2)
    t1 = text(surf, "IF", (rect.x + 4, top.y), size, shade(IF_COLOUR, f), bold=True)
    badge_w = font(size, True).size(cond)[0] + 6
    b = pygame.Rect(t1.right + 3, top.y, badge_w, size + 1)
    pygame.draw.rect(surf, shade(IF_COLOUR, f), b, border_radius=3)
    text(surf, cond, b.center, size, (20, 20, 20), bold=True, anchor="center")
    text(surf, ">", (b.right + 3, top.y), size, shade(TEXT, f), bold=True)
    body = pygame.Rect(rect.x, top.bottom, rect.w, rect.bottom - top.bottom)
    draw_move(surf, body, steps, dim=dim, max_cell=max(4, body.h // 4))


def _draw_for(surf, rect, dim):
    """FOR card: 'FOR' and a loop arrow."""
    import math
    f = 0.55 if dim else 1.0
    col = shade(FOR_COLOUR, f)
    size = max(10, rect.h // 3)
    text(surf, "FOR", (rect.centerx, rect.y + rect.h * 0.3), size, col, bold=True, anchor="center")
    r = max(4, rect.h // 6)
    c = (rect.centerx, int(rect.y + rect.h * 0.7))
    pygame.draw.arc(surf, col, (c[0] - r, c[1] - r, 2 * r, 2 * r), 0.6, 2 * math.pi - 0.2, max(2, r // 3))
    tip = (c[0] + r * math.cos(0.6), c[1] - r * math.sin(0.6))
    triangle(surf, tip, "U", max(2.0, r * 0.5), col)


def draw_move(surf, rect, steps: Optional[str], *, executed=(), skipped=(), current=None,
              dim: bool = False, max_cell: int = 18):
    """Shared card style (spec 4, identical on hub and node): start cell hollow (outline),
    one filled block per step in path order, each straight run (segment) in its own colour
    (E0E0E0, 56CCF2, BB6BD9, then repeating), a small arrow on the last block pointing in the
    last direction, scaled and centred. Optional highlight sets are step indices."""
    rect = pygame.Rect(rect)
    try:
        ct = CardType(steps, "x")
    except (ValueError, TypeError):
        text(surf, str(steps), rect.center, 12, DANGER, anchor="center")
        return
    cells = ct.cells()
    seg_of_step = [i for i, (_, n) in enumerate(ct.segments) for _ in range(n)]
    xs = [c[0] for c in cells]
    ys = [c[1] for c in cells]
    w, h = max(xs) - min(xs) + 1, max(ys) - min(ys) + 1
    pad = 4
    cs = max(4, min(max_cell, (rect.w - 2 * pad) // max(w, 1), (rect.h - 2 * pad) // max(h, 1)))
    ox = rect.centerx - (w * cs) // 2 - min(xs) * cs
    oy = rect.centery - (h * cs) // 2 - min(ys) * cs
    gap = max(1, cs // 9)
    f = 0.55 if dim else 1.0
    # start cell: hollow
    r0 = pygame.Rect(ox + cells[0][0] * cs + gap, oy + cells[0][1] * cs + gap, cs - 2 * gap, cs - 2 * gap)
    pygame.draw.rect(surf, shade(SEGMENT_COLOURS[0], f), r0, max(1, cs // 7), border_radius=2)
    last = None
    for i, (cx, cy) in enumerate(cells[1:]):
        r = pygame.Rect(ox + cx * cs + gap, oy + cy * cs + gap, cs - 2 * gap, cs - 2 * gap)
        c = shade(segment_colour(seg_of_step[i]), f)
        if i in skipped:
            c = (70, 70, 78)
        elif i in executed:
            c = shade(c, 0.45)
        pygame.draw.rect(surf, c, r, border_radius=2)
        if i == current:
            pygame.draw.rect(surf, (255, 255, 255), r.inflate(2, 2), 2, border_radius=3)
        if i in skipped:
            pygame.draw.line(surf, DANGER, r.topleft, r.bottomright, 2)
            pygame.draw.line(surf, DANGER, r.topright, r.bottomleft, 2)
        last = r
    if last is not None:
        triangle(surf, last.center, steps[-1], max(2.0, cs * 0.3), (15, 15, 20))


def seq_badge_text(seq: str) -> str:
    """SEQ payload -> badge text: 'H2' -> 'HEAD 2', '3' -> '#3', '-' -> ''."""
    if not seq or seq == "-":
        return ""
    if seq.startswith("H"):
        return f"HEAD {seq[1:]}".strip()
    return f"#{seq}"


def draw_node_screen(surf, rect, disp: dict, *, connected=True, blink=False, face_down=False):
    """Render what a TinyScreen+ node shows (96x64 OLED, scaled), v1.4 layout: the register strip
    is always at the top; below it the card (left) and the badge column (right); a one-line hint
    at the bottom. MSG covers the body but never the register strip. DOFF:1 + physically
    face-down = display off (the node's default is DOFF:0)."""
    rect = pygame.Rect(rect)
    pygame.draw.rect(surf, (0, 0, 0), rect)
    col = hex_rgb(disp.get("COL", "808080"))
    if not connected:
        text(surf, "DISCONNECTED", rect.center, 13, DANGER, bold=True, anchor="center")
        return
    if face_down and disp.get("DOFF", "0") == "1":
        text(surf, "(display off)", rect.center, 12, (60, 60, 60), anchor="center")
        return
    u = rect.h / 64.0                       # one OLED pixel
    # --- register strip (top, always visible) + colour bar + hand number
    strip_h = int(10 * u)
    reg = disp.get("REG", "0000")
    reg = reg if len(reg) == 4 and set(reg) <= {"0", "1"} else "0000"
    bw = int(11 * u)
    for i, (bit, ch) in enumerate(zip(reg, "RLVF")):
        r = pygame.Rect(rect.x + int(1 * u) + i * bw, rect.y + int(1 * u), bw - int(1 * u), strip_h - int(2 * u))
        on = bit == "1"
        if on:
            pygame.draw.rect(surf, ACCENT, r)
        else:
            pygame.draw.rect(surf, (70, 70, 80), r, 1)
        text(surf, ch, r.center, int(6 * u), (0, 0, 0) if on else (120, 120, 130), bold=True, anchor="center")
    bar = pygame.Rect(rect.x + 4 * bw + int(3 * u), rect.y + int(1 * u), 0, strip_h - int(2 * u))
    bar.width = rect.right - bar.x - int(14 * u)
    pygame.draw.rect(surf, col, bar)
    hand = disp.get("HAND", "0")
    if hand and hand != "0":
        text(surf, hand, (rect.right - int(7 * u), rect.y + strip_h // 2), int(9 * u), TEXT, bold=True,
             anchor="center")
    body = pygame.Rect(rect.x, rect.y + strip_h, rect.w, rect.h - strip_h - int(9 * u))
    msg = disp.get("MSG", "")
    hint = disp.get("HINT", "")
    if msg:
        text(surf, msg, body.center, int(10 * u), TEXT, bold=True, anchor="center")
    else:
        card_r = pygame.Rect(body.x, body.y, int(62 * u), body.h)
        draw_card(surf, card_r, disp.get("CARD", "-"), border=False, max_cell=int(11 * u))
        badge = seq_badge_text(disp.get("SEQ", "-"))
        if badge:
            br = pygame.Rect(0, 0, rect.w - card_r.w - int(3 * u), int(13 * u))
            br.midright = (rect.right - int(1 * u), body.centery)
            pygame.draw.rect(surf, GOLD if badge.startswith("HEAD") else (255, 255, 255), br,
                             border_radius=int(2 * u))
            text(surf, badge, br.center, int(7 * u), (0, 0, 0), bold=True, anchor="center")
    if hint:
        text(surf, hint, (rect.centerx, rect.bottom - int(1 * u)), int(7 * u), GOLD, anchor="midbottom")
    if blink:
        pygame.draw.rect(surf, (255, 255, 255), rect, max(2, int(3 * u)))
