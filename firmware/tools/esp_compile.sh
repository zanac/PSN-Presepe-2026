#!/bin/sh
# Compile the ESP8266 firmware of the "Mega + WiFi R3" board with arduino-cli.
#
# Usage: firmware/tools/esp_compile.sh [output_dir]
#   output_dir  default: firmware/PSN-Presepe-WiFi/build  (PSN-Presepe-WiFi.ino.bin is written there)
#
# Environment:
#   ARDUINO_CLI   arduino-cli executable (default: arduino-cli from PATH)
#   CTAGS_DIR     only if arduino-cli has no ctags of its own (Arduino's ctags build)
#
# One-time setup:
#   arduino-cli core update-index --additional-urls https://arduino.esp8266.com/stable/package_esp8266com_index.json
#   arduino-cli core install esp8266:esp8266 --additional-urls https://arduino.esp8266.com/stable/package_esp8266com_index.json
#
# Board: Generic ESP8266 Module, 4 MB flash (1 MB file system, ~1 MB for OTA updates).
# The web pages come from pagine_web.h: run firmware/tools/build_web.py after editing them.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SK=$(cd "$HERE/../PSN-Presepe-WiFi" && pwd)
OUT=${1:-$SK/build}
ACLI=${ARDUINO_CLI:-arduino-cli}
set -- --fqbn esp8266:esp8266:generic:eesz=4M1M,baud=115200 --warnings default --output-dir "$OUT"
if [ -n "$CTAGS_DIR" ]; then
  set -- "$@" --build-property "runtime.tools.ctags.path=$CTAGS_DIR" --build-property "tools.ctags.cmd.path=$CTAGS_DIR/ctags"
fi
"$ACLI" compile "$@" "$SK"
echo "BIN: $OUT/PSN-Presepe-WiFi.ino.bin"
