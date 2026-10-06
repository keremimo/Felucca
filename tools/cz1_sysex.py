#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Import/export a Casio CZ tone over USB MIDI (mido + python-rtmidi)."""
import argparse
from pathlib import Path
import time
import mido


def tones(raw):
    """Read complete Casio tone frames, rejecting malformed nibble payloads."""
    result = []
    for start in range(len(raw)):
        if raw[start] != 0xf0:
            continue
        end = raw.find(b'\xf7', start)
        if end < 0:
            raise ValueError('Unterminated SysEx message')
        frame = raw[start:end + 1]
        if len(frame) < 7 or frame[1:4] != bytes([0x44, 0, 0]):
            continue
        cmd = frame[5]
        offset = 6 if cmd == 0x30 else 7
        if cmd not in (0x20, 0x21, 0x30):
            continue
        data = frame[offset:-1]
        if len(data) not in (256, 288) or any(b > 15 for b in data):
            raise ValueError('Invalid Casio tone data')
        result.append(bytes(data))
    if not result:
        raise ValueError('No CZ tone dumps in the file')
    return result


def receive(incoming, predicate, seconds=3):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        for message in incoming.iter_pending():
            if message.type == 'sysex' and predicate(bytes(message.bytes())):
                return bytes(message.bytes())
        time.sleep(.003)
    raise TimeoutError('No CZ reply; select a CZ-1 track on the instrument')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='Felucca')
    parser.add_argument('--channel', type=int, default=1)
    parser.add_argument('--index', type=int, default=0, help='tone index in a multi-dump file')
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--import', dest='import_file', type=Path)
    mode.add_argument('--export', dest='export_file', type=Path)
    args = parser.parse_args()
    if not 1 <= args.channel <= 16 or args.index < 0:
        parser.error('channel must be 1..16; index must be nonnegative')
    ch = 0x70 + args.channel - 1
    with mido.open_input(args.port) as incoming, mido.open_output(args.port) as outgoing:
        if args.import_file:
            payloads = tones(args.import_file.read_bytes())
            if args.index >= len(payloads):
                parser.error(f'file contains {len(payloads)} tones')
            payload = payloads[args.index]
            outgoing.send(mido.Message('sysex', data=[0x44, 0, 0, ch, 0x21 if len(payload) == 288 else 0x20, 0x60, *payload]))
            receive(incoming, lambda b: b == bytes([0xf0, 0x44, 0, 0, ch, 0x30, 0xf7]))
            print(f'Imported tone {args.index + 1}/{len(payloads)} into the CZ track; SAVE to retain it.')
        else:
            outgoing.send(mido.Message('sysex', data=[0x44, 0, 0, ch, 0x11, 0x60]))
            frame = receive(incoming, lambda b: len(b) == 295 and b[1:6] == bytes([0x44, 0, 0, ch, 0x30]))
            payload = tones(frame)[0]
            args.export_file.write_bytes(bytes([0xf0, 0x44, 0, 0, ch, 0x21, 0x60]) + payload + b'\xf7')
            print(f'Exported CZ-1 tone to {args.export_file}')


if __name__ == '__main__':
    main()
