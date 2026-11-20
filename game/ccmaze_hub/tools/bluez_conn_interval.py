#!/usr/bin/env python3
"""Linux only: set / undo / show the BlueZ LE connection-interval limits in /etc/bluetooth/main.conf.

  python tools/bluez_conn_interval.py status          # no root needed
  sudo .venv/bin/python tools/bluez_conn_interval.py apply
  sudo .venv/bin/python tools/bluez_conn_interval.py undo

`apply` backs up main.conf to main.conf.ccmaze-backup-<timestamp>, sets
MinConnectionInterval=6 and MaxConnectionInterval=12 under [LE] (7.5-15 ms, the range the
nodes request) and restarts the bluetooth service. `undo` restores the newest backup and
restarts the service. setup.sh only runs `apply` after asking you.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ccmaze.nodes.ble_platform import (BLUEZ_MAIN_CONF, LE_MAX_INTERVAL, LE_MIN_INTERVAL,  # noqa: E402
                                       apply_le_intervals, le_intervals_ok, read_le_intervals)

BACKUP_GLOB = "main.conf.ccmaze-backup-*"


def _need_root():
    if hasattr(os, "geteuid") and os.geteuid() != 0:
        sys.exit("This needs root: run it with sudo.")


def _restart():
    print("Restarting the bluetooth service...")
    r = subprocess.run(["systemctl", "restart", "bluetooth"])
    if r.returncode != 0:
        print("Could not restart bluetooth; run `sudo systemctl restart bluetooth` yourself.")


def status(conf: Path, quiet: bool) -> int:
    if not conf.exists():
        print("missing" if quiet else f"{conf} not found (is BlueZ installed?)")
        return 2
    text = conf.read_text(encoding="utf-8", errors="replace")
    ok = le_intervals_ok(text)
    if quiet:
        print("set" if ok else "unset")
    else:
        v = read_le_intervals(text)
        print(f"{conf}: MinConnectionInterval={v['MinConnectionInterval']} "
              f"MaxConnectionInterval={v['MaxConnectionInterval']} "
              f"({'OK' if ok else f'want {LE_MIN_INTERVAL}/{LE_MAX_INTERVAL}'})")
        backups = sorted(conf.parent.glob(BACKUP_GLOB))
        if backups:
            print(f"newest backup: {backups[-1]}")
    return 0 if ok else 1


def apply(conf: Path, restart: bool) -> int:
    _need_root()
    text = conf.read_text(encoding="utf-8") if conf.exists() else ""
    if text and le_intervals_ok(text):
        print("Already set; nothing to do.")
        return 0
    if conf.exists():
        backup = conf.with_name(f"main.conf.ccmaze-backup-{time.strftime('%Y%m%d-%H%M%S')}")
        shutil.copy2(conf, backup)
        print(f"Backed up {conf} -> {backup}")
    conf.write_text(apply_le_intervals(text), encoding="utf-8")
    print(f"Set MinConnectionInterval={LE_MIN_INTERVAL} and MaxConnectionInterval={LE_MAX_INTERVAL} under [LE].")
    if restart:
        _restart()
    print(f"To undo: sudo {sys.executable} {Path(__file__).resolve()} undo")
    return 0


def undo(conf: Path, restart: bool) -> int:
    _need_root()
    backups = sorted(conf.parent.glob(BACKUP_GLOB))
    if not backups:
        print("No ccmaze backup found; nothing to undo.")
        return 1
    shutil.copy2(backups[-1], conf)
    print(f"Restored {conf} from {backups[-1]}")
    if restart:
        _restart()
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("action", choices=["status", "apply", "undo"])
    ap.add_argument("--conf", type=Path, default=BLUEZ_MAIN_CONF, help=argparse.SUPPRESS)
    ap.add_argument("--no-restart", action="store_true", help="don't restart the bluetooth service")
    ap.add_argument("--quiet", action="store_true", help="status: print only set/unset/missing")
    a = ap.parse_args(argv)
    if not sys.platform.startswith("linux"):
        print("This tool is for Linux/BlueZ only. On Windows the connection interval can't be configured.")
        return 0
    if a.action == "status":
        return status(a.conf, a.quiet)
    if a.action == "apply":
        return apply(a.conf, not a.no_restart)
    return undo(a.conf, not a.no_restart)


if __name__ == "__main__":
    sys.exit(main())
