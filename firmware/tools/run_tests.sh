#!/bin/sh
# Host-side tests (no Arduino needed; any C++11 compiler):
#  1. PRESEPE.INI reader and colour tappe: the "// ==== CONFIG BEGIN/END ====" block of
#     PSN-Presepe.ino is extracted into a temporary PresepeConfig.h and compiled with
#     tools/test_config.cpp.
#  2. Colour-picker mode: the MODALITA' COLORE section is extracted and driven with
#     mocked buttons/potentiometer/OLED by tools/test_colore.cpp.
# Usage: firmware/tools/run_tests.sh   [CXX=clang++]
set -e
HERE=$(cd "$(dirname "$0")" && pwd); SK=$HERE/../PSN-Presepe; INO=$SK/PSN-Presepe.ino
T=$(mktemp -d)
sed -n '/^\/\/ ==== CONFIG BEGIN ====$/,/^\/\/ ==== CONFIG END ====$/p' "$INO" > "$T/PresepeConfig.h"
[ -s "$T/PresepeConfig.h" ] || { echo "CONFIG BEGIN/END block not found in PSN-Presepe.ino"; exit 1; }
cp "$T/PresepeConfig.h" "$T/cfgblock.h"
sed -n '/^struct TastoColore {/,/^enum EventoTasto/p' "$INO" > "$T/tasto.h"
awk '/^\/\/ MODALITA. COLORE \(ricerca/{f=1} /^void setup\(\) \{/{f=0} f' "$INO" > "$T/mode.inc"
[ -s "$T/tasto.h" ] && [ -s "$T/mode.inc" ] || { echo "colour-mode section not found in PSN-Presepe.ino"; exit 1; }
${CXX:-g++} -std=c++11 -Wall -I"$T" "$HERE/test_config.cpp" -o "$T/test_config"
"$T/test_config" "$SK/PRESEPE.INI"
${CXX:-g++} -std=c++11 -w -I"$T" "$HERE/test_colore.cpp" -o "$T/test_colore"
"$T/test_colore"
rm -rf "$T"
