#!/usr/bin/env python3
"""check_mega_footprint.py <board.kicad_pcb> <kicad_footprint_dir>
Independent check of the hand-made Arduino Mega shield footprint (MCU1):
1. KiCad's official Module:Arduino_UNO_R3_WithMountingHoles is fitted onto MCU1
   (any rotation/mirror); all 32 UNO header pins and all 4 UNO holes must coincide.
2. The Mega-only features are compared with the Arduino Mega 2560 R3 reference
   coordinates (mm, origin = lower-left board corner, y up).
Exit code 1 on any mismatch."""
import sys
import pcbnew

pcb, fpdir = sys.argv[1:3]
TOL = 0.03
b = pcbnew.LoadBoard(pcb)
m = b.FindFootprintByReference("MCU1")
uno = pcbnew.FootprintLoad(fpdir.rstrip("/") + "/Module.pretty", "Arduino_UNO_R3_WithMountingHoles")
mm = lambda v: (pcbnew.ToMM(v.x), pcbnew.ToMM(v.y))
big = lambda p: pcbnew.ToMM(p.GetDrillSize().x) > 2.5
U = [mm(p.GetPosition()) for p in uno.Pads() if p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH and not big(p)]
UH = [mm(p.GetPosition()) for p in uno.Pads() if big(p)]
M = [mm(p.GetPosition()) for p in m.Pads() if p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH and not big(p)]
MH = [mm(p.GetPosition()) for p in m.Pads() if big(p)]
near = lambda q, pts: min(((q[0] - a) ** 2 + (q[1] - c) ** 2) ** .5 for a, c in pts)
T = [lambda x, y: (x, y), lambda x, y: (-x, -y), lambda x, y: (-y, x), lambda x, y: (y, -x),
     lambda x, y: (-x, y), lambda x, y: (x, -y)]
best = (0,)
for ti, t in enumerate(T):
    tu = [t(*p) for p in U]
    for m0 in M:
        dx, dy = m0[0] - tu[0][0], m0[1] - tu[0][1]
        hit = sum(near((x + dx, y + dy), M) < TOL for x, y in tu)
        if hit > best[0]: best = (hit, ti, dx, dy)
hit, ti, dx, dy = best
holes = [near((x + dx, y + dy), MH) for x, y in (T[ti](*h) for h in UH)]
ok = hit == len(U) and all(h < TOL for h in holes)
print(f"UNO R3 reference (KiCad Module library): {hit}/{len(U)} header pins coincide")
print("UNO R3 mounting holes, distance to board holes (mm):", ", ".join(f"{h:.3f}" for h in holes))

# Mega 2560 R3 reference (mm, lower-left origin, y up); UNO hole (13.97, 2.54) anchors the frame
REF = {"hole (90.17, 50.80)": (90.17, 50.80), "hole (96.52, 2.54)": (96.52, 2.54),
       "2x18 first pin (93.98, 50.80)": (93.98, 50.80), "2x18 last pin (96.52, 7.62)": (96.52, 7.62),
       "D14-D21 header first pin (68.58, 50.80)": (68.58, 50.80), "A8-A15 header first pin (73.66, 2.54)": (73.66, 2.54)}
anchor = min(MH, key=lambda q: (q[1] - max(h[1] for h in MH)) ** 2 + (q[0] - min(h[0] for h in MH)) ** 2)
ox, oy = anchor[0] - 13.97, anchor[1] + 2.54          # board = (ox + x, oy - y)
for name, (x, y) in REF.items():
    dist = near((ox + x, oy - y), MH + M)
    print(f"Mega {name}: {dist:.3f} mm"); ok &= dist < TOL
print("MEGA FOOTPRINT CHECK PASSED" if ok else "MEGA FOOTPRINT CHECK FAILED")
sys.exit(0 if ok else 1)
