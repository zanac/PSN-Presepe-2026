#!/bin/sh
# package.sh <release_dir> <dest_dir>
# Copies a make_release.sh output into the repository layout:
#   kicad/  manufacturing/  previews/  reports/
set -e
B=$(cd "$(dirname "$0")" && pwd); REL=$1; DST=$2; N=PSN-Presepe-Mega-RevD
mkdir -p "$DST/kicad/lib" "$DST/manufacturing" "$DST/previews" "$DST/reports"
cp "$REL/$N.kicad_pcb" "$REL/$N.kicad_pro" "$REL/$N.kicad_dru" "$DST/kicad/"
cp "$REL/schematic/$N.kicad_sch" "$REL/schematic/$N-schematic.pdf" "$DST/kicad/"
cp -r "$B/../lib/PSN_RevD.pretty" "$DST/kicad/lib/"
cat > "$DST/kicad/fp-lib-table" <<'T'
(fp_lib_table
  (version 7)
  (lib (name "PSN_RevD")(type "KiCad")(uri "${KIPRJMOD}/lib/PSN_RevD.pretty")(options "")(descr "PSN-Presepe Rev D local footprints"))
)
T
rm -f "$DST/manufacturing/$N-gerbers.zip"
( cd "$REL/gerbers" && zip -q -X "$DST/manufacturing/$N-gerbers.zip" *.gtl *.gbl *.gts *.gbs *.gto *.gbo *.gm1 *.drl *.gbrjob )
cp "$REL/gerbers/"*-drl_map.pdf "$DST/manufacturing/"
cp "$REL/$N-BOM.csv" "$REL/$N-BOM-PCBWay.csv" "$REL/$N-BOM-JLCPCB.csv" "$REL/$N-CPL-JLCPCB.csv" \
   "$REL/$N-1to1-A3-top.pdf" "$REL/assembly/"* "$DST/manufacturing/"
cp "$REL/previews/"*.png "$DST/previews/"
python3 "$B/render_gerbers.py" "$REL/gerbers" "$DST/previews/$N"
cp "$REL/reports/"* "$DST/reports/"
echo "packaged into $DST"
