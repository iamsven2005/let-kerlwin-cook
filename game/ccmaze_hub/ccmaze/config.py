"""Configuration: defaults, load/save, and typed access.

All values from section 5 of the brief (v1.2), plus two hub-only BLE extras.
Unknown keys in a loaded file are preserved; keys renamed between spec
versions are migrated on load. Paths stored in the config are relative, so
config files move between Linux and Windows unchanged.
"""
from __future__ import annotations

import copy
import json
from pathlib import Path
from typing import Any

DEFAULT_CONFIG: dict[str, Any] = {
    # --- keys from section 5 of the brief (v1.2), in the brief's order ---
    "maze_size": 15,
    "maze_braid_fraction": 0.15,
    "maze_seed": None,
    "deck_multiplier": 2,
    "min_copies_per_type": 1,                    # 14 A3: every enabled movement card is in every deck
    "min_domino_copies": 2,                      # 14 A3: solvability guarantee (U/D/L/R), minimum 1
    "control_card_share": 0.15,                  # 14 B2: IF + FOR, split evenly
    "card_catalog_file": "cards.json",          # relative to the config file's folder
    "deal_interval_s": 0.0,
    "tile_move_time_s": 0.0,
    "pause_after_step_s": 1.0,
    "pause_between_cards_s": 0.0,
    "pause_between_sequences_s": 0.0,
    "gesture_idle_window_ms": 400,
    "bump_correlation_ms": 80,                   # unused since v1.4 (spec HUB-Q26), kept for compatibility
    "manual_clear_window_ms": 100,
    "flip_commit_window_ms": 2000,
    "remove_window_ms": 3000,                    # 14 B3: member flipped down then up within this
    "manual_f_long_press_ms": 1000,              # 14 B3: long-press F = remove (member) / cancel (head)
    "manual_f_lockout_ms": 600,                  # 14 B3: F ignored after a push or discard
    "manual_auto_face_up": True,
    "lost_after_missed_pongs": 3,
    "battery_warn_percent": 20,
    "ble_service_uuid": "6E400001-B5A3-F393-E0A9-E50E24DCCA9E",
    "ble_rx_uuid": "6E400002-B5A3-F393-E0A9-E50E24DCCA9E",
    "ble_tx_uuid": "6E400003-B5A3-F393-E0A9-E50E24DCCA9E",
    "ble_name_prefix": "CCNODE-",
    "maze_algorithm": "backtracker",
    "manual_button_map": {"bit0": "L", "bit1": "R", "bit2": "V", "bit3": "F"},
    "stash_single_node": True,
    "stash_multi_node": False,
    "node_display_off_when_face_down": True,
    "optional_features": {
        "interrupt_mode": False,
        "coop_mode": False,
        "collision_penalty": False,
        "pickup_items": False,
        "players_5_6": False,
        "card_node_reassignment": False,
    },
    # --- hub-only extras (not in the brief) ---
    "ble_ping_interval_s": 2.0,
    "ble_scan_timeout_s": 5.0,
    "ble_reconnect_min_s": 1.0,                  # 14 A2 backoff: 1, 2, 4 ... capped at max
    "ble_reconnect_max_s": 10.0,
    "ble_write_with_response": False,            # spec HUB-Q34: diagnostic only
    "ble_write_gap_ms": 5,                       # v1.5 X5: writes at least this far apart
    "ble_burst_max": 10,                         # v1.5 X5: at most this many messages ...
    "ble_burst_window_ms": 100,                  # ... in any window of this length
}

# Keys renamed between spec versions: old name -> new name.
RENAMED_KEYS = {"ble_max_missed_pongs": "lost_after_missed_pongs"}


def _merge(base: dict, override: dict) -> dict:
    out = copy.deepcopy(base)
    for k, v in override.items():
        if isinstance(v, dict) and isinstance(out.get(k), dict):
            out[k] = _merge(out[k], v)
        else:
            out[k] = copy.deepcopy(v)
    return out


class Config:
    """Thin wrapper around a dict with attribute access and save/load."""

    def __init__(self, values: dict | None = None, path: str | Path | None = None):
        values = dict(values or {})
        for old, new in RENAMED_KEYS.items():
            if old in values:
                values.setdefault(new, values[old])
                del values[old]
        self._v = _merge(DEFAULT_CONFIG, values)
        self.path = Path(path) if path else None

    # --- access -----------------------------------------------------------
    def __getattr__(self, name: str) -> Any:
        try:
            return self.__dict__["_v"][name]
        except KeyError as e:
            raise AttributeError(name) from e

    def __getitem__(self, key: str) -> Any:
        return self._v[key]

    def get(self, key: str, default: Any = None) -> Any:
        return self._v.get(key, default)

    def set(self, key: str, value: Any) -> None:
        """Set a value; dotted keys address nested dicts (e.g. optional_features.coop_mode)."""
        parts = key.split(".")
        d = self._v
        for p in parts[:-1]:
            d = d[p]
        d[parts[-1]] = value

    def get_path(self, key: str) -> Any:
        d: Any = self._v
        for p in key.split("."):
            d = d[p]
        return d

    def feature(self, name: str) -> bool:
        return bool(self._v.get("optional_features", {}).get(name, False))

    def as_dict(self) -> dict:
        return copy.deepcopy(self._v)

    # --- persistence ------------------------------------------------------
    @classmethod
    def load(cls, path: str | Path) -> "Config":
        p = Path(path)
        if p.exists():
            with p.open("r", encoding="utf-8") as f:
                return cls(json.load(f), p)
        return cls({}, p)

    def save(self, path: str | Path | None = None) -> Path:
        p = Path(path) if path else self.path
        if p is None:
            raise ValueError("no path to save config to")
        with p.open("w", encoding="utf-8") as f:
            json.dump(self._v, f, indent=2)
            f.write("\n")
        self.path = p
        return p

    def reset_defaults(self) -> None:
        self._v = copy.deepcopy(DEFAULT_CONFIG)
