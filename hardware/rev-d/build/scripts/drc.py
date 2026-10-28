#!/usr/bin/env python3
"""drc.py <board.kicad_pcb> <report.txt> - KiCad DRC (project rules + .kicad_dru) via pcbnew."""
import sys, pcbnew
b = pcbnew.LoadBoard(sys.argv[1])
ok = pcbnew.WriteDRCReport(b, sys.argv[2], pcbnew.EDA_UNITS_MILLIMETRES, True)
print("DRC report written" if ok else "DRC FAILED TO RUN", sys.argv[2])
