"""Tutorial and controls test (section 14 B5), headless.

Each tutorial player gets their own practice game (spec HUB-Q32): a small symmetric maze, their
own nodes and control scheme, and a scripted deck per lesson, so players progress independently.
A lesson is completed by actually doing it; the hub watches the controller's actions and the game.

Lessons that can't apply are skipped automatically: single-node players have no second node for
lessons 3-4; lesson 6 needs a stash (single-node by default). Single-node players do lesson 7 across
pushes (an IF at the end of a chunk waits for the next one), so they need the stash only for 6.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable, Optional

from .cards import Card, CardType, Catalog, ControlType
from .controls import InputController
from .deck import Deck, ScriptedDeck
from .events import BSYNC, BUTTONS, FLIP, NodeEvent
from .resolve import BLANK, IF_SHAPE
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

# 21.1: one button per primitive, the same in both modes: TL = V, TR = F, BL = L, BR = R; any two
# buttons together = clear. Gesture mode also accepts turning the node over as F.
KEYS = "V = top-left, F = top-right, L = bottom-left, R = bottom-right"
F_G = "F (top-right, or turn the node face-down)"
F_M = "F (top-right)"
NO_CANCEL = " There is no cancel: once started, a row only ends when you push it."
# 19 / 21: presses latch for 3 s; any two buttons together clear them
LATCH = (" A press stays lit on the strip for 3 s, and F does what is lit (L lit + F = stash in, not "
         "discard). Made a mistake? Press any two buttons together to clear it, or wait 3 s.")


def _both(text: str) -> tuple[str, str]:
    """(Gesture text, Manual text) from one text with {F} placeholders: the layout is the same,
    only Gesture mode also accepts the physical flip."""
    return text.format(F=F_G, K=KEYS), text.format(F=F_M, K=KEYS)


def _lesson(key, title, text, hint, script, needs="", single="", **kw) -> Lesson:
    g, m = _both(text)
    sg, sm = _both(single) if single else ("", "")
    return Lesson(key, title, g, m, hint, hint, script, needs=needs, single=sg, single_manual=sm)


LESSONS: list[Lesson] = [
    _lesson("discard", "1. Discard",
            "Buttons: {K}. With nothing lit on the strip, press {F}: the card is discarded and the cards "
            "above slide down." + LATCH,
            "F = DISCARD", FILLER),
    _lesson("start", "2. Start a sequence",
            "Press V (top-left), then {F}. The node becomes the HEAD of your row. You have 3 s between "
            "the two." + NO_CANCEL,
            "V THEN F", FILLER),
    _lesson("add", "3. Add right, push, add left",
            "With a head on the table: press V on another node, then R (bottom-right): it joins the "
            "RIGHT end. Push: {F} on the head. Start again, then V and L (bottom-left) on another node "
            "for the LEFT end. Place each node at that end of the row on the table too.",
            "V THEN L OR R", FILLER, needs="multi"),
    _lesson("remove", "4. Remove a card",
            "Build a row of two, then press {F} twice (within 3 s) on the added, non-head node: it "
            "leaves the row and keeps its card. The head can't be removed.",
            "F TWICE", FILLER, needs="multi"),
    _lesson("push", "5. Push chunks and watch the route grow",
            "Each push ({F} on the head) adds your row to your ROUTE from your corner: it is locked and "
            "drawn solid on the maze. While you build, the dashed line shows where the next chunk goes. "
            "Push two chunks.",
            "F ON HEAD", FILLER),
    _lesson("stash", "6. Stash in, use stash, discard stash",
            "Press L (bottom-left), then {F}, to stash the card. Start a sequence and press V on the "
            "face-down head: the stash card joins the right end. Push, stash another card, then press R "
            "(bottom-right) and {F} to discard the stash. Careful: a stray L still lit means F stashes "
            "instead of discarding. Press any two buttons together to clear it.",
            "L THEN F", (mv("RD", "V"), mv("DR", "V"), mv("RR", "I3")), needs="stash"),
    _lesson("if", "7. IF card: transform or blank",
            "The IF card reads 'IF next is a SQUARE -> becomes RR'. Put it directly LEFT of a square card "
            "(V, then L or R) and push: the IF turns into RR. Then put an IF before any other card: it "
            "becomes a blank (no move). An IF at the end of a chunk waits for your next chunk's first card.",
            "IF THEN SQUARE", (mv("RDL", "O"), IF_SQ, mv("D", "D"), mv("R", "D")),
            single="Push the IF card on its own (V, then {F} twice): it waits. Then push the square card: "
                   "the IF turns into RR. Push another IF, then any other card: that IF becomes a blank."),
    _lesson("for", "8. FOR ... FOR block",
            "FOR opens a block and the next FOR closes it. The cards between run once per card: FOR A B "
            "FOR = A B A B. A block can stay open across pushes: push FOR and a card now, then a card and "
            "FOR in your next chunk. The map shows 'x?' until it closes.",
            "FOR .. FOR", (mv("R", "D"), FOR_, mv("DD", "I3"), FOR_),
            single="Push FOR on its own, then a card, then FOR: the block spans three pushes and runs the "
                   "card once."),
    _lesson("practice", "9. Build a route to the centre",
            "Build a complete route from your corner to the gold centre. When it gets there your avatar "
            "walks it: the lesson ends when the avatar arrives.",
            "REACH CENTRE", None),
]
CONTROLS = Lesson("controls", "Controls test",
                  "Press each button alone and any two together: they light up as V, F, L, R and clear. "
                  "Turn the node over for the flip check.",
                  "Press each button alone and any two together: they light up as V, F, L, R and clear.",
                  "TEST CONTROLS", "TEST CONTROLS", FILLER)


def applies(lesson: Lesson, n_nodes: int, stash_on: bool) -> bool:
    if lesson.needs == "multi":
        return n_nodes >= 2
    if lesson.needs == "stash":
        return stash_on
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
        """Scripted deal order (cycled). Deals fill the hand from the bottom (the first card dealt
        ends up in the lowest slot)."""
        if lesson.script is None:
            return None
        k = len(self.nodes)
        if lesson.key == "if":
            # an IF next to a square, plus other cards for the blank case
            if k == 1:
                return [IF_SQ, mv("RDL", "O"), IF_SQ, mv("D", "D")]
            return [mv("R", "D")] * max(0, k - 2) + [mv("RDL", "O"), IF_SQ] + [mv("D", "D"), IF_SQ, mv("R", "D")]
        if lesson.key == "for":
            if k == 1:
                return [FOR_, mv("DD", "I3"), FOR_, mv("R", "D")]
            return [mv("R", "D")] * max(0, k - 2) + [mv("DD", "I3"), FOR_] + [mv("R", "D"), FOR_, mv("D", "D")]
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
            return
        if self.game.state == ENDED and self.lesson.key != "practice":
            # the practice route reached the centre before the lesson was done: start a fresh
            # route on the same lesson, keeping what the player has already done
            done = list(self.controller.actions)
            self._new_game(self.lesson, now)
            self.controller.actions = done

    def _acts(self) -> set[str]:
        return {a for _, _, a in self.controller.actions}

    def _check(self) -> bool:
        key = self.lesson.key
        seq = [a for _, _, a in self.controller.actions]
        acts = set(seq)
        pl = self.game.players[0]
        if key == "discard":
            return "discard" in acts
        if key == "start":
            return "start" in acts
        if key == "add":
            # add right, push, start again, add left
            if "add_right" not in acts or "push" not in acts:
                return False
            return "add_left" in seq[seq.index("push"):]
        if key == "remove":
            return "remove" in acts
        if key == "push":
            return len(pl.route) >= 2
        if key == "stash":
            return {"stash_in", "use_stash", "discard_stash"} <= acts
        res = self.game.route_resolution(pl)
        if key == "if":
            ifs = [i for i, c in enumerate(res.cards) if c.kind == "IF"]
            return any(res.state[i] == IF_SHAPE for i in ifs) and any(res.state[i] == BLANK for i in ifs)
        if key == "for":
            return any(b.close is not None and self._chunk_of(pl, b.open) != self._chunk_of(pl, b.close)
                       for b in res.blocks)
        if key == "practice":
            return self.game.state == ENDED
        return False

    @staticmethod
    def _chunk_of(pl, written_index: int) -> int:
        k = 0
        for ci, ch in enumerate(pl.route):
            k += len(ch.cards)
            if written_index < k:
                return ci
        return len(pl.route)

    def node_display(self, node_id: str) -> dict:
        d = self.game.node_display(node_id)
        d["HINT"] = self.hint()
        if self.game.state == ENDED:
            d["MSG"] = "WELL DONE!"
        return d

    def summary(self) -> list[tuple[str, str]]:
        return [(s.lesson.title, s.status) for s in self.states]


INDICATOR_S = 0.6            # how long a live L / R / V / F indicator stays lit
BUTTON_NAMES = (("TL", 0x1), ("TR", 0x2), ("BL", 0x4), ("BR", 0x8))
BUTTON_ITEMS = ("TL", "TR", "BL", "BR", "2+")       # 21.1: "2+" = any two or more together
CHECK_ITEMS = BUTTON_ITEMS + ("F:1", "F:0")


def button_meanings(cfg, scheme: str) -> dict:
    """What each button means for this node (21.1: V, F, L, R; any 2+ together = clear)."""
    m = cfg.get("gesture_button_map" if scheme == "gesture" else "manual_button_map") or {}
    out = {name: m.get(f"bit{b.bit_length() - 1}", "?") for name, b in BUTTON_NAMES}
    out["2+"] = "clear"
    return out


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
        # 18.8 controls test: held buttons, which buttons / chord / flips have been seen
        self.buttons: dict[str, int] = {nid: 0 for nid in self.node_owner}
        self.button_seen: dict[str, float] = {}
        self.checks: dict[str, set] = {nid: set() for nid in self.node_owner}
        self.now = now

    def handle(self, ev: NodeEvent) -> None:
        p = self.node_owner.get(ev.node_id)
        if p is None:
            return
        log = self.raw.setdefault(ev.node_id, [])
        log.append((ev.ts, ev.kind, ev.value))
        del log[:-12]
        chk = self.checks.setdefault(ev.node_id, set())
        if ev.kind in (BUTTONS, BSYNC):
            try:
                mask = int(ev.value, 16) if isinstance(ev.value, str) else int(ev.value)
            except (TypeError, ValueError):
                mask = 0
            if mask & ~self.buttons.get(ev.node_id, 0):
                self.button_seen[ev.node_id] = ev.ts
            if ev.kind == BUTTONS:
                for name, b in BUTTON_NAMES:
                    if mask & b:
                        chk.add(name)
                if bin(mask).count("1") >= 2:
                    chk.add("2+")
            self.buttons[ev.node_id] = mask
        elif ev.kind == FLIP:
            chk.add("F:1" if ev.value == "1" else "F:0")
        self.players[p].handle(ev)

    def tick(self, now: float) -> None:
        self.now = now
        for pt in self.players.values():
            pt.tick(now)

    # ---- 18.8 controls-test helpers
    def lit(self, node_id: str, prim: str, now: float) -> bool:
        """Live indicator: did this node's input resolve to <prim> (L/R/V/F/clear) in the last
        INDICATOR_S? Uses the practice controller, so it shows exactly what the game reads."""
        p = self.node_owner.get(node_id)
        if p is None:
            return False
        for ts, nid, what in reversed(self.players[p].controller.primitives):
            if now - ts > INDICATOR_S:
                break
            if nid == node_id and what == prim:
                return True
        return False

    def check_done(self, node_id: str) -> tuple[bool, bool]:
        """(button check complete, flip check complete)."""
        c = self.checks.get(node_id, set())
        return (set(BUTTON_ITEMS) <= c, {"F:1", "F:0"} <= c)

    def start_lessons(self, now: float) -> None:
        self.phase = "lessons"
        for pt in self.players.values():
            if pt.index is None:
                pt.start_lessons(now)

    def node_display(self, node_id: str) -> Optional[dict]:
        p = self.node_owner.get(node_id)
        if p is None:
            return None
        return self.players[p].node_display(node_id)

    @property
    def all_finished(self) -> bool:
        return all(pt.finished for pt in self.players.values())
