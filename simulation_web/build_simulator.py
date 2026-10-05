#!/usr/bin/env python3
"""Build simulation_web/index.html from the template and the default PRESEPE.INI.

Usage: python3 simulation_web/build_simulator.py [--ini PATH] [--out PATH]
Run it after every change to firmware/PSN-Presepe/PRESEPE.INI so the
simulator's "Ripristina il file predefinito" button matches the repo.
"""
import argparse, json, pathlib

here = pathlib.Path(__file__).resolve().parent
root = here.parent
ap = argparse.ArgumentParser()
ap.add_argument("--ini", default=root / "firmware/PSN-Presepe/PRESEPE.INI")
ap.add_argument("--template", default=here / "simulator.template.html")
ap.add_argument("--out", default=here / "index.html")
a = ap.parse_args()

ini = pathlib.Path(a.ini).read_text(encoding="utf-8")
if "</script" in ini.lower():
    raise SystemExit("INI contains '</script', cannot embed it")
tpl = pathlib.Path(a.template).read_text(encoding="utf-8")
if tpl.count("__INI__") != 1:
    raise SystemExit("template must contain exactly one __INI__ placeholder")
body = tpl.replace("__INI__", json.dumps(ini))
page = ('<!doctype html>\n<html lang="it">\n<head>\n<meta charset="utf-8">\n'
        '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n'
        '<style>html,body{margin:0}[hidden]{display:none!important}</style>\n'
        '</head>\n<body>\n' + body + '\n</body>\n</html>\n')
pathlib.Path(a.out).write_text(page, encoding="utf-8")
print(f"wrote {a.out} ({len(page)} bytes) from {a.ini}")
