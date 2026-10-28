#!/usr/bin/env python3
"""import_ses.py <board.kicad_pcb> <routed.ses> <out.kicad_pcb>

Specctra session importer (KiCad 7's ImportSpecctraSES only works inside the
GUI). Replaces all non-MAINS tracks/vias with the session routing; the locked
MAINS tracks are kept untouched and the session copy of them is verified.
"""
import re, sys
import pcbnew, sexpdata
from sexpdata import Symbol as S

brd, ses, out = sys.argv[1:4]
b = pcbnew.LoadBoard(brd)
d = sexpdata.loads(open(ses).read())

def find(node, key):
    for x in node:
        if isinstance(x, list) and x and x[0] == S(key): return x
def all_(node, key):
    return [x for x in node if isinstance(x, list) and x and x[0] == S(key)]

routes = find(d, "routes")
res = find(routes, "resolution")
unit, per = str(res[1]), res[2]
scale_nm = {"um": 1000, "mm": 1_000_000, "mil": 25400}[unit] / per   # session unit -> nm
def pt(x, y): return pcbnew.VECTOR2I(int(round(x * scale_nm)), int(round(-y * scale_nm)))
LAYER = {"F.Cu": pcbnew.F_Cu, "B.Cu": pcbnew.B_Cu}

# drop existing non-MAINS routing
for t in list(b.GetTracks()):
    if not t.IsLocked(): b.Remove(t)
mains = {(t.GetNetname(), t.GetLayer(), t.GetStart().x, t.GetStart().y, t.GetEnd().x, t.GetEnd().y)
         for t in b.GetTracks() if t.GetNetClassName() == "MAINS"}

n_trk = n_via = 0; ses_mains = set()
for net in all_(find(routes, "network_out"), "net"):
    name = str(net[1])
    ni = b.FindNet(name)
    if ni is None: raise SystemExit(f"unknown net {name}")
    is_mains = ni.GetNetClassName() == "MAINS"
    for w in all_(net, "wire"):
        path = find(w, "path")
        layer, width = str(path[1]), path[2]
        c = path[3:]
        pts = [pt(c[i], c[i + 1]) for i in range(0, len(c), 2)]
        for a, e in zip(pts, pts[1:]):
            if is_mains:
                ses_mains.add((name, LAYER[layer], a.x, a.y, e.x, e.y)); continue
            t = pcbnew.PCB_TRACK(b); t.SetStart(a); t.SetEnd(e)
            t.SetWidth(int(round(width * scale_nm))); t.SetLayer(LAYER[layer]); t.SetNet(ni)
            b.Add(t); n_trk += 1
    for v in all_(net, "via"):
        m = re.search(r"_(\d+):(\d+)_um", str(v[1]))
        dia, drill = int(m.group(1)) * 1000, int(m.group(2)) * 1000
        if is_mains: raise SystemExit(f"router added a via on MAINS net {name}")
        via = pcbnew.PCB_VIA(b); via.SetPosition(pt(v[2], v[3]))
        via.SetViaType(pcbnew.VIATYPE_THROUGH); via.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
        via.SetWidth(dia); via.SetDrill(drill); via.SetNet(ni)
        b.Add(via); n_via += 1

def norm(s):  # direction-independent, 1 um tolerance
    return {(n, l) + tuple(sorted([(round(x1, -3), round(y1, -3)), (round(x2, -3), round(y2, -3))]))
            for n, l, x1, y1, x2, y2 in s}
if ses_mains and not norm(ses_mains) <= norm(mains):
    raise SystemExit(f"router produced extra MAINS copper: {len(norm(ses_mains) ^ norm(mains))} segments")
pcbnew.SaveBoard(out, b)
print(f"imported {n_trk} tracks, {n_via} vias; MAINS tracks verified unchanged ({len(mains)})")
