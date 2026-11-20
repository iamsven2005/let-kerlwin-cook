"""Headless game simulation: players, hands, stash, chunks, routes, deck, the walk, win.

Section 18.1 (route building): each player builds a route from their spawn corner to the centre,
one chunk (the row of nodes on the table) at a time. Pushing a chunk appends it to the route,
locked; its cards go straight to the discard pile. As soon as the route's definite trace enters
the centre, the route is complete: after route_flash_s the avatar walks it. First to arrive wins.

The game only ever deals with node *ids*. It never knows whether a node is
physical or virtual. All public action methods are idempotent-safe: they
check their preconditions and return False (no state change) if they fail.
"""
from __future__ import annotations

import random
from dataclasses import dataclass, field
from typing import Optional

from ..config import Config
from . import hand as conveyor
from .cards import Card, CardType, Catalog
from .deck import Deck
from .execution import Avatar, advance
from .preview import RoutePlan, plan_route
from .resolve import Resolution, resolve
from .hooks import CentreWin, CollisionHook, CornerStarts, ItemEffectHook, NoCollisionPenalty, NoItems, StartPositionProvider, WinCondition
from .maze import Maze, generate_maze

PLAYER_COLOURS = ["E8474C", "2F80ED", "27AE60", "F2C94C", "9B51E0", "F2994A"]

LOBBY, PLAYING, ENDED = "lobby", "playing", "ended"


@dataclass
class NodeSetup:
    node_id: str
    player: int          # 0-based
    hand_no: int         # 1 = top
    scheme: str = "gesture"   # "gesture" | "manual"


@dataclass
class NodeState:
    node_id: str
    player: int
    hand_no: int
    scheme: str = "gesture"
    face_down: bool = False
    register: set = field(default_factory=set)
    connected: bool = True

    @property
    def slot(self) -> int:
        return self.hand_no - 1


@dataclass
class SeqEntry:
    node_id: Optional[str]   # None = the stash card
    card: Card


@dataclass
class Sequence:
    """The row of nodes on the table (14 B3). entries are in left-to-right order, which is the
    execution order; the head (the first card placed) can sit anywhere in the row."""
    head: str
    entries: list[SeqEntry]
    opened_at: float = 0.0

    def position_of(self, node_id: str) -> Optional[int]:
        """1-based row position (1 = leftmost)."""
        for i, e in enumerate(self.entries):
            if e.node_id == node_id:
                return i + 1
        return None

    @property
    def has_stash(self) -> bool:
        return any(e.node_id is None for e in self.entries)


@dataclass
class Chunk:
    """A pushed chunk: locked, part of the route for good (18.1.2)."""
    cards: list[Card]
    push_time: float


@dataclass
class Player:
    idx: int
    colour: str
    node_ids: list[str]            # ordered by hand number
    slots: list[Optional[Card]]
    stash_enabled: bool
    avatar: Avatar
    stash: Optional[Card] = None
    sequence: Optional[Sequence] = None
    waiting_since: Optional[float] = None
    next_deal_at: Optional[float] = None
    route: list[Chunk] = field(default_factory=list)
    locked_plan: Optional[RoutePlan] = None         # the locked route, traced (recomputed per push)
    complete_at: Optional[float] = None             # time of the push that completed the route

    @property
    def name(self) -> str:
        return f"P{self.idx + 1}"

    @property
    def route_cards(self) -> list[Card]:
        return [c for ch in self.route for c in ch.cards]

    @property
    def last_push(self) -> Optional[Chunk]:
        return self.route[-1] if self.route else None

    @property
    def walking(self) -> bool:
        """Route complete: the avatar flashes / walks, and the player's nodes do nothing (18.1.7)."""
        return self.complete_at is not None


@dataclass
class LogEntry:
    t: float
    text: str


def doff_for(cfg: Config, scheme: str) -> str:
    """DOFF policy (spec 6): Gesture-mode nodes may switch their display off while physically
    face-down if node_display_off_when_face_down; Manual-mode nodes always stay on."""
    return "1" if (scheme == "gesture" and cfg.node_display_off_when_face_down) else "0"


class Game:
    def __init__(self, cfg: Config, catalog, setup: list[NodeSetup],
                 maze: Optional[Maze] = None, rng_seed: Optional[int] = None, now: float = 0.0,
                 deck: Optional[Deck] = None, colours: Optional[list[str]] = None,
                 start_provider: Optional[StartPositionProvider] = None,
                 win_condition: Optional[WinCondition] = None,
                 items: Optional[ItemEffectHook] = None,
                 collision: Optional[CollisionHook] = None):
        self.cfg = cfg
        if not isinstance(catalog, Catalog):
            catalog = Catalog(list(catalog))
        self.catalog = catalog
        self.hints: dict[str, str] = {}         # per-node HINT: text (tutorial)
        self.setup = list(setup)
        self.state = PLAYING
        self.winners: list[int] = []
        self.win_time: Optional[float] = None
        self.log: list[LogEntry] = []
        self.message = ""
        self.rng = random.Random(rng_seed)

        if not setup:
            raise ValueError("no nodes assigned")
        if len(setup) > 6:
            raise ValueError("at most 6 nodes")
        n_players = max(s.player for s in setup) + 1
        by_player: dict[int, list[NodeSetup]] = {i: [] for i in range(n_players)}
        for s in setup:
            by_player[s.player].append(s)
        for p, ns in by_player.items():
            if not ns:
                raise ValueError(f"player {p + 1} has no nodes")
            nums = sorted(n.hand_no for n in ns)
            if nums != list(range(1, len(ns) + 1)):
                raise ValueError(f"player {p + 1} hand numbers must be 1..{len(ns)}, got {nums}")
        if n_players > 4 and not cfg.feature("players_5_6"):
            raise ValueError("more than 4 players requires optional feature players_5_6")

        self.maze = maze or generate_maze(cfg.maze_size, cfg.maze_braid_fraction, cfg.maze_seed,
                                          cfg.get("maze_algorithm", "backtracker"))
        self.path_length = self.maze.corner_distances()[0]
        self.start_provider = start_provider or CornerStarts()
        self.win_condition = win_condition or CentreWin()
        self.items = items or NoItems()
        self.collision = collision or NoCollisionPenalty()

        starts = self.start_provider.positions(self.maze, n_players)
        self.nodes: dict[str, NodeState] = {
            s.node_id: NodeState(s.node_id, s.player, s.hand_no, s.scheme) for s in setup
        }
        self.players: list[Player] = []
        for p in range(n_players):
            ns = sorted(by_player[p], key=lambda n: n.hand_no)
            single = len(ns) == 1
            stash_on = cfg.stash_single_node if single else cfg.stash_multi_node
            self.players.append(Player(
                idx=p, colour=(colours[p] if colours else PLAYER_COLOURS[p % len(PLAYER_COLOURS)]),
                node_ids=[n.node_id for n in ns], slots=[None] * len(ns),
                stash_enabled=bool(stash_on), avatar=Avatar(p, starts[p], starts[p]),
            ))

        # 14 A3: max(multiplier x hand size, copies x types) + domino guarantee + IF/FOR share
        self.deck = deck if deck is not None else Deck.build(catalog, len(setup), cfg, self.rng)
        self._win_candidates: list[tuple[float, float, int]] = []
        # spec HUB-Q15: the initial deal is instant even when deal_interval_s > 0.
        for pl in self.players:
            self._fill(pl, now, instant=True)
            self._replan(pl)

    # ------------------------------------------------------------------ queries
    def player_of(self, node_id: str) -> Optional[Player]:
        n = self.nodes.get(node_id)
        return self.players[n.player] if n else None

    def card_at(self, node_id: str) -> Optional[Card]:
        n = self.nodes[node_id]
        return self.players[n.player].slots[n.slot]

    def in_sequence(self, node_id: str) -> bool:
        pl = self.player_of(node_id)
        return bool(pl and pl.sequence and pl.sequence.position_of(node_id) is not None)

    def is_head(self, node_id: str) -> bool:
        pl = self.player_of(node_id)
        return bool(pl and pl.sequence and pl.sequence.head == node_id)

    def locked(self, pl: Player) -> list[bool]:
        out = []
        for nid in pl.node_ids:
            n = self.nodes[nid]
            in_seq = bool(pl.sequence and pl.sequence.position_of(nid) is not None)
            out.append(in_seq or not n.connected)   # spec HUB-Q13: disconnected slots are frozen
        return out

    @property
    def playing(self) -> bool:
        return self.state == PLAYING

    def _log(self, t: float, text: str) -> None:
        self.log.append(LogEntry(t, text))
        if len(self.log) > 200:
            del self.log[:-200]

    # ------------------------------------------------------------------ dealing
    def _fill(self, pl: Player, now: float, instant: bool = False) -> None:
        locked = self.locked(pl)
        pl.slots = conveyor.settle(pl.slots, locked)
        if not conveyor.has_empty_unlocked(pl.slots, locked):
            pl.waiting_since = None
            pl.next_deal_at = None
            return
        if instant or self.cfg.deal_interval_s <= 0:
            while conveyor.has_empty_unlocked(pl.slots, locked):
                c = self.deck.draw()
                if c is None:
                    break
                pl.slots = conveyor.deal_one(pl.slots, locked, c)
        else:
            if pl.next_deal_at is None:
                pl.next_deal_at = now + self.cfg.deal_interval_s
        if conveyor.has_empty_unlocked(pl.slots, locked):
            if pl.waiting_since is None:
                pl.waiting_since = now
        else:
            pl.waiting_since = None
            pl.next_deal_at = None

    def _fill_all(self, now: float) -> None:
        # spec HUB-Q15: players that have waited longest are served first.
        order = sorted(self.players, key=lambda p: (p.waiting_since if p.waiting_since is not None else float("inf"), p.idx))
        for pl in order:
            self._fill(pl, now)

    def on_cards_discarded(self, now: float) -> None:
        self._fill_all(now)

    def _deal_timer_tick(self, now: float) -> None:
        if self.cfg.deal_interval_s <= 0:
            return
        order = sorted(self.players, key=lambda p: (p.next_deal_at if p.next_deal_at is not None else float("inf"), p.idx))
        for pl in order:
            if pl.next_deal_at is None or pl.next_deal_at > now:
                continue
            locked = self.locked(pl)
            if not conveyor.has_empty_unlocked(pl.slots, locked):
                pl.next_deal_at = None
                continue
            c = self.deck.draw()
            if c is None:
                continue  # dealing waits; retry next tick
            pl.slots = conveyor.deal_one(pl.slots, locked, c)
            pl.next_deal_at = now + self.cfg.deal_interval_s if conveyor.has_empty_unlocked(pl.slots, locked) else None
            if pl.next_deal_at is None:
                pl.waiting_since = None

    def _take_from_slot(self, pl: Player, node_id: str) -> Optional[Card]:
        i = self.nodes[node_id].slot
        card = pl.slots[i]
        pl.slots[i] = None
        return card

    # ------------------------------------------------------------------ actions
    def _ok(self, node_id: str) -> Optional[tuple[NodeState, Player]]:
        if not self.playing:
            return None
        n = self.nodes.get(node_id)
        if n is None or not n.connected:
            return None
        pl = self.players[n.player]
        if pl.walking:
            return None          # 18.1.7: nodes do nothing while the avatar walks
        return n, pl

    def discard(self, node_id: str, now: float) -> bool:
        r = self._ok(node_id)
        if not r:
            return False
        n, pl = r
        if self.in_sequence(node_id):
            return False
        card = self._take_from_slot(pl, node_id)
        if card is None:
            return False
        self.deck.discard(card)
        self._log(now, f"{pl.name} discarded {card.code}")
        self._fill(pl, now)
        self._fill_all(now)
        return True

    def start_sequence(self, node_id: str, now: float) -> bool:
        r = self._ok(node_id)
        if not r:
            return False
        n, pl = r
        if pl.sequence is not None:
            return False   # spec HUB-Q17(a)
        card = self.card_at(node_id)
        if card is None:
            return False
        pl.sequence = Sequence(head=node_id, entries=[SeqEntry(node_id, card)], opened_at=now)
        self._log(now, f"{pl.name} started a sequence with {card.code}")
        return True

    def add_to_sequence(self, node_id: str, now: float, side: str = "right") -> bool:
        """Join the row at its left or right end (14 B3)."""
        r = self._ok(node_id)
        if not r:
            return False
        n, pl = r
        if pl.sequence is None or self.in_sequence(node_id) or side not in ("left", "right"):
            return False
        card = self.card_at(node_id)
        if card is None:
            return False
        e = SeqEntry(node_id, card)
        if side == "left":
            pl.sequence.entries.insert(0, e)
        else:
            pl.sequence.entries.append(e)
        self._log(now, f"{pl.name} added {card.code} on the {side}")
        return True

    def remove_from_sequence(self, node_id: str, now: float) -> bool:
        r = self._ok(node_id)
        if not r:
            return False
        n, pl = r
        seq = pl.sequence
        if seq is None or seq.head == node_id or seq.position_of(node_id) is None:
            return False
        seq.entries = [e for e in seq.entries if e.node_id != node_id]
        self._log(now, f"{pl.name} removed a card from the sequence")
        self._fill(pl, now)
        return True

    def _drop_sequence(self, pl: Player, now: float, reason: str) -> bool:
        """System-only: drop the open chunk (a node in it was lost). Players have no cancel (18.6);
        spec HUB-Q43: HUB-Q22 stays as a system event so a lost head can't strand a player."""
        if pl.sequence is None:
            return False
        pl.sequence = None   # a stash card in it simply stays in the stash
        self._log(now, f"{pl.name} chunk dropped ({reason})")
        self._fill(pl, now)
        return True

    # ------------------------------------------------------------------ the route
    def route_plan(self, pl: Player) -> RoutePlan:
        """The locked route plus a live preview of the pending chunk (18.4)."""
        pending = [e.card for e in pl.sequence.entries] if pl.sequence else None
        if not pending:
            return pl.locked_plan
        return plan_route(self.maze, pl.avatar.start, self.maze.centre, pl.route_cards, pending)

    def route_resolution(self, pl: Player) -> Resolution:
        return pl.locked_plan.res_locked if pl.locked_plan else resolve(pl.route_cards)

    def _replan(self, pl: Player) -> None:
        pl.locked_plan = plan_route(self.maze, pl.avatar.start, self.maze.centre, pl.route_cards)

    def push(self, player_idx: int, now: float) -> bool:
        if not self.playing:
            return False
        pl = self.players[player_idx]
        seq = pl.sequence
        if seq is None or pl.walking:
            return False
        cards: list[Card] = []
        for e in seq.entries:
            if e.node_id is None:
                pl.stash = None
            else:
                self._take_from_slot(pl, e.node_id)
            cards.append(e.card)
        pl.sequence = None
        pl.route.append(Chunk(cards, now))      # locked for good (18.1.2)
        for c in cards:
            self.deck.discard(c)                # 18.1.8: discard at push; the route keeps the moves
        self._replan(pl)
        self._log(now, f"{pl.name} pushed {' '.join(c.code for c in cards)}")
        plan = pl.locked_plan.locked
        if plan.reached:                        # 18.1.4: the definite trace entered the centre
            pl.complete_at = now
            pl.sequence = None
            for nid in pl.node_ids:
                self.nodes[nid].register.clear()
            flash = float(self.cfg.get("route_flash_s", 1.0))
            pl.avatar.begin_walk(plan.move_list, now, now + flash, plan.length)
            self._log(now, f"{pl.name} route complete: {plan.length} tiles")
        self._fill(pl, now)
        self._fill_all(now)
        return True

    def stash_in(self, node_id: str, now: float) -> bool:
        r = self._ok(node_id)
        if not r:
            return False
        n, pl = r
        if not pl.stash_enabled or pl.stash is not None or self.in_sequence(node_id):
            return False
        card = self.card_at(node_id)
        if card is None:
            return False
        self._take_from_slot(pl, node_id)
        pl.stash = card
        self._log(now, f"{pl.name} stashed {card.code}")
        self._fill(pl, now)
        return True

    def use_stash(self, player_idx: int, now: float) -> bool:
        if not self.playing or self.players[player_idx].walking:
            return False
        pl = self.players[player_idx]
        if pl.sequence is None or pl.stash is None or pl.sequence.has_stash:
            return False   # spec HUB-Q17(d)
        pl.sequence.entries.append(SeqEntry(None, pl.stash))     # right end (14 B3, CR6)
        self._log(now, f"{pl.name} added stash card {pl.stash.code}")
        return True

    def discard_stash(self, player_idx: int, now: float) -> bool:
        if not self.playing or self.players[player_idx].walking:
            return False
        pl = self.players[player_idx]
        if pl.stash is None or (pl.sequence is not None and pl.sequence.has_stash):
            return False   # spec HUB-Q17(d)
        card, pl.stash = pl.stash, None
        self.deck.discard(card)
        self._log(now, f"{pl.name} discarded stash card {card.code}")
        self._fill_all(now)
        return True

    # ------------------------------------------------------------------ connectivity
    def node_disconnected(self, node_id: str, now: float) -> None:
        n = self.nodes.get(node_id)
        if n is None or not n.connected:
            return
        pl = self.players[n.player]
        was_in_sequence = pl.sequence is not None and pl.sequence.position_of(node_id) is not None
        n.connected = False          # freeze the slot first, so the conveyor skips it from now on
        n.register.clear()
        # spec HUB-Q22 / spec HUB-Q43: drop the open chunk only if the lost node is in it.
        if was_in_sequence:
            self._drop_sequence(pl, now, reason=f"{node_id} lost")
        self._log(now, f"{node_id} disconnected")
        self._fill(pl, now)

    def node_reconnected(self, node_id: str, now: float) -> None:
        n = self.nodes.get(node_id)
        if n is None or n.connected:
            return
        n.connected = True
        self._log(now, f"{node_id} reconnected")
        self._fill(self.players[n.player], now)

    # ------------------------------------------------------------------ simulation
    def on_avatar_enter(self, av: Avatar, t: float) -> None:
        self.items.on_enter(self, av, t)
        if self.win_condition.on_enter(self, av, t):
            av.arrived = True
            av.next_time = None                 # the rest of the route is ignored (18.1.4)
            self._win_candidates.append((t, av.push_time if av.push_time is not None else t, av.player))

    def update(self, now: float) -> None:
        if not self.playing:
            return
        self._deal_timer_tick(now)
        while True:
            due = [a for p in self.players for a in [p.avatar] if a.next_time is not None and a.next_time <= now]
            if self._win_candidates:
                t0 = min(c[0] for c in self._win_candidates)
                due = [a for a in due if a.next_time <= t0]   # finish same-instant events only
                if not due:
                    break
            if not due:
                break
            av = min(due, key=lambda a: (a.next_time, a.player))
            advance(self, av)
        if self._win_candidates:
            self._finish()

    def _finish(self) -> None:
        # spec HUB-Q5 (18.1.6): earliest arrival, then the earliest completing push, then a draw.
        best = min((c[0], c[1]) for c in self._win_candidates)
        self.winners = sorted({c[2] for c in self._win_candidates if (c[0], c[1]) == best})
        self.win_time = best[0]
        self.state = ENDED
        names = "&".join(f"P{w + 1}" for w in self.winners)
        self.message = f"{names} WIN" if len(self.winners) > 1 else f"{names} WINS"
        self._log(best[0], self.message)

    # ------------------------------------------------------------------ display
    def node_display(self, node_id: str) -> dict[str, str]:
        """What a node should show, as protocol field -> payload."""
        n = self.nodes[node_id]
        pl = self.players[n.player]
        card = pl.slots[n.slot]
        pos = pl.sequence.position_of(node_id) if pl.sequence else None
        if pos is None:
            seq = "-"
        elif pl.sequence.head == node_id:
            seq = f"H{pos}"          # head, at row position pos (14 B3)
        else:
            seq = str(pos)
        reg = "".join("1" if b in n.register else "0" for b in "RLV") + ("1" if n.face_down else "0")
        return {
            "CARD": card.code if card else "-",
            "SEQ": seq,
            "REG": reg,
            "COL": pl.colour,
            "HAND": str(n.hand_no),
            "MSG": self.message if self.state == ENDED else ("WALKING" if pl.walking else ""),
            "DOFF": doff_for(self.cfg, n.scheme),
            "HINT": self.hints.get(node_id, ""),
        }

    def total_cards(self) -> int:
        """Every card instance in the game (for invariant checks)."""
        n = len(self.deck.draw_pile) + len(self.deck.discard_pile)
        for pl in self.players:
            n += sum(1 for c in pl.slots if c is not None)
            n += 1 if pl.stash is not None else 0
        return n
