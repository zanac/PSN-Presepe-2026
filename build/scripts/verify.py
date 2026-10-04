#!/usr/bin/env python3
"""Independent verification of the Rev D board - does NOT use KiCad.

Parses the .kicad_pcb with sexpdata and rebuilds the copper with shapely, then
checks: shorts, clearances (incl. the MAINS isolation targets), connectivity,
edge/hole clearance, track widths, relay/terminal pinout, footprint geometry,
netlist parity with Rev B (+ documented Rev D changes), firmware pin map and
Mega stacking keep-out.

Usage: verify.py <board.kicad_pcb> <revb_netlist.json> <firmware.ino> <kicad_footprint_dir>
Exit code 0 = all checks passed.
"""
import json, math, re, sys, collections
import sexpdata
from sexpdata import Symbol as S
from shapely.geometry import LineString, Point, Polygon, box
from shapely import affinity
from shapely.strtree import STRtree

pcb, netlist_json, ino, fplib = sys.argv[1:5]
d = sexpdata.loads(open(pcb).read())
FAIL = []; INFO = []
def check(cond, msg):
    (INFO if cond else FAIL).append(("PASS " if cond else "FAIL ") + msg)

def get(n, k):
    for x in n:
        if isinstance(x, list) and x and x[0] == S(k): return x
def all_(n, k): return [x for x in n if isinstance(x, list) and x and x[0] == S(k)]

netname = {x[1]: str(x[2]) for x in all_(d, "net")}
def cls(n):
    if re.fullmatch(r"R\d+_(COM|NO|NC)", n): return "MAINS"
    if n in ("+12V", "GND"): return "POWER"
    if n == "+5V_MEGA": return "P5V"
    if n == "+3V3": return "P3V3"
    if n.endswith("_NEG"): return "RGB"
    if re.fullmatch(r"RELAY\d+_COIL_LOW", n): return "COIL"
    return "Default"
CLR = {"Default": 0.2, "POWER": 0.3, "P5V": 0.25, "P3V3": 0.2, "RGB": 0.3, "COIL": 0.2, "MAINS": 2.0}
SELV_MAINS, INTER, INTRA, EDGE_MAINS = 6.0, 5.0, 2.0, 3.0
def relay(n):
    m = re.fullmatch(r"R(\d+)_(COM|NO|NC)", n); return int(m.group(1)) if m else None
def required(a, b):
    ca, cb = cls(a), cls(b)
    if (ca == "MAINS") != (cb == "MAINS"): return SELV_MAINS
    if ca == cb == "MAINS": return INTRA if relay(a) == relay(b) else INTER
    return max(CLR[ca], CLR[cb])

# ------------------------------------------------------------------ geometry
items = []  # dict(net, layers, geom, kind, desc, width)
holes = []  # (Point-buffer, net, desc)
for s in all_(d, "segment"):
    a, e, w = get(s, "start"), get(s, "end"), get(s, "width")[1]
    n = netname[get(s, "net")[1]]
    items.append(dict(net=n, layers={str(get(s, "layer")[1])}, kind="trk", width=w,
                      geom=LineString([a[1:3], e[1:3]]).buffer(w / 2, 16), line=LineString([a[1:3], e[1:3]]),
                      desc=f"track {n} w={w} {a[1:3]}->{e[1:3]}"))
for v in all_(d, "via"):
    at, sz, dr = get(v, "at"), get(v, "size")[1], get(v, "drill")[1]
    n = netname[get(v, "net")[1]]
    items.append(dict(net=n, layers={"F.Cu", "B.Cu"}, kind="via", width=sz,
                      geom=Point(at[1:3]).buffer(sz / 2, 16), desc=f"via {n} @{at[1:3]}"))
    holes.append((Point(at[1:3]).buffer(dr / 2, 16), n, f"via {n}"))
fps = {}; pads = []
for fp in all_(d, "footprint"):
    ref = None
    for t in all_(fp, "fp_text"):
        if str(t[1]) == "reference": ref = t[2]
    at = get(fp, "at"); fx, fy = at[1], at[2]; fr = at[3] if len(at) > 3 else 0
    crt = []
    for ln in all_(fp, "fp_line") + all_(fp, "fp_rect"):
        if str(get(ln, "layer")[1]) == "F.CrtYd":
            crt += [get(ln, "start")[1:3], get(ln, "end")[1:3]]
    for c in all_(fp, "fp_circle"):
        if str(get(c, "layer")[1]) == "F.CrtYd":
            cx, cy = get(c, "center")[1:3]; ex, ey = get(c, "end")[1:3]; r = math.hypot(ex - cx, ey - cy)
            crt += [(cx - r, cy - r), (cx + r, cy + r)]
    def to_abs(px, py):
        t = math.radians(-fr)
        return fx + px * math.cos(t) - py * math.sin(t), fy + px * math.sin(t) + py * math.cos(t)
    cabs = [to_abs(*p) for p in crt]
    fps[ref] = dict(lib=str(fp[1]), x=fx, y=fy, rot=fr, layer=str(get(fp, "layer")[1]),
                    crt=box(min(p[0] for p in cabs), min(p[1] for p in cabs), max(p[0] for p in cabs), max(p[1] for p in cabs)) if cabs else None,
                    texts=[(str(t[2]) if str(t[1]) == "user" else None) for t in all_(fp, "fp_text")])
    for p in all_(fp, "pad"):
        num, typ, shp = str(p[1]), str(p[2]), str(p[3])
        pa = get(p, "at"); px, py = pa[1], pa[2]; prot = pa[3] if len(pa) > 3 else 0
        w, h = get(p, "size")[1:3]
        if shp == "circle": g = Point(0, 0).buffer(w / 2, 32)
        elif shp == "oval":
            r = min(w, h) / 2
            g = LineString([(-(w / 2 - r), -(h / 2 - r)), (w / 2 - r, h / 2 - r)]).buffer(r, 32) if w != h else Point(0, 0).buffer(r, 32)
        elif shp == "roundrect":
            rr = get(p, "roundrect_rratio"); r = min(w, h) * (rr[1] if rr else 0.25)
            g = box(-w / 2 + r, -h / 2 + r, w / 2 - r, h / 2 - r).buffer(r, 16)
        else: g = box(-w / 2, -h / 2, w / 2, h / 2)
        g = affinity.rotate(g, -prot, origin=(0, 0))
        ax_, ay_ = to_abs(px, py)
        g = affinity.translate(g, ax_, ay_)
        nn = get(p, "net"); n = str(nn[2]) if nn else ""
        lays = [str(x) for x in get(p, "layers")[1:]]
        cu = {"F.Cu", "B.Cu"} if "*.Cu" in lays else {l for l in lays if l.endswith(".Cu")}
        drl = get(p, "drill")
        rec = dict(net=n, layers=cu if typ != "np_thru_hole" else set(), kind="pad", geom=g, ref=ref, num=num,
                   pos=(ax_, ay_), typ=typ, size=(w, h), desc=f"pad {ref}.{num} {n}")
        pads.append(rec)
        if rec["layers"]: items.append(rec)
        if drl:
            dv = [x for x in drl[1:] if isinstance(x, (int, float))]
            holes.append((Point(ax_, ay_).buffer(dv[0] / 2, 32), n, f"hole {ref}.{num}"))

# ------------------------------------------------------------------ 1+2 shorts / clearance
tree = STRtree([i["geom"] for i in items])
worst = {}; shorts = []; viol = []
for i, it in enumerate(items):
    for j in tree.query(it["geom"].buffer(SELV_MAINS + 0.5)):
        if j <= i: continue
        jt = items[j]
        if jt["net"] == it["net"] or not (it["layers"] & jt["layers"]): continue
        dd = it["geom"].distance(jt["geom"])
        req = required(it["net"], jt["net"])
        key = tuple(sorted((cls(it["net"]), cls(jt["net"])))) if req != INTER else ("MAINS", "MAINS-inter")
        if req == INTRA: key = ("MAINS", "MAINS-intra")
        if dd < worst.get(key, (99,))[0]: worst[key] = (dd, it["desc"], jt["desc"], req)
        if dd < 1e-6: shorts.append((it["desc"], jt["desc"]))
        elif dd < req - 1e-3: viol.append((round(dd, 3), req, it["desc"], jt["desc"]))
check(not shorts, f"no copper shorts ({len(shorts)} found)")
check(not viol, f"all clearances met ({len(viol)} violations)")
for v in viol[:20]: FAIL.append(f"      {v}")
for k, (dd, a, b_, req) in sorted(worst.items()):
    INFO.append(f"INFO min clearance {k[0]:>7s} <-> {k[1]:<12s}: {dd:6.3f} mm (required {req} mm)")

# holes vs other-net copper
hv = []
for hg, hn, hd in holes:
    for j in tree.query(hg.buffer(SELV_MAINS + 0.5)):
        it = items[j]
        if it["net"] == hn and hn: continue
        dd = hg.distance(it["geom"])
        req = SELV_MAINS if cls(it["net"]) == "MAINS" and cls(hn or "x") != "MAINS" else 0.25
        if it["kind"] == "pad" and it["pos"] == (hg.centroid.x, hg.centroid.y): continue
        if dd < req - 1e-3 and not (it["kind"] == "pad" and it["geom"].contains(hg.centroid)):
            hv.append((round(dd, 3), req, hd, it["desc"]))
check(not hv, f"hole clearance (0.25 mm; 6 mm MAINS to non-MAINS holes) ({len(hv)} violations)")
for v in hv[:10]: FAIL.append(f"      {v}")

# ------------------------------------------------------------------ 3 connectivity
parent = list(range(len(items)))
def fnd(a):
    while parent[a] != a: parent[a] = parent[parent[a]]; a = parent[a]
    return a
for i, it in enumerate(items):
    for j in tree.query(it["geom"]):
        if j <= i: continue
        jt = items[j]
        if jt["net"] == it["net"] and it["net"] and (it["layers"] & jt["layers"]) and it["geom"].intersects(jt["geom"]):
            # a track touching a pad/via/track must reach copper (centerline-to-copper)
            parent[fnd(i)] = fnd(j)
comp = collections.defaultdict(set)
for i, it in enumerate(items):
    if it["net"]: comp[it["net"]].add(fnd(i))
split = {n: len(c) for n, c in comp.items() if len(c) > 1}
check(not split, f"every net is one connected copper island ({len(comp)} nets checked; split: {split})")

# ------------------------------------------------------------------ 4 edge clearance
outline = [x for x in d if isinstance(x, list) and x and x[0] in (S("gr_line"), S("gr_rect")) and str(get(x, "layer")[1]) == "Edge.Cuts"]
pts = [tuple(get(o, k)[1:3]) for o in outline for k in ("start", "end")]
bx0, by0 = min(p[0] for p in pts), min(p[1] for p in pts); bx1, by1 = max(p[0] for p in pts), max(p[1] for p in pts)
edge = box(bx0, by0, bx1, by1).exterior
ev = [(round(it["geom"].distance(edge), 3), it["desc"]) for it in items
      if it["geom"].distance(edge) < (EDGE_MAINS if cls(it["net"]) == "MAINS" else 0.5)]
check(not ev, f"copper-to-edge >= 0.5 mm (MAINS >= {EDGE_MAINS} mm) ({len(ev)} violations)")
INFO.append(f"INFO board outline {bx1 - bx0:.1f} x {by1 - by0:.1f} mm")

# ------------------------------------------------------------------ 5 widths
MINW = {"Default": 0.25, "COIL": 0.4, "P5V": 0.8, "P3V3": 0.4, "RGB": 1.5, "POWER": 1.0, "MAINS": 2.0}
wv = []
for it in items:
    if it["kind"] != "trk": continue
    c = cls(it["net"])
    if it["width"] < MINW[c] - 1e-6:
        # neck-down is accepted only for the last 4 mm into a pad of the same net
        near = any(p["net"] == it["net"] and Point(p["pos"]).distance(it["line"]) < 4.0 for p in pads)
        # (power classes may neck down to 0.5 mm into fine-pitch pads, e.g. the microSD VSS pin)
        floor = 0.5 if c in ("POWER", "P5V") else 0.8 * min(MINW[c], 1.5)
        if not (near and it["width"] >= floor): wv.append(it["desc"])
check(not wv, f"track widths meet net-class minimum ({len(wv)} too narrow)")
for v in wv[:10]: FAIL.append("      " + v)
wsum = collections.defaultdict(collections.Counter)
for it in items:
    if it["kind"] == "trk": wsum[cls(it["net"])][it["width"]] += 1
for c, cnt in sorted(wsum.items()): INFO.append(f"INFO widths {c:8s} {dict(sorted(cnt.items()))}")
# both-layer MAINS + 3 mm trunks present
mains_l = collections.defaultdict(set)
for it in items:
    if it["kind"] == "trk" and cls(it["net"]) == "MAINS": mains_l[it["net"]] |= it["layers"]
check(len(mains_l) == 48 and all(v == {"F.Cu", "B.Cu"} for v in mains_l.values()),
      "all 48 relay-contact nets routed on both copper layers at 2 mm")
trunk = sum(1 for it in items if it["kind"] == "trk" and it["net"] in ("+12V", "GND") and it["width"] >= 3.0)
check(trunk >= 15, f"3 mm +12V/GND trunks present ({trunk} segments)")
check(not any(it["kind"] == "via" and cls(it["net"]) == "MAINS" for it in items), "no vias on MAINS nets")

# ------------------------------------------------------------------ 6 netlist parity
want = json.load(open(netlist_json))
for i in range(1, 17):
    want[f"K{i}"]["3"], want[f"K{i}"]["4"] = f"R{i}_NO", f"R{i}_NC"
want["BZ1"]["1"] = "BUZZER_DRV"; want["RBZ1"] = {"1": "D6_BUZZER", "2": "BUZZER_DRV"}
# Rev D (independent restatement of the intended changes, from the datasheets):
want.pop("RV1"); want["J_POT"] = {"1": "+5V_MEGA", "2": "A0_POT", "3": "GND"}
want["MCU1"].update({"D49": "D49_SD_CD", "D50": "D50_SD_MISO", "D51": "D51_SD_MOSI", "D52": "D52_SD_SCK", "D53": "D53_SD_CS"})
want["U3"] = {"1": "GND", "2": "+3V3", "3": "+12V"}                       # AMS1117: ADJ/GND, VOUT(+tab), VIN
want["C4"] = {"1": "+12V", "2": "GND"}; want["C5"] = {"1": "+3V3", "2": "GND"}
want["C6"] = {"1": "+3V3", "2": "GND"}; want["C7"] = {"1": "+3V3", "2": "GND"}
want["U4"] = {"1": "GND", "2": "D52_SD_SCK", "3": "SD_CLK", "4": "GND", "5": "D51_SD_MOSI", "6": "SD_CMD", "7": "GND",
              "8": "SD_CS", "9": "D53_SD_CS", "10": "GND", "12": "GND", "13": "+3V3", "14": "+3V3"}  # 74LVC125A
want["J_SD"] = {"1": "SD_DAT2", "2": "SD_CS", "3": "SD_CMD", "4": "+3V3", "5": "SD_CLK", "6": "GND",
                "7": "D50_SD_MISO", "8": "SD_DAT1", "9": "D49_SD_CD", "10": "GND", "11": "GND"}   # microSD SPI mode
for k, n in (("RSD1", "SD_CS"), ("RSD2", "D50_SD_MISO"), ("RSD3", "SD_DAT1"), ("RSD4", "SD_DAT2")):
    want[k] = {"1": "+3V3", "2": n}
have = collections.defaultdict(dict)
for p in pads:
    if p["net"]: have[p["ref"]][p["num"]] = p["net"]
diff = [(r, n, want.get(r, {}).get(n), have.get(r, {}).get(n)) for r in set(want) | set(have)
        for n in set(want.get(r, {})) | set(have.get(r, {})) if want.get(r, {}).get(n) != have.get(r, {}).get(n)]
check(not diff, f"pad-net map == Rev B netlist + documented Rev D changes ({len(diff)} differences)")
for x in diff[:10]: FAIL.append(f"      {x}")

# ------------------------------------------------------------------ 7 relay / terminal pinout + footprint geometry
def libpads(lib):
    lb, nm_ = lib.split(":")
    s = open(f"{fplib}/{lb}.pretty/{nm_}.kicad_mod").read()
    return {m.group(1): (float(m.group(2)), float(m.group(3)))
            for m in re.finditer(r'\(pad "?(\w+)"? thru_hole \w+ \(at ([-\d.]+) ([-\d.]+)', s)}
G5Q = {"1": (0, 0), "2": (10.16, 0), "3": (17.78, 0), "4": (15.24, -7.62), "5": (0, -7.62)}  # KiCad Relay_THT, top view
ok_fp = True
for i in range(1, 17):
    k = have[f"K{i}"]
    ok = (k["1"] == "+12V" and k["2"] == f"R{i}_COM" and k["3"] == f"R{i}_NO" and k["4"] == f"R{i}_NC"
          and k["5"] == f"RELAY{i}_COIL_LOW")
    j = have[f"JR{i}"]
    ok &= (j["1"] == f"R{i}_COM" and j["2"] == f"R{i}_NO" and j["3"] == f"R{i}_NC")
    check(ok, f"K{i}/JR{i}: coil 1=+12V 5=COIL_LOW, 2=COM 3=NO 4=NC; terminal 1=COM 2=NO 3=NC")
    f = fps[f"K{i}"]; lp = {p["num"]: p["pos"] for p in pads if p["ref"] == f"K{i}"}
    t = math.radians(-f["rot"])
    for num, (lx, ly) in G5Q.items():
        ex = f["x"] + lx * math.cos(t) - ly * math.sin(t); ey = f["y"] + lx * math.sin(t) + ly * math.cos(t)
        if math.hypot(ex - lp[num][0], ey - lp[num][1]) > 0.01: ok_fp = False
    if f["layer"] != "F.Cu": ok_fp = False
check(ok_fp, "all 16 relays match the Omron G5Q-1 hole pattern (KiCad library, top view, not mirrored)")
check(libpads("Relay_THT:Relay_SPDT_Omron-G5Q-1") == {k: tuple(map(float, v)) for k, v in G5Q.items()},
      "reference G5Q-1 pattern equals installed KiCad library footprint")
for u in ("U1", "U2"):
    lp = {p["num"]: p["pos"] for p in pads if p["ref"] == u}
    row = abs(lp["18"][0] - lp["1"][0]); pitch = abs(lp["2"][1] - lp["1"][1])
    check(abs(row - 7.62) < 0.01 and abs(pitch - 2.54) < 0.01 and have[u]["9"] == "GND" and have[u]["10"] == "+12V",
          f"{u}: DIP-18 300 mil (row {row:.2f} mm, pitch {pitch:.2f}), pin 9 GND, pin 10 COM=+12V")
for q in range(1, 10):
    n = have[f"Q{q}"]
    check(n["1"] == f"GATE_Q{q}" and n["3"] == "GND" and n["2"].endswith("_NEG"), f"Q{q} IRLZ44N G-D-S = 1-2-3")

# terminal silkscreen labels: nearest label to each JR pad must name its function
texts = [(str(t[1]), get(t, "at")[1:3], str(get(t, "layer")[1])) for t in all_(d, "gr_text")]
badlab = []
for i in range(1, 17):
    for num, lab in (("1", "COM"), ("2", "NO"), ("3", "NC")):
        p = next(pp for pp in pads if pp["ref"] == f"JR{i}" and pp["num"] == num)
        cand = [(math.hypot(a[0] - p["pos"][0], a[1] - p["pos"][1]), s) for s, a, l in texts
                if l == "B.SilkS" and s in ("COM", "NO", "NC")]
        if min(cand)[1] != lab: badlab.append(f"JR{i}.{num}")
check(not badlab, f"back-side COM/NO/NC labels sit behind the right pads ({badlab})")

# ------------------------------------------------------------------ 8 firmware pin map
src = open(ino).read()
fw = {k: int(v) for k, v in re.findall(r"const uint8_t (PIN_\w+)\s*=\s*(\d+);", src)}
rel = [int(x) for x in re.search(r"PIN_RELE\[16\]\s*=\s*\{([^}]*)\}", src).group(1).replace(",", " ").split()]
mcu = have["MCU1"]
ok = all(f"D{pin}" in mcu for pin in fw.values()) and all(mcu[f"D{p}"] == f"D{p}_RELAY{i+1}" for i, p in enumerate(rel))
ok &= mcu.get("A0") == "A0_POT" and mcu.get("VIN") == "+12V"
check(ok, f"firmware pins ({len(fw)} PIN_ constants + 16 relays) all land on the expected Mega pads")
# relay channel -> ULN -> coil chain
chain = True
for i in range(1, 17):
    u = "U1" if i <= 8 else "U2"; k = (i - 1) % 8
    chain &= have[u][str(1 + k)] == f"D{24 + i}_RELAY{i}" and have[u][str(18 - k)] == f"RELAY{i}_COIL_LOW"
check(chain, "D25..D40 -> ULN2803 input n -> output (19-n) -> relay coil chain consistent")
# microSD: Mega SPI (D50 MISO, D51 MOSI, D52 SCK, D53 SS) -> 74LVC125A (3.3 V) -> card, MISO direct
sdok = (fw.get("PIN_SD_CS") == 53 and fw.get("PIN_SD_CD") == 49 and have["J_SD"]["4"] == "+3V3"
        and have["U4"]["14"] == "+3V3" and have["U3"]["3"] == "+12V"
        and all(have["U4"][oe] == "GND" for oe in ("1", "4", "10")))
check(sdok, "microSD: CS=D53 / CD=D49 match firmware, card and buffer on +3V3, buffer outputs enabled, LDO from +12V")
pot = have["J_POT"]
check(pot == {"1": "+5V_MEGA", "2": "A0_POT", "3": "GND"} and "RV1" not in have and fw.get("PIN_POT") is None,
      "J_POT terminal carries 5V / A0 / GND (on-board pot removed)")

# ------------------------------------------------------------------ 9 Mega stacking keep-out
mg = fps["MCU1"]; region = box(mg["x"], mg["y"] - 53.34, mg["x"] + 101.6, mg["y"])
tht = {p["ref"] for p in pads if p["typ"] == "thru_hole"}
hit = [r for r, f in fps.items() if r != "MCU1" and r in tht and f["crt"] is not None and f["crt"].intersects(region)]
check(not hit, f"no through-hole component courtyard over the Arduino Mega body ({hit})")
usb = box(mg["x"] - 6.6, mg["y"] - 44.3, mg["x"] + 16, mg["y"] - 31.9)
vu = [it["desc"] for it in items if it["kind"] == "via" and it["geom"].intersects(usb)]
check(not vu, f"no vias above the Mega USB-B shell ({vu})")
# courtyard overlaps between components
crt = [(r, f["crt"]) for r, f in fps.items() if f["crt"] is not None]
ov = [(a, b_) for i, (a, ga) in enumerate(crt) for b_, gb in crt[i + 1:] if ga.intersection(gb).area > 0.05]
INFO.append(f"INFO courtyard bounding-box overlaps (coarse): {ov}")

print("\n".join(INFO)); print("\n".join(FAIL))
print(f"\nRESULT: {'ALL CHECKS PASSED' if not FAIL else str(len([f for f in FAIL if f.startswith('FAIL')])) + ' CHECK(S) FAILED'}")
sys.exit(1 if FAIL else 0)
