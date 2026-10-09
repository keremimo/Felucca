#!/usr/bin/env python3
"""The redesign's reference screens: docs/design/ref/<screen>.svg (240 x 240, one per mockup screen, dumped from the
proposal's mockups in docs/design/mock/) -> docs/design/ref/<screen>.png at the device's pixels, in the bundled Rubik.

  tools/mock_refs.py [--chrome PATH]

Needs Google Chrome (headless) only to regenerate the PNGs; the PNGs are committed and tools/ui_mockcmp.py reads
them. A screen is 240 x 240 px: the mockups' viewBox, 1 unit = 1 device pixel."""
import argparse
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REF = os.path.join(ROOT, "docs", "design", "ref")
FONT = os.path.join(ROOT, "assets", "fonts", "Rubik[wght].ttf")
CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"

PAGE = """<!doctype html><html><head><meta charset="utf-8"><style>
@font-face {{ font-family: Rubik; src: url("file://{font}") format("truetype"); font-weight: 300 900; }}
html, body {{ margin: 0; padding: 0; width: 240px; height: 240px; overflow: hidden; background: #000; }}
svg {{ display: block; font-family: Rubik; }}
</style></head><body>{svg}</body></html>"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--chrome", default=CHROME)
    a = ap.parse_args()
    svgs = sorted(f for f in os.listdir(REF) if f.endswith(".svg"))
    if not svgs:
        sys.exit("mock_refs: no SVGs in docs/design/ref")
    with tempfile.TemporaryDirectory() as tmp:
        for f in svgs:
            svg = open(os.path.join(REF, f)).read()
            page = os.path.join(tmp, f + ".html")
            open(page, "w").write(PAGE.format(font=FONT.replace('"', "%22"), svg=svg))
            png = os.path.join(REF, f[:-4] + ".png")
            for attempt in range(3):                   # (headless Chrome now and then exits 2: again)
                rc = subprocess.run([a.chrome, "--headless=new", "--disable-gpu", "--hide-scrollbars",
                                     "--force-device-scale-factor=1", "--window-size=240,240",
                                     "--virtual-time-budget=2000", f"--screenshot={png}", "file://" + page],
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode
                if not rc:
                    break
            else:
                sys.exit(f"mock_refs: Chrome failed on {f}")
            print("mock_refs:", os.path.relpath(png, ROOT))


if __name__ == "__main__":
    main()
