#!/usr/bin/env bash
# Start the hub from the project's virtual environment. Extra arguments go to main.py
# (e.g. ./run.sh --virtual 4 --players 2 --start, or ./run.sh --help).
set -euo pipefail
cd "$(dirname "$0")"
if [ ! -x .venv/bin/python ] || ! .venv/bin/python -c 'import pygame' >/dev/null 2>&1; then
  echo "No working virtual environment here yet. Run ./setup.sh first."
  exit 1
fi
exec .venv/bin/python main.py "$@"
