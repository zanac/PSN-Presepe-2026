#!/bin/sh
# make_release.sh <final.kicad_pcb> <release_dir> <firmware.ino>
# Exports the manufacturing package and runs every verification; reports -> <release_dir>/reports
set -e
B=$(dirname "$0"); SRC=$1; REL=$2; INO=$3; N=PSN-Presepe-Mega-RevD
FPLIB=${KICAD_FOOTPRINT_DIR:-/usr/share/kicad/footprints}
python3 "$B/release.py" "$SRC" "$REL"
mkdir -p "$REL/reports" "$REL/schematic"
python3 "$B/gen_schematic.py" "$REL/$N.kicad_pcb" "$REL/schematic/$N.kicad_sch"
kicad-cli sch export netlist --format kicadsexpr -o "$REL/reports/schematic-netlist.net" "$REL/schematic/$N.kicad_sch" >/dev/null 2>&1
kicad-cli sch export pdf -o "$REL/schematic/$N-schematic.pdf" "$REL/schematic/$N.kicad_sch" >/dev/null 2>&1 || true
python3 "$B/check_sch_parity.py" "$REL/reports/schematic-netlist.net" "$REL/$N.kicad_pcb" | tee "$REL/reports/schematic-pcb-parity.txt"
python3 "$B/drc.py" "$REL/$N.kicad_pcb" "$REL/reports/kicad-drc.txt" >/dev/null
grep Found "$REL/reports/kicad-drc.txt"
# positive controls: one rule at a time forced to an impossible value -> DRC must flag that rule
echo "Positive controls - each run forces ONE isolation rule to an impossible value:" > "$REL/reports/positive-control-summary.txt"
for m in selv inter intra edge; do
  T=$(mktemp -d); cp "$REL/$N.kicad_pcb" "$T/c.kicad_pcb"; cp "$REL/$N.kicad_pro" "$T/c.kicad_pro"
  python3 "$B/make_rules.py" "$T" c --control=$m >/dev/null
  python3 "$B/drc.py" "$T/c.kicad_pcb" "$T/drc.txt" >/dev/null
  echo "--control=$m:" >> "$REL/reports/positive-control-summary.txt"
  grep "Rule:" "$T/drc.txt" | sed 's/;.*//' | sort | uniq -c >> "$REL/reports/positive-control-summary.txt" || echo "   NO VIOLATIONS (rule inactive!)" >> "$REL/reports/positive-control-summary.txt"
  rm -rf "$T"
done
cat "$REL/reports/positive-control-summary.txt"
python3 "$B/verify.py" "$REL/$N.kicad_pcb" "$B/../data/revb_netlist.json" "$INO" "$FPLIB" > "$REL/reports/independent-board-check.txt" || { cat "$REL/reports/independent-board-check.txt"; exit 1; }
tail -1 "$REL/reports/independent-board-check.txt"
python3 "$B/verify_gerbers.py" "$REL/gerbers" "$N" 2>/dev/null > "$REL/reports/independent-gerber-check.txt" || { cat "$REL/reports/independent-gerber-check.txt"; exit 1; }
tail -1 "$REL/reports/independent-gerber-check.txt"
python3 "$B/make_jlc.py" "$REL/$N.kicad_pcb" "$REL"
python3 "$B/make_assembly.py" "$REL"
python3 "$B/check_mega_footprint.py" "$REL/$N.kicad_pcb" "$FPLIB" > "$REL/reports/mega-footprint-check.txt" || { cat "$REL/reports/mega-footprint-check.txt"; exit 1; }
tail -1 "$REL/reports/mega-footprint-check.txt"
