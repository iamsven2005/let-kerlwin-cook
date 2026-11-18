"""Procedural maze with 4-fold rotational symmetry and BFS fairness checks.

Generation: randomized Kruskal over *orbits* of walls under 90-degree rotation
about the centre tile, so the maze is identical from every corner. Every
corner therefore has the same shortest-path distance to the centre; this is
still verified with BFS (and the maze regenerated if a check ever fails).

Note: a rotation-symmetric spanning tree must connect the centre on all four
sides (its four walls form a single orbit), so the objective is always open on
all sides.
"""
from __future__ import annotations

import random
from collections import deque

from .cards import DIRS

Pos = tuple[int, int]
Edge = tuple[Pos, Pos]


class MazeError(RuntimeError):
    pass


def _norm(a: Pos, b: Pos) -> Edge:
    return (a, b) if a <= b else (b, a)


class Maze:
    def __init__(self, size: int, open_edges: set[Edge], seed: int | None = None):
        self.size = size
        self.open_edges = open_edges
        self.seed = seed
        c = size // 2
        self.centre: Pos = (c, c)

    # --- geometry -----------------------------------------------------------
    @property
    def corners(self) -> list[Pos]:
        n = self.size - 1
        return [(0, 0), (n, 0), (n, n), (0, n)]  # TL, TR, BR, BL

    def in_bounds(self, p: Pos) -> bool:
        return 0 <= p[0] < self.size and 0 <= p[1] < self.size

    def can_move(self, p: Pos, d: str) -> bool:
        dx, dy = DIRS[d]
        q = (p[0] + dx, p[1] + dy)
        return self.in_bounds(q) and _norm(p, q) in self.open_edges

    def step(self, p: Pos, d: str) -> Pos:
        dx, dy = DIRS[d]
        return (p[0] + dx, p[1] + dy)

    def has_wall(self, p: Pos, d: str) -> bool:
        return not self.can_move(p, d)

    def neighbours(self, p: Pos):
        for d in DIRS:
            if self.can_move(p, d):
                yield self.step(p, d)

    def bfs(self, start: Pos) -> dict[Pos, int]:
        dist = {start: 0}
        q = deque([start])
        while q:
            p = q.popleft()
            for n in self.neighbours(p):
                if n not in dist:
                    dist[n] = dist[p] + 1
                    q.append(n)
        return dist

    def corner_distances(self) -> list[int | None]:
        d = self.bfs(self.centre)
        return [d.get(c) for c in self.corners]

    def check_fairness(self) -> tuple[bool, str]:
        dist = self.bfs(self.centre)
        cd = [dist.get(c) for c in self.corners]
        if any(x is None for x in cd):
            return False, f"corner unreachable: {cd}"
        if len(set(cd)) != 1:
            return False, f"unequal corner distances: {cd}"
        if len(dist) != self.size * self.size:
            return False, "maze not fully connected"
        return True, f"all corners at distance {cd[0]}"

    def dead_ends(self) -> list[Pos]:
        return [
            (x, y)
            for y in range(self.size)
            for x in range(self.size)
            if sum(1 for _ in self.neighbours((x, y))) == 1
        ]


# --- generation -----------------------------------------------------------------

class _UF:
    def __init__(self):
        self.p: dict = {}

    def find(self, a):
        self.p.setdefault(a, a)
        while self.p[a] != a:
            self.p[a] = self.p[self.p[a]]
            a = self.p[a]
        return a

    def union(self, a, b) -> bool:
        ra, rb = self.find(a), self.find(b)
        if ra == rb:
            return False
        self.p[ra] = rb
        return True


def _rot(p: Pos, n: int) -> Pos:
    return (n - 1 - p[1], p[0])


def _edge_orbit(e: Edge, n: int) -> frozenset[Edge]:
    out = set()
    a, b = e
    for _ in range(4):
        out.add(_norm(a, b))
        a, b = _rot(a, n), _rot(b, n)
    return frozenset(out)


def _cell_orbit(p: Pos, n: int) -> frozenset[Pos]:
    out = set()
    for _ in range(4):
        out.add(p)
        p = _rot(p, n)
    return frozenset(out)


def _all_edges(n: int) -> list[Edge]:
    es = []
    for y in range(n):
        for x in range(n):
            if x + 1 < n:
                es.append(((x, y), (x + 1, y)))
            if y + 1 < n:
                es.append(((x, y), (x, y + 1)))
    return es


def _orbits(n: int) -> list[frozenset[Edge]]:
    seen: set[Edge] = set()
    out = []
    for e in _all_edges(n):
        if e in seen:
            continue
        o = _edge_orbit(e, n)
        seen |= o
        out.append(o)
    return out


def _backtracker(n: int, rng: random.Random) -> set[Edge]:
    """Symmetric recursive backtracker grown out from the centre.

    Works on cell orbits: each step opens one wall orbit from a visited orbit to
    a wholly unvisited one, so every step adds 4 cells and 4 edges (a tree).
    """
    c = (n // 2, n // 2)
    visited: set[Pos] = {c}
    open_edges: set[Edge] = set()
    stack: list[Pos] = [c]
    while stack:
        p = stack[-1]
        options = []
        for d in "UDLR":
            q = (p[0] + DIRS[d][0], p[1] + DIRS[d][1])
            if 0 <= q[0] < n and 0 <= q[1] < n and q not in visited:
                if not (_cell_orbit(q, n) & visited):
                    options.append(q)
        if not options:
            stack.pop()
            continue
        q = rng.choice(options)
        open_edges |= _edge_orbit(_norm(p, q), n)
        visited |= _cell_orbit(q, n)
        stack.append(q)
    return open_edges


def _kruskal(n: int, rng: random.Random) -> set[Edge]:
    orbits = sorted(_orbits(n), key=lambda o: sorted(o))  # stable order before shuffle
    rng.shuffle(orbits)
    uf = _UF()
    open_edges: set[Edge] = set()
    rest = []
    for orb in orbits:
        # Accept the orbit only if adding all of its edges keeps the graph a forest.
        mini = _UF()
        ok = True
        for a, b in orb:
            if not mini.union(uf.find(a), uf.find(b)):
                ok = False
                break
        if ok:
            for a, b in orb:
                uf.union(a, b)
            open_edges |= orb
        else:
            rest.append(orb)

    # Rarely the orbit-Kruskal leaves >1 component: connect with extra orbits.
    cells = [(x, y) for y in range(n) for x in range(n)]
    def components() -> int:
        return len({uf.find(c) for c in cells})
    if components() > 1:
        rng.shuffle(rest)
        for orb in rest:
            if any(uf.find(a) != uf.find(b) for a, b in orb):
                for a, b in orb:
                    uf.union(a, b)
                open_edges |= orb
                if components() == 1:
                    break
    return open_edges


def _try_generate(n: int, braid: float, rng: random.Random, algorithm: str = "backtracker") -> set[Edge]:
    if algorithm == "kruskal":
        open_edges = _kruskal(n, rng)
    elif algorithm == "backtracker":
        open_edges = _backtracker(n, rng)
    else:
        raise ValueError(f"unknown maze algorithm {algorithm!r}")

    # Braiding (spec HUB-Q16): open a fraction of dead-end groups.
    if braid > 0:
        m = Maze(n, open_edges)
        groups: list[frozenset[Pos]] = []
        seen: set[Pos] = set()
        for p in m.dead_ends():
            if p in seen:
                continue
            g = _cell_orbit(p, n)
            seen |= g
            groups.append(g)
        groups.sort(key=lambda g: sorted(g))
        rng.shuffle(groups)
        k = round(braid * len(groups))
        for g in groups[:k]:
            p = min(g)
            if sum(1 for _ in m.neighbours(p)) != 1:
                continue  # no longer a dead end
            closed = [d for d in "UDLR" if m.in_bounds(m.step(p, d)) and not m.can_move(p, d)]
            if not closed:
                continue
            d = rng.choice(sorted(closed))
            open_edges |= _edge_orbit(_norm(p, m.step(p, d)), n)
    return open_edges


def generate_maze(size: int = 15, braid: float = 0.15, seed: int | None = None,
                  algorithm: str = "backtracker", max_tries: int = 50) -> Maze:
    if size < 3 or size % 2 == 0:
        raise ValueError("maze size must be odd and >= 3")
    if seed is None:
        seed = random.SystemRandom().randrange(1 << 31)
    rng = random.Random(seed)
    for _ in range(max_tries):
        m = Maze(size, _try_generate(size, braid, rng, algorithm), seed)
        ok, _ = m.check_fairness()
        if ok:
            return m
    raise MazeError(f"could not generate a fair maze in {max_tries} tries (seed {seed})")
