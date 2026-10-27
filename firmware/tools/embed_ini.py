#!/usr/bin/env python3
"""Embed firmware/PSN-Presepe/PRESEPE.INI into PSN-Presepe.ino as INI_PREDEFINITO (PROGMEM).

At power-on, when the microSD has neither PRESEPE.INI nor PRESEPE.TXT, the firmware
writes this text to the card as PRESEPE.INI (iniCreaPredefinito), so the user starts
from a complete file with every key and its explanation.

The text goes between the "// ==== INI PREDEFINITO BEGIN ====" and "END" markers of
the sketch, as ordinary string literals (one per line: no raw strings, which confuse
the Arduino prototype generator). A short header is added on top of the file to say
that the board created it.

Usage: python3 firmware/tools/embed_ini.py [--check]
Run it after every change to PRESEPE.INI and commit the sketch.
--check only compares: exit status 1 if the sketch is out of date (used by run_tests.sh / CI).
"""
import pathlib, sys

root = pathlib.Path(__file__).resolve().parents[2]
INO = root / "firmware/PSN-Presepe/PSN-Presepe.ino"
INI = root / "firmware/PSN-Presepe/PRESEPE.INI"
BEGIN = "// ==== INI PREDEFINITO BEGIN ===="
END = "// ==== INI PREDEFINITO END ===="

HEADER = (
    "; -----------------------------------------------------------------------------\n"
    "; Questo file l'ha scritto la centralina, perche' sulla microSD non c'era.\n"
    "; Contiene i valori predefiniti e la spiegazione di ogni parametro.\n"
    "; Modificalo dal computer con un editor di testo (anche Blocco note), salva e\n"
    "; riavvia la centralina. Con la scheda Mega + WiFi: imposta [WIFI] in fondo al file,\n"
    "; poi lo puoi modificare anche dal telefono, su http://presepe.local/config\n"
    "; -----------------------------------------------------------------------------\n"
)


def c_line(s: str) -> str:
    out = []
    for ch in s:
        if ch == "\\": out.append("\\\\")
        elif ch == '"': out.append('\\"')
        elif ch == "\n": out.append("\\n")
        elif ch == "\t": out.append("\\t")
        elif 32 <= ord(ch) < 127: out.append(ch)
        else: raise SystemExit(f"PRESEPE.INI: non-ASCII character {ch!r}; keep the file plain ASCII")
    return '  "' + "".join(out) + '"'


text = HEADER + INI.read_text(encoding="ascii").replace("\r", "")
if not text.endswith("\n"):
    text += "\n"
lines = text.splitlines(keepends=True)
block = "\n".join([
    BEGIN,
    "// GENERATED from PRESEPE.INI by firmware/tools/embed_ini.py - do not edit by hand.",
    "// Scritto sulla microSD come PRESEPE.INI quando manca (vedi iniCreaPredefinito).",
    "const char INI_PREDEFINITO[] PROGMEM =",
    "\n".join(c_line(l) for l in lines) + ";",
    END,
])

src = INO.read_text(encoding="utf-8")
a, b = src.find(BEGIN), src.find(END)
if a < 0 or b < 0 or b < a:
    raise SystemExit("INI PREDEFINITO BEGIN/END markers not found in PSN-Presepe.ino")
new = src[:a] + block + src[b + len(END):]

if "--check" in sys.argv:
    if new != src:
        raise SystemExit("PSN-Presepe.ino: INI_PREDEFINITO is out of date: run python3 firmware/tools/embed_ini.py")
    print(f"INI_PREDEFINITO up to date ({len(text)} bytes)")
else:
    INO.write_text(new, encoding="utf-8")
    print(f"embedded PRESEPE.INI into PSN-Presepe.ino: {len(text)} bytes, {len(lines)} lines")
