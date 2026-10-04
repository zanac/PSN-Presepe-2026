#!/bin/sh
# Compile the PSN-Presepe firmware for the Arduino Mega 2560 with arduino-cli.
#
# Usage: firmware/tools/fw_compile.sh [sketch_dir] [output_dir]
#   sketch_dir  default: firmware/PSN-Presepe (next to this script)
#   output_dir  default: <sketch_dir>/build   (PSN-Presepe.ino.hex is written there)
#
# Environment:
#   ARDUINO_CLI   arduino-cli executable (default: arduino-cli from PATH)
#   SYSTEM_AVR=1  use the system avr-gcc in /usr/bin instead of the Arduino toolchain;
#                 CTAGS_DIR must then contain Arduino's ctags build (github.com/arduino/ctags)
#
# One-time setup with the standard Arduino toolchain:
#   arduino-cli core install arduino:avr
#   arduino-cli lib install "Adafruit NeoPixel" "Adafruit GFX Library" "Adafruit SSD1306" "SD"
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SK=${1:-$HERE/../PSN-Presepe}
SK=$(cd "$SK" && pwd)
OUT=${2:-$SK/build}
ACLI=${ARDUINO_CLI:-arduino-cli}
set -- --fqbn arduino:avr:mega:cpu=atmega2560 --warnings default --output-dir "$OUT"
if [ "$SYSTEM_AVR" = 1 ]; then
  : "${CTAGS_DIR:?SYSTEM_AVR=1 needs CTAGS_DIR}"
  set -- "$@" --build-property "compiler.path=/usr/bin/" \
              --build-property "runtime.tools.avr-gcc.path=/usr" \
              --build-property "runtime.tools.ctags.path=$CTAGS_DIR" \
              --build-property "tools.ctags.cmd.path=$CTAGS_DIR/ctags"
fi
"$ACLI" compile "$@" "$SK"
echo "HEX: $OUT/$(basename "$SK").ino.hex"
