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

# Rev E: same-net parallel tracks whose edges just touch (e.g. a 1 mm route hugging a 3 mm
# trunk) leave a zero-width seam that KiCad reports as a copper sliver. Fill the seam with a
# short same-net track laid between the two centre lines: it stays inside the copper the two
# tracks already cover (plus the seam), so clearances to other nets do not change.
import math
def seg(t):
    return (t.GetStart().x / 1e6, t.GetStart().y / 1e6, t.GetEnd().x / 1e6, t.GetEnd().y / 1e6, t.GetWidth() / 1e6)
filled = 0
tracks = [t for t in b.GetTracks() if t.GetClass() == "PCB_TRACK"]
for i, t1 in enumerate(tracks):
    x1, y1, x2, y2, w1 = seg(t1)
    l1 = math.hypot(x2 - x1, y2 - y1)
    if l1 < 0.3: continue
    ux, uy = (x2 - x1) / l1, (y2 - y1) / l1
    for t2 in tracks[i + 1:]:
        if t2.GetNetCode() != t1.GetNetCode() or t2.GetLayer() != t1.GetLayer(): continue
        a1, b1, a2, b2, w2 = seg(t2)
        l2 = math.hypot(a2 - a1, b2 - b1)
        if l2 < 0.3 or abs(ux * (b2 - b1) / l2 - uy * (a2 - a1) / l2) > 0.02: continue      # not parallel
        d = (a1 - x1) * -uy + (b1 - y1) * ux                                                  # signed offset
        gap = abs(d) - (w1 + w2) / 2
        if not (-0.02 <= gap <= 0.06): continue
        s = sorted([(a1 - x1) * ux + (b1 - y1) * uy, (a2 - x1) * ux + (b2 - y1) * uy])
        lo, hi = max(0.0, s[0]), min(l1, s[1])
        w = abs(d); lo += w / 2; hi -= w / 2
        if hi - lo < 0.05: continue
        cx, cy = -uy * d / 2, ux * d / 2
        f = pcbnew.PCB_TRACK(b)
        f.SetStart(pcbnew.VECTOR2I(pcbnew.FromMM(x1 + ux * lo + cx), pcbnew.FromMM(y1 + uy * lo + cy)))
        f.SetEnd(pcbnew.VECTOR2I(pcbnew.FromMM(x1 + ux * hi + cx), pcbnew.FromMM(y1 + uy * hi + cy)))
        f.SetWidth(pcbnew.FromMM(w)); f.SetLayer(t1.GetLayer()); f.SetNet(t1.GetNet()); b.Add(f)
        filled += 1
pcbnew.SaveBoard(dst, b)
print("dangling fan-outs removed:", len(pts), "items:", removed, "| seams filled:", filled)
