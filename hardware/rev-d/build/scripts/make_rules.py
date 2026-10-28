#!/usr/bin/env python3
"""Writes <name>.kicad_pro (net classes) and <name>.kicad_dru (isolation rules).

Usage: make_rules.py <out_dir> <basename> [--control]
--control=selv|inter|intra|edge forces one rule to an impossible value used by the positive-control
test (the DRC must then report violations, proving the rule engine is active).
"""
import json, os, sys

out, name = sys.argv[1], sys.argv[2]

# ----------------------------------------------------------------- net classes
CLASSES = [
    # name, track, clearance, via_d, via_drill
    ("Default", 0.25, 0.20, 0.6, 0.3),
    ("POWER",   1.00, 0.30, 1.0, 0.5),   # +12V, GND branches (trunks are pre-routed at 3 mm)
    ("P5V",     0.80, 0.25, 0.8, 0.4),   # +5V_MEGA
    ("P3V3",    0.40, 0.20, 0.6, 0.3),   # +3V3 microSD supply (< 150 mA)
    ("RGB",     1.50, 0.30, 1.0, 0.5),   # MOSFET drains to RGB terminals (~1.7 A/ch)
    ("COIL",    0.40, 0.20, 0.6, 0.3),   # ULN2803 -> relay coil (33 mA)
    ("MAINS",   2.00, 2.00, 1.0, 0.5),   # relay contacts (pre-routed, never autorouted)
]
PATTERNS = [("POWER", "+12V"), ("POWER", "GND"), ("P5V", "+5V_MEGA"), ("P3V3", "+3V3"), ("RGB", "*_NEG"),
            ("COIL", "RELAY*_COIL_LOW"), ("MAINS", "R*_COM"), ("MAINS", "R*_NO"), ("MAINS", "R*_NC")]

def cls(n, w, c, vd, vh):
    return {"name": n, "clearance": c, "track_width": w, "via_diameter": vd, "via_drill": vh,
            "microvia_diameter": 0.3, "microvia_drill": 0.1, "diff_pair_width": 0.2,
            "diff_pair_gap": 0.25, "diff_pair_via_gap": 0.25, "wire_width": 6, "bus_width": 12,
            "line_style": 0, "schematic_color": "rgba(0, 0, 0, 0.000)", "pcb_color": "rgba(0, 0, 0, 0.000)"}

pro = {
    "board": {
        "3dviewports": [],
        "design_settings": {
            "defaults": {"board_outline_line_width": 0.1, "copper_line_width": 0.2,
                         "copper_text_size_h": 1.5, "copper_text_size_v": 1.5, "copper_text_thickness": 0.3,
                         "silk_line_width": 0.12, "silk_text_size_h": 1.0, "silk_text_size_v": 1.0,
                         "silk_text_thickness": 0.15},
            "rules": {"min_clearance": 0.2, "min_track_width": 0.2, "min_via_annular_width": 0.13,
                      "min_via_diameter": 0.6, "min_through_hole_diameter": 0.3, "min_hole_clearance": 0.25,
                      "min_hole_to_hole": 0.25, "min_copper_edge_clearance": 0.5, "min_silk_clearance": 0.0,
                      "solder_mask_to_copper_clearance": 0.0, "min_microvia_diameter": 0.2,
                      "min_microvia_drill": 0.1, "max_error": 0.005, "use_height_for_length_calcs": True,
                      "allow_blind_buried_vias": False, "allow_microvias": False},
            "rule_severities": {"lib_footprint_issues": "ignore", "lib_footprint_mismatch": "ignore",
                                "silk_overlap": "warning", "silk_over_copper": "warning",
                                "text_height": "warning", "text_thickness": "warning",
                                "courtyards_overlap": "error", "missing_courtyard": "ignore",
                                "unconnected_items": "error", "clearance": "error",
                                "copper_edge_clearance": "error", "hole_clearance": "error",
                                "track_dangling": "warning", "via_dangling": "warning",
                                "starved_thermal": "error", "footprint_type_mismatch": "ignore"},
            "track_widths": [0.25, 0.4, 0.8, 1.5, 2.0, 3.0],
            "via_dimensions": [{"diameter": 0.6, "drill": 0.3}, {"diameter": 1.0, "drill": 0.5}],
        },
    },
    "meta": {"filename": name + ".kicad_pro", "version": 1},
    "net_settings": {"classes": [cls(*c) for c in CLASSES], "meta": {"version": 3},
                     "net_colors": None,
                     "netclass_assignments": None,
                     "netclass_patterns": [{"netclass": c, "pattern": p} for c, p in PATTERNS]},
    "pcbnew": {"last_paths": {}, "page_layout_descr_file": ""},
    "sheets": [], "text_variables": {},
}
json.dump(pro, open(os.path.join(out, name + ".kicad_pro"), "w"), indent=2)

# ----------------------------------------------------------------- custom rules
# KiCad custom rules support only wildcards ('*', '?') in string compares - no
# regular expressions (Rev A/B used '=~' regex conditions that never matched).
# Later rules take precedence over earlier ones.
SELV = 6.0      # relay contacts <-> everything else (reinforced, 230 V, PD2)
INTER = 5.0     # contacts of different relays (2x basic creepage for 250 V, PD2)
INTRA = 2.0     # COM/NO/NC of the same relay (terminal block itself: 2.48 mm)
EDGE = 3.0      # relay contacts <-> board edge
for a in sys.argv:   # positive-control modes: force ONE rule to an impossible value
    if a == "--control=selv": SELV = 60.0
    if a == "--control=inter": INTER = 50.0
    if a == "--control=intra": INTRA = 4.9
    if a == "--control=edge": EDGE = 30.0

r = ["(version 1)", ""]
r += [f'(rule "MAINS to SELV {SELV}mm"',
      "  (layer outer)",
      "  (condition \"(A.NetClass == 'MAINS' && B.NetClass != 'MAINS') || (B.NetClass == 'MAINS' && A.NetClass != 'MAINS')\")",
      f"  (constraint clearance (min {SELV}mm)))", ""]
r += [f'(rule "MAINS inter-relay {INTER}mm"',
      "  (layer outer)",
      "  (condition \"A.NetClass == 'MAINS' && B.NetClass == 'MAINS'\")",
      f"  (constraint clearance (min {INTER}mm)))", ""]
for i in range(1, 17):
    r += [f'(rule "MAINS intra-relay R{i} {INTRA}mm"',
          "  (layer outer)",
          f"  (condition \"A.NetName == 'R{i}_*' && B.NetName == 'R{i}_*'\")",
          f"  (constraint clearance (min {INTRA}mm)))", ""]
r += [f'(rule "MAINS to board edge {EDGE}mm"',
      "  (condition \"A.NetClass == 'MAINS'\")",
      f"  (constraint edge_clearance (min {EDGE}mm)))", ""]
r += [f'(rule "MAINS to non-MAINS holes {SELV}mm"',
      "  (condition \"A.NetClass == 'MAINS' && B.NetClass != 'MAINS'\")",
      f"  (constraint hole_clearance (min {SELV}mm)))", ""]
open(os.path.join(out, name + ".kicad_dru"), "w").write("\n".join(r))
print("rules written:", name)
