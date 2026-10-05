#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Validate the actual preprocessed USB descriptors in every app configuration."""
import os
from pathlib import Path
import re
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def descriptors(audio, cdc):
    source = subprocess.check_output([
        *shlex.split(os.environ.get("CC", "cc")), "-E", "-P",
        f"-DMELODEE_USB_AUDIO={audio}", f"-DMELODEE_CDC={cdc}",
        str(ROOT / "firmware/src/usb.c"),
    ], text=True)
    body = re.search(r"CFG_DESC\[.*?\]\s*=\s*\{(.*?)\};", source, re.S)[1]
    data = bytes(int(x.strip(), 0) for x in body.split(",") if x.strip())
    assert int.from_bytes(data[2:4], "little") == len(data)
    records, pos = [], 0
    while pos < len(data):
        length = data[pos]
        assert length >= 2 and pos + length <= len(data)
        records.append(data[pos:pos + length])
        pos += length
    interfaces = {(d[2], d[3]): d for d in records if d[1] == 4}
    assert len({i for i, alt in interfaces}) == data[4]
    current, endpoints = None, {}
    for d in records:
        if d[1] == 4:
            current = (d[2], d[3])
            endpoints[current] = []
        elif d[1] == 5:
            endpoints[current].append(d)
    for key, interface in interfaces.items():
        assert len(endpoints[key]) == interface[4]
    if audio:
        assert len(data) == 411 and data[4] == 6
        # Two audio functions, so hosts list an output and a separate input
        # device, each on its own clock. IAD and control interface share the name.
        iads = [d for d in records if d[1] == 0x0B]
        assert [tuple(d[2:4]) for d in iads] == [(0, 2), (2, 2), (4, 2)]
        assert [d[7] for d in iads] == [0, 3, 4]
        assert interfaces[2, 0][5:9] == bytes([1, 1, 0, 3])
        assert interfaces[4, 0][5:9] == bytes([1, 1, 0, 4])
        assert interfaces[3, 0][4] == interfaces[5, 0][4] == 0
        for alt, width in ((1, 2), (2, 3)):
            playback, feedback = endpoints[3, alt]
            capture, = endpoints[5, alt]
            assert playback[2:4] == bytes([0x02, 0x05])
            assert capture[2:4] == bytes([0x82, 0x05])
            assert feedback[2:6] == bytes([0x83, 0x11, 3, 0])
            assert playback[8] == feedback[2]
            for ep, channels in ((playback, 2), (capture, 4)):
                assert int.from_bytes(ep[4:6], "little") == 45 * channels * width and ep[6] == 1
        current = None
        formats, links = {}, {}
        for d in records:
            if d[1] == 4:
                current = (d[2], d[3])
            elif current and current[0] in (3, 5) and d[1:3] == bytes([0x24, 1]):
                links[current] = d[3]
            elif current and current[0] in (3, 5) and d[1:3] == bytes([0x24, 2]):
                formats[current] = d
            elif current and current[0] in (3, 5) and d[1] == 0x25:
                assert d[3] == 1  # sampling-frequency control advertised
        assert set(formats) == {(3, 1), (3, 2), (5, 1), (5, 2)}
        for (interface, alt), fmt in formats.items():
            width = alt + 1
            assert len(fmt) == 11  # exactly one discrete rate; no hidden 48 kHz entry
            assert fmt[3:8] == bytes([1, 4 if interface == 5 else 2, width, width * 8, 1])
            assert int.from_bytes(fmt[8:11], "little") == 44100
        # Playback streams into IT1, recording leaves through OT4.
        assert links == {(3, 1): 1, (3, 2): 1, (5, 1): 4, (5, 2): 4}
        # Each AC header lists only its own streaming interface, and its
        # wTotalLength covers exactly one terminal pair.
        for ac_if, as_if, chain in ((2, 3, [(2, 1, 0x0101, None), (3, 2, 0x0301, 1)]),
                                    (4, 5, [(2, 3, 0x0713, None), (3, 4, 0x0101, 3)])):
            start = next(i for i, d in enumerate(records) if d[1] == 4 and d[2] == ac_if)
            ac = []
            for d in records[start + 1:]:
                if d[1] == 4:
                    break
                ac.append(d)
            assert sum(map(len, ac)) == int.from_bytes(ac[0][5:7], "little") == 30
            assert ac[0][7:] == bytes([1, as_if]) and len(ac) == 1 + len(chain)
            for d, (subtype, unit, kind, source) in zip(ac[1:], chain):
                assert d[2:4] == bytes([subtype, unit])
                assert int.from_bytes(d[4:6], "little") == kind
                if source is not None:
                    assert d[7] == source
                else:
                    assert d[7:10] == bytes([4, 0, 0] if ac_if == 4 else [2, 3, 0])
    print(f"USB descriptors: audio={audio}, CDC={cdc}, {len(data)} bytes: OK")


for mode in ((0, 0), (0, 1), (1, 0)):
    descriptors(*mode)
