#!/usr/bin/env bash
# Cross-compile + link the sketch for a SAMD21G18A with arm-none-eabi-gcc,
# against the upstream Arduino SAMD core and a copy of the Zero variant with a
# second SPI port added (the TinyScreen library needs SPI1, as on TinyScreen+).
# This is a COMPILE/LINK CHECK ONLY: the output is not a TinyScreen+ image.
# Flash with the Arduino IDE + TinyCircuits board package instead (README).
#
#   tools/compile_check.sh <ArduinoCore-samd> <CMSIS_5> <CMSIS-Atmel> <TinyScreen lib>
set -euo pipefail
CORE=$1; CMSIS=$2; ATMEL=$3; TSLIB=$4
HERE=$(cd "$(dirname "$0")/.." && pwd)
SK=${SKETCH:-$HERE/firmware/CCNode}
B=$(mktemp -d)
VAR=$B/variant
cp -r "$CORE/variants/arduino_zero" "$VAR"
python3 - "$VAR/variant.h" <<'PY'
import sys; p=sys.argv[1]; s=open(p).read()
s=s.replace("#define SPI_INTERFACES_COUNT 1","#define SPI_INTERFACES_COUNT 2\n#define PIN_SPI1_MISO (22u)\n#define PIN_SPI1_MOSI (23u)\n#define PIN_SPI1_SCK (24u)\n#define PERIPH_SPI1 sercom1\n#define PAD_SPI1_TX SPI_PAD_2_SCK_3\n#define PAD_SPI1_RX SERCOM_RX_PAD_0")
open(p,'w').write(s)
PY
CPU="-mcpu=cortex-m0plus -mthumb"
DEFS="-DF_CPU=48000000L -DARDUINO=10819 -DARDUINO_SAMD_ZERO -DARDUINO_ARCH_SAMD -D__SAMD21G18A__ -DUSBCON -DUSB_VID=0x03EB -DUSB_PID=0x8009 -DUSB_MANUFACTURER=\"TinyCircuits\" -DUSB_PRODUCT=\"TinyScreen+\""
INC="-isystem $CORE/cores/arduino -isystem $VAR -isystem $CORE/libraries/SPI -isystem $CORE/libraries/Wire -isystem $TSLIB -isystem $CMSIS/CMSIS/Core/Include -isystem $ATMEL/CMSIS/Device/ATMEL -I$SK"
COMMON="$CPU -Os -g -ffunction-sections -fdata-sections -nostdlib --param max-inline-insns-single=500 -MMD $DEFS $INC"
CFLAGS="$COMMON -std=gnu11"
CXXFLAGS="$COMMON -std=gnu++11 -fno-threadsafe-statics -fno-rtti -fno-exceptions -Wall -Wextra -Wno-unused-parameter"
OBJS=()
compile() {  # src  [extra flags]
  local src=$1; shift
  local o=$B/obj/$(echo "$src" | md5sum | cut -c1-8)_$(basename "$src").o
  mkdir -p "$B/obj"
  case "$src" in
    *.c)   arm-none-eabi-gcc $CFLAGS "$@" -c "$src" -o "$o" ;;
    *.S)   arm-none-eabi-gcc $COMMON -x assembler-with-cpp -c "$src" -o "$o" ;;
    *)     arm-none-eabi-g++ $CXXFLAGS "$@" -c "$src" -o "$o" ;;
  esac
  OBJS+=("$o")
}
# core, variant, libraries (warnings from third-party code suppressed)
while IFS= read -r f; do compile "$f" -w; done < <(find "$CORE/cores/arduino" "$VAR" "$CORE/libraries/SPI" "$CORE/libraries/Wire" "$TSLIB" -maxdepth 3 \( -name '*.c' -o -name '*.cpp' -o -name '*.S' \) ! -path '*/examples/*' ! -path '*/extras/*')
[ -d "$SK/src" ] && while IFS= read -r f; do compile "$f" -w; done < <(find "$SK/src" \( -name '*.c' -o -name '*.cpp' \))
# the sketch itself, with warnings on
cp "$SK"/*.ino "$B/sketch.ino.cpp"
compile "$B/sketch.ino.cpp" -I"$SK"
for f in "$SK"/*.cpp; do [ -e "$f" ] && compile "$f"; done
arm-none-eabi-g++ $CPU -Os -Wl,--gc-sections -save-temps -T"$VAR/linker_scripts/gcc/flash_with_bootloader.ld" \
  -Wl,-Map,"$B/ccnode.map" --specs=nano.specs --specs=nosys.specs -Wl,--cref -Wl,--check-sections \
  -Wl,--unresolved-symbols=report-all -Wl,--warn-common -Wl,--warn-section-align \
  -o "$B/ccnode.elf" "${OBJS[@]}" -Wl,--start-group -lm -Wl,--end-group
arm-none-eabi-size -A "$B/ccnode.elf" | awk '/^\.text|^\.data|^\.bss|^\.relocate/'
arm-none-eabi-size "$B/ccnode.elf"
echo "OK: compiled and linked ($B/ccnode.elf)"
