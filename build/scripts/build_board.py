#!/usr/bin/env python3
"""PSN-Presepe Rev D - board generator (KiCad 7+ pcbnew API).

Rev D adds a push-push microSD slot (SPI on D50-D53, card-detect on D49) with
a 3.3 V regulator and 5 V -> 3.3 V level shifter, and replaces the on-board
potentiometer with a 3-pole terminal J_POT next to the push-button terminals.

Builds the unrouted board from:
  * revb_netlist.json  - pad -> net map extracted from the Rev-B release PCB
  * revb_pads.json     - Rev-B absolute pad positions (kept-in-place parts)
  * the placement table below (relay section re-placed for Rev D)

Rev D changes vs Rev B (see README):
  * Omron G5Q-1 now uses the official KiCad footprint (Rev B footprint was mirrored)
  * G5Q-1 contact mapping fixed: pad 3 = NO, pad 4 = NC (Rev B had them swapped)
  * ULN2803 now uses the standard DIP-18 300 mil footprint (Rev B used 400 mil rows)
  * relay section re-placed: every relay sits directly behind its own terminal
  * series resistor RBZ1 (220R) added between D6 and the buzzer

Usage: python3 build_board.py <out_dir>
"""
import json, math, os, sys
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "out"))
os.makedirs(OUT, exist_ok=True)
KILIB = os.environ.get("KICAD_FOOTPRINT_DIR", "/usr/share/kicad/footprints")
LOCAL = os.path.join(ROOT, "lib", "PSN_RevD.pretty")
NAME = "PSN-Presepe-Mega-RevD"

NET = json.load(open(os.path.join(ROOT, "data", "revb_netlist.json")))
VAL = json.load(open(os.path.join(ROOT, "data", "revb_values.json")))
PADPOS = json.load(open(os.path.join(ROOT, "data", "revb_pads.json")))

mm = pcbnew.FromMM

# ---------------------------------------------------------------- netlist fixes
# 1) G5Q-1 contacts: pad 3 = NO, pad 4 = NC (KiCad Relay.lib G5Q-1 / G5Q-1A).
for i in range(1, 17):
    k = NET[f"K{i}"]
    assert k["3"] == f"R{i}_NC" and k["4"] == f"R{i}_NO", k
    k["3"], k["4"] = f"R{i}_NO", f"R{i}_NC"
# 2) buzzer series resistor
assert NET["BZ1"]["1"] == "D6_BUZZER"
NET["BZ1"]["1"] = "BUZZER_DRV"
NET["RBZ1"] = {"1": "D6_BUZZER", "2": "BUZZER_DRV"}
VAL["RBZ1"] = "220R"
VAL["U1"] = VAL["U2"] = "ULN2803A"
VAL["BZ1"] = "BUZZER_PASSIVE_RM7.6"

# 3) Rev D: external potentiometer terminal replaces RV1 (same nets)
PADPOS.pop("RV1", None)
assert NET.pop("RV1") == {"1": "GND", "2": "A0_POT", "3": "+5V_MEGA"}
VAL.pop("RV1", None)
NET["J_POT"] = {"1": "+5V_MEGA", "2": "A0_POT", "3": "GND"}
# 4) Rev D: microSD (Hirose DM3AT push-push) + AMS1117-3.3 + 74LVC125A
for pin in ("D49", "D50", "D51", "D52", "D53"):
    assert pin not in NET["MCU1"], pin
NET["MCU1"].update({"D49": "D49_SD_CD", "D50": "D50_SD_MISO", "D51": "D51_SD_MOSI",
                    "D52": "D52_SD_SCK", "D53": "D53_SD_CS"})
NET["U3"] = {"1": "GND", "2": "+3V3", "3": "+12V"}                      # AMS1117-3.3 (tab = pin 2)
NET["C4"] = {"1": "+12V", "2": "GND"}                                   # 10 uF regulator input
NET["C5"] = {"1": "+3V3", "2": "GND"}                                   # 22 uF regulator output
NET["C6"] = {"1": "+3V3", "2": "GND"}                                   # 100 nF at the card VDD
NET["C7"] = {"1": "+3V3", "2": "GND"}                                   # 100 nF at U4 VCC
NET["U4"] = {"1": "GND", "2": "D52_SD_SCK", "3": "SD_CLK",              # 1OE 1A 1Y
             "4": "GND", "5": "D51_SD_MOSI", "6": "SD_CMD",             # 2OE 2A 2Y
             "7": "GND", "8": "SD_CS", "9": "D53_SD_CS", "10": "GND",   # GND 3Y 3A 3OE
             "12": "GND", "13": "+3V3", "14": "+3V3"}                   # 4A 4OE(off) VCC; 4Y n.c.
NET["J_SD"] = {"1": "SD_DAT2", "2": "SD_CS", "3": "SD_CMD", "4": "+3V3", "5": "SD_CLK",
               "6": "GND", "7": "D50_SD_MISO", "8": "SD_DAT1", "9": "D49_SD_CD", "10": "GND", "11": "GND"}
NET["RSD1"] = {"1": "+3V3", "2": "SD_CS"}
NET["RSD2"] = {"1": "+3V3", "2": "D50_SD_MISO"}
NET["RSD3"] = {"1": "+3V3", "2": "SD_DAT1"}
NET["RSD4"] = {"1": "+3V3", "2": "SD_DAT2"}
VAL.update({"J_POT": "POT_5V_A0_GND", "U3": "AMS1117-3.3", "C4": "10uF_25V", "C5": "22uF_10V",
            "C6": "100nF", "C7": "100nF", "U4": "74LVC125A", "J_SD": "DM3AT-SF-PEJM5",
            "RSD1": "10k", "RSD2": "10k", "RSD3": "10k", "RSD4": "10k"})

# ---------------------------------------------------------------- footprints
LIB = {
    "MCU1": (LOCAL, "Arduino_Mega2560_R3_Shield"),
    "U": (KILIB + "/Package_DIP.pretty", "DIP-18_W7.62mm"),
    "K": (KILIB + "/Relay_THT.pretty", "Relay_SPDT_Omron-G5Q-1"),
    "Q": (KILIB + "/Package_TO_SOT_THT.pretty", "TO-220-3_Vertical"),
    "R": (KILIB + "/Resistor_THT.pretty", "R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal"),
    "C1": (KILIB + "/Capacitor_THT.pretty", "C_Radial_D10.0mm_H12.5mm_P5.00mm"),
    "C": (KILIB + "/Capacitor_THT.pretty", "C_Disc_D5.0mm_W2.5mm_P5.00mm"),
    "BZ1": (KILIB + "/Buzzer_Beeper.pretty", "Buzzer_12x9.5RM7.6"),
    "RV1": (KILIB + "/Potentiometer_THT.pretty", "Potentiometer_Alps_RK09K_Single_Horizontal"),
    "H": (KILIB + "/MountingHole.pretty", "MountingHole_3.2mm_M3"),
    "TH": (LOCAL, "ToolingHole_JLC_1.152mm_NPTH"),
    "J_SD": (KILIB + "/Connector_Card.pretty", "microSD_HC_Hirose_DM3AT-SF-PEJM5"),
    "U3": (KILIB + "/Package_TO_SOT_SMD.pretty", "SOT-223-3_TabPin2"),
    "U4": (KILIB + "/Package_SO.pretty", "SOIC-14_3.9x8.7mm_P1.27mm"),
    "CSMD": (KILIB + "/Capacitor_SMD.pretty", "C_0805_2012Metric"),
    "RSD": (KILIB + "/Resistor_SMD.pretty", "R_0805_2012Metric"),
}
def term(n):
    return (KILIB + "/TerminalBlock_Phoenix.pretty",
            f"TerminalBlock_Phoenix_MKDS-1,5-{n}-5.08_1x{n:02d}_P5.08mm_Horizontal")

def lib_for(ref):
    if ref in LIB: return LIB[ref]
    if ref in ("C4", "C5", "C6", "C7"): return LIB["CSMD"]
    if ref.startswith("RSD"): return LIB["RSD"]
    if ref.startswith("JR"): return term(3)
    if ref.startswith("J"): return term(len(NET[ref]))
    if ref.startswith(("RG", "RPD", "RBZ")): return LIB["R"]
    return LIB[ref.rstrip("0123456789")]

# ---------------------------------------------------------------- placement
# Relay section geometry (all mm). See README "Relay section".
P = 19.3            # relay / terminal pitch (both rows)
TOP_X0 = 140.2      # K1 pad-1 x
TOP_YC = 53.0       # top row coil-pad y
TOP_YT = 25.5       # top row terminal pad y   (wire entry towards y=20 edge)
COL_X0 = 289.0      # column relays pad-1 x
COL_Y0 = 45.0       # K9 pad-1 y
COL_T = 314.8       # column terminal pad x    (wire entry towards x=320 edge)

PLACE = {}  # ref -> (x, y, rot)
for i in range(8):
    x0 = TOP_X0 + P * i
    PLACE[f"K{i+1}"] = (x0, TOP_YC, 90)
    PLACE[f"JR{i+1}"] = (x0 + 2.54, TOP_YT, 180)
for j in range(8):
    y0 = COL_Y0 + P * j
    PLACE[f"K{j+9}"] = (COL_X0, y0, 0)
    PLACE[f"JR{j+9}"] = (COL_T, y0 + 2.54, 90)
PLACE.update({
    "U1": (133.0, 62.0, 0), "U2": (133.0, 92.0, 0),
    "C2": (133.3, 86.0, 0), "C3": (133.3, 116.5, 0), "C1": (122.0, 121.0, 0),
    "H1": (26.0, 26.0, 0), "H2": (121.0, 26.0, 0), "H3": (100.0, 182.0, 0),
    "H4": (250.0, 192.0, 0), "H5": (210.0, 120.0, 0), "H6": (135.0, 170.0, 0),
    # JLCPCB PCBA tooling holes (1.152 mm NPTH), opposite corners, far from relay contacts
    "TH1": (22.5, 185.0, 0), "TH2": (316.0, 196.0, 0), "TH3": (40.0, 23.0, 0),
    # buzzer + series resistor in the free left area
    "BZ1": (26.0, 160.0, 0), "RBZ1": (25.0, 172.0, 0),
    # Rev D bottom-edge terminals: J_POT right after TEST, strip terminals shifted +12.5 mm
    "J_POT": (103.0, 194.0, 0), "J_CIELO": (120.5, 194.0, 0), "J_TRAMONTO": (144.5, 194.0, 0),
    "J_ALBA": (168.5, 194.0, 0), "J_STELLE": (192.5, 194.0, 0), "J_CASETTE": (210.5, 194.0, 0),
    # Rev D microSD block (top edge, above the Mega, far from the relay contacts).
    # Card inserts from the y = 20 edge (socket rotated 180 deg, front flush with the edge).
    "J_SD": (95.0, 29.0, 180),
    "U4": (110.5, 29.0, 0), "C7": (110.5, 36.0, 0),
    "U3": (75.0, 29.0, 0), "C4": (68.0, 34.5, 90), "C5": (79.0, 35.0, 0),
    "C6": (84.0, 23.5, 0),
    "RSD1": (84.0, 26.0, 0), "RSD2": (84.0, 28.5, 0), "RSD3": (84.0, 31.0, 0), "RSD4": (84.0, 33.5, 0),
})
NEW_PARTS = {}
# MOSFET block: 3 x 3 cells, each = RG (top) + RPD (bottom) left of the TO-220
for n in range(9):
    c, rw = n % 3, n // 3
    qx, qy = 56.2 + 28 * c, 135.0 + 15 * rw
    PLACE[f"Q{n+1}"] = (qx, qy, 0)
    PLACE[f"RG{n+1}"] = (qx - 14.2, qy - 1.6, 0)
    PLACE[f"RPD{n+1}"] = (qx - 14.2, qy + 1.6, 0)

def load(ref):
    lp, name = lib_for(ref)
    fp = pcbnew.FootprintLoad(lp, name)
    if fp is None: raise SystemExit(f"cannot load {lp}:{name} for {ref}")
    return fp

def pad_local(fp):
    return {p.GetNumber(): (p.GetPosition().x / 1e6, p.GetPosition().y / 1e6)
            for p in fp.Pads() if p.GetNumber()}

def fit_to_revb(fp, ref):
    """Rotate/translate a library footprint so its numbered pads land on the
    Rev-B absolute pad positions (parts that are not moved in Rev D)."""
    target = PADPOS[ref]
    best = None
    for rot in (0, 90, 180, 270):
        fp.SetOrientationDegrees(rot); fp.SetPosition(pcbnew.VECTOR2I(0, 0))
        loc = pad_local(fp)
        common = [n for n in target if n in loc]
        if not common: continue
        n0 = common[0]
        dx = target[n0][0] - loc[n0][0]; dy = target[n0][1] - loc[n0][1]
        err = max(math.hypot(loc[n][0] + dx - target[n][0], loc[n][1] + dy - target[n][1]) for n in common)
        if best is None or err < best[0]: best = (err, rot, dx, dy)
    err, rot, dx, dy = best
    fp.SetOrientationDegrees(rot); fp.SetPosition(pcbnew.VECTOR2I(mm(dx), mm(dy)))
    return err

board = pcbnew.BOARD()
board.SetCopperLayerCount(2)
nets = {}
def net(name):
    if name not in nets:
        ni = pcbnew.NETINFO_ITEM(board, name); board.Add(ni); nets[name] = ni
    return nets[name]

refs = sorted(set(NET) | set(PADPOS) | set(PLACE) | set(NEW_PARTS))
report = []
for ref in refs:
    fp = load(ref)
    fp.SetReference(ref)
    fp.SetValue(VAL.get(ref, fp.GetValue()))
    board.Add(fp)
    if ref in PLACE:
        x, y, r = PLACE[ref]
        fp.SetOrientationDegrees(r); fp.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y)))
    elif ref in NEW_PARTS:
        x, y, r = NEW_PARTS[ref]
        fp.SetOrientationDegrees(r); fp.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y)))
    else:
        err = fit_to_revb(fp, ref)
        if err > 0.05:
            report.append(f"WARN {ref}: library pads differ from Rev-B by {err:.3f} mm")
    for p in fp.Pads():
        n = NET.get(ref, {}).get(p.GetNumber())
        if n: p.SetNet(net(n))
    # reference designators: terminals/holes use the generated labels instead
    if ref.startswith(("H", "J", "TH")):
        fp.Reference().SetLayer(pcbnew.F_Fab)
    elif ref in ("C4", "C5", "C6", "C7") or ref.startswith("RSD"):
        fp.Reference().SetLayer(pcbnew.F_Fab)          # 0805 parts: refdes on the assembly drawing
    elif ref.startswith(("RG", "RPD", "RBZ", "C2", "C3")):
        ps = [p.GetPosition() for p in fp.Pads()]
        fp.Reference().SetPosition(pcbnew.VECTOR2I((ps[0].x + ps[1].x) // 2, (ps[0].y + ps[1].y) // 2))
        fp.Reference().SetTextSize(pcbnew.VECTOR2I(mm(0.8), mm(0.8))); fp.Reference().SetTextThickness(mm(0.12))
        fp.Reference().SetTextAngleDegrees(0)

# verify every netlist entry landed on a pad
for ref, pads in NET.items():
    fp = board.FindFootprintByReference(ref)
    have = {p.GetNumber() for p in fp.Pads()}
    miss = [n for n in pads if n not in have]
    if miss: raise SystemExit(f"{ref}: netlist pads {miss} not in footprint")

# ---------------------------------------------------------------- outline
def seg(layer, a, b, w=0.15):
    s = pcbnew.PCB_SHAPE(board); s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(pcbnew.VECTOR2I(mm(a[0]), mm(a[1]))); s.SetEnd(pcbnew.VECTOR2I(mm(b[0]), mm(b[1])))
    s.SetLayer(layer); s.SetWidth(mm(w)); board.Add(s); return s
X0, Y0, X1, Y1 = 20, 20, 320, 200
for a, b in (((X0, Y0), (X1, Y0)), ((X1, Y0), (X1, Y1)), ((X1, Y1), (X0, Y1)), ((X0, Y1), (X0, Y0))):
    seg(pcbnew.Edge_Cuts, a, b, 0.1)

# ---------------------------------------------------------------- silkscreen
def text(s, x, y, size=1.0, rot=0, layer=pcbnew.F_SilkS, thick=None):
    t = pcbnew.PCB_TEXT(board); t.SetText(s)
    t.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y))); t.SetLayer(layer)
    t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
    t.SetTextThickness(mm(thick if thick else max(0.12, size * 0.15)))
    t.SetTextAngleDegrees(rot); board.Add(t); return t

def padpos(ref, num):
    fp = board.FindFootprintByReference(ref)
    for p in fp.Pads():
        if p.GetNumber() == num: return p.GetPosition().x / 1e6, p.GetPosition().y / 1e6

LABELS = {
    "J1": ["12V", "GND"], "J_OLED": ["5V", "GND", "SDA", "SCL"],
    "J_START": ["START", "GND"], "J_NEXT": ["NEXT", "GND"], "J_TEST": ["TEST", "GND"],
    "J_CIELO": ["12V", "R", "G", "B"], "J_TRAMONTO": ["12V", "R", "G", "B"],
    "J_ALBA": ["12V", "R", "G", "B"], "J_STELLE": ["12V", "GND", "DATA"],
    "J_CASETTE": ["12V", "GND", "DATA"], "J_POT": ["5V", "A0", "GND"],
}
TITLE = {"J1": "POWER 12V", "J_OLED": "OLED", "J_START": "START", "J_NEXT": "NEXT",
         "J_TEST": "TEST", "J_CIELO": "CIELO", "J_TRAMONTO": "TRAMONTO", "J_ALBA": "ALBA",
         "J_STELLE": "STELLE", "J_CASETTE": "CASETTE", "J_POT": "POT 10k"}
for ref, labs in LABELS.items():
    for k, s in enumerate(labs, 1):
        x, y = padpos(ref, str(k)); text(s, x, y - 7.3, 0.9)
    x1, y1 = padpos(ref, "1"); xn, _ = padpos(ref, str(len(labs)))
    text(TITLE[ref], (x1 + xn) / 2, y1 - 9.0, 1.0)
for i in range(1, 17):
    ref = f"JR{i}"
    if i <= 8:
        for num, s_ in (("1", "COM"), ("2", "NO"), ("3", "NC")):
            x, y = padpos(ref, num); text(s_, x, y + 6.5, 0.8)
        x, y = padpos(ref, "3"); text(f"R{i}", x - 3.6, y + 6.5, 0.9)
    else:
        x, y = padpos(ref, "1")
        text(f"R{i}: NC/NO/COM", 314.6, y + 4.6, 0.8)
    for num, s_ in (("1", "COM"), ("2", "NO"), ("3", "NC")):   # back-side labels behind each pad
        x, y = padpos(ref, num)
        t = text(s_, x, y + (3.2 if i <= 8 else 0), 0.8, layer=pcbnew.B_SilkS, rot=0 if i <= 8 else 90)
        if i > 8: t.SetPosition(pcbnew.VECTOR2I(mm(x - 2.8), mm(y)))
        t.SetMirrored(True)
text("!  RELAY CONTACTS MAY CARRY 230 V AC  -  SELV KEEP-OUT 6 mm  !", 222, 160, 1.2)
text("PSN-Presepe Rev D", 222, 145, 2.0)
text("microSD", 95, 39.2, 0.9)
text("MOSFET / HEATSINK AIRFLOW ZONE", 85.5, 127.6, 0.9)


# ---------------------------------------------------------------- save
out_pcb = os.path.join(OUT, NAME + ".kicad_pcb")
pcbnew.SaveBoard(out_pcb, board)
print("saved", out_pcb, "footprints", len(board.GetFootprints()), "nets", len(nets))
for r in report: print(r)
