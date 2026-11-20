import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ccmaze.config import Config  # noqa: E402
from ccmaze.sim.cards import Card, CardType, load_catalog  # noqa: E402
from ccmaze.sim.controls import InputController  # noqa: E402
from ccmaze.sim.events import NodeEvent  # noqa: E402
from ccmaze.sim.game import Game, NodeSetup  # noqa: E402
from ccmaze.sim.maze import Maze, _norm  # noqa: E402


def open_maze(size=5, closed=()):
    """A maze with every internal wall open, except the given (pos, dir) walls."""
    edges = set()
    for y in range(size):
        for x in range(size):
            if x + 1 < size:
                edges.add(_norm((x, y), (x + 1, y)))
            if y + 1 < size:
                edges.add(_norm((x, y), (x, y + 1)))
    d = {"U": (0, -1), "D": (0, 1), "L": (-1, 0), "R": (1, 0)}
    for (x, y), k in closed:
        dx, dy = d[k]
        edges.discard(_norm((x, y), (x + dx, y + dy)))
    return Maze(size, edges)


def make_game(nodes_per_player=(3,), cfg=None, maze=None, seed=1, scheme="gesture", **cfgkw):
    cfg = cfg or Config(cfgkw)
    setup = []
    k = 0
    for p, n in enumerate(nodes_per_player):
        for h in range(1, n + 1):
            k += 1
            sch = scheme[k - 1] if isinstance(scheme, (list, tuple)) else scheme
            setup.append(NodeSetup(f"N{k}", p, h, sch))
    g = Game(cfg, load_catalog(None), setup, maze=maze or open_maze(5), rng_seed=seed)
    return g, InputController(g, cfg)


class Driver:
    """Feeds timestamped events to the controller, ticking the game as it goes."""

    def __init__(self, game, ctl, t=0.0):
        self.g, self.c, self.t = game, ctl, t

    def at(self, dt):
        self.t += dt
        self.c.tick(self.t)
        self.g.update(self.t)
        return self

    def ev(self, node, kind, value, dt=0.0):
        self.t += dt
        self.c.handle(NodeEvent(node, kind, value, self.t))
        self.g.update(self.t)
        return self

    def tap(self, node, side, dt=0.01):
        return self.ev(node, "T", side, dt)

    def flip(self, node, down, dt=0.01):
        return self.ev(node, "F", "1" if down else "0", dt)

    def btn(self, node, mask, dt=0.01):
        return self.ev(node, "B", format(mask, "X"), dt)

    def settle(self, dt=3.0):
        return self.at(dt)


@pytest.fixture
def driver_factory():
    def f(*a, **kw):
        g, c = make_game(*a, **kw)
        return Driver(g, c)
    return f


def card(steps):
    return Card(CardType(steps, "test"))
