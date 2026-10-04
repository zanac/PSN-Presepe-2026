#!/bin/sh
# Host-side tests of the PRESEPE.INI reader (no Arduino needed; any C++11 compiler).
# The reader lives in PSN-Presepe.ino between "// ==== CONFIG BEGIN ====" and
# "// ==== CONFIG END ====": that block is extracted into a temporary header
# (PresepeConfig.h) and compiled together with tools/test_config.cpp.
# Usage: firmware/tools/run_tests.sh   [CXX=clang++]
set -e
HERE=$(cd "$(dirname "$0")" && pwd); SK=$HERE/../PSN-Presepe
T=$(mktemp -d)
sed -n '/^\/\/ ==== CONFIG BEGIN ====$/,/^\/\/ ==== CONFIG END ====$/p' "$SK/PSN-Presepe.ino" > "$T/PresepeConfig.h"
[ -s "$T/PresepeConfig.h" ] || { echo "CONFIG BEGIN/END block not found in PSN-Presepe.ino"; exit 1; }
${CXX:-g++} -std=c++11 -Wall -I"$T" "$HERE/test_config.cpp" -o "$T/test_config"
"$T/test_config" "$SK/PRESEPE.INI"
rm -rf "$T"
