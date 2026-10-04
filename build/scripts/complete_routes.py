#!/usr/bin/env python3
"""complete_routes.py <in.kicad_pcb> <drc_report.txt> <out.kicad_pcb>

Finishes the few connections the autorouter may leave open. For every
'unconnected_items' pair in the KiCad DRC report it runs a two-layer grid
Dijkstra (0.2 mm) on a clearance-inflated obstacle map that applies the same
rules as the board (net-class clearance, 6 mm SELV<->MAINS keep-out, edge and
hole clearance). The result is re-checked by KiCad DRC afterwards.
"""
import heapq, math, re, sys
import numpy as np
import pcbnew
from PIL import Image, ImageDraw
from shapely.geometry import LineString, Point, box
from shapely.ops import unary_union

src, rep, dst = sys.argv[1:4]
b = pcbnew.LoadBoard(src)
RES = 0.2
X0, Y0, X1, Y1 = 20.0, 20.0, 320.0, 200.0
W = {"Default": 0.25, "POWER": 1.0, "P5V": 0.8, "P3V3": 0.4, "RGB": 1.5, "COIL": 0.4}
CLR = {"Default": 0.2, "POWER": 0.3, "P5V": 0.25, "P3V3": 0.2, "RGB": 0.3, "COIL": 0.2, "MAINS": 2.0}
VIA_D, VIA_H = 0.6, 0.3
MARGIN = 0.15   # > half grid diagonal: covers rasterisation of diagonal moves
LAYERS = (pcbnew.F_Cu, pcbnew.B_Cu)

def nm(v): return v / 1e6

# --------------------------------------------------------------- copper items
items = []   # (net, netclass, set(layers), shapely geom)
holes = []   # (geom of drilled hole, net)
for t in b.GetTracks():
    if t.GetClass() == "PCB_VIA":
        p = t.GetPosition(); g = Point(nm(p.x), nm(p.y)).buffer(nm(t.GetWidth()) / 2, 16)
        items.append((t.GetNetname(), t.GetNetClassName(), set(LAYERS), g))
        holes.append((Point(nm(p.x), nm(p.y)).buffer(nm(t.GetDrillValue()) / 2, 16), t.GetNetname()))
    else:
        g = LineString([(nm(t.GetStart().x), nm(t.GetStart().y)), (nm(t.GetEnd().x), nm(t.GetEnd().y))]).buffer(nm(t.GetWidth()) / 2, 8)
        items.append((t.GetNetname(), t.GetNetClassName(), {t.GetLayer()}, g))
for fp in b.GetFootprints():
    for p in fp.Pads():
        poly = pcbnew.SHAPE_POLY_SET()
        p.TransformShapeToPolygon(poly, pcbnew.F_Cu, 0, pcbnew.FromMM(0.005))
        pts = [(nm(poly.COutline(0).CPoint(i).x), nm(poly.COutline(0).CPoint(i).y)) for i in range(poly.COutline(0).PointCount())]
        from shapely.geometry import Polygon
        g = Polygon(pts)
        lays = {l for l in LAYERS if p.IsOnLayer(l)}
        if p.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH: lays = set()
        if lays: items.append((p.GetNetname(), p.GetNetClassName(), lays, g))
        if p.GetDrillSize().x > 0:
            pp = p.GetPosition()
            holes.append((Point(nm(pp.x), nm(pp.y)).buffer(nm(p.GetDrillSize().x) / 2, 16), p.GetNetname()))

# keep-out rule areas (board level and inside footprints, e.g. under the microSD socket)
keepouts = []   # (set(layers), geom, no_tracks, no_vias)
from shapely.geometry import Polygon as _Poly
zl = list(b.Zones())
for fp in b.GetFootprints(): zl += list(fp.Zones())
for z in zl:
    if not z.GetIsRuleArea(): continue
    ol = z.Outline()
    for k in range(ol.OutlineCount()):
        c = ol.COutline(k)
        g = _Poly([(nm(c.CPoint(i).x), nm(c.CPoint(i).y)) for i in range(c.PointCount())])
        lays = {l for l in LAYERS if z.IsOnLayer(l)}
        keepouts.append((lays, g, z.GetDoNotAllowTracks(), z.GetDoNotAllowVias()))

# --------------------------------------------------------------- DRC pairs
txt = open(rep).read()
pairs = []
for blk in txt.split("[unconnected_items]")[1:]:
    locs = re.findall(r"@\(([-\d.]+) mm, ([-\d.]+) mm\): (.*)", blk)[:2]
    pairs.append([(float(x), float(y), d) for x, y, d in locs])
if not pairs:
    pcbnew.SaveBoard(dst, b); print("nothing to complete"); sys.exit(0)

def endpoint_layers(desc):
    if "PTH pad" in desc or "Via" in desc: return set(LAYERS)
    return {pcbnew.F_Cu} if "F.Cu" in desc else {pcbnew.B_Cu}

def raster(geoms, x0, y0, w, h):
    im = Image.new("1", (w, h), 0); dr = ImageDraw.Draw(im)
    def tp(c): return [((x - x0) / RES, (y - y0) / RES) for x, y in c]
    for g in geoms:
        for pg in (g.geoms if hasattr(g, "geoms") else [g]):
            if pg.is_empty: continue
            dr.polygon(tp(pg.exterior.coords), fill=1)
            for hole in pg.interiors: dr.polygon(tp(hole.coords), fill=0)
    return np.array(im, dtype=bool)

added = 0
for (ax, ay, ad), (bx, by, bd) in pairs:
    net = re.search(r"\[([^\]]+)\]", ad).group(1)
    ncls = b.FindNet(net).GetNetClassName()
    w = W.get(ncls, 0.25); hw = w / 2
    # crop window
    pad = 40.0
    x0 = max(X0, min(ax, bx) - pad); x1 = min(X1, max(ax, bx) + pad)
    y0 = max(Y0, min(ay, by) - pad); y1 = min(Y1, max(ay, by) + pad)
    gw, gh = int((x1 - x0) / RES) + 1, int((y1 - y0) / RES) + 1
    win = box(x0, y0, x1, y1)
    blocked = {}; vblock = None
    for li, L in enumerate(LAYERS):
        obs = []
        for n, c, lays, g in items:
            if n == net or L not in lays or not g.intersects(win.buffer(8)): continue
            clr = max(CLR.get(ncls, 0.2), CLR.get(c, 0.2))
            if c == "MAINS": clr = 6.0
            obs.append(g.buffer(clr + hw + MARGIN, 8))
        for lays, g, nt, nv in keepouts:
            if L in lays and nt and g.intersects(win): obs.append(g.buffer(hw + MARGIN, 8))
        edge = box(X0, Y0, X1, Y1).buffer(-(0.5 + hw + MARGIN))
        outside = win.difference(edge)
        obs.append(outside)
        for hg, hn in holes:
            if hn != net and hg.intersects(win.buffer(8)): obs.append(hg.buffer(0.25 + hw + MARGIN, 8))
        blocked[L] = raster(obs, x0, y0, gw, gh)
    # via feasibility: via copper vs every other-net copper on both layers + hole spacing
    vobs = []
    for n, c, lays, g in items:
        if n == net or not g.intersects(win.buffer(8)): continue
        clr = 6.0 if c == "MAINS" else max(CLR.get(ncls, 0.2), CLR.get(c, 0.2))
        vobs.append(g.buffer(clr + VIA_D / 2 + MARGIN, 8))
    for hg, hn in holes:
        if hg.intersects(win.buffer(8)): vobs.append(hg.buffer(0.25 + VIA_H / 2 + 0.1, 8))
    vobs.append(win.difference(box(X0, Y0, X1, Y1).buffer(-(0.5 + VIA_D / 2 + MARGIN))))
    for lays, g, nt, nv in keepouts:
        if nv and g.intersects(win): vobs.append(g.buffer(VIA_D / 2 + MARGIN, 8))
    vblock = raster(vobs, x0, y0, gw, gh)

    def cell(x, y): return int(round((y - y0) / RES)), int(round((x - x0) / RES))
    sa, sb = cell(ax, ay), cell(bx, by)
    la, lb = endpoint_layers(ad), endpoint_layers(bd)
    # start/goal = grid cells inside the endpoint's own copper (pad / track), never
    # inside keep-outs; obstacles are not cleared, so no rule can be bypassed.
    netitems = [(ls, g) for n, c, ls, g in items if n == net]
    def island(px, py):
        """all same-net copper galvanically connected to the item at (px, py), per layer"""
        seed = [k for k, (ls, g) in enumerate(netitems) if g.distance(Point(px, py)) < 0.05]
        got = set(seed); todo = list(seed)
        while todo:
            k = todo.pop(); lk, gk = netitems[k]
            for j, (lj, gj) in enumerate(netitems):
                if j not in got and (lk & lj) and gk.intersects(gj):
                    got.add(j); todo.append(j)
        return [netitems[k] for k in got]
    def endpoint_cells(px, py, lays):
        cells = set()
        isl = island(px, py)
        for L in LAYERS:
            # shrink so every chosen cell centre lies strictly inside the copper
            own = [g.buffer(-0.16) for ls, g in isl if L in ls]
            own = [g for g in own if not g.is_empty]
            if not own: continue
            free = raster(own, x0, y0, gw, gh) & ~blocked[L]
            rr, cc = np.nonzero(free)
            cells |= {(LAYERS.index(L), int(r), int(c)) for r, c in zip(rr, cc)}
        return cells
    src_cells = endpoint_cells(ax, ay, la); goal = endpoint_cells(bx, by, lb)
    if not src_cells or not goal:
        print(f"FAILED {net}: endpoint copper fully blocked ({ad} -> {bd})"); continue
    dist = {}; prev = {}; hq = []
    for s in src_cells: dist[s] = 0; heapq.heappush(hq, (0, s))
    moves = [(-1, 0, 1), (1, 0, 1), (0, -1, 1), (0, 1, 1), (-1, -1, 1.4142), (-1, 1, 1.4142), (1, -1, 1.4142), (1, 1, 1.4142)]
    VIA_COST = 25.0
    found = None
    while hq:
        dcur, s = heapq.heappop(hq)
        if dcur > dist.get(s, 1e18): continue
        if s in goal: found = s; break
        l, r, c = s
        for dr_, dc_, cst in moves:
            nr, nc = r + dr_, c + dc_
            if 0 <= nr < gh and 0 <= nc < gw and not blocked[LAYERS[l]][nr, nc]:
                ns = (l, nr, nc); nd = dcur + cst
                if nd < dist.get(ns, 1e18): dist[ns] = nd; prev[ns] = s; heapq.heappush(hq, (nd, ns))
        if not vblock[r, c]:
            ns = (1 - l, r, c)
            if not blocked[LAYERS[1 - l]][r, c]:
                nd = dcur + VIA_COST
                if nd < dist.get(ns, 1e18): dist[ns] = nd; prev[ns] = s; heapq.heappush(hq, (nd, ns))
    if not found:
        print(f"FAILED {net}: {ad} -> {bd}"); continue
    path = [found]
    while path[-1] in prev: path.append(prev[path[-1]])
    path.reverse()
    clean = []
    for st in path:
        if len(clean) >= 2 and clean[-2] == st: clean.pop(); continue   # A -> via -> A
        clean.append(st)
    path = clean
    # emit: collapse collinear runs per layer, vias on layer changes
    def xy(s): return (x0 + s[2] * RES, y0 + s[1] * RES)
    ni = b.FindNet(net); segs = 0; vias = 0
    run = [path[0]]
    def flush(run):
        global segs
        pts = [xy(run[0])]
        for k in range(1, len(run) - 1):
            d1 = (run[k][1] - run[k - 1][1], run[k][2] - run[k - 1][2])
            d2 = (run[k + 1][1] - run[k][1], run[k + 1][2] - run[k][2])
            if d1 != d2: pts.append(xy(run[k]))
        pts.append(xy(run[-1]))
        L = LAYERS[run[0][0]]
        for a, c in zip(pts, pts[1:]):
            if a == c: continue
            t = pcbnew.PCB_TRACK(b); t.SetStart(pcbnew.VECTOR2I(pcbnew.FromMM(a[0]), pcbnew.FromMM(a[1])))
            t.SetEnd(pcbnew.VECTOR2I(pcbnew.FromMM(c[0]), pcbnew.FromMM(c[1])))
            t.SetWidth(pcbnew.FromMM(w)); t.SetLayer(L); t.SetNet(ni); b.Add(t); segs += 1
    for s in path[1:]:
        if s[0] != run[-1][0]:
            flush(run)
            v = pcbnew.PCB_VIA(b); x, y = xy(s); v.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y)))
            v.SetViaType(pcbnew.VIATYPE_THROUGH); v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
            v.SetWidth(pcbnew.FromMM(VIA_D)); v.SetDrill(pcbnew.FromMM(VIA_H)); v.SetNet(ni); b.Add(v); vias += 1
            run = [s]
        else:
            run.append(s)
    flush(run)
    added += 1
    print(f"completed {net}: {segs} segments, {vias} vias")
pcbnew.SaveBoard(dst, b)
print("connections completed:", added, "of", len(pairs))
