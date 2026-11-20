"""Lobby / setup screen."""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import TYPE_CHECKING

import pygame

from ..nodes.physical import PhysicalNode
from ..nodes.virtual import VirtualNode
from ..sim import events as E
from ..sim.game import PLAYER_COLOURS, NodeSetup
from .widgets import ACCENT, BG, DANGER, GOLD, LINE, MUTED, OK, PANEL, TEXT, ButtonGroup, hex_rgb, text, wrap_text

if TYPE_CHECKING:  # pragma: no cover
    from .app import App

MAX_NODES = 6


@dataclass
class LobbyState:
    n_players: int = 1
    player_nodes: list[list[str]] = field(default_factory=lambda: [[] for _ in range(6)])
    schemes: dict[str, str] = field(default_factory=dict)
    vtap_assign: bool = False
    vtap_seen: dict[int, list[str]] = field(default_factory=dict)

    def player_of(self, nid: str):
        for p, lst in enumerate(self.player_nodes):
            if nid in lst:
                return p
        return None

    def hand_of(self, nid: str):
        p = self.player_of(nid)
        return None if p is None else self.player_nodes[p].index(nid) + 1

    def set_player(self, nid: str, p):
        old = self.player_of(nid)
        if old is not None:
            self.player_nodes[old].remove(nid)
        if p is not None:
            self.player_nodes[p].append(nid)

    def move(self, nid: str, delta: int):
        p = self.player_of(nid)
        if p is None:
            return
        lst = self.player_nodes[p]
        i = lst.index(nid)
        j = max(0, min(len(lst) - 1, i + delta))
        lst[i], lst[j] = lst[j], lst[i]

    def forget(self, nid: str):
        self.set_player(nid, None)
        self.schemes.pop(nid, None)

    def setup(self) -> list[NodeSetup]:
        out = []
        for p in range(self.n_players):
            for h, nid in enumerate(self.player_nodes[p], start=1):
                out.append(NodeSetup(nid, p, h, self.schemes.get(nid, "gesture")))
        return out

    def problems(self, nodes) -> list[str]:
        out = []
        for p in range(self.n_players):
            if not self.player_nodes[p]:
                out.append(f"P{p + 1} needs at least one node")
            for nid in self.player_nodes[p]:
                n = nodes.get(nid)
                if n is None or not n.connected:
                    out.append(f"{nid} is not connected")
        if sum(len(self.player_nodes[p]) for p in range(self.n_players)) > MAX_NODES:
            out.append("at most 6 nodes")
        return out


class LobbyScreen:
    def __init__(self, app: "App"):
        self.app = app
        self.buttons = ButtonGroup()
        self._sig = None
        self.flash: dict[str, float] = {}

    @property
    def st(self) -> LobbyState:
        return self.app.lobby

    # ------------------------------------------------------------------ actions
    def _add_virtual(self):
        if len(self.app.hub.nodes) >= MAX_NODES:
            return
        n = self.app.add_virtual_node()
        # auto-assign: to the player with fewest nodes
        counts = [len(self.st.player_nodes[p]) for p in range(self.st.n_players)]
        self.st.set_player(n.node_id, counts.index(min(counts)))

    def _remove_virtual(self):
        vs = [n for n in self.app.hub.nodes.values() if isinstance(n, VirtualNode)]
        if vs:
            self.app.remove_node(vs[-1].node_id)

    def _players(self, d):
        st = self.st
        st.n_players = max(1, min(4, st.n_players + d))
        for p in range(st.n_players, 6):
            for nid in list(st.player_nodes[p]):
                st.set_player(nid, None)

    def _cycle_player(self, nid, d):
        st = self.st
        cur = st.player_of(nid)
        opts = [None] + list(range(st.n_players))
        i = opts.index(cur)
        st.set_player(nid, opts[(i + d) % len(opts)])

    def _toggle_scheme(self, nid):
        self.st.schemes[nid] = "manual" if self.st.schemes.get(nid, "gesture") == "gesture" else "gesture"

    def _toggle_vtap(self):
        self.st.vtap_assign = not self.st.vtap_assign
        self.st.vtap_seen = {}

    def on_node_event(self, ev) -> None:
        """Lobby-time node events: identify nodes, and hand-number assignment by button press
        (18.5: the V tap is gone, so any button press counts; a T:V still does for old firmware)."""
        import time
        self.flash[ev.node_id] = time.monotonic() + 0.4
        st = self.st
        pressed = ev.kind == E.BUTTONS and ev.value not in ("0", "00", "")
        if (pressed or (ev.kind == E.TAP and ev.value == "V")) and st.vtap_assign:
            p = st.player_of(ev.node_id)
            if p is None:
                return
            seen = st.vtap_seen.setdefault(p, [])
            if ev.node_id in seen:
                return
            seen.append(ev.node_id)
            rest = [n for n in st.player_nodes[p] if n not in seen]
            st.player_nodes[p] = seen + rest

    # ------------------------------------------------------------------ layout
    def _signature(self):
        st = self.st
        ble = self.app.ble
        return (tuple(self.app.hub.nodes), tuple(tuple(x) for x in st.player_nodes), tuple(sorted(st.schemes.items())),
                st.n_players, st.vtap_assign, tuple(sorted(ble.scan_results)) if ble else (), ble.scanning if ble else False,
                tuple(n.connected for n in self.app.hub.nodes.values()))

    def _rebuild(self):
        b = self.buttons
        b.clear()
        app = self.app
        ble = app.ble
        # column 1
        b.add((16, 116, 120, 28), "Scan BLE", ble.scan if ble else None,
              enabled=bool(ble and ble.available and not ble.scanning))
        y = 180
        if ble:
            for addr, res in sorted(ble.scan_results.items(), key=lambda kv: kv[1].name):
                node = app.hub.nodes.get(res.node_id)
                connected = bool(node and getattr(node, "wanted", False))
                if connected:
                    b.add((330, y, 90, 26), "Disconnect", lambda nid=res.node_id: app.remove_node(nid), size=13)
                else:
                    b.add((330, y, 90, 26), "Connect", lambda r=res: app.connect_physical(r), size=13,
                          enabled=len(app.hub.nodes) < MAX_NODES)
                b.add((426, y, 60, 26), "Blink", (lambda n=node: n.blink()) if node and node.connected else None,
                      size=13, enabled=bool(node and node.connected))
                y += 32
                if y > 420:
                    break
        b.add((16, 500, 150, 30), "+ Virtual node", self._add_virtual,
              enabled=len(app.hub.nodes) < MAX_NODES)
        b.add((176, 500, 150, 30), "− Remove virtual", self._remove_virtual,
              enabled=any(isinstance(n, VirtualNode) for n in app.hub.nodes.values()))

        # column 2
        b.add((690, 92, 30, 28), "−", lambda: self._players(-1))
        b.add((760, 92, 30, 28), "+", lambda: self._players(+1))
        y = 170
        for nid, node in app.hub.nodes.items():
            b.add((850, y, 26, 26), "@L", lambda n=nid: self._cycle_player(n, -1), size=12)
            b.add((930, y, 26, 26), "@R", lambda n=nid: self._cycle_player(n, +1), size=12)
            b.add((1010, y, 26, 26), "@U", lambda n=nid: self.st.move(n, -1), size=12)
            b.add((1090, y, 26, 26), "@D", lambda n=nid: self.st.move(n, +1), size=12)
            sch = self.st.schemes.get(nid, "gesture")
            b.add((1140, y, 96, 26), sch.capitalize(), lambda n=nid: self._toggle_scheme(n), size=13,
                  toggled=sch == "manual")
            b.add((1246, y, 60, 26), "Blink", node.blink, size=13, enabled=node.connected)
            if isinstance(node, PhysicalNode):
                # 14 A2: same scan-then-connect path as the background loop, right now
                b.add((1312, y, 96, 26), "Reconnect", node.reconnect_now, size=13)
            y += 34
        b.add((560, 400, 260, 30), "Assign hand # by press: " + ("ON" if self.st.vtap_assign else "OFF"),
              self._toggle_vtap, toggled=self.st.vtap_assign, size=14)
        b.add((1290, 610, 130, 40), "Settings", lambda: app.open_settings(back="lobby"), size=16)
        b.add((1150, 610, 130, 40), "Tutorial", app.open_tutorial, size=16,
              enabled=any(self.st.player_nodes[p] for p in range(self.st.n_players)))
        b.add((1430, 610, 154, 40), "Start game >", app.start_game, size=17,
              enabled=not self.st.problems(app.hub.nodes))

    def handle(self, ev):
        sig = self._signature()
        if sig != self._sig:
            self._rebuild()
            self._sig = sig
        if self.buttons.handle(ev):
            return
        if self.app.handle_node_strip(ev):
            return
        if ev.type == pygame.KEYDOWN:
            if ev.key == pygame.K_RETURN and not self.st.problems(self.app.hub.nodes):
                self.app.start_game()
            elif ev.key == pygame.K_v and (ev.mod & pygame.KMOD_CTRL):
                self._add_virtual()
            else:
                self.app.node_hotkey(ev)

    # ------------------------------------------------------------------ draw
    def draw(self, surf, now):
        import time
        sig = self._signature()
        if sig != self._sig:
            self._rebuild()
            self._sig = sig
        app = self.app
        st = self.st
        surf.fill(BG)
        text(surf, "Command Card Maze Race — Lobby", (16, 16), 26, TEXT, bold=True)
        text(surf, "Set up nodes and players, then start. Virtual nodes are fully playable without hardware.",
             (16, 52), 14, MUTED)
        # column 1 -------------------------------------------------------
        pygame.draw.rect(surf, PANEL, (8, 80, 520, 470), border_radius=8)
        ble = app.ble
        status = ble.status if ble else "BLE disabled (--no-ble)"
        bad = status.startswith("scan failed") or status.startswith("BLE unavailable")
        if bad:
            wrap_text(surf, status, (16, 440 - 4 * 19), 500, 13, DANGER, max_lines=4, line_h=19)
            text(surf, "see below", (146, 121), 14, DANGER)
        else:
            text(surf, status, (146, 121), 14, MUTED)
        text(surf, "Name", (16, 156), 13, MUTED)
        text(surf, "ID", (170, 156), 13, MUTED)
        text(surf, "RSSI", (250, 156), 13, MUTED)
        y = 180
        if ble:
            for addr, res in sorted(ble.scan_results.items(), key=lambda kv: kv[1].name):
                node = app.hub.nodes.get(res.node_id)
                text(surf, res.name, (16, y + 4), 14, TEXT)
                text(surf, res.node_id, (170, y + 4), 14, TEXT)
                text(surf, f"{res.rssi if res.rssi is not None else '?'} dBm", (250, y + 4), 14, TEXT)
                if node is not None:
                    text(surf, getattr(node, "status", ""), (492, y + 6), 11, OK if node.connected else MUTED)
                y += 32
                if y > 420:
                    break
            if not ble.scan_results:
                text(surf, "No nodes found yet. Nodes advertise as CCNODE-<id>.", (16, y + 4), 14, MUTED)
        text(surf, "2. Virtual nodes", (16, 470), 17, ACCENT, bold=True)
        text(surf, f"{len(app.hub.nodes)}/{MAX_NODES} nodes total  (Ctrl+V adds one)", (340, 506), 13, MUTED)
        text(surf, "1. Physical nodes (BLE)", (16, 84), 17, ACCENT, bold=True)

        # column 2 -------------------------------------------------------
        pygame.draw.rect(surf, PANEL, (548, 80, 1044, 590), border_radius=8)
        text(surf, "3. Players", (560, 84), 17, ACCENT, bold=True)
        text(surf, str(st.n_players), (740, 106), 20, TEXT, bold=True, anchor="center")
        text(surf, "players (1–4)", (800, 98), 14, MUTED)
        heads = [("Node", 560), ("Type", 660), ("Battery", 770), ("Player", 850), ("Hand #", 1010),
                 ("Scheme", 1140), ("", 1246)]
        for h, x in heads:
            text(surf, h, (x, 144), 13, MUTED)
        y = 170
        tnow = time.monotonic()
        for nid, node in app.hub.nodes.items():
            if self.flash.get(nid, 0) > tnow:
                pygame.draw.rect(surf, (60, 66, 90), (552, y - 3, 760, 32), border_radius=4)
            p = st.player_of(nid)
            col = hex_rgb(PLAYER_COLOURS[p]) if p is not None else MUTED
            text(surf, nid, (560, y + 4), 15, TEXT, bold=True)
            kind = "virtual" if isinstance(node, VirtualNode) else "BLE"
            label = kind if node.connected else f"{kind}: {getattr(node, 'status', 'offline')}"
            text(surf, label[:16], (660, y + 4), 14, TEXT if node.connected else DANGER)
            if node.battery is not None:
                low = node.battery < app.cfg.battery_warn_percent
                text(surf, f"{node.battery}%" + (" LOW" if low else ""), (770, y + 4), 14,
                     DANGER if low else TEXT, bold=low)
            else:
                text(surf, "—", (770, y + 4), 14, MUTED)
            text(surf, f"P{p + 1}" if p is not None else "—", (903, y + 4), 15, col, bold=True, anchor="midtop")
            h = st.hand_of(nid)
            text(surf, str(h) if h else "—", (1063, y + 4), 15, TEXT, bold=True, anchor="midtop")
            y += 34
        if not app.hub.nodes:
            text(surf, "No nodes yet: add virtual nodes or connect physical ones.", (560, y + 4), 14, MUTED)
        text(surf, "4. Hand numbers", (560, 374), 17, ACCENT, bold=True)
        msg = ("Press a button on each player's nodes in top-to-bottom order (a corner button on the widget, "
               "Q E Z C, or the real node)."
               if st.vtap_assign else "Use the up/down arrows to reorder, or turn on assignment by button press.")
        text(surf, msg, (830, 406), 13, MUTED)
        text(surf, "5. Scheme per node: Gesture (taps/flips) or Manual (4 buttons). Then start.", (560, 450),
             14, MUTED)
        seed = app.cfg.maze_seed
        text(surf, f"Maze {app.cfg.maze_size}×{app.cfg.maze_size}, seed {seed if seed is not None else 'random'}, "
                   f"braid {app.cfg.maze_braid_fraction:.2f}, deck ×{app.cfg.deck_multiplier}", (560, 480), 14, MUTED)
        warns = list(getattr(app, "catalog_warnings", []))
        low = [f"{nid} battery {n.battery}%" for nid, n in app.hub.nodes.items()
               if n.battery is not None and n.battery < app.cfg.battery_warn_percent]
        if low:
            warns.insert(0, "Low battery: " + ", ".join(low))
        for i, wmsg in enumerate(warns[:3]):
            text(surf, wmsg, (560, 520 + i * 22), 13, GOLD)
        if len(warns) > 3:
            text(surf, f"(+{len(warns) - 3} more warnings, see the console)", (560, 586), 13, GOLD)
        probs = st.problems(app.hub.nodes)
        if probs:
            text(surf, "Can't start yet: " + "; ".join(probs[:3]), (560, 620), 14, DANGER)
        else:
            text(surf, "Ready. Press Start (or Enter).", (560, 620), 14, OK)
        self.buttons.draw(surf)
        app.draw_node_strip(surf, info_for_node=self._node_info)

    def _node_info(self, nid):
        p = self.st.player_of(nid)
        if p is None:
            return "unassigned"
        return f"P{p + 1} · hand #{self.st.hand_of(nid)} · {self.st.schemes.get(nid, 'gesture')}"
