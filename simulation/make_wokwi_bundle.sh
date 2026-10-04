#!/bin/sh
# make_wokwi_bundle.sh [out_dir] [--no-display]
# Copies, into one flat folder, the files to paste into the Wokwi project:
#   sketch.ino    = firmware/PSN-Presepe/PSN-Presepe.ino (the whole firmware, unchanged)
#   PRESEPE.TXT   = firmware/PSN-Presepe/PRESEPE.INI (Wokwi does not accept .ini files; the
#                   firmware falls back to PRESEPE.TXT, and Wokwi copies every project file
#                   onto the simulated microSD at start-up)
#   diagram.json, libraries.txt, rgb-strip.chip.json, rgb-strip.chip.c
set -e
HERE=$(cd "$(dirname "$0")" && pwd); FW=$HERE/../firmware/PSN-Presepe
OUT=${1:-$HERE/wokwi-bundle}
mkdir -p "$OUT"
cp "$FW/PSN-Presepe.ino" "$OUT/sketch.ino"
cp "$FW/PRESEPE.INI" "$OUT/PRESEPE.TXT"
if [ "$2" = "--no-display" ]; then cp "$HERE/diagram-no-display.json" "$OUT/diagram.json"
else cp "$HERE/diagram.json" "$OUT/diagram.json"; fi
cp "$HERE/libraries.txt" "$HERE/rgb-strip.chip.json" "$HERE/rgb-strip.chip.c" "$OUT/"
echo "Wokwi files in $OUT:"; ls "$OUT"
