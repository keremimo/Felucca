#!/usr/bin/env python3
"""The mockups' screens as reference SVGs: runs a mockup page (docs/design/mock/*.html, its script builds each screen's
<svg>) in headless Chrome, takes the screens named on the command line and writes each as docs/design/ref/<name>.svg
(240 x 240, the device's pixels); tools/mock_refs.py then renders them in the bundled Rubik.

  tools/mock_svgs.py docs/design/mock/r5_pages.html env=p_env lfo=p_lfo .. [--chrome PATH]"""
import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REF = os.path.join(ROOT, "docs", "design", "ref")
CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mock")
    ap.add_argument("screens", nargs="+", help="name=svg id")
    ap.add_argument("--chrome", default=CHROME)
    a = ap.parse_args()
    for attempt in range(3):                            # (headless Chrome now and then exits 2: again)
        p = subprocess.run([a.chrome, "--headless=new", "--disable-gpu", "--virtual-time-budget=2000", "--dump-dom",
                            "file://" + os.path.abspath(a.mock)], capture_output=True, text=True)
        if not p.returncode:
            break
    else:
        sys.exit("mock_svgs: Chrome failed")
    for pair in a.screens:
        name, sid = pair.split("=", 1)
        m = re.search(r'<svg viewBox="0 0 240 240" id="%s">(.*?)</svg>' % re.escape(sid), p.stdout, re.S)
        if not m:
            sys.exit(f"mock_svgs: no <svg id={sid}> in {a.mock}")
        svg = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 240 240" width="240" height="240">' + m.group(1) +
               "</svg>")
        open(os.path.join(REF, name + ".svg"), "w").write(svg)
        print("mock_svgs:", os.path.relpath(os.path.join(REF, name + ".svg"), ROOT))


if __name__ == "__main__":
    main()
