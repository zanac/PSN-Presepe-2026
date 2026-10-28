#!/usr/bin/env python3
"""release.py <final.kicad_pcb> <release_dir>
Stamps the title block / plot settings, saves the release board + project +
rules, then exports Gerbers (copper, mask, silk, outline), Excellon drill
(PTH + NPTH), drill map, position file, BOM and previews with kicad-cli."""
import csv, collections, datetime, os, shutil, subprocess, sys
import pcbnew

src, rel = sys.argv[1:3]
NAME = "PSN-Presepe-Mega-RevD"
os.makedirs(rel, exist_ok=True)
here = os.path.dirname(os.path.abspath(__file__))
b = pcbnew.LoadBoard(src)
tb = b.GetTitleBlock()
tb.SetTitle("PSN-Presepe Arduino Mega carrier"); tb.SetRevision("D")
tb.SetDate(datetime.date.today().isoformat()); tb.SetCompany("PSN-Presepe")
tb.SetComment(0, "FR-4 1.6 mm, 2 layers, 2 oz (70 um) copper recommended, HASL, green mask, white silk")
tb.SetComment(1, "Relay contacts may carry 230 V AC: 6 mm SELV isolation, 5 mm between relays")
pp = b.GetPlotOptions()
pp.SetUseGerberProtelExtensions(True); pp.SetUseGerberX2format(True); pp.SetIncludeGerberNetlistInfo(True)
pp.SetSubtractMaskFromSilk(True); pp.SetPlotViaOnMaskLayer(False)   # vias tented
out_pcb = os.path.join(rel, NAME + ".kicad_pcb")
pcbnew.SaveBoard(out_pcb, b)
txt = open(out_pcb).read().replace('(paper "A4")', '(paper "A3")', 1)   # 1:1 print fits on A3
open(out_pcb, "w").write(txt)
subprocess.run([sys.executable, os.path.join(here, "make_rules.py"), rel, NAME], check=True)

g = os.path.join(rel, "gerbers"); os.makedirs(g, exist_ok=True)
def cli(*a): subprocess.run(["kicad-cli", *a], check=True, stdout=subprocess.DEVNULL)
cli("pcb", "export", "gerbers", "-o", g + "/", "--layers",
    "F.Cu,B.Cu,F.Mask,B.Mask,F.SilkS,B.SilkS,Edge.Cuts", "--subtract-soldermask", out_pcb)
cli("pcb", "export", "drill", "-o", g + "/", "--format", "excellon", "--excellon-separate-th",
    "--generate-map", "--map-format", "pdf", "-u", "mm", out_pcb)
cli("pcb", "export", "pos", "-o", os.path.join(rel, NAME + "-CPL.csv"), "--format", "csv", "--units", "mm",
    "--side", "front", out_pcb)

# BOM grouped by value + footprint
MPN = {
    "G5Q-1_DC12": "Omron G5Q-1 DC12 (contact rating 10 A 250 VAC; verify suffix)",
    "ULN2803A": "TI ULN2803A / ST ULN2803A (DIP-18, 300 mil)",
    "100R_GATE": "100 R 1/4 W axial", "100K_PULLDOWN": "100 k 1/4 W axial", "220R": "220 R 1/4 W axial",
    "B10K": "Alps RK09K 10 k linear (B), horizontal", "BUZZER_PASSIVE_RM7.6": "12 mm passive buzzer, 7.6 mm pitch (piezo or 16-42 R magnetic)",
    "470uF_25V_RELAY_BULK": "470 uF 25 V electrolytic, D10 mm, 5 mm pitch",
    "DM3AT-SF-PEJM5": "Hirose push-push microSD with card detect", "AMS1117-3.3": "3.3 V LDO SOT-223",
    "74LVC125A": "Nexperia 74LVC125AD,118 (SOIC-14)", "10uF_25V": "MLCC 0805 X5R", "22uF_10V": "MLCC 0805 X5R",
    "100nF": "MLCC 0805 X7R", "10k": "0805 1 %", "POT_5V_A0_GND": "external 10 k linear pot: 5V / wiper / GND",
}
groups = collections.OrderedDict()
for fp in sorted(b.GetFootprints(), key=lambda f: f.GetReference()):
    ref = fp.GetReference()
    if ref.startswith(("H", "TH")): continue
    val = fp.GetValue(); lib = str(fp.GetFPID().GetLibItemName())
    if ref.startswith("Q"): val = "IRLZ44N"
    if ref.startswith("J") and ref != "J_SD":
        n = len([p for p in fp.Pads()]); val = f"Phoenix MKDS 1,5/{n}-5,08 (or equivalent 5.08 mm screw terminal rated >= 250 V AC / >= 10 A)"
    if ref in ("C2", "C3"): val = "100 nF 50 V ceramic, 5 mm pitch"
    if ref == "MCU1": val = "Arduino Mega R3 shield headers: 1x10 + 5x 1x8 + 2x18 (stackable or male)"
    groups.setdefault((val, lib), []).append(ref)
with open(os.path.join(rel, NAME + "-BOM.csv"), "w", newline="") as f:
    w = csv.writer(f); w.writerow(["Qty", "References", "Value / part", "Footprint", "Notes"])
    for (val, lib), refs in groups.items():
        w.writerow([len(refs), " ".join(refs), val, lib, MPN.get(val, "")])
    w.writerow([1, "-", "Arduino Mega 2560 R3 (sits under the board)", "", "not soldered to this PCB"])
    w.writerow([6, "H1-H6", "M3 mounting hole (NPTH 3.2 mm)", "MountingHole_3.2mm_M3", "no part"])

# 1:1 paper-fit print (A3 landscape, scale 1)
cli("pcb", "export", "pdf", "-o", os.path.join(rel, NAME + "-1to1-A3-top.pdf"), "--layers",
    "F.Cu,F.SilkS,Edge.Cuts", "--ibt", out_pcb)
# previews
pv = os.path.join(rel, "previews"); os.makedirs(pv, exist_ok=True)
for name, layers in (("top", "F.Cu,F.SilkS,Edge.Cuts"), ("bottom", "B.Cu,B.SilkS,Edge.Cuts"),
                     ("copper-both", "F.Cu,B.Cu,Edge.Cuts"), ("assembly", "F.SilkS,F.Fab,F.CrtYd,Edge.Cuts")):
    subprocess.run([os.path.join(here, "render.sh"), out_pcb, os.path.join(pv, f"{NAME}-{name}.png"), layers, "4000"], check=True)
print("release written to", rel)
