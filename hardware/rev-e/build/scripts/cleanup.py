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
# thin same-net track laid along the seam: it stays inside the copper of the two tracks (plus
# the seam and the notch at their ends), so clearances to other nets do not change.
import math
def seg(t):
    return (t.GetStart().x / 1e6, t.GetStart().y / 1e6, t.GetEnd().x / 1e6, t.GetEnd().y / 1e6, t.GetWidth() / 1e6)
filled = 0
# only when KiCad actually reports a sliver: otherwise the routed copper is left untouched
sliver = "copper_sliver" in open(rep).read()
tracks = [t for t in b.GetTracks() if t.GetClass() == "PCB_TRACK"] if sliver else []
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
        # filler (as wide as the thinner track) centred on the seam (the touching edges), over the whole common
        # length: it lies inside both tracks except right at their ends, where it closes the
        # notch between the round end of one track and the side of the other
        w = min(w1, w2); lo += w / 2; hi -= w / 2              # >= the net-class width
        if hi - lo < 0.05: continue
        off = (w1 / 2 + gap / 2) * (1 if d > 0 else -1)
        cx, cy = -uy * off, ux * off
        e1 = (round(x1 + ux * lo + cx, 4), round(y1 + uy * lo + cy, 4))
        e2 = (round(x1 + ux * hi + cx, 4), round(y1 + uy * hi + cy, 4))
        e1, e2 = sorted((e1, e2))                   # same output whatever the track order
        f = pcbnew.PCB_TRACK(b)
        f.SetStart(pcbnew.VECTOR2I(pcbnew.FromMM(e1[0]), pcbnew.FromMM(e1[1])))
        f.SetEnd(pcbnew.VECTOR2I(pcbnew.FromMM(e2[0]), pcbnew.FromMM(e2[1])))
        f.SetWidth(pcbnew.FromMM(w)); f.SetLayer(t1.GetLayer()); f.SetNet(t1.GetNet()); b.Add(f)
        filled += 1
# Same for a track END whose round cap just touches the side of another same-net track (a
# T that misses by nothing): bridge the end to the nearest point of the other track's centre
# line with a track as wide as the thinner of the two.
def closest(px, py, ax, ay, bx, by):
    vx, vy = bx - ax, by - ay; ll = vx * vx + vy * vy
    k = 0.0 if ll == 0 else max(0.0, min(1.0, ((px - ax) * vx + (py - ay) * vy) / ll))
    return ax + k * vx, ay + k * vy
for t1 in tracks:
    x1, y1, x2, y2, w1 = seg(t1)
    for ex, ey in ((x1, y1), (x2, y2)):
        for t2 in tracks:
            if t2 is t1 or t2.GetNetCode() != t1.GetNetCode() or t2.GetLayer() != t1.GetLayer(): continue
            a1, b1, a2, b2, w2 = seg(t2)
            if min(math.hypot(ex - a1, ey - b1), math.hypot(ex - a2, ey - b2)) < 1e-3: continue   # joined
            qx, qy = closest(ex, ey, a1, b1, a2, b2)
            gap = math.hypot(ex - qx, ey - qy) - (w1 + w2) / 2
            if not (-0.02 <= gap <= 0.06): continue
            f = pcbnew.PCB_TRACK(b)
            f.SetStart(pcbnew.VECTOR2I(pcbnew.FromMM(ex), pcbnew.FromMM(ey)))
            f.SetEnd(pcbnew.VECTOR2I(pcbnew.FromMM(qx), pcbnew.FromMM(qy)))
            f.SetWidth(pcbnew.FromMM(min(w1, w2))); f.SetLayer(t1.GetLayer()); f.SetNet(t1.GetNet()); b.Add(f)
            filled += 1
pcbnew.SaveBoard(dst, b)
print("dangling fan-outs removed:", len(pts), "items:", removed, "| seams filled:", filled)
