#!/usr/bin/env python3
"""Mockup parity: each device screen (tests/ui_render.c's ref_* scenes, rendered by tests/ui_render.py) beside the
redesign's reference screen (docs/design/ref, tools/mock_refs.py), scored.

  tools/ui_mockcmp.py RENDER_DIR OUT_DIR [--palette NIGHT] [--min SCORE]

RENDER_DIR is ui_render's output (RENDER_DIR/<palette>/ref_<screen>.png). Writes OUT_DIR/<screen>.png (reference |
device | difference) and OUT_DIR/report.txt. Scores, 0..100:
  colour  the 8 x 8 blocks whose mean colour is within 32 (RGB distance) of the reference's: palette and areas
  layout  F1 of the edges (the device's against the reference's, 2 px apart allowed): where things are and their shapes
  parity  their mean. --min: exit 1 when a screen scores under it (a phase's gate)."""
import argparse
import os
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REF = os.path.join(ROOT, "docs", "design", "ref")
SCREENS = ["stage_held", "stage_released", "stage_cutoff", "stage_drum", "browser", "patterns", "song"]


def blocks(im, n=8):
    b = im.resize((im.width // n, im.height // n), Image.BOX).tobytes()
    return [tuple(b[i:i + 3]) for i in range(0, len(b), 3)]


def colour_score(a, b):
    ba, bb = blocks(a), blocks(b)
    ok = sum(1 for p, q in zip(ba, bb) if sum((x - y) ** 2 for x, y in zip(p, q)) <= 32 ** 2)
    return 100.0 * ok / len(ba)


def edges(im):
    g = im.convert("L").filter(ImageFilter.BoxBlur(1)).filter(ImageFilter.FIND_EDGES)
    return g.point(lambda v: 255 if v > 40 else 0)


def layout_score(a, b):
    ea, eb = edges(a), edges(b)
    da, db = ea.filter(ImageFilter.MaxFilter(5)), eb.filter(ImageFilter.MaxFilter(5))
    pa, pb, dpa, dpb = ea.tobytes(), eb.tobytes(), da.tobytes(), db.tobytes()
    na = sum(1 for v in pa if v)
    nb = sum(1 for v in pb if v)
    if not na or not nb:
        return 0.0
    recall = sum(1 for v, w in zip(pa, dpb) if v and w) / na      # the reference's edges the device has
    precision = sum(1 for v, w in zip(pb, dpa) if v and w) / nb   # the device's edges the reference has
    return 0.0 if not recall + precision else 200.0 * recall * precision / (recall + precision)


def sheet(ref, dev, path):
    diff = ImageChops.difference(ref, dev).convert("L").point(lambda v: min(255, v * 3))
    heat = Image.merge("RGB", (diff, Image.new("L", diff.size, 0), Image.new("L", diff.size, 0)))
    out = Image.new("RGB", (3 * 240 + 16, 240 + 18), (40, 40, 40))
    d = ImageDraw.Draw(out)
    for i, (im, label) in enumerate(((ref, "mockup"), (dev, "device"), (heat, "difference"))):
        out.paste(im, (i * 248, 18))
        d.text((i * 248 + 2, 3), label, fill=(230, 230, 230))
    out.save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("render_dir")
    ap.add_argument("out_dir")
    ap.add_argument("--palette", default="NIGHT")
    ap.add_argument("--min", type=float, default=0.0)
    a = ap.parse_args()
    os.makedirs(a.out_dir, exist_ok=True)
    lines, bad = ["screen            colour  layout  parity"], 0
    for s in SCREENS:
        rp, dp = os.path.join(REF, s + ".png"), os.path.join(a.render_dir, a.palette, "ref_" + s + ".png")
        if not os.path.exists(rp) or not os.path.exists(dp):
            lines.append(f"{s:16s}  missing {'reference' if not os.path.exists(rp) else 'device render'}")
            bad += 1
            continue
        ref, dev = Image.open(rp).convert("RGB"), Image.open(dp).convert("RGB")
        c, l = colour_score(ref, dev), layout_score(ref, dev)
        p = (c + l) / 2
        bad += p < a.min
        lines.append(f"{s:16s}  {c:6.1f}  {l:6.1f}  {p:6.1f}{'  < ' + str(a.min) if p < a.min else ''}")
        sheet(ref, dev, os.path.join(a.out_dir, s + ".png"))
    open(os.path.join(a.out_dir, "report.txt"), "w").write("\n".join(lines) + "\n")
    print("\n".join(lines))
    sys.exit(1 if bad and a.min else 0)


if __name__ == "__main__":
    main()
