#!/bin/sh
# PSN-Presepe - complete rebuild and verification.
#
# Usage: ./build.sh [OUT_DIR]                     (default OUT_DIR = ./out, git-ignored)
#
#   firmware: compiles PSN-Presepe.ino for the Arduino Mega 2560 into OUT_DIR/firmware and runs
#             the host tests (firmware/tools/run_tests.sh)
#   PCB:      rebuilds every board revision with hardware/<rev>/build.sh into OUT_DIR/<rev>
#
# Environment (all optional):
#   SKIP_PCB=1 / SKIP_FIRMWARE=1    skip one half
#   REVS="rev-d rev-e"              PCB revisions to rebuild (default: every hardware/rev-* folder)
#   ARDUINO_CLI, SYSTEM_AVR, CTAGS_DIR   see firmware/tools/fw_compile.sh
#   REROUTE, FREEROUTING_JAR, JAVA, KICAD_FOOTPRINT_DIR, UPDATE_REPO   see hardware/<rev>/build.sh
set -e
ROOT=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-$ROOT/out}
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)

if [ "$SKIP_PCB" != 1 ]; then
  for d in ${REVS:-$(cd "$ROOT/hardware" && ls -d rev-*)}; do
    "$ROOT/hardware/$d/build.sh" "$OUT/$d"
  done
fi

if [ "$SKIP_FIRMWARE" != 1 ]; then
  echo "== Firmware: compile for Arduino Mega 2560"
  "$ROOT/firmware/tools/fw_compile.sh" "$ROOT/firmware/PSN-Presepe" "$OUT/firmware"
  echo "== Firmware: host tests"
  "$ROOT/firmware/tools/run_tests.sh"
fi
echo "== build.sh finished: $OUT"
