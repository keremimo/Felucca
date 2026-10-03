#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Validate the actual preprocessed USB descriptors in every app configuration."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def descriptors(audio, cdc):
    source = subprocess.check_output([
        *shlex.split(os.environ.get("CC", "cc")), "-E", "-P",
        f"-DFELUCCA_USB_AUDIO={audio}", f"-DFELUCCA_CDC={cdc}",
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
        assert len(data) == 398 and data[4] == 5
        assert interfaces[3, 0][4] == interfaces[4, 0][4] == 0
        for alt, width in ((1, 2), (2, 3)):
            playback, feedback = endpoints[3, alt]
            capture, = endpoints[4, alt]
            assert playback[2:4] == bytes([0x02, 0x05])
            assert capture[2:4] == bytes([0x82, 0x05])
            assert feedback[2:6] == bytes([0x83, 0x11, 3, 0])
            assert playback[8] == feedback[2]
            for ep in (playback, capture):
                assert int.from_bytes(ep[4:6], "little") == 49 * 2 * width and ep[6] == 1
        current = None
        formats = {}
        for d in records:
            if d[1] == 4:
                current = (d[2], d[3])
            elif len(d) == 14 and d[1:3] == bytes([0x24, 2]):
                formats[current] = d
            elif current and current[0] in (3, 4) and d[1] == 0x25:
                assert d[3] == 1  # sampling-frequency control advertised
        assert set(formats) == {(3, 1), (3, 2), (4, 1), (4, 2)}
        for (_, alt), fmt in formats.items():
            width = alt + 1
            assert fmt[3:8] == bytes([1, 2, width, width * 8, 2])
            assert int.from_bytes(fmt[8:11], "little") == 44100
            assert int.from_bytes(fmt[11:14], "little") == 48000
        # AC class-specific wTotalLength must include exactly its terminals.
        ac_start = next(i for i, d in enumerate(records) if d[1] == 4 and d[2] == 2)
        ac = []
        for d in records[ac_start + 1:]:
            if d[1] == 4:
                break
            ac.append(d)
        assert sum(map(len, ac)) == int.from_bytes(ac[0][5:7], "little") == 52
        assert ac[0][7:] == bytes([2, 3, 4])
        assert [d[3] for d in ac[1:]] == [1, 2, 3, 4]
    print(f"USB descriptors: audio={audio}, CDC={cdc}, {len(data)} bytes: OK")


for mode in ((0, 0), (0, 1), (1, 0)):
    descriptors(*mode)

generated = subprocess.check_output([sys.executable, str(ROOT / "tools/gen_usb_audio.py")], text=True)
assert generated == (ROOT / "firmware/src/usb_audio_filter.h").read_text()
print("USB audio FIR tables: reproducible, unity gain and int32 accumulator bounds: OK")
