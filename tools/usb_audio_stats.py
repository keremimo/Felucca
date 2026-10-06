#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Read USB audio counters over MIDI without changing the sound or settings."""
import argparse
import json
import time
import mido

FIELDS = (
    'play_alt', 'cap_alt', 'play_rate', 'cap_rate', 'play_fill', 'cap_fill',
    'play_underruns', 'play_overruns', 'cap_underruns', 'cap_overruns',
    'bad_packets', 'rx_packets', 'tx_packets', 'missed_frames',
    'poll_max_us', 'service_max_us', 'audio_late', 'feedback_q14',
    'audio_max_us', 'cpu_q8',
)
HEADER = [0x7D, 0x46, 0x4C, 72]


def snapshot(incoming, outgoing, window=False, voices=False):
    outgoing.send(mido.Message('sysex', data=HEADER + ([(1 if window else 0) | (2 if voices else 0)] if window or voices else [])))
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        for message in incoming.iter_pending():
            if message.type != 'sysex' or list(message.data[:4]) != HEADER:
                continue
            data = message.data[4:]
            fields = FIELDS + (("voices_active", "voices_held", "cz1_active", "cz1_held", "voices_shed", "voices_killed") if data[0] == 3 else ())
            if data[0] not in (2, 3) or len(data) != 1 + 5 * len(fields):
                raise RuntimeError('Unsupported audio diagnostics schema')
            return {field: sum(data[1 + i * 5 + j] << (7 * j) for j in range(5))
                    for i, field in enumerate(fields)}
        time.sleep(0.005)
    raise TimeoutError('No USB audio diagnostics reply; requires a USB audio build (editor command 72)')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='Melodee')
    parser.add_argument('--seconds', type=float, default=0)
    parser.add_argument('--interval', type=float, default=5)
    parser.add_argument('--window', action='store_true',
                        help='report the maxima of each interval instead of since boot')
    args = parser.parse_args()
    if args.interval < 1 or args.seconds < 0:
        parser.error('interval must be >= 1 s; seconds must be >= 0')
    with mido.open_input(args.port) as incoming, mido.open_output(args.port) as outgoing:
        start = time.monotonic()
        while True:
            stats = snapshot(incoming, outgoing, args.window)
            stats['cpu_pct'] = round(stats['cpu_q8'] * 100 / 256, 1)
            stats['elapsed_s'] = round(time.monotonic() - start, 3)
            print(json.dumps(stats), flush=True)
            remaining = start + args.seconds - time.monotonic()
            if remaining <= 0:
                break
            time.sleep(min(args.interval, remaining))


if __name__ == '__main__':
    main()
