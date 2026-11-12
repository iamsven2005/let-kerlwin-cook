#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
check_dir="$(mktemp -d)"
trap 'rm -rf "$check_dir"' EXIT
python3 -m unittest discover -s tests -p 'test_*.py' -v
g++ -std=c++11 -Itests/stubs tests/test_ir.cpp -o "$check_dir/ir"
"$check_dir/ir"
gcc -std=c99 -Itests/stubs tests/test_hci.c \
  firmware/Helmet_Left/src/stble/STBlueNRG/hci.c \
  firmware/Helmet_Left/src/stble/STBlueNRG/ble_list.c \
  firmware/Helmet_Left/src/stble/STBlueNRG/osal.c \
  firmware/Helmet_Left/src/stble/STBlueNRG/gp_timer.c -o "$check_dir/hci"
"$check_dir/hci"
