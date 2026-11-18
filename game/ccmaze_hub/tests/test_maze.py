import pytest

from ccmaze.sim.maze import _rot, generate_maze


@pytest.mark.parametrize("algorithm", ["backtracker", "kruskal"])
@pytest.mark.parametrize("braid", [0.0, 0.15, 0.5])
def test_fairness_many_seeds(algorithm, braid):
    for seed in range(60):
        m = generate_maze(15, braid, seed, algorithm)
        dist = m.bfs(m.centre)
        assert len(dist) == 15 * 15, "every tile reachable"
        cd = [dist[c] for c in m.corners]
        assert len(set(cd)) == 1, f"seed {seed}: {cd}"
        # independent check: BFS from each corner to the centre
        for c in m.corners:
            assert m.bfs(c)[m.centre] == cd[0]


@pytest.mark.parametrize("size", [5, 7, 9, 15, 21])
def test_sizes(size):
    m = generate_maze(size, 0.15, 7)
    assert m.centre == (size // 2, size // 2)
    assert m.check_fairness()[0]


def test_rotational_symmetry():
    m = generate_maze(15, 0.15, 42)
    for y in range(15):
        for x in range(15):
            for d, rd in zip("URDL", "RDLU"):
                assert m.can_move((x, y), d) == m.can_move(_rot((x, y), 15), rd)


def test_seed_is_reproducible():
    a = generate_maze(15, 0.15, 123)
    b = generate_maze(15, 0.15, 123)
    c = generate_maze(15, 0.15, 124)
    assert a.open_edges == b.open_edges
    assert a.open_edges != c.open_edges


def test_braiding_adds_openings():
    a = generate_maze(15, 0.0, 5)
    b = generate_maze(15, 0.5, 5)
    assert len(a.open_edges) == 15 * 15 - 1   # perfect maze = spanning tree
    assert len(b.open_edges) > len(a.open_edges)
    assert len(b.dead_ends()) < len(a.dead_ends())


def test_even_size_rejected():
    with pytest.raises(ValueError):
        generate_maze(14)


def test_border_is_closed():
    m = generate_maze(9, 0.3, 1)
    for i in range(9):
        assert not m.can_move((i, 0), "U")
        assert not m.can_move((i, 8), "D")
        assert not m.can_move((0, i), "L")
        assert not m.can_move((8, i), "R")
