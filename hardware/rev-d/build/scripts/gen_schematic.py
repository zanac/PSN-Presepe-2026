#!/usr/bin/env python3
"""Generates the Rev D connectivity schematic (KiCad 7 format, opens in 7..10)
from the routed board, so schematic and PCB share one netlist by construction.
Each part is a box symbol; every pin carries a global label with its net.
Parity is then proven independently by exporting the netlist with kicad-cli and
comparing it pin-by-pin with the PCB (check_sch_parity.py).

Usage: gen_schematic.py <board.kicad_pcb> <out.kicad_sch>
"""
import sys, uuid, re, math
import pcbnew

brd, out = sys.argv[1:3]
b = pcbnew.LoadBoard(brd)
PROJECT = re.sub(r"\.kicad_sch$", "", out.split("/")[-1])
ROOT = str(uuid.uuid4())
U = lambda: str(uuid.uuid4())

PIN_NAMES = {"K": {"1": "COIL+", "2": "COM", "3": "NO", "4": "NC", "5": "COIL-"},
             "JR": {"1": "COM", "2": "NO", "3": "NC"},
             "Q": {"1": "G", "2": "D", "3": "S"},
             "U": {"9": "GND", "10": "COM"}}
DESC = {"K": "Omron G5Q-1 DC12 SPDT relay (pin 3 = NO, pin 4 = NC)",
        "JR": "Relay contact terminal COM/NO/NC (may carry 230 V AC)",
        "U": "ULN2803A Darlington array, DIP-18", "Q": "IRLZ44N logic-level N-MOSFET"}

parts = []
for fp in sorted(b.GetFootprints(), key=lambda f: (re.sub(r"\d", "", f.GetReference()), int(re.sub(r"\D", "", f.GetReference()) or 0))):
    ref = fp.GetReference()
    if ref.startswith("H"): continue
    pins = {}
    for p in fp.Pads():
        if p.GetNetname() and p.GetNumber(): pins[p.GetNumber()] = p.GetNetname()
    if not pins: continue
    parts.append((ref, fp.GetValue(), str(fp.GetFPID().GetUniStringLibId()), pins))

def kind(ref):
    for k in ("JR", "K", "U", "Q"):
        if ref.startswith(k) and ref[len(k):].isdigit(): return k
    return None

def pin_sort(n):
    m = re.match(r"([A-Za-z+]*)(\d*)", n)
    return (m.group(1), int(m.group(2)) if m.group(2) else 0)

G = 2.54
lib, inst, labels = [], [], []
x, y, colw, maxy = 30.48, 30.48, 60.96, 560.0
for ref, val, fpid, pins in parts:
    k = kind(ref)
    nums = sorted(pins, key=pin_sort)
    h = (len(nums) + 1) * G
    if y + h + 10 > maxy: x += colw; y = 30.48
    top = (len(nums) - 1) / 2 * G
    ytop = y; y = ytop + math.ceil((top + 2 * G) / G) * G   # symbol centre
    pl = []
    for i, n in enumerate(nums):
        py = round(top - i * G, 4)   # library Y axis points up
        name = PIN_NAMES.get(k, {}).get(n, pins[n]) if k else pins[n]
        pl.append(f'        (pin passive line (at -7.62 {py} 0) (length 2.54) (name "{name}" (effects (font (size 1 1)))) (number "{n}" (effects (font (size 1 1)))))')
        # schematic Y axis points down: label at (x - 7.62, y - py)
        labels.append(f'  (global_label "{pins[n]}" (shape bidirectional) (at {x - 7.62:.2f} {y - py:.2f} 180) (fields_autoplaced) (effects (font (size 1 1)) (justify right)) (uuid {U()}) (property "Intersheetrefs" "${{INTERSHEET_REFS}}" (at 0 0 0) (effects (font (size 1.27 1.27)) hide)))')
    hh = top + G
    lib.append(f'''    (symbol "PSN:{ref}" (pin_names (offset 0)) (in_bom yes) (on_board yes)
      (property "Reference" "{re.sub(r'[0-9]', '', ref)}" (at 0 {hh + 1.5:.2f} 0) (effects (font (size 1.27 1.27))))
      (property "Value" "{val}" (at 0 {-hh - 1.5:.2f} 0) (effects (font (size 1.27 1.27))))
      (property "Footprint" "{fpid}" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))
      (property "Datasheet" "" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))
      (property "Description" "{DESC.get(k, '')}" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))
      (symbol "{ref}_0_1"
        (rectangle (start -5.08 {hh:.2f}) (end 5.08 {-hh:.2f}) (stroke (width 0.254) (type default)) (fill (type background)))
{chr(10).join(pl)}
      )
    )''')
    pinu = "\n".join(f'    (pin "{n}" (uuid {U()}))' for n in nums)
    inst.append(f'''  (symbol (lib_id "PSN:{ref}") (at {x:.2f} {y:.2f} 0) (unit 1) (in_bom yes) (on_board yes) (dnp no) (uuid {U()})
    (property "Reference" "{ref}" (at {x:.2f} {y - hh - 1.5:.2f} 0) (effects (font (size 1.27 1.27))))
    (property "Value" "{val}" (at {x:.2f} {y + hh + 1.5:.2f} 0) (effects (font (size 1 1))))
    (property "Footprint" "{fpid}" (at {x:.2f} {y:.2f} 0) (effects (font (size 1.27 1.27)) hide))
    (property "Datasheet" "" (at {x:.2f} {y:.2f} 0) (effects (font (size 1.27 1.27)) hide))
{pinu}
    (instances (project "{PROJECT}" (path "/{ROOT}" (reference "{ref}") (unit 1))))
  )''')
    y = ytop + math.ceil((2 * top + 6 * G) / G) * G

txt = f'''(kicad_sch (version 20230121) (generator eeschema)
  (uuid {ROOT})
  (paper "A1")
  (title_block (title "PSN-Presepe Arduino Mega carrier - connectivity schematic") (rev "D") (company "PSN-Presepe")
    (comment 1 "Generated from the routed Rev D PCB; every pin carries a global net label")
    (comment 2 "Relay G5Q-1: pin 3 = NO, pin 4 = NC (fixed in Rev D)"))
  (lib_symbols
{chr(10).join(lib)}
  )
{chr(10).join(labels)}
{chr(10).join(inst)}
  (sheet_instances (path "/" (page "1")))
)
'''
open(out, "w").write(txt)
print("schematic:", out, "symbols", len(parts), "labels", len(labels))
