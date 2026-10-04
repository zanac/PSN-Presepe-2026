#!/usr/bin/env python3
"""render_gerbers.py <gerber_dir> <out_prefix>
Photo-style render of the fabrication files themselves (what the PCB fab sees):
<out_prefix>-gerber-render-top.png and -bottom.png (needs gerbonara + rsvg-convert)."""
import os, subprocess, sys, tempfile, warnings
from gerbonara import LayerStack
warnings.simplefilter("ignore")
gdir, prefix = sys.argv[1:3]
stack = LayerStack.open(gdir)
for side in ("top", "bottom"):
    with tempfile.NamedTemporaryFile("w", suffix=".svg", delete=False) as f:
        f.write(str(stack.to_pretty_svg(side=side)))
    subprocess.run(["rsvg-convert", "-b", "white", "-w", "3000", f.name, "-o", f"{prefix}-gerber-render-{side}.png"], check=True)
    os.unlink(f.name)
print("gerber renders written:", prefix + "-gerber-render-{top,bottom}.png")
