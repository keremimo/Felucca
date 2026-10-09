#!/usr/bin/env python3
"""Back up Prophet user banks, then remove only exact Sequential factory copies."""
import argparse
import hashlib
import json
import struct
import zlib
from pathlib import Path

from gen_prophet_factory import records
from prophet_device import Device, inventory, pack, u32, unpack


def read_bank(device, ident, length, crc):
    raw = bytearray()
    for offset in range(0, length, 256):
        count = min(256, length - offset)
        reply = device.request(66, [ident] + u32(offset) + [count & 127, count >> 7])
        if len(reply) < 9 or reply[0] != ident or reply[1] != 0:
            raise RuntimeError(f"Prophet bank {ident} read failed at {offset}")
        raw.extend(unpack(reply[9:]))
    if len(raw) != length or zlib.crc32(raw) != crc:
        raise RuntimeError(f"Prophet bank {ident} CRC mismatch")
    return bytes(raw)


def write_bank(device, ident, raw):
    def put(args):
        reply = device.request(67, args)
        if len(reply) < 3 or reply[:2] != [args[0], ident] or reply[2] != 0:
            raise RuntimeError(f"Prophet bank {ident} write failed: {reply[:3]}")
    put([0, ident] + u32(len(raw)) + u32(zlib.crc32(raw)))
    try:
        for offset in range(0, len(raw), 256):
            put([1, ident] + u32(offset) + pack(raw[offset:offset + 256]))
        put([2, ident])
    except Exception:
        try:
            put([3, ident])
        except Exception:
            pass
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="Felucca")
    parser.add_argument("--backup", required=True, type=Path)
    args = parser.parse_args()
    factory = records()
    device = Device(args.port)
    try:
        listed = {ident: (length, crc) for ident, length, crc in inventory(device)}
        original = {}
        replacement = {}
        report = []
        for ident in range(23, 28):
            length, crc = listed[ident]
            if length != 3600:
                raise RuntimeError(f"Unexpected Prophet bank {ident} length: {length}")
            raw = read_bank(device, ident, length, crc)
            magic, used, favorites = struct.unpack_from("<III", raw)
            if magic != 0x31553550:
                raise RuntimeError(f"Invalid Prophet bank {ident}")
            changed = bytearray(raw)
            removed = []
            for index in range(26 if ident < 27 else 24):
                slot = (ident - 23) * 26 + index
                offset = 12 + 138 * index
                if used >> index & 1 and raw[offset:offset + 138] == factory[slot]:
                    changed[offset:offset + 138] = bytes(138)
                    used &= ~(1 << index)
                    favorites &= ~(1 << index)
                    removed.append(slot + 1)
            struct.pack_into("<II", changed, 4, used, favorites)
            original[ident] = raw
            replacement[ident] = bytes(changed)
            report.append({"id": ident, "sha256": hashlib.sha256(raw).hexdigest(), "removed_slots": removed})
        args.backup.mkdir(parents=True, exist_ok=False)
        for ident, raw in original.items():
            (args.backup / f"{ident}.bin").write_bytes(raw)
        (args.backup / "manifest.json").write_text(json.dumps(report, indent=2) + "\n")
        for ident in range(23, 28):
            if replacement[ident] == original[ident]:
                continue
            write_bank(device, ident, replacement[ident])
            current = {key: (length, crc) for key, length, crc in inventory(device)}
            length, crc = current[ident]
            if read_bank(device, ident, length, crc) != replacement[ident]:
                raise RuntimeError(f"Prophet bank {ident} did not verify after write")
        print(json.dumps({"backup": str(args.backup),
                          "removed": sum(len(row["removed_slots"]) for row in report),
                          "retained": sum(struct.unpack_from("<I", raw, 4)[0].bit_count()
                                          for raw in replacement.values())}))
    finally:
        device.close()


if __name__ == "__main__":
    main()
