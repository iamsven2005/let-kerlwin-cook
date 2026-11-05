#!/bin/bash
# build.sh <outdir> [-DFEAT_X=0 ...]
# Compiles and links the glove firmware on a PC (g++, Linux or WSL) against small
# stand-ins for the Arduino libraries (arduino_stubs/). This checks that a combination of
# feature switches builds; it does not test the hardware. Use the Arduino IDE to upload.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../../GloveSurvey"
OUT=$1; shift
mkdir -p "$OUT"
printf '#include <Arduino.h>\n#include "%s/GloveSurvey.ino"\n' "$SRC" > "$OUT/ino.cpp"
g++ -std=gnu++17 -Wall -Wextra -Wno-unused-parameter -Werror=format -Wno-format-truncation \
    -I"$HERE/arduino_stubs" "$@" "$OUT/ino.cpp" "$HERE/arduino_stubs/stubs.cpp" $(find "$SRC/src" -name '*.cpp') -o "$OUT/fw" 2>&1
