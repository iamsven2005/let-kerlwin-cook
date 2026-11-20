"""BLE debug view (section 14 A1), toggled with F12 on any screen.

Per node: link status, last PING sent / PONG received, round-trip time, write counters and a live
log of every message sent (>) and received (<), errors (!) and connection events (*), with
timestamps. "Save log" writes a snapshot to logs/ble_debug_<time>.txt; every line also goes to the
rolling logs/ble_debug.log while the hub runs.
"""
from __future__ import annotations

import time
from pathlib import Path
from typing import TYPE_CHECKING

import pygame

from ..nodes.physical import PhysicalNode
from .widgets import ACCENT, DANGER, GOLD, MUTED, OK, TEXT, ButtonGroup, text

if TYPE_CHECKING:  # pragma: no cover
    from .app import App

DIR_COL = {">": ACCENT, "<": OK, "!": DANGER, "*": GOLD}


def format_snapshot(nodes, t0: float) -> str:
    out = [f"Command Card Maze Race hub - BLE debug snapshot {time.strftime('%Y-%m-%d %H:%M:%S')}", ""]
    for n in nodes:
        out.append(f"=== {n.node_id} ({n.kind}) connected={n.connected} status={getattr(n, 'status', '')} "
                   f"address={getattr(n, 'address', '')} fw={getattr(n, 'fw_version', '')}")
        out.append(f"    last PING sent={_ago(n.last_ping_sent, t0)} last PONG={_ago(n.last_pong, t0)} "
                   f"RTT={'%.0f ms' % n.rtt_ms if n.rtt_ms is not None else '-'} "
                   f"writes ok={n.writes_ok} failed={n.write_errors} battery={n.battery}")
        for ts, d, line in n.debug_lines():
            out.append(f"    {ts - t0:10.3f}  {d}  {line}")
        out.append("")
    return "\n".join(out) + "\n"


def _ago(ts, now):
    return "-" if ts is None else f"{now - ts:.1f} s ago"


class DebugView:
    def __init__(self, app: "App"):
        self.app = app
        self.visible = False
        self.buttons = ButtonGroup()
        self.status = ""
        self.buttons.add((1420, 10, 80, 28), "Save log", self.save, size=13)
        self.buttons.add((1506, 10, 80, 28), "Close", self.toggle, size=13)

    def toggle(self):
        self.visible = not self.visible

    def save(self) -> Path:
        d = self.app.logs_dir
        d.mkdir(parents=True, exist_ok=True)
        p = d / f"ble_debug_{time.strftime('%Y%m%d-%H%M%S')}.txt"
        p.write_text(format_snapshot(self.app.hub.ordered(), self.app.started_at), encoding="utf-8")
        self.status = f"saved {p.name} in {d.name}/"
        return p

    def handle(self, ev) -> bool:
        if not self.visible:
            return False
        if self.buttons.handle(ev):
            return True
        return ev.type in (pygame.MOUSEBUTTONDOWN, pygame.MOUSEBUTTONUP)

    def draw(self, surf, now):
        if not self.visible:
            return
        shade = pygame.Surface(surf.get_size(), pygame.SRCALPHA)
        shade.fill((5, 6, 10, 235))
        surf.blit(shade, (0, 0))
        text(surf, "BLE debug view (F12)", (16, 12), 22, TEXT, bold=True)
        text(surf, "> sent (write completed)   < received   ! error   * connection event", (300, 18), 13, MUTED)
        if self.status:
            text(surf, self.status, (1410, 44), 12, OK, anchor="topright")
        self.buttons.draw(surf)
        nodes = self.app.hub.ordered()
        if not nodes:
            text(surf, "No nodes.", (16, 60), 16, MUTED)
            return
        n_cols = min(len(nodes), 3)
        rows = (len(nodes) + n_cols - 1) // n_cols
        cw = (1600 - 24) // n_cols
        ch = (960 - 64) // rows
        for i, n in enumerate(nodes):
            x = 12 + (i % n_cols) * cw
            y = 56 + (i // n_cols) * ch
            box = pygame.Rect(x, y, cw - 8, ch - 8)
            pygame.draw.rect(surf, (18, 20, 28), box, border_radius=6)
            pygame.draw.rect(surf, OK if n.connected else DANGER, box, 1, border_radius=6)
            phys = isinstance(n, PhysicalNode)
            head = f"{n.node_id}  {'BLE' if phys else 'virtual'}  {'connected' if n.connected else 'OFFLINE'}"
            if phys:
                head += f"  ·  {n.status}  ·  {n.address}"
            text(surf, head, (box.x + 8, box.y + 6), 14, TEXT, bold=True)
            stats = (f"PING sent {_ago(n.last_ping_sent, now)}   PONG {_ago(n.last_pong, now)}   "
                     f"RTT {('%.0f ms' % n.rtt_ms) if n.rtt_ms is not None else '-'}   "
                     f"writes {n.writes_ok} ok / {n.write_errors} failed")
            if phys:
                stats += f"   missed PONGs {n.missed_pongs}"
            text(surf, stats, (box.x + 8, box.y + 26), 12, MUTED)
            lines = n.debug_lines()
            max_lines = max(1, (box.h - 52) // 15)
            for j, (ts, d, line) in enumerate(lines[-max_lines:]):
                ly = box.y + 46 + j * 15
                text(surf, f"{ts - self.app.started_at:9.2f}", (box.x + 8, ly), 11, MUTED)
                text(surf, d, (box.x + 78, ly), 11, DIR_COL.get(d, TEXT), bold=True)
                text(surf, line[:90], (box.x + 92, ly), 11, DIR_COL.get(d, TEXT) if d == "!" else TEXT)
