#!/usr/bin/env python3
"""Independent check of the MANUFACTURING files (Gerber X2 + Excellon) - what the
fab will actually build. Uses gerbonara + shapely only (no KiCad).

Checks: outline size; copper islands (each island exactly one net -> no shorts;
each net one island across layers through plated holes -> no opens);
clearances between islands of different nets incl. the MAINS isolation targets;
every plated hole sits inside copper on both layers with annular ring;
NPTH holes clear of copper; solder-mask opening over every pad, vias tented.

Usage: verify_gerbers.py <gerber_dir> <basename>
"""
import math, re, sys, collections
from gerbonara import GerberFile, ExcellonFile
from gerbonara.graphic_primitives import Circle, Line, Rectangle, Arc
from shapely.geometry import Point, LineString, Polygon, box
from shapely.ops import unary_union
from shapely import affinity
from shapely.strtree import STRtree

gd, base = sys.argv[1:3]
FAIL, INFO = [], []
def check(c, m): (INFO if c else FAIL).append(("PASS " if c else "FAIL ") + m)

def shp(p):
    if isinstance(p, Circle): return Point(p.x, p.y).buffer(p.r, 24)
    if isinstance(p, Line): return LineString([(p.x1, p.y1), (p.x2, p.y2)]).buffer(p.width / 2, 16)
    if isinstance(p, Rectangle):
        g = box(p.x - p.w / 2, p.y - p.h / 2, p.x + p.w / 2, p.y + p.h / 2)
        return affinity.rotate(g, p.rotation, origin=(p.x, p.y), use_radians=True)
    if hasattr(p, "outline"):
        pts = [(x, y) for x, y in p.outline]
        return Polygon(pts).buffer(0) if len(pts) > 2 else None
    if isinstance(p, Arc):
        return Point(p.x1, p.y1).buffer(p.width / 2)
    raise TypeError(type(p))

def layer(fn):
    g = GerberFile.open(f"{gd}/{base}-{fn}")
    out = []
    for o in g.objects:
        geo = unary_union([s for s in (shp(pr) for pr in o.to_primitives()) if s is not None])
        nn = (getattr(o, "attrs", None) or {}).get(".N") or ("",)
        net = nn[0]
        out.append(dict(geom=geo, net=net, kind=type(o).__name__, attrs=o.attrs or {},
                        func=getattr(getattr(o, "aperture", None), "attrs", ())))
    return out

def cls(n):
    if re.fullmatch(r"R\d+_(COM|NO|NC)", n): return "MAINS"
    if n in ("+12V", "GND"): return "POWER"
    if n == "+5V_MEGA": return "P5V"
    if n.endswith("_NEG"): return "RGB"
    return "OTHER"
def req(a, b):
    ca, cb = cls(a), cls(b)
    if (ca == "MAINS") != (cb == "MAINS"): return 6.0
    if ca == cb == "MAINS":
        return 2.0 if a.split("_")[0] == b.split("_")[0] else 5.0
    return 0.3 if "POWER" in (ca, cb) or "RGB" in (ca, cb) else 0.2

# ---------------------------------------------------------------- outline
edge = GerberFile.open(f"{gd}/{base}-Edge_Cuts.gm1")
(x0, y0), (x1, y1) = edge.bounding_box()
INFO.append(f"INFO outline bounding box {x1 - x0:.2f} x {y1 - y0:.2f} mm (incl. 0.1 mm line)")
check(abs((x1 - x0) - 300.1) < 0.2 and abs((y1 - y0) - 180.1) < 0.2, "board outline 300 x 180 mm")

# ---------------------------------------------------------------- drills
pth = ExcellonFile.open(f"{gd}/{base}-PTH.drl"); npth = ExcellonFile.open(f"{gd}/{base}-NPTH.drl")
def hole(o):   # drill hit or routed slot -> (centre point, diameter, outline)
    if hasattr(o, "x1"):
        return (LineString([(o.x1, o.y1), (o.x2, o.y2)]).centroid, o.tool.diameter,
                LineString([(o.x1, o.y1), (o.x2, o.y2)]).buffer(o.tool.diameter / 2))
    return Point(o.x, o.y), o.tool.diameter, Point(o.x, o.y).buffer(o.tool.diameter / 2)
H = [hole(o) for o in pth.objects]; NH = [hole(o) for o in npth.objects]
holes = [(p, d) for p, d, _ in H]; nholes = [(p, d) for p, d, _ in NH]
INFO.append(f"INFO slots: PTH {sum(1 for o in pth.objects if hasattr(o, 'x1'))}, NPTH {sum(1 for o in npth.objects if hasattr(o, 'x1'))}")
INFO.append(f"INFO PTH holes {len(holes)} {dict(collections.Counter(round(d, 3) for _, d in holes))}")
INFO.append(f"INFO NPTH holes {len(nholes)} {dict(collections.Counter(round(d, 3) for _, d in nholes))}")

# ---------------------------------------------------------------- copper
cu = {"F": layer("F_Cu.gtl"), "B": layer("B_Cu.gbl")}
islands = {}
for L, objs in cu.items():
    u = unary_union([o["geom"] for o in objs])
    isl = list(u.geoms) if hasattr(u, "geoms") else [u]
    tree = STRtree(isl)
    nets = [set() for _ in isl]
    for o in objs:
        if not o["net"]: continue
        c = o["geom"].representative_point()
        for k in tree.query(c):
            if isl[k].contains(c) or isl[k].distance(c) < 1e-6: nets[k].add(o["net"])
    for k, g in enumerate(isl):   # no-net copper (mechanical pads): give it a unique pseudo-net
        if not nets[k]:
            c = g.centroid; nets[k].add(f"<no-net@{c.x:.1f},{-c.y:.1f}>")
    islands[L] = list(zip(isl, nets))
    multi = [sorted(n) for _, n in islands[L] if len(n) > 1]
    check(not multi, f"{L}.Cu: {len(isl)} copper islands, none contains two nets (shorts: {multi[:3]})")
    INFO.append(f"INFO {L}.Cu no-net copper islands: {[next(iter(n)) for _, n in islands[L] if next(iter(n)).startswith('<')]}")

# connectivity across layers through plated holes
nodes = [(L, i) for L in islands for i in range(len(islands[L]))]
par = {n: n for n in nodes}
def f(a):
    while par[a] != a: par[a] = par[par[a]]; a = par[a]
    return a
trees = {L: STRtree([g for g, _ in islands[L]]) for L in islands}
ring_bad = []
for hp, dia in holes:
    hit = {}
    for L in islands:
        for k in trees[L].query(hp):
            if islands[L][k][0].contains(hp): hit[L] = k
    if len(hit) == 2: par[f(("F", hit["F"]))] = f(("B", hit["B"]))
    else: ring_bad.append((round(hp.x, 2), round(hp.y, 2), dia))
    for L, k in hit.items():
        ring = islands[L][k][0].exterior.distance(hp) - dia / 2
        if ring < 0.12: ring_bad.append((round(hp.x, 2), round(hp.y, 2), dia, L, round(ring, 3)))
check(not ring_bad, f"every plated hole lands in copper on both layers with >= 0.12 mm ring ({ring_bad[:5]})")
comp = collections.defaultdict(set)
for L in islands:
    for i, (g, n) in enumerate(islands[L]):
        for nn in n: comp[nn].add(f((L, i)))
opens = {n: len(c) for n, c in comp.items() if len(c) > 1 and not n.startswith("<") and n != "N/C"}
check(not opens, f"each of {len(comp)} nets is a single connected copper body (opens: {opens})")

# clearances between islands of different nets (same layer)
worst = {}; bad = []
for L in islands:
    isl = islands[L]; tree = trees[L]
    for i, (g, n) in enumerate(isl):
        na = next(iter(n))
        for j in tree.query(g.buffer(6.5)):
            if j <= i: continue
            nb = next(iter(isl[j][1]))
            dd = g.distance(isl[j][0]); r = req(na, nb)
            key = (cls(na), cls(nb), r)
            if dd < worst.get(key, (99,))[0]: worst[key] = (dd, na, nb)
            if dd < r - 0.005: bad.append((L, na, nb, round(dd, 3), r))
check(not bad, f"Gerber copper clearances meet rules incl. MAINS 6/5/2 mm ({bad[:5]})")
for (a, b, r), (dd, na, nb) in sorted(worst.items(), key=lambda kv: kv[0][2]):
    if "MAINS" in (a, b): INFO.append(f"INFO gerber min {a}-{b} (req {r}): {dd:.3f} mm  [{na} / {nb}]")
# NPTH clear of copper
nb_ = [(round(p.x, 1), round(p.y, 1)) for p, dd in nholes for L in islands
       for k in trees[L].query(p.buffer(dd / 2 + 0.25)) if islands[L][k][0].distance(p) < dd / 2 + 0.25]
check(not nb_, f"NPTH holes >= 0.25 mm from copper ({nb_[:5]})")
mains_isl = [g for L in islands for g, n in islands[L] if cls(next(iter(n))) == "MAINS"]
nm = min(g.distance(p) - dd / 2 for g in mains_isl for p, dd in nholes)
check(nm >= 6.0, f"MAINS copper >= 6 mm from mounting/NPTH holes (min {nm:.2f} mm)")

# ---------------------------------------------------------------- solder mask
for L, fn in (("F", "F_Mask.gts"), ("B", "B_Mask.gbs")):
    m = GerberFile.open(f"{gd}/{base}-{fn}")
    mu = unary_union([shp(p) for o in m.objects for p in o.to_primitives()])
    pads = [o for o in cu[L] if o["kind"] == "Flash" and "ViaPad" not in str(o["func"])]
    vias = [o for o in cu[L] if o["kind"] == "Flash" and "ViaPad" in str(o["func"])]
    no_open = sum(1 for o in pads if not mu.contains(o["geom"].representative_point()))
    via_open = sum(1 for o in vias if mu.contains(o["geom"].representative_point()))
    check(no_open == 0, f"{L}.Mask opening over all {len(pads)} pads ({no_open} missing)")
    check(via_open == 0, f"{L}.Mask: all {len(vias)} vias tented ({via_open} exposed)")
print("\n".join(INFO)); print("\n".join(FAIL))
print(f"\nRESULT: {'ALL GERBER CHECKS PASSED' if not FAIL else str(sum(1 for x in FAIL if x.startswith('FAIL'))) + ' CHECK(S) FAILED'}")
sys.exit(1 if FAIL else 0)
