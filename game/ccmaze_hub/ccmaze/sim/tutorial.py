"""Tutorial and controls test (section 14 B5), headless.

Each tutorial player gets their own practice game (spec HUB-Q32): a small symmetric maze, their
own nodes and control scheme, and a scripted deck per lesson, so players progress independently.
A lesson is completed by actually doing it; the hub watches the controller's actions and the game.

Lessons that can't apply are skipped automatically: single-node players have no second node for
lessons 3-4; lesson 6 needs a stash (single-node by default); single-node players do lessons 7-8
with the stash, so they need it enabled too.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable, Optional

from .cards import Card, CardType, Catalog, ControlType
from .controls import InputController
from .deck import Deck, ScriptedDeck
from .events import NodeEvent
from .game import ENDED, Game, NodeSetup
from .maze import Maze, generate_maze

PRACTICE_SIZE = 7
FREE_PRACTICE_SIZE = 9
PRACTICE_SEED = 4242
DONE_PAUSE_S = 1.5


def mv(steps: str, fam: str) -> CardType:
    return CardType(steps, fam)


IF_SQ = ControlType("IF", "O", mv("RR", "I3"))
FOR_ = ControlType("FOR")


@dataclass(frozen=True)
class Lesson:
    key: str
    title: str
    gesture: str            # instruction, Gesture mode
    manual: str             # instruction, Manual mode
    hint_g: str             # node HINT (<= 14 chars), Gesture
    hint_m: str             # node HINT, Manual
    script: tuple           # card types dealt in order (cycled); None = normal random deck
    needs: str = ""         # "" | "multi" (2+ nodes) | "stash" | "combo" (multi, or single with stash)
    single: str = ""        # replacement instruction for single-node players (lessons 7-8)
    single_manual: str = ""


FILLER = (mv("R", "D"), mv("D", "D"), mv("RR", "I3"), mv("DD", "I3"), mv("RD", "V"), mv("DR", "V"))

LESSONS: list[Lesson] = [
    Lesson("discard", "1. Flip to discard",
           "Turn any node face-down with nothing tapped: its card is discarded and the cards above slide "
           "down. Turn it back up to see the new card.",
           "Press F (bottom-right) on any node with nothing pressed before it: its card is discarded.",
           "FLIP=DISCARD", "PRESS F", FILLER),
    Lesson("start", "2. Start a sequence",
           "Tap a node flat on the table (V), then turn it face-down. It becomes the HEAD of your row.",
           "Press V (bottom-left), then F. The node becomes the HEAD of your row.",
           "V THEN FLIP", "V THEN F", FILLER),
    Lesson("add", "3. Add a card to the right, then to the left",
           "With a head on the table: tap another node (V), then bump it against the RIGHT end of the row. "
           "Then cancel (button on the head), start again, and add a card at the LEFT end.",
           "With a head: press V on another node, then R to add it at the RIGHT end. Then cancel (two "
           "buttons on the head), start again, and use V then L for the LEFT end.",
           "V+BUMP AN END", "V THEN L OR R", FILLER, needs="multi"),
    Lesson("remove", "4. Remove a card",
           "Build a row of two, then flip the added (non-head) node down and back up within 3 s: it leaves "
           "the row and keeps its card.",
           "Build a row of two, then HOLD F for 1 s on the added (non-head) node: it leaves the row.",
           "FLIP 2X=REMOVE", "HOLD F=REMOVE", FILLER, needs="multi"),
    Lesson("push", "5. Push and watch the path",
           "Build a row; its path appears on the maze as you build. Flip the head back up to push, and "
           "watch your avatar follow the path.",
           "Build a row; its path appears on the maze. Press F on the head to push and watch the avatar.",
           "FLIP HEAD UP", "F ON HEAD", FILLER),
    Lesson("stash", "6. Stash in / use stash",
           "Tap L then flip down to stash the card (it shows on this screen). Then start a sequence and tap "
           "V on the face-down head to add the stash card at the right end.",
           "Press L then F to stash the card. Then start a sequence (V, F) and press V on the head to add "
           "the stash card at the right end.",
           "L+FLIP=STASH", "L,F=STASH", (mv("RD", "V"), mv("DR", "V"), mv("RR", "I3")), needs="stash"),
    Lesson("if", "7. IF card combo",
           "The IF card reads 'IF next is a SQUARE -> also do RR'. Put the IF card directly LEFT of the "
           "square card in your row and push: the bonus move runs first (dashed on the map).",
           "Put the IF card directly LEFT of the square card in your row (add with V, then L or R) and push.",
           "IF THEN SQUARE", "IF THEN SQUARE",
           (mv("RDL", "O"), IF_SQ, mv("D", "D"), mv("R", "D")), needs="combo",
           single="Stash the square card first (L, flip). Start with the IF card (V, flip), tap V on it to add "
                  "the stash to its right, then flip it up to push.",
           single_manual="Stash the square (L, F). Start with the IF card (V, F), press V on it to add the "
                         "stash on its right, then F to push."),
    Lesson("for", "8. FOR card combo",
           "FOR repeats the card right after it once per card after the FOR. Row: FOR, then the card to "
           "repeat, then more cards to count. Push and watch the 'xn' on the map.",
           "Row: FOR, then the card to repeat, then more cards (add with V then R). Press F on the head.",
           "FOR THEN CARD", "FOR THEN CARD",
           (mv("R", "D"), mv("DD", "I3"), FOR_), needs="combo",
           single="Stash the DD card (L, flip). Start with FOR (V, flip), tap V on it to add DD on its right, "
                  "then flip it up to push.",
           single_manual="Stash DD (L, F). Start with FOR (V, F), press V on it to add DD, then F to push."),
    Lesson("practice", "9. Free practice: reach the centre",
           "Use everything you've learned to reach the gold centre of this small maze.",
           "Use everything you've learned to reach the gold centre of this small maze.",
           "REACH CENTRE", "REACH CENTRE", None),
]
CONTROLS = Lesson("controls", "Controls test",
                  "Tap, flip and press buttons: every raw event shows up here.",
                  "Press the buttons: every raw event shows up here.",
                  "TEST CONTROLS", "TEST CONTROLS", FILLER)


def applies(lesson: Lesson, n_nodes: int, stash_on: bool) -> bool:
    if lesson.needs == "multi":
        return n_nodes >= 2
    if lesson.needs == "stash":
        return stash_on
    if lesson.needs == "combo":
        return n_nodes >= 2 or stash_on
    return True


@dataclass
class LessonState:
    lesson: Lesson
    status: str = "todo"      # "todo" | "active" | "done" | "skipped" | "n/a"


class PlayerTutorial:
    """One player's way through the tutorial, with their own practice game."""

    def __init__(self, cfg, player_idx: int, colour: str, nodes: list[NodeSetup], catalog: Catalog,
                 now: float = 0.0, maze_small: Optional[Maze] = None, maze_free: Optional[Maze] = None):
        self.cfg = cfg
        self.player_idx = player_idx
        self.colour = colour
        self.nodes = [NodeSetup(n.node_id, 0, n.hand_no, n.scheme) for n in nodes]
        self.catalog = catalog
        self.manual = all(n.scheme == "manual" for n in nodes)
        self.mixed = not self.manual and any(n.scheme == "manual" for n in nodes)
        single = len(nodes) == 1
        self.single = single
        self.stash_on = bool(cfg.stash_single_node if single else cfg.stash_multi_node)
        self.maze_small = maze_small or generate_maze(PRACTICE_SIZE, 0.3, PRACTICE_SEED)
        self.maze_free = maze_free or generate_maze(FREE_PRACTICE_SIZE, 0.2, PRACTICE_SEED + 1)
        self.states = [LessonState(l, "todo" if applies(l, len(nodes), self.stash_on) else "n/a")
                       for l in LESSONS]
        self.index: Optional[int] = None          # None = controls test page
        self.completed_at: Optional[float] = None
        self.progress: set[str] = set()           # partial progress inside a lesson
        self.game: Game
        self.controller: InputController
        self._new_game(CONTROLS, now)

    # ------------------------------------------------------------------ lesson flow
    @property
    def lesson(self) -> Lesson:
        return CONTROLS if self.index is None else LESSONS[self.index]

    @property
    def finished(self) -> bool:
        return self.index is not None and self.index >= len(LESSONS)

    def instruction(self) -> str:
        l = self.lesson
        if self.single and l.single:
            return l.single_manual if self.manual and l.single_manual else l.single
        if self.mixed:
            return f"{l.gesture}  Manual-mode nodes: {l.manual}"
        return l.manual if self.manual else l.gesture

    def hint(self) -> str:
        if self.finished:
            return "ALL DONE"
        if self.completed_at is not None:
            return "WELL DONE!"
        return (self.lesson.hint_m if self.manual else self.lesson.hint_g)[:14]

    def _script(self, lesson: Lesson) -> Optional[list]:
        """Scripted deal order. Deals fill the hand from the bottom (the first card dealt ends up in
        the lowest slot), so the combo pair is placed in hand slots 1 and 2."""
        if lesson.script is None:
            return None
        k = len(self.nodes)
        if lesson.key in ("if", "for"):
            pair = [mv("RDL", "O"), IF_SQ] if lesson.key == "if" else [mv("DD", "I3"), FOR_]
            if k == 1:
                return pair + list(FILLER)            # stash the first, then the control card arrives
            return [mv("R", "D")] * (k - 2) + pair + list(FILLER)
        return list(lesson.script)

    def _new_game(self, lesson: Lesson, now: float) -> None:
        maze = self.maze_free if lesson.key == "practice" else self.maze_small
        script = self._script(lesson)
        deck = ScriptedDeck(script) if script else None
        self.game = Game(self.cfg, self.catalog, self.nodes, maze=maze, now=now, deck=deck,
                         colours=[self.colour], rng_seed=PRACTICE_SEED)
        self.controller = InputController(self.game, self.cfg)
        self.progress = set()
        self.completed_at = None
        self._refresh_hints()

    def _refresh_hints(self) -> None:
        h = self.hint()
        for n in self.nodes:
            self.game.hints[n.node_id] = h

    def start_lessons(self, now: float) -> None:
        self.index = -1
        self._advance(now)

    def _advance(self, now: float) -> None:
        i = (self.index if self.index is not None else -1) + 1
        while i < len(LESSONS) and self.states[i].status == "n/a":
            i += 1
        self.index = i
        if i < len(LESSONS):
            self.states[i].status = "active"
            self._new_game(LESSONS[i], now)
        else:
            self.completed_at = None
            self._refresh_hints()

    def skip(self, now: float) -> None:
        if self.index is None:
            self.start_lessons(now)
            return
        if self.finished:
            return
        self.states[self.index].status = "skipped"
        self._advance(now)

    def restart_lesson(self, now: float) -> None:
        self._new_game(self.lesson, now)

    # ------------------------------------------------------------------ runtime
    def handle(self, ev: NodeEvent) -> None:
        if self.finished:
            return
        self.controller.handle(ev)

    def tick(self, now: float) -> None:
        if self.finished:
            return
        self.controller.tick(now)
        self.game.update(now)
        if self.index is None:
            return
        if self.completed_at is not None:
            if now - self.completed_at >= DONE_PAUSE_S:
                self.states[self.index].status = "done"
                self._advance(now)
            return
        if self._check():
            self.completed_at = now
            self._refresh_hints()

    def _acts(self) -> set[str]:
        return {a for _, _, a in self.controller.actions}

    def _check(self) -> bool:
        key = self.lesson.key
        acts = self._acts()
        pl = self.game.players[0]
        if key == "discard":
            return "discard" in acts
        if key == "start":
            return "start" in acts
        if key == "add":
            return {"add_left", "add_right"} <= acts
        if key == "remove":
            return "remove" in acts
        if key == "push":
            return "push" in acts and not pl.avatar.busy
        if key == "stash":
            return {"stash_in", "use_stash"} <= acts
        if key == "if":
            return bool(pl.last_push and pl.last_push.resolution.if_effective)
        if key == "for":
            return bool(pl.last_push and pl.last_push.resolution.for_effective)
        if key == "practice":
            return self.game.state == ENDED
        return False

    def node_display(self, node_id: str) -> dict:
        d = self.game.node_display(node_id)
        d["HINT"] = self.hint()
        if self.game.state == ENDED:
            d["MSG"] = "WELL DONE!"
        return d

    def summary(self) -> list[tuple[str, str]]:
        return [(s.lesson.title, s.status) for s in self.states]


class Tutorial:
    """All tutorial players together; routes node events to each player's practice game."""

    def __init__(self, cfg, catalog: Catalog, players: dict[int, tuple[str, list[NodeSetup]]], now: float = 0.0):
        maze_small = generate_maze(PRACTICE_SIZE, 0.3, PRACTICE_SEED)
        maze_free = generate_maze(FREE_PRACTICE_SIZE, 0.2, PRACTICE_SEED + 1)
        self.players: dict[int, PlayerTutorial] = {
            p: PlayerTutorial(cfg, p, colour, nodes, catalog, now, maze_small, maze_free)
            for p, (colour, nodes) in sorted(players.items())
        }
        self.node_owner = {n.node_id: p for p, (_, nodes) in players.items() for n in nodes}
        self.phase = "controls"
        self.raw: dict[str, list[tuple[float, str, str]]] = {nid: [] for nid in self.node_owner}

    def handle(self, ev: NodeEvent) -> None:
        p = self.node_owner.get(ev.node_id)
        if p is None:
            return
        log = self.raw.setdefault(ev.node_id, [])
        log.append((ev.ts, ev.kind, ev.value))
        del log[:-12]
        self.players[p].handle(ev)

    def tick(self, now: float) -> None:
        for pt in self.players.values():
            pt.tick(now)

    def start_lessons(self, now: float) -> None:
        self.phase = "lessons"
        for pt in self.players.values():
            if pt.index is None:
                pt.start_lessons(now)

    def node_display(self, node_id: str) -> Optional[dict]:
        p = self.node_owner.get(node_id)
        return None if p is None else self.players[p].node_display(node_id)

    @property
    def all_finished(self) -> bool:
        return all(pt.finished for pt in self.players.values())
