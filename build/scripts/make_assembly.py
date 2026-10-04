#!/usr/bin/env python3
"""make_assembly.py <release_dir>
Turnkey-assembly extras for PCBWay (or any full-service assembler):
  <release_dir>/assembly/PSN-Presepe-Mega-RevD-Centroid-PCBWay.csv
      every placed part incl. MCU1 (Arduino Mega headers) on the BOTTOM side
  <release_dir>/assembly/PSN-Presepe-Mega-RevD-Assembly-Instructions.pdf
      page 1: top assembly drawing, Mega header pins marked red, ICSP holes blue
      page 2: written assembly instructions
Needs <release_dir>/PSN-Presepe-Mega-RevD.kicad_pcb, -CPL-JLCPCB.csv (make_jlc.py)
and previews/PSN-Presepe-Mega-RevD-assembly.png (release.py)."""
import csv, os, sys
from collections import defaultdict
import pcbnew
from PIL import Image, ImageDraw, ImageFont
from reportlab.lib.pagesizes import A3, landscape
from reportlab.lib.utils import ImageReader
from reportlab.pdfgen import canvas

rel = sys.argv[1]
N = "PSN-Presepe-Mega-RevD"
out = os.path.join(rel, "assembly"); os.makedirs(out, exist_ok=True)
b = pcbnew.LoadBoard(os.path.join(rel, N + ".kicad_pcb"))
mcu = b.FindFootprintByReference("MCU1")
mm = lambda v: (pcbnew.ToMM(v.x), pcbnew.ToMM(v.y))

# Mega header pins vs the 2x3 ICSP holes (ICSP = the 6 pads named MISO/SCK/MOSI/RST2/5V2/GND4)
ICSP = {"MISO", "SCK", "MOSI", "RST2", "5V2", "GND4"}
hdr, icsp = [], []
for p in mcu.Pads():
    if p.GetAttribute() != pcbnew.PAD_ATTRIB_PTH: continue
    (icsp if p.GetNumber() in ICSP else hdr).append(mm(p.GetPosition()))
assert len(hdr) == 86 and len(icsp) == 6, (len(hdr), len(icsp))

# ---- centroid: JLC CPL + MCU1 on the bottom (centre of the header pin field)
rows = list(csv.reader(open(os.path.join(rel, N + "-CPL-JLCPCB.csv"))))
xs = [x for x, _ in hdr]; ys = [y for _, y in hdr]
cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
rows = [r for r in rows if r[0] != "MCU1"] + [["MCU1", "%.4fmm" % cx, "%.4fmm" % -cy, "Bottom", "0"]]
with open(os.path.join(out, N + "-Centroid-PCBWay.csv"), "w", newline="") as f:
    csv.writer(f).writerows(rows)

# ---- assembly drawing: map board mm -> pixels of the 4000 px assembly preview
im = Image.open(os.path.join(rel, "previews", N + "-assembly.png")).convert("RGB")
bb = b.ComputeBoundingBox(False)              # = the area kicad-cli plots with --page-size-mode 2
x0, y0 = pcbnew.ToMM(bb.GetLeft()), pcbnew.ToMM(bb.GetTop())
x1, y1 = pcbnew.ToMM(bb.GetRight()), pcbnew.ToMM(bb.GetBottom())
S = im.width / (x1 - x0)                       # render.sh scales that area to the image width
P = lambda x, y: ((x - x0) * S, (y - y0) * S)
d = ImageDraw.Draw(im)
try:
    F = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 50)
except OSError:
    F = ImageFont.load_default()
RED, BLUE = (220, 0, 0), (0, 90, 220)
strips = defaultdict(list)
for x, y in hdr: strips[round(y, 1)].append(x)
for y, row in strips.items():                  # horizontal 1xN headers
    row.sort(); cur = [row[0]]
    for x in row[1:] + [1e9]:
        if x - cur[-1] < 2.6: cur.append(x); continue
        if len(cur) > 2: d.rectangle([P(cur[0] - 1.4, y - 1.4), P(cur[-1] + 1.4, y + 1.4)], outline=RED, width=8)
        cur = [x]
dbl = [(x, y) for x, y in hdr if x > max(xs) - 3]   # 2x18 double row at the right end
d.rectangle([P(min(x for x, _ in dbl) - 1.4, min(y for _, y in dbl) - 1.4),
             P(max(x for x, _ in dbl) + 1.4, max(y for _, y in dbl) + 1.4)], outline=RED, width=8)
for pts, col in ((hdr, RED), (icsp, BLUE)):
    for x, y in pts:
        px, py = P(x, y); d.ellipse([px - 10, py - 10, px + 10, py + 10], fill=col)
d.rectangle([P(min(x for x, _ in icsp) - 1.4, min(y for _, y in icsp) - 1.4),
             P(max(x for x, _ in icsp) + 1.4, max(y for _, y in icsp) + 1.4)], outline=BLUE, width=8)
d.text(P(min(xs) - 6, max(ys) + 4.5), "RED = MCU1 Mega headers\nMOUNT ON BOTTOM SIDE", fill=RED, font=F)
d.text(P(min(xs) - 6, max(ys) + 14.5), "BLUE = ICSP 2x3 holes:\nleave EMPTY", fill=BLUE, font=F)

c = canvas.Canvas(os.path.join(out, N + "-Assembly-Instructions.pdf"), pagesize=landscape(A3), invariant=1)
W, H = landscape(A3)
c.setTitle(N + " assembly instructions")
c.setFont("Helvetica-Bold", 20); c.drawString(30, H - 40, "PSN-Presepe Rev D - Assembly drawing (TOP view)")
r = im.width / im.height; w = W - 60; h = w / r
if h > H - 90: h = H - 90; w = h * r
c.drawImage(ImageReader(im), 30, H - 60 - h, w, h); c.showPage()
c.setFont("Helvetica-Bold", 22); c.drawString(40, H - 50, "PSN-Presepe Rev D - Assembly instructions")
L = [
    f"1. Turnkey assembly, all parts per BOM ({N}-BOM-PCBWay.csv). Centroid: {N}-Centroid-PCBWay.csv.",
    "2. TOP side: all SMD parts (12) and all through-hole parts except MCU1.",
    "3. MCU1 = Arduino Mega 2560 R3 shield male pin headers (2.54 mm): 1x10, 5x 1x8, 1x 2x18 = 86 pins (red on page 1).",
    "   MOUNT ON THE BOTTOM SIDE: plastic body under the board, long pins pointing DOWN (they plug into an Arduino Mega).",
    "   Solder on the TOP side. Pins must be straight and perpendicular: use an Arduino Mega board as alignment jig.",
    "4. ICSP 2x3 holes (blue on page 1): leave EMPTY, no header.",
    "5. Polarity: C1 + = square pad. BZ1 + mark = pin 1. U1/U2 (ULN2803A DIP-18) pin-1 notch as silkscreen.",
    "   K1-K16 (Omron G5Q-1 DC12) pin 1 = square pad (coil +12V). Q1-Q9 IRLZ44N: metal tab towards silkscreen tab line.",
    "6. J_SD (Hirose DM3AT-SF-PEJM5 microSD push-push): card opening faces the top board edge.",
    "7. Screw terminals (Phoenix MKDS 1,5 5.08 mm): wire entry faces the nearest board edge.",
    "8. Relay contacts may carry 230 VAC: do NOT add wires, jumpers, vias or copper. Clean flux residues, especially in the relay area.",
    "9. Mounting holes H1-H6 and tooling holes TH1-TH3 are NPTH: leave empty.",
    "10. No IC programming required.",
    "11. Please send photos of the first assembled board (top and bottom) before shipping.",
]
y = H - 100; c.setFont("Helvetica", 15)
for line in L: c.drawString(40, y, line); y -= 30
c.save()
print("assembly extras:", len(rows) - 1, "placements, 86 header pins bottom, 6 ICSP holes empty")
