"""Register (RLVF), the 3 s latch, buttons and the action table (sections 18.5-18.7, 19, 21).

Everything is driven by event timestamps and explicit tick(now) calls, so it runs headless and
deterministically in tests.

Register model: each node holds a *set* of R/L/V bits; the F bit shown on the node is its
*logical* face-down state. An F (down) on a free node is the commit that reads and clears R/L/V.

Latching (section 19): a single L, R or V press sets its bit and holds it *latched* until latch_ms
(3000) after the most recent press on that node; every press restarts the timer. When it runs out
the register clears silently, with no action. latch_ms replaces gesture_idle_window_ms and
flip_commit_window_ms. Any chord (2+ buttons) clears the latched register at once.

Where the primitives come from (section 21, v1.11: one button per primitive, both modes)
  TL = V, TR = F, BL = L, BR = R (gesture_button_map / manual_button_map). Any 2+ buttons
  together only clear the latched register. Every press is resolved when the chord window
  (manual_clear_window_ms, 100 ms) from its first press closes (spec HUB-Q37, HUB-Q44), so a
  two-finger clear never fires an action first.
  The F button toggles the logical face state; after an action that leaves the node free it snaps
  back face-up (spec HUB-Q41). Gesture mode also accepts the physical flip (F:1 / F:0, sets the
  state directly; physical_flip_enabled); that is the only difference between the modes. Tilt and
  the V tap are ignored unless accept_tilt / accept_tap_v (Gesture mode only).

Action table (both modes)
  discard     F, empty register (free node)
  start       V, then F
  add left    V, then L      (open sequence; immediately)
  add right   V, then R
  push        F on the head
  remove      F twice on a non-head member within remove_window_ms (button, or physical down + up)
  stash in    L, then F
  disc. stash R, then F
  use stash   V on the face-down head (immediately)
  clear       any 2+ buttons together
There is no cancel in either mode (18.6). Inputs on row members are ignored, except the remove
gesture. After a push or a discard the node ignores F (button or flip) for manual_f_lockout_ms, in both
modes; a physical flip in that time still updates the logical state, silently.
While a player's avatar walks (route complete) their nodes do nothing (18.1.7).
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Optional

from ..config import Config
from .events import BSYNC, BUTTONS, CONNECTED, DISCONNECTED, FLIP, FSTATE, FSYNC, TAP, NodeEvent
from .game import Game

# Section 21 (v1.11): the same layout in both modes. TL = V, TR = F, BL = L, BR = R; any chord = clear.
DEFAULT_BUTTON_MAP = {"bit0": "V", "bit1": "F", "bit2": "L", "bit3": "R"}


def _bits(mask: int) -> int:
    return bin(mask).count("1")


@dataclass
class _PendingButtons:
    mask: int
    ts: float
    max_held: int = 0                   # most buttons held at once inside the window


class InputController:
    def __init__(self, game: Game, cfg: Config):
        self.game = game
        self.cfg = cfg
        self.last_gesture: dict[str, float] = {}
        self.pending_buttons: dict[str, _PendingButtons] = {}
        self.prev_mask: dict[str, int] = {}
        self.member_down_at: dict[str, float] = {}     # remove: member's first F (down) at
        self.f_lockout_until: dict[str, float] = {}    # ignore F until (18.6)
        self.actions: list[tuple[float, str, str]] = []    # (ts, node_id, action) for UI/log/tests
        self.raw: list[tuple[float, str, str, str]] = []   # (ts, node, kind, value) for the controls test
        self.primitives: list[tuple[float, str, str]] = []  # (ts, node, L/R/V/F/clear) resolved inputs

    # ------------------------------------------------------------------ helpers
    def _ms(self, key: str, default: float) -> float:
        return float(self.cfg.get(key, default)) / 1000.0

    @property
    def latch_s(self) -> float:
        """Section 19: how long a press keeps the register latched (replaces the idle and
        flip-commit windows)."""
        return self._ms("latch_ms", 3000)

    def latch_remaining(self, nid: str, now: float) -> float:
        """Seconds until this node's latched register clears (0 if nothing is latched)."""
        n = self.game.nodes.get(nid)
        if n is None or not n.register:
            return 0.0
        return max(0.0, self.last_gesture.get(nid, now) + self.latch_s - now)

    @property
    def chord_s(self) -> float:
        return self._ms("manual_clear_window_ms", 100)

    @property
    def remove_s(self) -> float:
        return self._ms("remove_window_ms", 3000)

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

    def _prim_log(self, ts: float, nid: str, what: str) -> None:
        self.primitives.append((ts, nid, what))
        if len(self.primitives) > 300:
            del self.primitives[:-300]

    def _is_free(self, nid: str) -> bool:
        n = self.game.nodes[nid]
        return not n.face_down and not self.game.in_sequence(nid)

    def _has_sequence(self, nid: str) -> bool:
        pl = self.game.player_of(nid)
        return bool(pl and pl.sequence)

    def _walking(self, nid: str) -> bool:
        pl = self.game.player_of(nid)
        return bool(pl and pl.walking)

    def _locked_out(self, nid: str, ts: float) -> bool:
        return ts < self.f_lockout_until.get(nid, float("-inf"))

    def _lockout(self, nid: str, ts: float) -> None:
        self.f_lockout_until[nid] = ts + self.lockout_s

    def _normalise_manual(self, player_idx: int) -> None:
        """Manual nodes not in an open sequence snap back to virtual face-up (spec HUB-Q11)."""
        if not self.cfg.get("manual_auto_face_up", True):
            return
        pl = self.game.players[player_idx]
        for nid in pl.node_ids:
            n = self.game.nodes[nid]
            if n.scheme == "manual" and n.face_down and not self.game.in_sequence(nid):
                n.face_down = False

    def _forget(self, nid: str) -> None:
        self.pending_buttons.pop(nid, None)
        self.member_down_at.pop(nid, None)

    # ------------------------------------------------------------------ entry points
    def handle(self, ev: NodeEvent) -> None:
        n = self._node(ev.node_id)
        if n is None:
            return  # unassigned node: ignore
        self.raw.append((ev.ts, ev.node_id, ev.kind, ev.value))
        if len(self.raw) > 300:
            del self.raw[:-300]
        if ev.kind == DISCONNECTED:
            self._forget(ev.node_id)
            self.prev_mask.pop(ev.node_id, None)
            self.game.node_disconnected(ev.node_id, ev.ts)
            self._normalise_manual(n.player)      # a dropped chunk's head lies face-up again
            return
        if ev.kind == CONNECTED:
            self.game.node_reconnected(ev.node_id, ev.ts)
            return
        if ev.kind in (FSYNC, BSYNC, FSTATE):
            self._sync_state(n, ev)   # silent state syncs: never an action
            return
        if not self.game.playing or not n.connected:
            return
        if self._walking(ev.node_id):
            # 18.1.7: the player's nodes do nothing while the avatar walks; keep state in step only
            if ev.kind == FLIP and n.scheme == "gesture" and self._physical_flips:
                n.face_down = ev.value == "1"
            elif ev.kind == BUTTONS:
                self.prev_mask[ev.node_id] = self._parse_mask(ev.value) or 0
            self._forget(ev.node_id)
            n.register.clear()
            return
        self.tick(ev.ts)
        if ev.kind == TAP:
            if n.scheme == "gesture":
                self._on_tap(ev.node_id, ev.value, ev.ts)
        elif ev.kind == FLIP:
            if n.scheme == "gesture" and self._physical_flips:
                self._set_face(ev.node_id, ev.value == "1", ev.ts, physical=True)
        elif ev.kind == BUTTONS:
            mask = self._parse_mask(ev.value)
            if mask is not None:
                self._on_buttons(ev.node_id, mask, ev.ts)

    @staticmethod
    def _parse_mask(value) -> Optional[int]:
        try:
            return int(value, 16)
        except (TypeError, ValueError):
            return None

    @property
    def _physical_flips(self) -> bool:
        return bool(self.cfg.get("physical_flip_enabled", True))

    def _sync_state(self, n, ev: NodeEvent) -> None:
        if ev.kind == FSYNC:
            if n.scheme == "gesture" and self._physical_flips:
                n.face_down = ev.value == "1"
        elif ev.kind == FSTATE:
            # v1.5 X1: the node's accelerometer came back and the orientation had changed while it
            # was missing. Take the new state and clear the register, but never act on it. Manual
            # nodes keep their virtual face state (the physical one doesn't matter to them).
            if n.scheme == "gesture" and self._physical_flips:
                n.face_down = ev.value == "1"
                n.register.clear()
                self.member_down_at.pop(n.node_id, None)
        else:
            mask = self._parse_mask(ev.value)
            if mask is not None:
                self.prev_mask[n.node_id] = mask
            self.pending_buttons.pop(n.node_id, None)

    def tick(self, now: float) -> None:
        # 1. button presses whose chord window has closed
        for nid, pb in list(self.pending_buttons.items()):
            if now - pb.ts >= self.chord_s:
                del self.pending_buttons[nid]
                self._resolve_buttons(nid, pb)
        # 2. the latch runs out: clear silently, no action (19.1)
        for nid, n in self.game.nodes.items():
            if n.register and now - self.last_gesture.get(nid, now) >= self.latch_s:
                n.register.clear()

    # ------------------------------------------------------------------ taps (off by default)
    def _on_tap(self, nid: str, side: str, ts: float) -> None:
        if side in ("L", "R") and self.cfg.get("accept_tilt", False):
            self._prim_log(ts, nid, side)
            self._on_primitive(nid, side, ts)
        elif side == "V" and self.cfg.get("accept_tap_v", False):
            self._prim_log(ts, nid, side)
            self._on_primitive(nid, side, ts)
        # 18.5: otherwise tilts and V taps are ignored

    # ------------------------------------------------------------------ L / R / V
    def _on_primitive(self, nid: str, prim: str, ts: float) -> None:
        g = self.game
        n = g.nodes[nid]
        if g.in_sequence(nid):
            if g.is_head(nid) and prim == "V":
                n.register.clear()                # 19.1: V on the face-down head -> use stash, now
                self._record(ts, nid, "use_stash", g.use_stash(n.player, ts))
            return                                # members: ignored (only the remove gesture)
        if prim in ("L", "R") and "V" in n.register and not n.face_down and self._has_sequence(nid):
            # 17.2 / 18.6: literal. V, then L = left end; V, then R = right end.
            n.register.clear()
            where = "left" if prim == "L" else "right"
            self._record(ts, nid, f"add_{where}", g.add_to_sequence(nid, ts, where))
            return
        self._add_bit(nid, prim, ts)

    def _add_bit(self, nid: str, bit: str, ts: float) -> None:
        n = self._node(nid)
        if n is None or not n.connected:
            return
        n.register.add(bit)
        self.last_gesture[nid] = ts               # 19.1: every press restarts the latch

    # ------------------------------------------------------------------ F
    def _set_face(self, nid: str, down: bool, ts: float, physical: bool) -> None:
        """An F: a physical flip (Gesture mode; sets the state) or the F button (toggles it)."""
        g = self.game
        n = g.nodes[nid]
        if n.face_down == down:
            return  # duplicate flip: idempotent
        if self._locked_out(nid, ts):
            # 18.6 / 21.1 lockout: the F button is ignored (handled by the caller); a physical flip still
            # updates the logical state so it never drifts from the real one, but does nothing.
            n.face_down = down
            return
        n.face_down = down
        if physical:
            self._prim_log(ts, nid, "F")
        if g.in_sequence(nid) and not g.is_head(nid):
            # member: F twice (down, then up) within remove_window_ms removes it (18.6)
            if down:
                self.member_down_at[nid] = ts
            else:
                t0 = self.member_down_at.pop(nid, None)
                if t0 is not None and ts - t0 <= self.remove_s:
                    self._record(ts, nid, "remove", g.remove_from_sequence(nid, ts))
            return
        if down:
            action = self._commit_down(nid, ts)
            if action == "discard":
                self._lockout(nid, ts)
            if not physical and not g.in_sequence(nid):
                # spec HUB-Q41: after an F-button commit that leaves the node free, it is still lying
                # face-up, so its logical state snaps back up (only a start keeps it down).
                n.face_down = False
        else:
            n.register.clear()
            if g.is_head(nid):
                if self._record(ts, nid, "push", g.push(n.player, ts)):
                    self._lockout(nid, ts)
            # face-up flip on a free node: the normal "turn it back over" motion

    def _commit_down(self, nid: str, ts: float) -> Optional[str]:
        """F (down) on a free node. Returns the action taken, or None."""
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
        elif reg == {"R"}:
            action, ok = "discard_stash", g.discard_stash(n.player, ts)    # 18.6: R, then F
        else:
            return None   # unrecognised: no effect, not a discard
        return action if self._record(ts, nid, action, ok) else None

    # ------------------------------------------------------------------ buttons
    def _button_map(self, nid: str) -> dict:
        n = self.game.nodes.get(nid)
        key = "gesture_button_map" if (n is not None and n.scheme == "gesture") else "manual_button_map"
        return self.cfg.get(key) or DEFAULT_BUTTON_MAP

    def _on_buttons(self, nid: str, mask: int, ts: float) -> None:
        prev = self.prev_mask.get(nid, 0)
        self.prev_mask[nid] = mask
        presses = mask & ~prev
        if not presses:
            return                                 # releases mean nothing (21.1: no long press)
        pb = self.pending_buttons.get(nid)
        if pb is None:
            pb = self.pending_buttons[nid] = _PendingButtons(0, ts)
        pb.mask |= presses
        pb.max_held = max(pb.max_held, _bits(mask))

    def _resolve_buttons(self, nid: str, pb: _PendingButtons) -> None:
        n = self._node(nid)
        g = self.game
        if n is None or not n.connected or not g.playing:
            return
        ts = pb.ts
        if _bits(pb.mask) >= 2 or pb.max_held >= 2:
            # 21.1: any 2+ buttons together (in the window, or pressed while another is still held)
            # only clear the latched register: never V / F / L / R (spec HUB-Q44).
            n.register.clear()
            self._prim_log(ts, nid, "clear")
            return
        prim = self._button_map(nid).get(f"bit{pb.mask.bit_length() - 1}")
        if prim == "F":
            self._prim_log(ts, nid, "F")
            if not self._locked_out(nid, ts):     # 21.1: the lockout applies in both modes
                self._set_face(nid, not n.face_down, ts, physical=False)
            return
        if prim in ("L", "R", "V"):
            self._prim_log(ts, nid, prim)
            self._on_primitive(nid, prim, ts)
