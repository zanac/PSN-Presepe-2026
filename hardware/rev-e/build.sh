#!/bin/sh
# PSN-Presepe Rev E - PCB rebuild and verification (next revision, not ordered yet).
#
# Usage: hardware/rev-e/build.sh [OUT_DIR]        (default OUT_DIR = hardware/rev-e/out, git-ignored)
#
# Produces, under OUT_DIR:
#   work/          intermediate boards of the PCB pipeline
#   release/       raw KiCad exports, Gerbers, reports
#   package/       the same layout as this folder: kicad/ manufacturing/ previews/ reports/
# The firmware is built and tested by the build.sh at the repository root.
#
# Environment (all optional):
#   REROUTE=1                       run Freerouting again instead of replaying build/data/routing.ses
#     FREEROUTING_JAR=<path>        freerouting-2.2.4 (or newer) executable jar, needed with REROUTE=1
#     JAVA=<path>                   Java runtime for Freerouting (2.2.x needs Java 25)
#   KICAD_FOOTPRINT_DIR=<dir>       KiCad 7 footprint libraries (default /usr/share/kicad/footprints)
#   UPDATE_REPO=1                   copy package/ over kicad/ manufacturing/ previews/ reports/ of this folder
set -e
ROOT=$(cd "$(dirname "$0")" && pwd)          # hardware/rev-e
REPO=$(cd "$ROOT/../.." && pwd)
OUT=${1:-$ROOT/out}
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
S=$ROOT/build/scripts

{
  echo "== PCB Rev E: place, route, DRC"
  rm -rf "$OUT/work" "$OUT/release" "$OUT/package"
  if [ "$REROUTE" = 1 ]; then
    "$S/pipeline.sh" "$OUT/work" "${FREEROUTING_JAR:?REROUTE=1 needs FREEROUTING_JAR}" "${JAVA:-java}"
  else
    SES_IN="$ROOT/build/data/routing.ses" "$S/pipeline.sh" "$OUT/work"
  fi
  echo "== PCB: release exports and independent verification"
  "$S/make_release.sh" "$OUT/work/clean.kicad_pcb" "$OUT/release" "$REPO/firmware/PSN-Presepe/PSN-Presepe.ino"
  "$S/package.sh" "$OUT/release" "$OUT/package"
  if [ "$UPDATE_REPO" = 1 ]; then
    for d in kicad previews reports; do rm -rf "$ROOT/$d"; cp -r "$OUT/package/$d" "$ROOT/$d"; done
    mkdir -p "$ROOT/manufacturing"; find "$ROOT/manufacturing" -maxdepth 1 -type f -delete
    cp "$OUT/package/manufacturing/"* "$ROOT/manufacturing/"
    echo "hardware/rev-e folders updated from $OUT/package"
  fi
}
echo "== hardware/rev-e/build.sh finished: $OUT"
