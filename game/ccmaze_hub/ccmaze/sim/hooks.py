"""Extension points for [OPTIONAL] features. Defaults implement the base game.

- StartPositionProvider  -> players_5_6
- WinCondition           -> coop_mode (or other objectives)
- ItemEffectHook         -> pickup_items
- CollisionHook          -> collision_penalty
(ExecutionMode for interrupt_mode lives in execution.py.)
"""
from __future__ import annotations

from typing import TYPE_CHECKING, Protocol

if TYPE_CHECKING:  # pragma: no cover
    from .execution import Avatar
    from .game import Game
    from .maze import Maze, Pos


class StartPositionProvider(Protocol):
    def positions(self, maze: "Maze", n_players: int) -> list["Pos"]: ...


class CornerStarts:
    """Base game: 1-4 players in the corners. spec HUB-Q17(f): 2 players get opposite corners."""

    ORDER = {1: [0], 2: [0, 2], 3: [0, 2, 1], 4: [0, 1, 2, 3]}  # indices into maze.corners

    def positions(self, maze: "Maze", n_players: int) -> list["Pos"]:
        if n_players not in self.ORDER:
            raise ValueError("base game supports 1-4 players (players_5_6 is an optional feature)")
        return [maze.corners[i] for i in self.ORDER[n_players]]


class WinCondition(Protocol):
    def on_enter(self, game: "Game", avatar: "Avatar", t: float) -> bool:
        """Return True if this avatar entering its tile at time t wins."""
        ...


class CentreWin:
    def on_enter(self, game: "Game", avatar: "Avatar", t: float) -> bool:
        return avatar.pos == game.maze.centre


class ItemEffectHook(Protocol):
    def on_enter(self, game: "Game", avatar: "Avatar", t: float) -> None: ...


class NoItems:
    def on_enter(self, game: "Game", avatar: "Avatar", t: float) -> None:
        return None


class CollisionHook(Protocol):
    def on_blocked(self, game: "Game", avatar: "Avatar", direction: str, t: float) -> float:
        """Return the extra delay (seconds) to apply after a blocked step."""
        ...


class NoCollisionPenalty:
    def on_blocked(self, game: "Game", avatar: "Avatar", direction: str, t: float) -> float:
        return 0.0
