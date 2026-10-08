#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Diagnostics compatibility without a MIDI backend or connected FM-1."""
import importlib.util
import sys
from pathlib import Path
from types import SimpleNamespace
sys.modules['mido'] = SimpleNamespace(Message=lambda kind, data: SimpleNamespace(type=kind, data=data))
spec = importlib.util.spec_from_file_location('stats', Path(__file__).resolve().parents[1] / 'tools/usb_audio_stats.py')
stats = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stats)


def reply(schema, count):
    values = [0xFFFF_FFFF - i * 137 for i in range(count)]
    data = stats.HEADER + [schema] + [value >> (7 * j) & 127 for value in values for j in range(5)]
    incoming = SimpleNamespace(iter_pending=lambda: [SimpleNamespace(type='sysex', data=data)])
    sent = []
    outgoing = SimpleNamespace(send=sent.append)
    return values, incoming, outgoing, sent


for schema, count in [(2, 20), (3, 26), (4, 32)]:
    expected, incoming, outgoing, sent = reply(schema, count)
    result = stats.snapshot(incoming, outgoing, window=True, voices=schema >= 3, cores=schema == 4)
    assert list(result.values()) == expected
    assert sent[0].data == stats.HEADER + [7 if schema == 4 else 3 if schema == 3 else 1]

_, incoming, outgoing, _ = reply(2, 20)
try:
    stats.snapshot(incoming, outgoing, cores=True)
except RuntimeError as exc:
    assert 'schema 4' in str(exc)
else:
    raise AssertionError('old firmware must not appear to have core telemetry')

_, incoming, outgoing, _ = reply(4, 31)
try:
    stats.snapshot(incoming, outgoing, cores=True)
except RuntimeError:
    pass
else:
    raise AssertionError('truncated telemetry must be rejected')
print('audio stats: schemas 2/3/4, full-width counters, flags and malformed replies ok')
