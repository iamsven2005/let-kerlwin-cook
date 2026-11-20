"""Headless smoke test of the Pygame UI with virtual nodes (SDL dummy driver)."""
import os
import time

import pytest

os.environ.setdefault("SDL_VIDEODRIVER", "dummy")
os.environ.setdefault("SDL_AUDIODRIVER", "dummy")
pygame = pytest.importorskip("pygame")

from ccmaze.config import Config  # noqa: E402
from ccmaze.sim.game import ENDED  # noqa: E402
from ccmaze.ui.app import App  # noqa: E402


def key(app, k, mod=0):
    pygame.event.post(pygame.event.Event(pygame.KEYDOWN, key=k, mod=mod, unicode=""))
    app.step()


def tap_key(app, k):
    """Press and release a held-button key, then let the chord window close."""
    pygame.event.post(pygame.event.Event(pygame.KEYDOWN, key=k, mod=0, unicode=""))
    app.step()
    pygame.event.post(pygame.event.Event(pygame.KEYUP, key=k, mod=0, unicode=""))
    frames(app, 0.2)


def frames(app, sec):
    t = time.monotonic()
    while time.monotonic() - t < sec:
        app.step()
        time.sleep(0.005)


def test_full_ui_flow(tmp_path):
    cfgp = tmp_path / "config.json"
    Config({"maze_seed": 3, "pause_after_step_s": 0.02}, cfgp).save()
    app = App(cfgp, n_virtual=3, players=1, ble=False, window=(800, 480))
    app.step()
    assert app.screen == "lobby"
    key(app, pygame.K_RETURN)
    assert app.screen == "game" and app.game is not None
    # keyboard play on node 1 (21.1: Q E Z C = V F L R): V, then F: start
    key(app, pygame.K_1)
    tap_key(app, pygame.K_z)          # L latched ...
    assert app.game.nodes["V1"].register == {"L"}
    key(app, pygame.K_x)              # ... X = two buttons together: clear
    frames(app, 0.2)
    assert not app.game.nodes["V1"].register
    tap_key(app, pygame.K_q)
    tap_key(app, pygame.K_e)
    assert app.game.players[0].sequence is not None
    # node 2: V, then R (C = BR): it joins at the right end
    key(app, pygame.K_2)
    tap_key(app, pygame.K_q)
    tap_key(app, pygame.K_c)
    assert [e.node_id for e in app.game.players[0].sequence.entries] == ["V1", "V2"]
    # node 3: V, then L (Z = BL): left end; the latch shows a countdown in between (19.2)
    key(app, pygame.K_3)
    tap_key(app, pygame.K_q)
    assert app.latch_info("V3")[0] == "V"
    tap_key(app, pygame.K_z)
    assert [e.node_id for e in app.game.players[0].sequence.entries] == ["V3", "V1", "V2"]
    # the pending chunk is previewed on the map
    assert app.game.route_plan(app.game.players[0]).pending is not None
    key(app, pygame.K_1)
    key(app, pygame.K_f)          # turning the head over (physical flip: down -> up): push
    frames(app, 0.05)
    key(app, pygame.K_f)
    frames(app, 0.05)
    assert app.game.players[0].sequence is None
    assert len(app.game.players[0].route) == 1
    assert any("pushed" in e.text for e in app.game.log)
    # node displays were synced to the virtual widgets
    v = app.hub.nodes["V3"] if "V3" in app.hub.nodes else list(app.hub.nodes.values())[2]
    assert v.display["HAND"] in ("1", "2", "3")
    # settings round trip
    app.open_settings("game")
    frames(app, 0.02)
    key(app, pygame.K_ESCAPE)
    assert app.screen == "game"
    # force a win and use the overlay buttons
    g = app.game
    g.state = ENDED
    g.winners = [0]
    g.win_time = time.monotonic()
    frames(app, 0.02)
    app.rematch()
    assert app.screen == "game" and app.game is not g and app.game.maze is g.maze
    app.new_maze()
    assert app.game.maze is not None
    # BLE debug view (F12) and snapshot
    key(app, pygame.K_F12)
    assert app.debug_view.visible
    p = app.debug_view.save()
    assert p.exists() and "V1" in p.read_text()
    key(app, pygame.K_F12)
    key(app, pygame.K_ESCAPE)
    assert app.screen == "lobby"
    # tutorial: controls test, then lessons; a discard completes lesson 1
    app.open_tutorial()
    assert app.screen == "tutorial" and app.tutorial.phase == "controls"
    frames(app, 0.05)
    key(app, pygame.K_RETURN)
    assert app.tutorial.phase == "lessons"
    key(app, pygame.K_1)
    key(app, pygame.K_f)
    frames(app, 0.1)
    pt = app.tutorial.players[0]
    assert pt.completed_at is not None
    assert app.hub.nodes["V1"].display["HINT"] in ("WELL DONE!", "F = DISCARD")
    key(app, pygame.K_ESCAPE)
    assert app.screen == "lobby"
    pygame.quit()
