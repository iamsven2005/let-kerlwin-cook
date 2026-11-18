"""Gesture register (RLVF), idle window, manual mode, and the v1.4 action table (section 14 B3).

Everything is driven by event timestamps and explicit tick(now) calls, so it runs headless and
deterministically in tests.

Register model: each node holds a *set* of R/L/V bits; the F bit shown on the node is its
face-down state. A face-down flip on a free node is the commit that reads and clears R/L/V.

The sequence is a physical row. "Free node" = face-up and not in a sequence.

Gesture mode                                  Manual mode (buttons; T:/F: ignored)
  discard     free: flip down, empty reg        F (short)
  start       free: V, flip down                V, F
  add left    free: V, then T:R (its right      V, then L button
              edge touched the row's left end)
  add right   free: V, then T:L                 V, then R button
  remove      member: flip down + up within     long-press F on the member
              remove_window_ms
  push        head: flip up                     short F on the head
  cancel      head: any button                  2+ buttons on the head, or long-press F on it
  stash in    free: L, flip down                L, F
  use stash   head: V (idle resolve)            V on the head
  disc. stash free: R + L (idle resolve)        R + L
Taps on members are ignored (bump side effects); the head only takes V.
After a push or a discard, a manual node ignores F for manual_f_lockout_ms.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Optional

from ..config import Config
from .events import BSYNC, BUTTONS, CONNECTED, DISCONNECTED, FLIP, FSTATE, FSYNC, TAP, NodeEvent
from .game import Game

FLIP_PREFIXES = ({"V"}, {"L"})   # first half of Start-or-Add / Stash in: wait for the flip or tap
F_BIT_NAME = "F"


@dataclass
class _PendingButtons:
    mask: int
    ts: float
    released: Optional[float] = None    # F released before the 2-button window closed


@dataclass
class _FHold:
    ts: float
    fired: bool = False


class InputController:
    def __init__(self, game: Game, cfg: Config):
        self.game = game
        self.cfg = cfg
        self.last_gesture: dict[str, float] = {}
        self.pending_buttons: dict[str, _PendingButtons] = {}
        self.prev_mask: dict[str, int] = {}
        self.member_down_at: dict[str, float] = {}     # gesture remove: member flipped down at
        self.f_hold: dict[str, _FHold] = {}            # manual: F held since
        self.f_lockout_until: dict[str, float] = {}    # manual: ignore F until
        self.actions: list[tuple[float, str, str]] = []    # (ts, node_id, action) for UI/log/tests
        self.raw: list[tuple[float, str, str, str]] = []   # (ts, node, kind, value) for the controls test

    # ------------------------------------------------------------------ helpers
    def _ms(self, key: str, default: float) -> float:
        return float(self.cfg.get(key, default)) / 1000.0

    @property
    def idle_s(self) -> float:
        return self._ms("gesture_idle_window_ms", 400)

    @property
    def commit_s(self) -> float:
        return max(self._ms("flip_commit_window_ms", 2000), self.idle_s)

    @property
    def manual_s(self) -> float:
        return self._ms("manual_clear_window_ms", 100)

    @property
    def remove_s(self) -> float:
        return self._ms("remove_window_ms", 3000)

    @property
    def long_s(self) -> float:
        return self._ms("manual_f_long_press_ms", 1000)

    @property
    def lockout_s(self) -> float:
        return self._ms("manual_f_lockout_ms", 600)

    def _node(self, node_id: str):
        return self.game.nodes.get(node_id)

    def _record(self, ts: float, node_id: str, action: str, ok: bool) -> bool:
        if ok:
            self.actions.append((ts, node_id, action))
            if len(self.actions) > 300:
                del self.actions[:-300]
        return ok

    def _is_free(self, nid: str) -> bool:
        n = self.game.nodes[nid]
        return not n.face_down and not self.game.in_sequence(nid)

    def _has_sequence(self, nid: str) -> bool:
        pl = self.game.player_of(nid)
        return bool(pl and pl.sequence)

    def _normalise_manual(self, player_idx: int) -> None:
        """Manual nodes not in an open sequence snap back to virtual face-up (spec HUB-Q11)."""
        if not self.cfg.get("manual_auto_face_up", True):
            return
        pl = self.game.players[player_idx]
        for nid in pl.node_ids:
            n = self.game.nodes[nid]
            if n.scheme == "manual" and n.face_down and not self.game.in_sequence(nid):
                n.face_down = False

    # ------------------------------------------------------------------ entry points
    def handle(self, ev: NodeEvent) -> None:
        n = self._node(ev.node_id)
        if n is None:
            return  # unassigned node: ignore
        self.raw.append((ev.ts, ev.node_id, ev.kind, ev.value))
        if len(self.raw) > 300:
            del self.raw[:-300]
        if ev.kind == DISCONNECTED:
            self.pending_buttons.pop(ev.node_id, None)
            self.prev_mask.pop(ev.node_id, None)
            self.f_hold.pop(ev.node_id, None)
            self.member_down_at.pop(ev.node_id, None)
            self.game.node_disconnected(ev.node_id, ev.ts)
            return
        if ev.kind == CONNECTED:
            self.game.node_reconnected(ev.node_id, ev.ts)
            return
        if ev.kind in (FSYNC, BSYNC, FSTATE):
            self._sync_state(n, ev)   # silent state syncs: never an action
            return
        if not self.game.playing or not n.connected:
            return
        self.tick(ev.ts)
        if ev.kind == TAP:
            if n.scheme == "gesture":
                self._on_tap(ev.node_id, ev.value, ev.ts)
        elif ev.kind == FLIP:
            if n.scheme == "gesture":
                self._on_flip(ev.node_id, ev.value == "1", ev.ts)
        elif ev.kind == BUTTONS:
            try:
                mask = int(ev.value, 16)
            except ValueError:
                return
            self._on_buttons(ev.node_id, mask, ev.ts)

    def _sync_state(self, n, ev: NodeEvent) -> None:
        if ev.kind == FSYNC:
            if n.scheme == "gesture":
                n.face_down = ev.value == "1"
        elif ev.kind == FSTATE:
            # v1.5 X1: the node's accelerometer came back and the orientation had changed while it
            # was missing. Take the new state and clear the register, but never act on it. Manual
            # nodes keep their virtual face state (the physical one doesn't matter to them).
            if n.scheme == "gesture":
                n.face_down = ev.value == "1"
                n.register.clear()
                self.member_down_at.pop(n.node_id, None)
        else:
            try:
                self.prev_mask[n.node_id] = int(ev.value, 16)
            except ValueError:
                pass
            self.pending_buttons.pop(n.node_id, None)
            self.f_hold.pop(n.node_id, None)

    def tick(self, now: float) -> None:
        # 1. manual-mode button presses whose 2-button window has passed
        for nid, pb in list(self.pending_buttons.items()):
            if now - pb.ts >= self.manual_s:
                del self.pending_buttons[nid]
                self._resolve_manual(nid, pb)
        # 2. manual long-press F
        for nid, h in list(self.f_hold.items()):
            if not h.fired and now - h.ts >= self.long_s and self.game.in_sequence(nid):
                h.fired = True                      # free nodes: F still acts on release
                self._f_long(nid, h.ts + self.long_s)
        # 3. idle window
        if self.game.playing:
            for nid, n in self.game.nodes.items():
                if n.register:
                    self._idle_resolve(nid, now)

    # ------------------------------------------------------------------ taps
    def _on_tap(self, nid: str, side: str, ts: float) -> None:
        if side not in ("L", "R", "V"):
            return
        g = self.game
        if g.in_sequence(nid):
            if g.is_head(nid) and side == "V":
                self._add_bit(nid, "V", ts)       # use stash (idle resolve)
            return                                # members: bump side effects, ignored
        n = g.nodes[nid]
        if side in ("L", "R") and n.register == {"V"} and not n.face_down and self._has_sequence(nid):
            # spec HUB-Q26: the side comes from the new node's own tap, no correlation needed.
            # Its RIGHT edge touched -> it sits left of the row -> left end (14 B3, CR3).
            n.register.clear()
            where = "left" if side == "R" else "right"
            self._record(ts, nid, f"add_{where}", g.add_to_sequence(nid, ts, where))
            return
        self._add_bit(nid, side, ts)

    def _add_bit(self, nid: str, bit: str, ts: float) -> None:
        n = self._node(nid)
        if n is None or not n.connected:
            return
        n.register.add(bit)
        self.last_gesture[nid] = max(ts, self.last_gesture.get(nid, ts))

    # ------------------------------------------------------------------ flips
    def _on_flip(self, nid: str, down: bool, ts: float) -> None:
        g = self.game
        n = g.nodes[nid]
        if n.face_down == down:
            return  # duplicate flip: idempotent
        n.face_down = down
        if g.in_sequence(nid) and not g.is_head(nid):
            # member: down then up within remove_window_ms removes it (14 B3, spec HUB-Q31)
            if down:
                self.member_down_at[nid] = ts
            else:
                t0 = self.member_down_at.pop(nid, None)
                if t0 is not None and ts - t0 <= self.remove_s:
                    self._record(ts, nid, "remove", g.remove_from_sequence(nid, ts))
            return
        if down:
            self._commit_down(nid, ts)
        else:
            n.register.clear()
            if g.is_head(nid):
                self._record(ts, nid, "push", g.push(n.player, ts))
            # face-up flip on a free node: the normal "turn it back over" motion
        self._normalise_manual(n.player)

    def _commit_down(self, nid: str, ts: float) -> Optional[str]:
        """Face-down commit on a free node. Returns the action taken, or None."""
        n = self.game.nodes[nid]
        reg = set(n.register)
        n.register.clear()
        g = self.game
        if g.in_sequence(nid):
            return None
        if not reg:
            action, ok = "discard", g.discard(nid, ts)
        elif reg == {"V"}:
            action, ok = "start", g.start_sequence(nid, ts)
        elif reg == {"L"}:
            action, ok = "stash_in", g.stash_in(nid, ts)
        else:
            return None   # unrecognised (incl. the old R + flip): no effect, not a discard
        return action if self._record(ts, nid, action, ok) else None

    # ------------------------------------------------------------------ idle window
    def _idle_resolve(self, nid: str, now: float) -> None:
        n = self.game.nodes[nid]
        reg = n.register
        elapsed = now - self.last_gesture.get(nid, now)
        g = self.game
        if g.is_head(nid):
            if elapsed >= self.idle_s:
                if reg == {"V"}:
                    self._record(now, nid, "use_stash", g.use_stash(n.player, now))
                reg.clear()
            return
        if self._is_free(nid):
            if any(reg == p for p in FLIP_PREFIXES):
                # wait longer for the flip (or side tap) that completes this gesture
                if elapsed >= self.commit_s:
                    reg.clear()
                return
            if elapsed >= self.idle_s:
                if reg == {"R", "L"}:     # 14 B3 / CR8: discard stash moved to R + L
                    self._record(now, nid, "discard_stash", g.discard_stash(n.player, now))
                reg.clear()
            return
        if elapsed >= self.idle_s:
            reg.clear()

    # ------------------------------------------------------------------ buttons
    def _f_mask(self) -> int:
        for b in range(4):
            if self.cfg.manual_button_map.get(f"bit{b}") == F_BIT_NAME:
                return 1 << b
        return 0

    def _on_buttons(self, nid: str, mask: int, ts: float) -> None:
        n = self.game.nodes[nid]
        prev = self.prev_mask.get(nid, 0)
        self.prev_mask[nid] = mask
        presses = mask & ~prev
        releases = prev & ~mask
        if n.scheme == "gesture":
            if presses:
                # any button press clears the register; on the head it cancels the sequence
                n.register.clear()
                if self.game.is_head(nid):
                    self._record(ts, nid, "cancel", self.game.cancel_sequence(n.player, ts))
            return
        if bin(mask).count("1") >= 3:
            # v1.5 X3: a 3+ button chord (e.g. the firmware's 4-button debug chord) is a clear
            # only, never L/R/V/F; on the head it cancels like any multi-button press.
            self.pending_buttons.pop(nid, None)
            self.f_hold.pop(nid, None)
            if presses:
                n.register.clear()
                if self.game.is_head(nid):
                    self._record(ts, nid, "cancel", self.game.cancel_sequence(n.player, ts))
                    self._normalise_manual(n.player)
            return
        fm = self._f_mask()
        if releases & fm:
            pb = self.pending_buttons.get(nid)
            if pb is not None and pb.mask & fm:
                pb.released = ts                   # quick tap: released inside the 2-button window
            else:
                h = self.f_hold.pop(nid, None)
                if h is not None and not h.fired:
                    self._f_short(nid, ts)
        if not presses:
            return
        pb = self.pending_buttons.get(nid)
        if pb is None:
            pb = self.pending_buttons[nid] = _PendingButtons(0, ts)
        pb.mask |= presses
        if bin(pb.mask).count("1") >= 2:
            del self.pending_buttons[nid]
            self._resolve_manual(nid, pb)

    def _button_primitive(self, bit: int) -> Optional[str]:
        return self.cfg.manual_button_map.get(f"bit{bit}")

    def _resolve_manual(self, nid: str, pb: _PendingButtons) -> None:
        n = self._node(nid)
        g = self.game
        if n is None or not n.connected or not g.playing:
            return
        ts = pb.ts
        if bin(pb.mask).count("1") >= 2 or bin(self.prev_mask.get(nid, 0)).count("1") >= 3:
            n.register.clear()
            self.f_hold.pop(nid, None)
            if g.is_head(nid):
                self._record(ts, nid, "cancel", g.cancel_sequence(n.player, ts))
                self._normalise_manual(n.player)
            return
        prim = self._button_primitive(pb.mask.bit_length() - 1)
        if prim == "F":
            if ts < self.f_lockout_until.get(nid, float("-inf")):
                return                              # just pushed / discarded: ignore F
            if pb.released is not None:
                self._f_short(nid, pb.released)
            else:
                self.f_hold[nid] = _FHold(ts)       # short on release, long at the threshold
            return
        if prim not in ("R", "L", "V"):
            return
        if g.in_sequence(nid):
            if g.is_head(nid) and prim == "V":
                self._add_bit(nid, "V", ts)
            return
        if prim in ("L", "R") and n.register == {"V"} and self._has_sequence(nid):
            n.register.clear()
            where = "left" if prim == "L" else "right"     # manual: L button = left end
            self._record(ts, nid, f"add_{where}", g.add_to_sequence(nid, ts, where))
            return
        self._add_bit(nid, prim, ts)

    def _lockout(self, nid: str, ts: float) -> None:
        self.f_lockout_until[nid] = ts + self.lockout_s

    def _f_short(self, nid: str, ts: float) -> None:
        """Short F (spec HUB-Q30: acts on release)."""
        g = self.game
        n = g.nodes[nid]
        if g.is_head(nid):
            n.face_down = False
            n.register.clear()
            if self._record(ts, nid, "push", g.push(n.player, ts)):
                self._lockout(nid, ts)
            self._normalise_manual(n.player)
            return
        if g.in_sequence(nid):
            return                                  # short F on a member does nothing
        if n.face_down:                             # only with manual_auto_face_up off:
            n.face_down = False                     # the literal toggle back up does nothing
            return
        n.face_down = True                          # free node: virtual face-down = commit
        if self._commit_down(nid, ts) == "discard":
            self._lockout(nid, ts)
        self._normalise_manual(n.player)

    def _f_long(self, nid: str, ts: float) -> None:
        g = self.game
        n = g.nodes.get(nid)
        if n is None or not g.playing:
            return
        if g.is_head(nid):
            self._record(ts, nid, "cancel", g.cancel_sequence(n.player, ts))
            self._normalise_manual(n.player)
        elif g.in_sequence(nid):
            self._record(ts, nid, "remove", g.remove_from_sequence(nid, ts))
