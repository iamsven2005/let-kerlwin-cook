"""Settings screen: every config value, editable, with save/load."""
from __future__ import annotations

from typing import TYPE_CHECKING, Any

import pygame

from ..config import DEFAULT_CONFIG
from .widgets import ACCENT, BG, DANGER, LINE, MUTED, OK, PANEL, TEXT, ButtonGroup, text

if TYPE_CHECKING:  # pragma: no cover
    from .app import App

CHOICES = {
    "maze_algorithm": ["backtracker", "kruskal"],
}
for _b in ("bit0", "bit1", "bit2", "bit3"):
    CHOICES[f"manual_button_map.{_b}"] = ["V", "F", "L", "R", "-"]
    CHOICES[f"gesture_button_map.{_b}"] = ["V", "F", "L", "R", "-"]

STEPS = {
    "maze_size": 2, "maze_braid_fraction": 0.05, "deck_multiplier": 1,
    "deal_interval_s": 0.5, "tile_move_time_s": 0.05, "pause_after_step_s": 0.1,
    "pause_between_cards_s": 0.1, "route_flash_s": 0.25,
    "latch_ms": 250,
    "manual_clear_window_ms": 10, "remove_window_ms": 250,
    "manual_f_lockout_ms": 50, "min_copies_per_type": 1, "min_domino_copies": 1,
    "control_card_share": 0.05, "ble_reconnect_min_s": 0.5, "ble_reconnect_max_s": 1.0,
    "ble_write_gap_ms": 1, "ble_burst_max": 1, "ble_burst_window_ms": 10, "ble_ping_interval_s": 0.5, "lost_after_missed_pongs": 1, "battery_warn_percent": 5,
    "ble_scan_timeout_s": 1.0,
}
MINS = {"maze_size": 5, "deck_multiplier": 1, "lost_after_missed_pongs": 1,
        "min_domino_copies": 1, "ble_burst_max": 1}   # 14 A3: dominoes can't be switched off
TEXT_KEYS = {"card_catalog_file", "ble_service_uuid", "ble_rx_uuid", "ble_tx_uuid", "ble_name_prefix"}


# deprecated in v1.9 (19.1) / retired in v1.11 (21.1)
HIDDEN_KEYS = {"gesture_idle_window_ms", "flip_commit_window_ms", "manual_f_long_press_ms"}


def flat_keys(d: dict, prefix: str = "") -> list[str]:
    out = []
    for k, v in d.items():
        key = f"{prefix}{k}"
        if isinstance(v, dict):
            out.extend(flat_keys(v, key + "."))
        else:
            out.append(key)
    return out


class SettingsScreen:
    def __init__(self, app: "App"):
        self.app = app
        self.buttons = ButtonGroup()
        self.back = "lobby"
        self.editing: str | None = None
        self.edit_text = ""
        self.status = ""
        # deprecated keys stay in config.json for compatibility but aren't offered for editing
        self.keys = [k for k in flat_keys(DEFAULT_CONFIG) if k not in HIDDEN_KEYS]
        self._dirty = True

    def open(self, back: str):
        self.back = back
        self.editing = None
        self.status = f"Editing {self.app.cfg.path or 'config (unsaved)'}"
        self._dirty = True

    # ------------------------------------------------------------------ value edits
    def _get(self, key):
        return self.app.cfg.get_path(key)

    def _set(self, key, value):
        if key == "maze_size":
            value = max(5, int(value)) | 1
        if key in MINS:
            value = max(MINS[key], value)
        if isinstance(value, float):
            value = round(max(0.0, value), 3)
        if key in ("maze_braid_fraction", "control_card_share"):
            value = min(1.0, value)
        self.app.cfg.set(key, value)
        self._dirty = True

    def _bump(self, key, sign):
        v = self._get(key)
        step = STEPS.get(key, 1)
        if key == "maze_seed":
            self._set(key, max(0, (v or 0) + sign))
            return
        nv = v + sign * step
        if isinstance(v, int) and not isinstance(v, bool):
            nv = int(round(nv))
        self._set(key, max(0, nv))

    def _cycle(self, key):
        opts = CHOICES[key]
        v = self._get(key)
        self._set(key, opts[(opts.index(v) + 1) % len(opts)] if v in opts else opts[0])

    def _start_edit(self, key):
        self.editing = key
        v = self._get(key)
        self.edit_text = "" if v is None else str(v)

    def _commit_edit(self):
        key, s = self.editing, self.edit_text.strip()
        self.editing = None
        default = DEFAULT_CONFIG
        for p in key.split("."):
            default = default[p]
        try:
            if key == "maze_seed":
                val: Any = None if s in ("", "none", "None", "random") else int(s)
            elif isinstance(default, bool):
                val = s.lower() in ("1", "true", "yes", "on")
            elif isinstance(default, int):
                val = int(s)
            elif isinstance(default, float):
                val = float(s)
            else:
                val = s
            self._set(key, val)
            self.status = f"{key} = {val}"
        except ValueError:
            self.status = f"invalid value for {key}: {s!r}"

    def _save(self):
        try:
            p = self.app.cfg.save(self.app.cfg.path or "config.json")
            self.status = f"saved to {p}"
        except Exception as e:
            self.status = f"save failed: {e}"

    def _load(self):
        try:
            self.app.reload_config()
            self.status = f"loaded {self.app.cfg.path}"
        except Exception as e:
            self.status = f"load failed: {e}"
        self._dirty = True

    def _defaults(self):
        self.app.cfg.reset_defaults()
        self.status = "defaults restored (not saved yet)"
        self._dirty = True

    # ------------------------------------------------------------------ layout
    def _row_pos(self, i):
        per_col = (len(self.keys) + 2) // 3
        col, row = divmod(i, per_col)
        return 20 + col * 528, 88 + row * 34

    def _rebuild(self):
        b = self.buttons
        b.clear()
        for i, key in enumerate(self.keys):
            x, y = self._row_pos(i)
            v = self._get(key)
            cx = x + 372
            if isinstance(v, bool):
                b.add((cx, y, 120, 26), "ON" if v else "OFF", lambda k=key: self._set(k, not self._get(k)),
                      toggled=v, size=13)
            elif key in CHOICES:
                b.add((cx, y, 120, 26), "change", lambda k=key: self._cycle(k), size=13)
            elif key in TEXT_KEYS:
                b.add((cx, y, 120, 26), "edit", lambda k=key: self._start_edit(k), size=13)
            elif key == "maze_seed":
                b.add((cx, y, 56, 26), "edit", lambda k=key: self._start_edit(k), size=12)
                b.add((cx + 62, y, 70, 26), "random", lambda: self._set("maze_seed", None), size=12)
            else:
                b.add((cx, y, 34, 26), "−", lambda k=key: self._bump(k, -1), size=14)
                b.add((cx + 38, y, 34, 26), "+", lambda k=key: self._bump(k, +1), size=14)
                b.add((cx + 76, y, 44, 26), "edit", lambda k=key: self._start_edit(k), size=12)
        b.add((24, 900, 110, 36), "Save", self._save, size=16)
        b.add((144, 900, 110, 36), "Load", self._load, size=16)
        b.add((264, 900, 130, 36), "Defaults", self._defaults, size=16)
        b.add((1450, 900, 126, 36), "Back", lambda: self.app.close_settings(self.back), size=16)
        self._dirty = False

    def handle(self, ev):
        if self._dirty:
            self._rebuild()
        if self.editing:
            if ev.type == pygame.KEYDOWN:
                if ev.key in (pygame.K_RETURN, pygame.K_KP_ENTER):
                    self._commit_edit()
                elif ev.key == pygame.K_ESCAPE:
                    self.editing = None
                elif ev.key == pygame.K_BACKSPACE:
                    self.edit_text = self.edit_text[:-1]
                elif ev.unicode and ev.unicode.isprintable():
                    self.edit_text += ev.unicode
                return
            if ev.type == pygame.MOUSEBUTTONDOWN:
                self._commit_edit()
        if self.buttons.handle(ev):
            return
        if ev.type == pygame.KEYDOWN and ev.key == pygame.K_ESCAPE:
            self.app.close_settings(self.back)

    def draw(self, surf, now):
        if self._dirty:
            self._rebuild()
        surf.fill(BG)
        text(surf, "Settings", (24, 20), 26, TEXT, bold=True)
        text(surf, "Timing and input windows apply immediately. Maze, deck and stash settings apply to the "
                   "next new game. Save writes config.json.", (24, 56), 14, MUTED)
        for i, key in enumerate(self.keys):
            x, y = self._row_pos(i)
            pygame.draw.rect(surf, PANEL, (x - 8, y - 3, 516, 32), border_radius=4)
            text(surf, key if len(key) <= 30 else key[:29] + "…", (x, y + 5), 12, TEXT)
            if self.editing == key:
                r = pygame.Rect(x + 222, y, 270, 26)
                pygame.draw.rect(surf, (10, 12, 18), r)
                pygame.draw.rect(surf, ACCENT, r, 2)
                text(surf, self.edit_text + "|", (r.x + 6, r.y + 4), 14, TEXT)
            else:
                v = self._get(key)
                s = "random (null)" if (key == "maze_seed" and v is None) else str(v)
                if len(s) > 13:
                    s = s[:6] + "…" + s[-6:]
                changed = v != _default(key)
                text(surf, s, (x + 236, y + 5), 13, ACCENT if changed else MUTED)
        pygame.draw.line(surf, LINE, (0, 888), (1600, 888))
        col = DANGER if ("fail" in self.status or "invalid" in self.status) else OK
        text(surf, self.status, (420, 908), 15, col)
        self.buttons.draw(surf)


def _default(key):
    d = DEFAULT_CONFIG
    for p in key.split("."):
        d = d[p]
    return d
