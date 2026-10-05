#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""FM6 against Dexed, sample by sample (tests/fm6_parity.sh builds the two renderers).

Scores (tests/fm6_score.h) of the factory voices and random DX7 voices, in the three engines,
with chords, repeats, releases, velocities, pitch bend, the four controllers, portamento and
mono; each one renders through Dexed's code (dexed_ref) and through FM6 (fm6_parity), and the
voice sums (Q24, before either output stage) are compared. Bit-exact is the goal; where the
renders differ the error is reported in dB under the signal.
  fm6_parity.py DEXED_REF FM6_PARITY WORKDIR [--quick] [--seed=N]"""
import math
import random
import struct
import subprocess
import sys
from pathlib import Path


def rand_voice(r):
    v = []
    for _ in range(6):
        v += [r.randint(0, 99) for _ in range(4)]                        # rates
        v += [r.choice([99, r.randint(0, 99)]), r.randint(0, 99), r.randint(0, 99), r.choice([0, 0, r.randint(0, 99)])]
        v += [r.randint(0, 99), r.randint(0, 99), r.randint(0, 99), r.randint(0, 3), r.randint(0, 3)]
        v += [r.randint(0, 7), r.randint(0, 3), r.randint(0, 7), r.randint(40, 99)]
        v += [r.choice([0, 0, 0, 1]), r.randint(0, 31), r.choice([0, r.randint(0, 99)]), r.randint(0, 14)]
    v += [r.randint(0, 99) for _ in range(4)]                            # pitch EG rates
    v += [r.choice([50, r.randint(0, 99)]) for _ in range(4)]            # pitch EG levels
    v += [r.randint(0, 31), r.randint(0, 7), r.randint(0, 1)]            # ALG FB OKS
    v += [r.randint(0, 99), r.randint(0, 99), r.randint(0, 99), r.choice([0, r.randint(0, 99)]), r.randint(0, 1),
          r.randint(0, 5), r.randint(0, 7)]                              # LFO
    v += [r.choice([24, 24, r.randint(12, 36)])]
    v += [ord(c) for c in "RANDOM    "]
    return v


def playing(r, n_blocks):
    """notes: chords, overlaps, a note again once released, quick and long ones (a key is not
    pressed twice without its release between: a keyboard cannot, and FM6's MIDI input releases a
    repeated note first)"""
    ev, b, held = [], 0, {}
    while b < n_blocks - 200:
        chord = [k for k in r.sample(range(36, 90), r.randint(1, 5)) if held.get(k, -1) < b]
        if r.random() < 0.3:
            chord = chord[:1]
        hold = r.choice([3, 20, 60, 150, 400])
        for k in chord:
            off = b + hold + r.randint(0, 5)
            ev.append((b, 0, f"on {k} {r.randint(1, 127)}"))
            ev.append((off, 1, f"off {k}"))
            held[k] = off
        b += r.choice([2, hold // 2, hold, hold + 100])
    return [(e[0], e[2]) for e in sorted(ev, key=lambda e: (e[0], e[1]))]


def score(voice, engine, ev, n_blocks, extra=()):
    lines = [f"engine {engine}", "voice " + " ".join(map(str, voice)), f"len {n_blocks}"]
    lines += list(extra)
    lines += [f"at {b} {e}" for b, e in sorted(ev, key=lambda e: e[0])]
    return "\n".join(lines) + "\n"


def controllers(r, n_blocks):
    ev, extra = [], []
    for src in ("wheel", "foot", "breath", "at"):
        extra.append(f"mod {src} {r.randint(0, 99)} {r.randint(0, 1)} {r.randint(0, 1)} {r.randint(0, 1)}")
    extra.append(f"pb {r.randint(0, 12)} {r.randint(0, 12)} {r.choice([0, 0, r.randint(1, 12)])}")
    for b in range(0, n_blocks, r.randint(5, 40)):
        what = r.randint(0, 4)
        if what == 0:
            ev.append((b, f"bend {r.randint(0, 16383)}"))
        elif what == 4:
            ev.append((b, f"press {r.randint(0, 127)}"))
        else:
            ev.append((b, f"cc {(1, 2, 4)[what - 1]} {r.randint(0, 127)}"))
    return ev, extra


def compare(a_path, b_path):
    a = Path(a_path).read_bytes()
    b = Path(b_path).read_bytes()
    n = min(len(a) // 16, len(b) // 8)
    sa = [struct.unpack_from("<q", a, 16 * i)[0] for i in range(n)]
    sb = [struct.unpack_from("<q", b, 8 * i)[0] for i in range(n)]
    d = [x - y for x, y in zip(sa, sb)]
    nd = sum(1 for x in d if x)
    sig = math.sqrt(sum(x * x for x in sa) / max(n, 1)) or 1.0
    err = math.sqrt(sum(x * x for x in d) / max(n, 1))
    first = next((i for i, x in enumerate(d) if x), -1)
    return n, nd, (20 * math.log10(sig / err) if err else math.inf), first


def main():
    ref, f6, work = sys.argv[1], sys.argv[2], Path(sys.argv[3])
    quick = "--quick" in sys.argv
    work.mkdir(parents=True, exist_ok=True)
    seed = next((int(a[7:]) for a in sys.argv if a.startswith("--seed=")), 1)
    r = random.Random(seed)
    cases = []
    for k in range(16):                                                  # the factory voices
        v = list(map(int, subprocess.check_output([f6, "--rom", str(k)]).split()))
        for e in range(3):
            cases.append((f"rom{k:02d}_e{e}", score(v, e, playing(r, 1500), 1500)))
    for k in range(10 if quick else 60):                                 # random voices
        v = rand_voice(r)
        e = k % 3
        cases.append((f"rnd{k:02d}_e{e}", score(v, e, playing(r, 1200), 1200)))
    for k in range(6 if quick else 24):                                  # controllers, bend
        v = rand_voice(r)
        v[139] = r.randint(20, 99)                                       # PMD
        v[140] = r.randint(0, 99)                                        # AMD
        ev, extra = controllers(r, 1200)
        cases.append((f"ctl{k:02d}_e{k % 3}", score(v, k % 3, playing(r, 1200) + ev, 1200, extra)))
    for k in range(6 if quick else 18):                                  # portamento, mono
        v = rand_voice(r)
        ev = playing(r, 1200)
        extra = [f"porta {r.randint(1, 127)} {r.randint(0, 1)}", f"mono {k % 2}"]
        ev.append((0, "cc 65 127"))
        if k % 3 == 2:
            ev.append((600, "cc 65 0"))
        cases.append((f"por{k:02d}_e{k % 3}", score(v, k % 3, ev, 1200, extra)))
    for k in range(4 if quick else 12):                                  # more than 16 keys: stealing
        v = rand_voice(r)
        ev, b, held = [], 0, {}
        while b < 1000:
            for key in r.sample(range(30, 100), r.randint(4, 12)):
                if held.get(key, -1) < b:
                    off = b + r.randint(20, 300)
                    ev += [(b, f"on {key} {r.randint(1, 127)}"), (off, f"off {key}")]
                    held[key] = off
            b += r.randint(2, 30)
        cases.append((f"stl{k:02d}_e{k % 3}", score(v, k % 3, ev, 1200)))
    for k in range(4 if quick else 12):                                  # operators switched off
        v = rand_voice(r)
        ops = "".join(r.choice("0111") for _ in range(6))
        cases.append((f"ops{k:02d}_e{k % 3}", score(v, k % 3, playing(r, 1200), 1200, [f"ops {ops}"])))
    exact = 0
    worst = (math.inf, "")
    for name, text in cases:
        sp = work / f"{name}.score"
        sp.write_text(text)
        subprocess.check_call([ref, str(sp), str(work / f"{name}.dx")])
        subprocess.check_call([f6, str(sp), str(work / f"{name}.f6")])
        n, nd, snr, first = compare(work / f"{name}.dx", work / f"{name}.f6")
        exact += nd == 0
        if snr < worst[0]:
            worst = (snr, name)
        if nd:
            print(f"  {name}: {nd} of {n} samples differ, error {snr:.1f} dB under the signal, first at {first}")
    print(f"FM6 vs Dexed: {exact} of {len(cases)} renders bit-exact; worst {worst[0]:.1f} dB ({worst[1]})")
    return 0 if worst[0] > 90 else 1


if __name__ == "__main__":
    sys.exit(main())
