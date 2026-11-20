#!/usr/bin/env bash
# Linux setup for the Command Card Maze Race hub.
#   ./setup.sh             create .venv (fresh if it came from another OS), install, run doctor.py,
#                          then OFFER (asks first) to tune the BlueZ connection interval.
#   ./setup.sh --recreate  always delete and rebuild .venv
# Never copy .venv between machines or OSes: run this script on each one instead.
set -euo pipefail
cd "$(dirname "$0")"
ROOT="$(pwd)"
VENV="$ROOT/.venv"
RECREATE=0
[ "${1:-}" = "--recreate" ] && RECREATE=1

# 1. a Python between 3.10 and 3.13
PY="${PYTHON:-}"
if [ -z "$PY" ]; then
  for c in python3.13 python3.12 python3.11 python3.10 python3; do
    if command -v "$c" >/dev/null 2>&1 && "$c" -c 'import sys; sys.exit(0 if (3,10) <= sys.version_info[:2] <= (3,13) else 1)'; then
      PY="$c"; break
    fi
  done
fi
if [ -z "$PY" ]; then
  echo "No Python 3.10-3.13 found. Install one (e.g. sudo apt install python3 python3-venv) and re-run."
  exit 1
fi
echo "Using $($PY --version) ($(command -v "$PY"))"

# 2. a fresh, working virtual environment for THIS OS
if [ -d "$VENV" ]; then
  if [ "$RECREATE" = 1 ] || [ ! -x "$VENV/bin/python" ] || ! "$VENV/bin/python" -c 'import sys' >/dev/null 2>&1; then
    echo "Removing $VENV (unusable here: missing, broken, or created on another OS)."
    rm -rf "$VENV"
  fi
fi
if [ ! -d "$VENV" ]; then
  if ! "$PY" -m venv "$VENV"; then
    echo "Could not create a virtual environment. On Debian/Ubuntu: sudo apt install python3-venv"
    exit 1
  fi
fi
"$VENV/bin/python" -m pip install --upgrade pip >/dev/null
"$VENV/bin/python" -m pip install -r requirements.txt

# 3. environment check (a failure here is reported, not fatal)
echo
"$VENV/bin/python" doctor.py || true

# 4. optional BlueZ connection-interval tuning (asks first, needs sudo, backs up main.conf)
if [ "$(uname -s)" = "Linux" ]; then
  state="$("$VENV/bin/python" tools/bluez_conn_interval.py status --quiet 2>/dev/null || true)"
  if [ "$state" = "unset" ]; then
    echo
    echo "The nodes ask for a 7.5-15 ms BLE connection interval (low, steady tap latency for bump detection)."
    echo "BlueZ only grants that if /etc/bluetooth/main.conf allows it:"
    echo "  [LE] MinConnectionInterval=6  MaxConnectionInterval=12"
    if [ -t 0 ]; then
      read -r -p "Set this now? It backs up main.conf, needs sudo and restarts bluetooth. [y/N] " ans
      case "$ans" in
        y|Y|yes|YES)
          sudo "$VENV/bin/python" tools/bluez_conn_interval.py apply ;;
        *)
          echo "Skipped. You can do it later with:"
          echo "  sudo $VENV/bin/python tools/bluez_conn_interval.py apply" ;;
      esac
    else
      echo "Not interactive: skipped. To set it later: sudo $VENV/bin/python tools/bluez_conn_interval.py apply"
    fi
    echo "To undo at any time: sudo $VENV/bin/python tools/bluez_conn_interval.py undo"
  fi
fi

echo
echo "Setup done. Start the hub with ./run.sh   (no Bluetooth: ./run.sh --no-ble --virtual 4 --players 2)"
