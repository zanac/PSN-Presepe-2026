#!/usr/bin/env python3
"""check_sch_parity.py <kicad-cli exported netlist (.net, kicadsexpr)> <board.kicad_pcb>
Compares every (ref, pin) -> net of the schematic netlist with the PCB pads."""
import sys, re
import sexpdata, pcbnew
from sexpdata import Symbol as S
net, brd = sys.argv[1:3]
d = sexpdata.loads(open(net).read())
def get(n, k):
    for x in n:
        if isinstance(x, list) and x and x[0] == S(k): return x
sch = {}
for n in [x for x in get(d, "nets")[1:]]:
    name = str(get(n, "name")[1]).lstrip("/")
    for node in [x for x in n if isinstance(x, list) and x and x[0] == S("node")]:
        sch[(str(get(node, "ref")[1]), str(get(node, "pin")[1]))] = name
comps = {str(get(c, "ref")[1]): str(get(c, "footprint")[1]) for c in get(d, "components")[1:]}
b = pcbnew.LoadBoard(brd)
pcb = {}; pfp = {}
for fp in b.GetFootprints():
    pfp[fp.GetReference()] = str(fp.GetFPID().GetUniStringLibId())
    for p in fp.Pads():
        if p.GetNetname() and p.GetNumber(): pcb[(fp.GetReference(), p.GetNumber())] = p.GetNetname()
diff = sorted(k for k in set(sch) | set(pcb) if sch.get(k) != pcb.get(k))
fpd = sorted(r for r in comps if comps[r] != pfp.get(r))
print(f"schematic pins {len(sch)}, pcb pads {len(pcb)}, net mismatches {len(diff)}, footprint mismatches {len(fpd)}")
for k in diff[:20]: print("  ", k, "sch:", sch.get(k), "pcb:", pcb.get(k))
for r in fpd[:20]: print("  ", r, comps[r], pfp.get(r))
sys.exit(1 if diff or fpd else 0)
