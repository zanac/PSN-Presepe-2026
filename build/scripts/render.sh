#!/bin/sh
# render.sh <board.kicad_pcb> <out.png> [layers] [width_px]
set -e
L=${3:-F.Cu,B.Cu,F.SilkS,Edge.Cuts,F.CrtYd}
T=$(mktemp -d)
kicad-cli pcb export svg --exclude-drawing-sheet --page-size-mode 2 -l "$L" -o "$T/r.svg" "$1" >/dev/null
rsvg-convert -b white -w "${4:-2400}" "$T/r.svg" -o "$2"
rm -rf "$T"
