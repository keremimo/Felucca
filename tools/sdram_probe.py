#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Ask the FM-1 whether its chip carries SDRAM (editor command 34, firmware/hal/fm1_sdram.h).

The probe takes up to about half a second; the UI and MIDI input pause meanwhile, audio keeps playing.
Every register it touches is restored afterwards (--keep leaves found SDRAM running)."""
import argparse
import json
import re
import time
import mido

FIELDS = (
    'found', 'geom', 'pass0', 'pass1', 'pass2', 'best', 'errors', 'narrow', 'retain',
    *(f'alias{k}' for k in range(8)),
    'first_wr', 'first_rd', 'cache_con', 'clk_con1', 'sys_div', 'iomap0', 'drpg', 'dbg_msg', 'us',
)
HEADER = [0x7D, 0x46, 0x4C, 34]
GEOM = {0xE01: '8 MB', 0xE00: '2 MB'}
PORT_RE = re.compile(r'melodee|felucca|fm-1', re.I)    # macOS may keep the name from before the rename


def find_port():
    names = [n for n in mido.get_output_names() if PORT_RE.search(n)]
    if not names:
        raise SystemExit('FM-1 not found; pass --port')
    return names[0]


def probe(port, keep=False):
    with mido.open_input(port) as incoming, mido.open_output(port) as outgoing:
        outgoing.send(mido.Message('sysex', data=HEADER + ([1] if keep else [])))
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            for message in incoming.iter_pending():
                if message.type != 'sysex' or list(message.data[:4]) != HEADER:
                    continue
                data = message.data[4:]
                if len(data) != 1 + 5 * len(FIELDS) or data[0] != 1:
                    raise RuntimeError('Unsupported SDRAM probe schema')
                return {field: sum(data[1 + i * 5 + j] << (7 * j) for j in range(5))
                        for i, field in enumerate(FIELDS)}
            time.sleep(0.005)
    raise TimeoutError('No SDRAM probe reply; needs firmware with command 34')


def phase(ix):
    """sweep index -> the SDK's (i, j, k, m) phase"""
    return {'i': 5 + (ix >> 4 & 1), 'j': ix >> 2 & 3, 'k': ix & 3, 'm': 1 + (ix >> 5)}


def size_mib(r):
    """wrap period of the MiB tags (k written at k MiB, k = 0..7, in order)"""
    got = [r[f'alias{k}'] for k in range(8)]
    for s in (1, 2, 4, 8):
        want = [0x5D000000 | (k % s + s * ((7 - k % s) // s)) for k in range(8)]
        if got == want:
            return s
    return None


def summary(r):
    passes = sum(bin(r[f'pass{n}']).count('1') for n in range(3))
    lines = []
    if r['found']:
        geom = GEOM.get(r['geom'], hex(r['geom']))
        lines.append(f"SDRAM answered: {passes}/96 phases passed with the {geom} geometry, "
                     f"chosen {phase(r['best'])}")
        size = size_mib(r)
        lines.append(f"size by address wrap: {size} MiB" if size else "size: tags read back inconsistently")
        lines.append(f"checks: {r['errors']} word errors (64 KB at each MiB), {r['narrow']} byte/halfword "
                     f"errors, {r['retain']} errors after 300 ms" + (" -- all clean" if r['found'] == 1 else ""))
    else:
        lines.append("no SDRAM answered: no phase of either geometry read back its data")
        lines.append(f"default phase: wrote {r['first_wr']:#010x}, read {r['first_rd']:#010x}")
    lines.append(f"registers before: CACHE_CON {r['cache_con']:#010x}  CLK_CON1 {r['clk_con1']:#010x}  "
                 f"SYS_DIV {r['sys_div']:#010x}  IOMAP_CON0 {r['iomap0']:#010x}  DRPG {r['drpg']:#06x}")
    lines.append(f"DBG_MSG after the window: {r['dbg_msg']:#010x}; took {r['us'] / 1000:.1f} ms")
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', help='MIDI port (default: the first Melodee / Felucca / FM-1 port)')
    parser.add_argument('--keep', action='store_true', help='leave found SDRAM running')
    parser.add_argument('--json', action='store_true', help='print the raw fields as JSON')
    args = parser.parse_args()
    r = probe(args.port or find_port(), args.keep)
    print(json.dumps(r) if args.json else summary(r))


if __name__ == '__main__':
    main()
