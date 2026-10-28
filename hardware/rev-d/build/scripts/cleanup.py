#!/usr/bin/env python3
"""cleanup.py <board.kicad_pcb> <drc_report.txt> <out.kicad_pcb>
Removes pre-routed fan-out vias that ended up connected on one layer only
(the autorouter joined the stub on the same layer). The tracks meeting at the
via keep a common end point, so connectivity is unchanged.
The caller re-runs DRC; connectivity must stay complete."""
import re, sys
import pcbnew
src, rep, dst = sys.argv[1:4]
b = pcbnew.LoadBoard(src)
pts = [(float(x), float(y)) for blk in open(rep).read().split("[via_dangling]")[1:]
       for x, y in re.findall(r"@\(([-\d.]+) mm, ([-\d.]+) mm\): Via", blk)[:1]]
removed = 0
for x, y in pts:
    at = pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y))
    for t in list(b.GetTracks()):
        if t.GetClass() == "PCB_VIA" and (t.GetPosition() - at).EuclideanNorm() < 2000:
            b.Remove(t); removed += 1
pcbnew.SaveBoard(dst, b)
print("dangling fan-outs removed:", len(pts), "items:", removed)
