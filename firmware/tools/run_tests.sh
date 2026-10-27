#!/bin/sh
# Host-side tests (no Arduino needed; any C++11 compiler):
#  1. PRESEPE.INI reader and colour tappe: the "// ==== CONFIG BEGIN/END ====" block of
#     PSN-Presepe.ino is extracted into a temporary PresepeConfig.h and compiled with
#     tools/test_config.cpp.
#  2. Colour-picker mode: the MODALITA' COLORE section is extracted and driven with
#     mocked buttons/potentiometer/OLED by tools/test_colore.cpp.
#  3. WiFi link: the "// ==== LINK WIFI BEGIN/END ====" block is driven by
#     tools/test_link.cpp with a mocked ESP8266 (Serial3), microSD and outputs.
#  3b. Default file: PRESEPE.INI embedded in the sketch (embed_ini.py --check) and written
#     to an empty card by iniCreaPredefinito (tools/test_ini.cpp).
#  4. End to end: the real ESP8266 firmware (PSN-Presepe-WiFi.ino, with mocked WiFi and
#     web server, tools/e2e/mock) talks to the real Mega link block over a serial line
#     that loses bytes like the Mega does during WS2811 refreshes (tools/e2e).
# Usage: firmware/tools/run_tests.sh   [CXX=clang++]
set -e
HERE=$(cd "$(dirname "$0")" && pwd); SK=$HERE/../PSN-Presepe; INO=$SK/PSN-Presepe.ino
T=$(mktemp -d)
sed -n '/^\/\/ ==== CONFIG BEGIN ====$/,/^\/\/ ==== CONFIG END ====$/p' "$INO" > "$T/PresepeConfig.h"
[ -s "$T/PresepeConfig.h" ] || { echo "CONFIG BEGIN/END block not found in PSN-Presepe.ino"; exit 1; }
cp "$T/PresepeConfig.h" "$T/cfgblock.h"
sed -n '/^struct TastoColore {/,/^enum EventoTasto/p' "$INO" > "$T/tasto.h"
awk '/^\/\/ MODALITA. COLORE \(ricerca/{f=1} /^\/\/ ==== LINK WIFI BEGIN ====$/{f=0} /^void setup\(\) \{/{f=0} f' "$INO" > "$T/mode.inc"
sed -n '/^\/\/ ==== LINK WIFI BEGIN ====$/,/^\/\/ ==== LINK WIFI END ====$/p' "$INO" > "$T/link.inc"
[ -s "$T/tasto.h" ] && [ -s "$T/mode.inc" ] || { echo "colour-mode section not found in PSN-Presepe.ino"; exit 1; }
[ -s "$T/link.inc" ] || { echo "LINK WIFI BEGIN/END block not found in PSN-Presepe.ino"; exit 1; }
${CXX:-g++} -std=c++11 -Wall -I"$T" "$HERE/test_config.cpp" -o "$T/test_config"
"$T/test_config" "$SK/PRESEPE.INI"
${CXX:-g++} -std=c++11 -w -I"$T" "$HERE/test_colore.cpp" -o "$T/test_colore"
"$T/test_colore"
${CXX:-g++} -std=c++11 -Wall -Wno-unused-function -I"$T" "$HERE/test_link.cpp" -o "$T/test_link"
"$T/test_link"
python3 "$HERE/embed_ini.py" --check
sed -n '/^\/\/ ==== INI PREDEFINITO BEGIN ====$/,/^\/\/ ==== INI PREDEFINITO END ====$/p' "$INO" > "$T/iniprede.inc"
sed -n '/^\/\/ ==== INI SCRIVI BEGIN ====$/,/^\/\/ ==== INI SCRIVI END ====$/p' "$INO" > "$T/iniscrivi.inc"
[ -s "$T/iniprede.inc" ] && [ -s "$T/iniscrivi.inc" ] || { echo "INI PREDEFINITO / INI SCRIVI blocks not found in PSN-Presepe.ino"; exit 1; }
${CXX:-g++} -std=c++11 -Wall -Wno-unused-function -I"$T" "$HERE/test_ini.cpp" -o "$T/test_ini"
"$T/test_ini" "$SK/PRESEPE.INI"
E=$HERE/e2e
${CXX:-g++} -std=c++14 -Wall -Wno-unused-function -I"$T" -I"$E" -c "$E/mega_side.cpp" -o "$T/mega_side.o"
${CXX:-g++} -std=c++14 -Wall -Wno-unused-function -I"$E/mock" -DESP_INO="\"$HERE/../PSN-Presepe-WiFi/PSN-Presepe-WiFi.ino\"" -c "$E/esp_side.cpp" -o "$T/esp_side.o"
${CXX:-g++} "$T/mega_side.o" "$T/esp_side.o" -o "$T/e2e"
"$T/e2e"
# 5. syntax of the page scripts (simulator / editor and control panel), when Node.js is installed
if command -v node >/dev/null 2>&1; then
  R=$HERE/../..
  for f in "$R/simulation_web/simulator.template.html" "$R/firmware/web/pannello.html" "$R/demo/index.html" "$R/demo/config.html"; do
    awk '/<script>/{f=1;next} /<\/script>/{f=0} f' "$f" | sed 's/__INI__/""/' > "$T/page.js"
    node --check "$T/page.js" || { echo "JavaScript syntax error in $f"; exit 1; }
  done
  echo "page scripts: syntax OK"
fi
rm -rf "$T"
