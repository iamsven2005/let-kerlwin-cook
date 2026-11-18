"""Pygame application: owns config, node hub, BLE worker, screens and the game session.

The loop never blocks: BLE runs on its own thread, node events arrive on a
thread-safe queue, and the simulation advances on wall-clock (monotonic) time.
"""
from __future__ import annotations

import logging
import logging.handlers
import os
import time
from pathlib import Path
from typing import Optional

import pygame

from ..config import Config
from ..nodes.base import NodeHub
from ..nodes.physical import BleWorker, PhysicalNode, ScanResult
from ..nodes.virtual import VirtualNode
from ..sim import events as E
from ..sim.cards import load_catalog
from ..sim.controls import InputController
from ..sim.game import ENDED, PLAYER_COLOURS, Game, doff_for
from ..sim.maze import generate_maze
from ..sim.tutorial import Tutorial
from . import widgets
from .debug_view import DebugView
from .game_screen import GameScreen
from .lobby import LobbyScreen, LobbyState
from .node_widget import H as NODE_H
from .node_widget import W as NODE_W
from .node_widget import NodeWidget
from .settings import SettingsScreen
from .tutorial_screen import TutorialScreen

log = logging.getLogger(__name__)

PROJECT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CONFIG_PATH = PROJECT_ROOT / "config.json"
LOGS_DIR = PROJECT_ROOT / "logs"
BUTTON_KEYS = {pygame.K_q: 0x1, pygame.K_e: 0x2, pygame.K_z: 0x4, pygame.K_c: 0x8}
CANVAS = (1600, 960)
STRIP_Y = 690


class App:
    def __init__(self, config_path: str | Path = DEFAULT_CONFIG_PATH, n_virtual: int = 0,
                 players: Optional[int] = None, ble: bool = True, window: Optional[tuple[int, int]] = None,
                 autostart: bool = False):
        self.config_path = Path(config_path)
        self.cfg = Config.load(self.config_path)
        self.base_dir = self.config_path.resolve().parent
        self.catalog = self._load_catalog()
        self.hub = NodeHub()
        self.ble: Optional[BleWorker] = BleWorker(self.cfg, self.hub) if ble else None
        self.lobby = LobbyState()
        self.game: Optional[Game] = None
        self.controller: Optional[InputController] = None
        self.game_started_at = 0.0
        self.widgets: list[NodeWidget] = []
        self.selected = 0
        self.last_flip: dict[str, bool] = {}
        self.running = True
        self._window_size = window
        self.tutorial: Optional[Tutorial] = None
        self.started_at = time.monotonic()
        self.logs_dir = self.config_path.resolve().parent / "logs"

        pygame.init()
        pygame.display.set_caption("Command Card Maze Race — hub")
        info = pygame.display.Info()
        if window is None:
            dw, dh = info.current_w or CANVAS[0], info.current_h or CANVAS[1]
            s = min(1.0, (dw - 40) / CANVAS[0], (dh - 90) / CANVAS[1]) if dw and dh else 1.0
            window = (int(CANVAS[0] * s), int(CANVAS[1] * s))
        self.window = pygame.display.set_mode(window, pygame.RESIZABLE)
        self.canvas = pygame.Surface(CANVAS)
        self.clock = pygame.time.Clock()

        self.lobby_screen = LobbyScreen(self)
        self.game_screen = GameScreen(self)
        self.settings_screen = SettingsScreen(self)
        self.tutorial_screen = TutorialScreen(self)
        self.debug_view = DebugView(self)
        self.screen = "lobby"

        if players:
            self.lobby.n_players = max(1, min(4, players))
        for _ in range(n_virtual):
            self.lobby_screen._add_virtual()
        if autostart and not self.lobby.problems(self.hub.nodes):
            self.start_game()

    # ------------------------------------------------------------------ config
    def _load_catalog(self):
        p = Path(self.cfg.card_catalog_file)
        if not p.is_absolute():
            p = self.base_dir / p          # relative to the config file: portable across OSes
        self.catalog_warnings: list[str] = []
        if not p.exists():
            self.catalog_warnings.append(f"{p.name} not found: using the built-in catalog")
        return load_catalog(p if p.exists() else None, self.catalog_warnings)

    def reload_config(self):
        self.cfg = Config.load(self.config_path)
        self.catalog = self._load_catalog()
        if self.ble:
            self.ble.cfg = self.cfg
        if self.game:
            self.game.cfg = self.cfg
        if self.controller:
            self.controller.cfg = self.cfg

    # ------------------------------------------------------------------ nodes
    def add_virtual_node(self) -> VirtualNode:
        n = VirtualNode(self.hub)
        while n.node_id in self.hub.nodes:
            n = VirtualNode(self.hub)
        self.hub.add(n)
        self._layout_widgets()
        return n

    def connect_physical(self, res: ScanResult) -> Optional[PhysicalNode]:
        if not self.ble:
            return None
        n = self.ble.connect(res)
        if n.node_id not in self.lobby.schemes:
            self.lobby.schemes[n.node_id] = "gesture"
        self._layout_widgets()
        return n

    def remove_node(self, nid: str):
        self.lobby.forget(nid)
        self.hub.remove(nid)
        self._layout_widgets()

    def _layout_widgets(self):
        old = {w.node.node_id: w for w in self.widgets}
        self.widgets = []
        x = 8
        for n in self.hub.ordered():
            w = old.get(n.node_id) or NodeWidget(n, (x, STRIP_Y), on_bump_drop=self._bump_drop,
                                                 warn_percent=lambda: self.cfg.battery_warn_percent)
            w.move((x, STRIP_Y))
            self.widgets.append(w)
            x += NODE_W + 12
        self.selected = min(self.selected, max(0, len(self.widgets) - 1))

    def _bump_drop(self, a: NodeWidget, b: NodeWidget, pos=None):
        """Dragged node a bumped into node b. Dropped on b's left half: a sits LEFT of b, so a's
        right edge touches (a reports T:R, b reports T:L); right half: the other way round."""
        left = pos is None or pos[0] < b.rect.centerx
        side_a, side_b = ("R", "L") if left else ("L", "R")
        if isinstance(a.node, VirtualNode):
            a.node.tap(side_a)
        if isinstance(b.node, VirtualNode):
            b.node.tap(side_b)

    def handle_node_strip(self, ev) -> bool:
        for i, w in enumerate(self.widgets):
            if w.handle(ev, self.widgets):
                self.selected = i
                return True
        if ev.type == pygame.MOUSEBUTTONDOWN and ev.button == 1:
            for i, w in enumerate(self.widgets):
                if w.rect.collidepoint(ev.pos):
                    self.selected = i
                    return True
        return False

    def draw_node_strip(self, surf, info_for_node=None):
        for i, w in enumerate(self.widgets):
            info = info_for_node(w.node.node_id) if info_for_node else ""
            w.draw(surf, selected=(i == self.selected), info=info, hotkey=str(i + 1))
        for w in self.widgets:
            w.draw_drag(surf)

    def _row_end(self, node: VirtualNode, end: str) -> Optional[VirtualNode]:
        """The virtual node at the given end of this node's player's row (for the [ / ] keys)."""
        g = self.tutorial.players[self.tutorial.node_owner[node.node_id]].game \
            if self.tutorial and node.node_id in self.tutorial.node_owner else self.game
        if not g or node.node_id not in g.nodes:
            return None
        seq = g.player_of(node.node_id).sequence
        if not seq:
            return None
        e = seq.entries[0] if end == "left" else seq.entries[-1]
        other = self.hub.nodes.get(e.node_id) if e.node_id else None
        return other if isinstance(other, VirtualNode) and other is not node else None

    def node_hotkey(self, ev):
        k = ev.key
        if pygame.K_1 <= k <= pygame.K_6:
            idx = k - pygame.K_1
            if idx < len(self.widgets):
                self.selected = idx
            return
        if k == pygame.K_TAB and self.widgets:
            self.selected = (self.selected + (-1 if ev.mod & pygame.KMOD_SHIFT else 1)) % len(self.widgets)
            return
        node = self.selected_virtual()
        if node is None:
            return
        if k == pygame.K_a:
            node.tap("L")
        elif k == pygame.K_d:
            node.tap("R")
        elif k in (pygame.K_w, pygame.K_s):
            node.tap("V")
        elif k in (pygame.K_f, pygame.K_SPACE):
            node.toggle_flip()
        elif k == pygame.K_x:
            node.click_buttons(0x3)
        elif k in (pygame.K_LEFTBRACKET, pygame.K_RIGHTBRACKET, pygame.K_b):
            # bump against the left end ([) or right end (] or B) of the row
            end = "left" if k == pygame.K_LEFTBRACKET else "right"
            other = self._row_end(node, end)
            node.tap("R" if end == "left" else "L")
            if other:
                other.tap("L" if end == "left" else "R")

    def selected_virtual(self) -> Optional[VirtualNode]:
        if not self.widgets:
            return None
        node = self.widgets[self.selected].node
        return node if isinstance(node, VirtualNode) else None

    def _button_key(self, ev) -> bool:
        """Q/E/Z/C hold the selected virtual node's buttons while the key is down (long presses)."""
        if ev.type not in (pygame.KEYDOWN, pygame.KEYUP) or ev.key not in BUTTON_KEYS:
            return False
        if self.screen == "settings" and self.settings_screen.editing:
            return False
        node = self.selected_virtual()
        if node is None:
            return True
        if ev.type == pygame.KEYDOWN:
            node.press(BUTTON_KEYS[ev.key])
        else:
            node.release(BUTTON_KEYS[ev.key])
        return True

    # ------------------------------------------------------------------ session
    def start_game(self, maze=None):
        setup = self.lobby.setup()
        if not setup or self.lobby.problems(self.hub.nodes):
            return
        self.catalog = self._load_catalog()
        now = time.monotonic()
        try:
            self.game = Game(self.cfg, self.catalog, setup, maze=maze, now=now)
        except Exception as e:
            log.exception("could not start game")
            self.lobby_screen.flash["__error__"] = now + 3
            print(f"could not start game: {e}")
            return
        self.controller = InputController(self.game, self.cfg)
        for s in setup:
            node = self.game.nodes[s.node_id]
            hub_node = self.hub.nodes.get(s.node_id)
            if s.scheme == "gesture":
                if isinstance(hub_node, VirtualNode):
                    node.face_down = hub_node.face_down
                else:
                    node.face_down = self.last_flip.get(s.node_id, False)
            node.connected = bool(hub_node and hub_node.connected)
        self.game_started_at = now
        self.screen = "game"
        self.hub.drain()   # drop events from before the start

    def rematch(self):
        # spec HUB-Q17(e): full reset (new deck, hands, queues) on the same maze.
        if self.game:
            self.start_game(maze=self.game.maze)

    def new_maze(self):
        seed = self.cfg.maze_seed
        m = None
        if seed is not None and self.game and self.game.maze.seed == seed:
            m = generate_maze(self.cfg.maze_size, self.cfg.maze_braid_fraction, None,
                              self.cfg.get("maze_algorithm", "backtracker"))
        self.start_game(maze=m)

    def to_lobby(self):
        self.screen = "lobby"
        self.game = None
        self.controller = None
        self.tutorial = None

    def open_tutorial(self):
        """Tutorial for every player that has nodes (14 B5)."""
        players = {}
        for p in range(self.lobby.n_players):
            nodes = [s for s in self.lobby.setup() if s.player == p]
            if nodes and all(self.hub.nodes.get(s.node_id) and self.hub.nodes[s.node_id].connected for s in nodes):
                players[p] = (PLAYER_COLOURS[p], nodes)
        if not players:
            return
        self.catalog = self._load_catalog()
        self.tutorial = Tutorial(self.cfg, self.catalog, players, time.monotonic())
        for pt in self.tutorial.players.values():
            for ns in pt.nodes:
                hn = self.hub.nodes.get(ns.node_id)
                if ns.scheme == "gesture" and hn is not None:
                    pt.game.nodes[ns.node_id].face_down = (hn.face_down if isinstance(hn, VirtualNode)
                                                           else hn.phys_face_down)
        self.hub.drain()
        self.screen = "tutorial"

    def quit_tutorial(self):
        self.tutorial = None
        self.screen = "lobby"

    def open_settings(self, back="lobby"):
        self.settings_screen.open(back)
        self.screen = "settings"

    def close_settings(self, back):
        self.screen = back if (back != "game" or self.game) else "lobby"

    # ------------------------------------------------------------------ display sync
    def _lobby_display(self, nid: str) -> dict[str, str]:
        # spec HUB-Q19: HAND:0 = unassigned; MSG identifies the node / its seat while in the lobby.
        p = self.lobby.player_of(nid)
        # spec HUB-Q25: the lobby uses the same DOFF rule as the game, from the node's chosen scheme.
        doff = doff_for(self.cfg, self.lobby.schemes.get(nid, "gesture"))
        if p is None:
            return {"COL": "808080", "HAND": "0", "CARD": "-", "SEQ": "-", "REG": "0000", "DOFF": doff,
                    "MSG": f"NODE {nid}"[:14]}
        h = self.lobby.hand_of(nid)
        return {"COL": PLAYER_COLOURS[p], "HAND": str(h), "CARD": "-", "SEQ": "-", "REG": "0000", "DOFF": doff,
                "MSG": f"P{p + 1} #{h}"}

    def _sync_displays(self):
        for nid in self.hub.nodes:
            if self.tutorial and self.screen == "tutorial":
                desired = self.tutorial.node_display(nid) or {
                    "COL": "404040", "HAND": "0", "CARD": "-", "SEQ": "-", "REG": "0000", "DOFF": "0",
                    "MSG": "NOT IN TUTOR.", "HINT": ""}
            elif self.game and self.screen in ("game", "settings") and nid in self.game.nodes:
                desired = self.game.node_display(nid)
            elif self.game and self.screen in ("game", "settings"):
                desired = {"COL": "404040", "HAND": "0", "CARD": "-", "SEQ": "-", "REG": "0000", "DOFF": "0",
                           "MSG": self.game.message if self.game.state == ENDED else "NOT PLAYING", "HINT": ""}
            else:
                desired = self._lobby_display(nid)
                desired["HINT"] = ""
            self.hub.sync(nid, desired)

    # ------------------------------------------------------------------ events
    def _dispatch_node_events(self):
        for ev in self.hub.drain():
            if ev.kind in (E.FLIP, E.FSYNC, E.FSTATE):
                self.last_flip[ev.node_id] = ev.value == "1"
                n = self.hub.nodes.get(ev.node_id)
                if n is not None:
                    n.phys_face_down = ev.value == "1"
            if ev.kind == E.HELLO:
                n = self.hub.nodes.get(ev.node_id)
                if n:
                    n.invalidate()
            for w in self.widgets:
                if w.node.node_id == ev.node_id:
                    w.flash_until = time.monotonic() + 0.15
            if self.tutorial and self.screen == "tutorial":
                self.tutorial.handle(ev)
            elif self.controller and self.game and self.screen in ("game", "settings"):
                self.controller.handle(ev)
            elif self.screen == "lobby":
                self.lobby_screen.on_node_event(ev)

    def _map_event(self, ev):
        if hasattr(ev, "pos") and ev.type in (pygame.MOUSEBUTTONDOWN, pygame.MOUSEBUTTONUP, pygame.MOUSEMOTION):
            ox, oy, s = widgets.MOUSE_XFORM
            d = dict(ev.dict)
            d["pos"] = (int((ev.pos[0] - ox) / s), int((ev.pos[1] - oy) / s))
            return pygame.event.Event(ev.type, d)
        return ev

    def step(self):
        now = time.monotonic()
        for ev in pygame.event.get():
            if ev.type == pygame.QUIT:
                self.running = False
                continue
            if ev.type == pygame.VIDEORESIZE:
                self.window = pygame.display.set_mode((max(400, ev.w), max(240, ev.h)), pygame.RESIZABLE)
                continue
            ev = self._map_event(ev)
            if ev.type == pygame.KEYDOWN and ev.key == pygame.K_F12:
                self.debug_view.toggle()
                continue
            if self.debug_view.handle(ev):
                continue
            if self._button_key(ev):
                continue
            if ev.type == pygame.KEYUP:
                continue
            self._screens()[self.screen].handle(ev)
        self._dispatch_node_events()
        if self.tutorial and self.screen == "tutorial":
            self.tutorial.tick(now)
        if self.controller:
            self.controller.tick(now)
        if self.game:
            self.game.update(now)
        self._sync_displays()
        self._screens()[self.screen].draw(self.canvas, now)
        self.debug_view.draw(self.canvas, now)
        self._present()

    def _screens(self):
        return {"lobby": self.lobby_screen, "game": self.game_screen, "settings": self.settings_screen,
                "tutorial": self.tutorial_screen}

    def _present(self):
        ww, wh = self.window.get_size()
        s = min(ww / CANVAS[0], wh / CANVAS[1])
        w, h = int(CANVAS[0] * s), int(CANVAS[1] * s)
        ox, oy = (ww - w) // 2, (wh - h) // 2
        widgets.MOUSE_XFORM = (ox, oy, s)
        self.window.fill((0, 0, 0))
        if (w, h) == CANVAS:
            self.window.blit(self.canvas, (ox, oy))
        else:
            self.window.blit(pygame.transform.smoothscale(self.canvas, (w, h)), (ox, oy))
        pygame.display.flip()

    def run(self):
        while self.running:
            self.step()
            self.clock.tick(60)
        for n in list(self.hub.nodes.values()):
            n.close()
        pygame.quit()

    def screenshot(self, path: str):
        pygame.image.save(self.canvas, path)


def setup_file_logging(logs_dir: Path) -> None:
    """Rolling log files in <project>/logs: hub.log (INFO+) and ble_debug.log (every BLE line)."""
    try:
        logs_dir.mkdir(parents=True, exist_ok=True)
        fmt = logging.Formatter("%(asctime)s %(levelname)s %(name)s: %(message)s")
        hub = logging.handlers.RotatingFileHandler(logs_dir / "hub.log", maxBytes=2_000_000, backupCount=3,
                                                   encoding="utf-8")
        hub.setLevel(logging.INFO)
        hub.setFormatter(fmt)
        logging.getLogger().addHandler(hub)
        ble = logging.handlers.RotatingFileHandler(logs_dir / "ble_debug.log", maxBytes=5_000_000, backupCount=3,
                                                   encoding="utf-8")
        ble.setLevel(logging.DEBUG)
        ble.setFormatter(fmt)
        bl = logging.getLogger("ccmaze.ble")
        bl.setLevel(logging.DEBUG)
        bl.addHandler(ble)
        bl.propagate = False
    except OSError as e:  # read-only folder etc.: run without file logs
        logging.getLogger(__name__).warning("no log files: %s", e)


def main(argv=None):
    import argparse

    ap = argparse.ArgumentParser(description="Command Card Maze Race — desktop hub")
    ap.add_argument("--config", default=str(DEFAULT_CONFIG_PATH),
                    help="path to config.json (created with defaults if missing); default: the project folder")
    ap.add_argument("--virtual", type=int, default=0, help="add N virtual nodes at startup")
    ap.add_argument("--players", type=int, default=None, help="player count at startup (1-4)")
    ap.add_argument("--no-ble", action="store_true", help="disable Bluetooth entirely")
    ap.add_argument("--start", action="store_true", help="start a game immediately (with --virtual)")
    ap.add_argument("--window", default=None, help="window size, e.g. 1280x768")
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args(argv)
    logging.basicConfig(level=logging.DEBUG if a.verbose else logging.INFO, format="%(levelname)s %(name)s: %(message)s")
    setup_file_logging(Path(a.config).resolve().parent / "logs")
    win = tuple(int(x) for x in a.window.lower().split("x")) if a.window else None
    cfg_path = Path(a.config)
    if not cfg_path.exists():
        Config({}, cfg_path).save()
    app = App(cfg_path, n_virtual=a.virtual, players=a.players, ble=not a.no_ble, window=win, autostart=a.start)
    app.run()
