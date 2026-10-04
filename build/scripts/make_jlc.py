#!/usr/bin/env python3
"""make_jlc.py <board.kicad_pcb> <out_dir>
JLCPCB PCBA files: BOM (Comment, Designator, Footprint, LCSC Part #, spec) and
CPL (Designator, Mid X, Mid Y, Layer, Rotation). LCSC codes are filled only for
parts checked on LCSC/JLCPCB; the rest must be picked in the JLCPCB BOM matcher
against the 'Required spec' column. MCU1 (Arduino headers) is left out: hand-fit."""
import csv, os, re, sys, collections
import pcbnew

brd, out = sys.argv[1:3]
N = "PSN-Presepe-Mega-RevD"
b = pcbnew.LoadBoard(brd)
SPEC = {  # prefix -> (Comment, LCSC, required spec)
    "K":   ("G5Q-1 DC12", "C397244", "Omron G5Q-1 DC12, SPDT 10 A 250 VAC, 12 V coil (verified LCSC C397244)"),
    "Q":   ("IRLZ44NPBF", "C38774", "Infineon IRLZ44NPBF, TO-220AB logic-level N-MOSFET (verified LCSC C38774)"),
    "U":   ("ULN2803A", "C73936", "ST ULN2803A, DIP-18 300 mil (verified LCSC C73936)"),
    "RG":  ("100R", "", "Axial resistor 100 R 1/4 W, 10.16 mm (0.4 in) lead spacing"),
    "RPD": ("100k", "", "Axial resistor 100 k 1/4 W, 10.16 mm lead spacing"),
    "RBZ": ("220R", "", "Axial resistor 220 R 1/4 W, 10.16 mm lead spacing"),
    "C1":  ("470uF 25V", "", "Radial electrolytic 470 uF >= 25 V, D10 mm, 5.0 mm pitch"),
    "C":   ("100nF 50V", "", "Radial ceramic 100 nF >= 50 V, 5.0 mm pitch"),
    "BZ":  ("Passive buzzer 12mm", "", "Passive buzzer/transducer 12 mm, 7.6 mm pitch (not an active/self-oscillating buzzer)"),
    "RV":  ("10k linear pot", "", "Alps RK09K horizontal 10 k LINEAR (B) or pin-compatible (3 pins 2.5 mm + 2 mounting tabs)"),
    "J2":  ("Screw terminal 2P 5.08mm", "", "Screw terminal 2-pole 5.08 mm, horizontal wire entry, >= 250 V / >= 10 A (e.g. Phoenix 1715721, KEFA KF128-5.08-2P)"),
    "J3":  ("Screw terminal 3P 5.08mm", "", "Screw terminal 3-pole 5.08 mm, horizontal wire entry, >= 250 V / >= 10 A (e.g. Phoenix 1715734, KEFA KF128-5.08-3P)"),
    "J4":  ("Screw terminal 4P 5.08mm", "", "Screw terminal 4-pole 5.08 mm, horizontal wire entry, >= 250 V / >= 10 A (e.g. Phoenix 1715747, KEFA KF128-5.08-4P)"),
    "J_SD": ("DM3AT-SF-PEJM5", "C114218", "Hirose DM3AT-SF-PEJM5 microSD push-push with card detect (verified LCSC C114218)"),
    "U3":  ("AMS1117-3.3", "C6186", "AMS1117-3.3 LDO, SOT-223 (verified LCSC C6186)"),
    "U4":  ("74LVC125AD", "C6057", "Nexperia 74LVC125AD,118 quad buffer, SOIC-14, 5 V tolerant inputs (verified LCSC C6057)"),
    "CIN": ("10uF 25V", "", "MLCC 10 uF >= 25 V X5R/X7R, 0805"),
    "COUT": ("22uF 10V", "", "MLCC 22 uF >= 10 V X5R/X7R, 0805"),
    "C100": ("100nF", "", "MLCC 100 nF >= 16 V X7R, 0805"),
    "RSD": ("10k", "", "Resistor 10 k 1 %, 0805"),
}
MPN = {"K": ("Omron", "G5Q-1 DC12"), "Q": ("Infineon", "IRLZ44NPBF"), "U": ("STMicroelectronics", "ULN2803A"),
       "J_SD": ("Hirose", "DM3AT-SF-PEJM5"), "U3": ("Advanced Monolithic Systems", "AMS1117-3.3"),
       "U4": ("Nexperia", "74LVC125AD,118"), "J2": ("Phoenix Contact", "1715721"), "J3": ("Phoenix Contact", "1715734"),
       "J4": ("Phoenix Contact", "1715747")}
def key(fp):
    r = fp.GetReference()
    if r in ("J_SD", "U3", "U4"): return r
    if r == "C4": return "CIN"
    if r == "C5": return "COUT"
    if r in ("C6", "C7"): return "C100"
    if r.startswith("RSD"): return "RSD"
    if r.startswith("J"): return f"J{len(list(fp.Pads()))}"
    if r == "C1": return "C1"
    for k in ("RPD", "RBZ", "RG", "BZ", "RV", "K", "Q", "U", "C"):
        if r.startswith(k): return k
    return None
groups = collections.OrderedDict()
cpl = []
for fp in sorted(b.GetFootprints(), key=lambda f: (re.sub(r"\d", "", f.GetReference()), int(re.sub(r"\D", "", f.GetReference()) or 0))):
    k = key(fp)
    if not k: continue          # MCU1 headers, mounting/tooling holes
    groups.setdefault(k, []).append(fp)
    # Mid X/Y = centre of the pad field (KiCad THT anchors sit on pin 1, JLC wants the part centre)
    xs = [q.GetPosition().x for q in fp.Pads()]; ys = [q.GetPosition().y for q in fp.Pads()]
    cx, cy = (min(xs) + max(xs)) / 2e6, (min(ys) + max(ys)) / 2e6
    cpl.append([fp.GetReference(), f"{cx:.4f}mm", f"{-cy:.4f}mm", "Top", f"{fp.GetOrientationDegrees() % 360:.0f}"])
with open(os.path.join(out, f"{N}-BOM-JLCPCB.csv"), "w", newline="") as f:
    w = csv.writer(f); w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #", "Quantity", "Required spec"])
    for k, fps in groups.items():
        c, lcsc, spec = SPEC[k]
        w.writerow([c, ",".join(x.GetReference() for x in fps), str(fps[0].GetFPID().GetLibItemName()), lcsc, len(fps), spec])
with open(os.path.join(out, f"{N}-CPL-JLCPCB.csv"), "w", newline="") as f:
    w = csv.writer(f); w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"]); w.writerows(cpl)
# PCBWay / generic turnkey BOM (manufacturer part numbers + assembly instructions)
SMD = {"J_SD", "U3", "U4", "CIN", "COUT", "C100", "RSD"}
NOTE = {"K": "Polarised footprint: pin 1 (square pad) = coil +12V. Contacts may carry 230 VAC.",
        "U": "Pin 1 notch as on silkscreen", "Q": "Metal tab towards the silkscreen tab line",
        "C1": "Polarised: + = square pad", "BZ": "+ marked pad = pin 1",
        "J_SD": "Push-push microSD at board edge, card opening towards the edge"}
with open(os.path.join(out, f"{N}-BOM-PCBWay.csv"), "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["Item #", "Designator", "Qty", "Manufacturer", "Mfg Part #", "Description / Value",
                "Package / Footprint", "Type", "Instructions"])
    item = 0
    for k, fps in groups.items():
        c, lcsc, spec = SPEC[k]; man, mpn = MPN.get(k, ("", ""))
        item += 1
        w.writerow([item, ",".join(x.GetReference() for x in fps), len(fps), man, mpn, spec,
                    str(fps[0].GetFPID().GetLibItemName()), "SMD" if k in SMD else "THT", NOTE.get(k, "")])
    item += 1
    w.writerow([item, "MCU1", 1, "any", "2.54 mm male pin headers: 1x10, 5x 1x8, 1x 2x18 (or Arduino Mega shield header set)",
                "Arduino Mega 2560 R3 shield headers", "Arduino_Mega2560_R3_Shield", "THT",
                "MOUNT ON THE BOTTOM SIDE: plastic body under the board, long pins pointing DOWN (they plug into the Arduino Mega), solder on the top side. Use an Arduino Mega as alignment jig."])
print("JLC BOM lines", len(groups), "CPL rows", len(cpl), "+ PCBWay BOM")
