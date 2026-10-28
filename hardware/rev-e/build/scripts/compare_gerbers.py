#!/usr/bin/env python3
"""compare_gerbers.py <gerbers_A.zip|dir> <gerbers_B.zip|dir>
Geometric comparison of two Gerber/Excellon sets, independent of object order,
aperture numbering, timestamps and title-block attributes: every flash/stroke/
region is reduced to its primitives (shape, coordinates, size, polarity) and
the two multisets must be equal layer by layer. Exit code 1 if they differ."""
import collections, os, sys, tempfile, warnings, zipfile
from gerbonara import GerberFile, ExcellonFile
warnings.simplefilter("ignore")

def files(src):
    if os.path.isdir(src): return {f: os.path.join(src, f) for f in os.listdir(src)}
    d = tempfile.mkdtemp(); zipfile.ZipFile(src).extractall(d)
    return {f: os.path.join(d, f) for f in os.listdir(d)}

def sig(path):
    obj = (ExcellonFile if path.endswith(".drl") else GerberFile).open(path)
    out = collections.Counter()
    for o in obj.objects:
        for p in o.to_primitives(unit=None) if hasattr(o, "to_primitives") else []:
            v = []
            for k, x in sorted(vars(p).items()):
                if isinstance(x, float): v.append((k, round(x, 4)))
                elif isinstance(x, (int, bool, str)): v.append((k, x))
                elif isinstance(x, (list, tuple)): v.append((k, str([tuple(round(c, 4) for c in pt) if isinstance(pt, tuple) else pt for pt in x])))
            out[(type(p).__name__, tuple(v))] += 1
    return out

a, b = files(sys.argv[1]), files(sys.argv[2])
ok = True
for name in sorted(set(a) | set(b)):
    if not name.endswith((".gtl", ".gbl", ".gts", ".gbs", ".gto", ".gbo", ".gm1", ".drl")): continue
    if name not in a or name not in b: print(f"{name}: missing in one set"); ok = False; continue
    sa, sb = sig(a[name]), sig(b[name])
    same = sa == sb
    ok &= same
    print(f"{name}: {sum(sa.values())} primitives - {'geometry identical' if same else 'GEOMETRY DIFFERS'}")
print("RESULT: GERBER GEOMETRY IDENTICAL" if ok else "RESULT: GERBER GEOMETRY DIFFERS")
sys.exit(0 if ok else 1)
