#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fetch free CZ references, audition the bank, and report provisional spectra.

Requires generated build/gen files, cc, ffmpeg and numpy. External assets stay
in the ignored build directory; their authors' rights are not changed. Reference
recordings have no complete tone dump, so spectra are diagnostic, not a fidelity
pass/fail test. This does not access or change the connected instrument.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from urllib.request import urlopen
import wave
import zipfile

ROOT = Path(__file__).resolve().parents[1]
BANK = 'https://www.muzines.co.uk/files/mag_blog/SYX%20-%20Magazine%20Patches.zip'
WAVES = [('saw', '000', 0, 0), ('square', '001', 1, 0), ('pulse', '010', 2, 0),
         ('null', '011', 3, 0), ('pulse2', '111', 7, 0),
         ('multi sine', '110', 6, 0), ('double sine', '100', 4, 0), ('saw pulse', '101', 5, 0),
         ('resonance saw', '110_wd001', 6, 1),
         ('resonance triangle', '110_wd010', 6, 2),
         ('resonance trapezoid', '110_wd011', 6, 3),
         ('half-saw window', '110_wd100', 6, 4),
         ('double-saw window', '110_wd101', 6, 5)]
MEDIA = 'https://media.kasploosh.com/audio/cz/11800-spelunking/waveform/'


def probe(code, name, window=0):
    """Single line, full DCW, fixed pitch/amplitude, no velocity modulation.

    Deliberately constructed stimulus, not the original recording's tone dump.
    Native END targets are zero; the first stage sustains at its fixed level.
    """
    d = bytearray(144)
    d[4] = 8
    d[8:11] = bytes([50, 0xe0, 9])
    d[12] = 1
    for o in (14, 71):
        d[o] = code << 5 | window >> 2
        d[o + 1] = (window & 3) << 6
        for off, rate, level in ((o + 6, 119, 127), (o + 23, 127, 127), (o + 40, 127, 0)):
            if off == o + 23:
                for stage in range(8):
                    d[off + 1 + 2 * stage] = 8  # DCW RATE 0's machine value
            d[off] = 0xf1
            d[off + 1] = rate
            d[off + 2] = level | 128
            d[off + 3] = rate | (128 if level else 0)
    d[128:] = name[:16].ljust(16).encode('ascii')
    return bytes([240, 68, 0, 0, 112, 33, 96]) + bytes(
        nibble for byte in d for nibble in (byte & 15, byte >> 4)) + bytes([247])


def spectrum(path, base=220):
    import numpy as np
    with wave.open(str(path)) as f:
        if f.getsampwidth() != 2:
            raise ValueError('Expected 16-bit PCM')
        sr, channels = f.getframerate(), f.getnchannels()
        x = np.frombuffer(f.readframes(f.getnframes()), dtype='<i2').astype(float)
        x = x.reshape(-1, channels).mean(axis=1)[sr:sr * 3]
    x -= x.mean()
    power = abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    freq = np.fft.rfftfreq(len(x), 1 / sr)
    bins = np.where((freq > base*.9) & (freq < base*1.1))[0]
    f0 = float(freq[bins[np.argmax(power[bins])]])
    # Broad bands accommodate modulation/drift in the hardware recordings.
    amps = [float(np.sqrt(power[abs(freq - h * f0) < max(3, h * f0 * .02)].sum()))
            for h in range(1, 33)]
    db = 20 * np.log10(np.maximum(amps, 1e-9) / max(amps[0], 1e-9))
    return {'fundamental_peak_hz': f0, 'harmonics_db_relative_to_fundamental': db.tolist()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fetch', action='store_true', help='download missing free references')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/cz1-reference')
    args = parser.parse_args()
    out = args.out.resolve()
    source = out / 'source'
    source.mkdir(parents=True, exist_ok=True)
    urls = {'magazine.zip': BANK}
    urls.update({f'wv{bits}.ogg': (MEDIA if '_wd' not in bits else MEDIA.replace('waveform/', 'window/demo/')) + f'wv{bits}.ogg' for _, bits, _, _ in WAVES})
    manifest = []
    for name, url in urls.items():
        path = source / name
        if not path.exists() and args.fetch:
            with urlopen(url, timeout=20) as response:
                path.write_bytes(response.read())
        if not path.exists():
            parser.error(f'Missing {path}; use --fetch')
        manifest.append({'file': name, 'url': url, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
    (out / 'sources.json').write_text(json.dumps(manifest, indent=2) + '\n')
    binary = out / 'cz1_reference'
    subprocess.run(['cc', '-O2', '-w', '-Ibuild/gen', '-Ifirmware/src',
                    'tests/cz1_reference.c', '-lm', '-o', str(binary)], cwd=ROOT, check=True)
    audits = []
    with zipfile.ZipFile(source / 'magazine.zip') as archive:
        for item in sorted(archive.namelist()):
            if not item.lower().endswith('.syx') or item.startswith('__MACOSX/'):
                continue
            path = source / Path(item).name.strip()
            path.write_bytes(archive.read(item))
            result = subprocess.run([str(binary), str(path), str(out / (path.stem + '.wav'))],
                                    capture_output=True, text=True)
            audits.append({'patch': path.name, 'accepted': result.returncode == 0,
                           'result': (result.stdout + result.stderr).strip()})
    (out / 'bank-audit.json').write_text(json.dumps(audits, indent=2) + '\n')
    comparisons = []
    for index, (name, bits, code, window) in enumerate(WAVES):
        recording = source / f'wv{bits}.wav'
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(source / f'wv{bits}.ogg'),
                        str(recording)], check=True)
        patch, audio = out / f'wave-{index}.syx', out / f'wave-{index}.wav'
        patch.write_bytes(probe(code, f'WAVE {index}', window))
        subprocess.run([str(binary), str(patch), str(audio), '57', '127', '3', '1'], check=True)
        comparisons.append({'wave': name, 'recording': spectrum(recording,440 if code==7 else 220), 'render': spectrum(audio,440 if code==7 else 220),
                            'matched_full_patch': False})
    (out / 'waveform-comparison.json').write_text(json.dumps(comparisons, indent=2) + '\n')
    print(f"Accepted {sum(a['accepted'] for a in audits)}/{len(audits)} patches; results in {out}")
    for audit in audits:
        if not audit['accepted']:
            print(audit['result'])
    print('Waveform comparisons are provisional: exact source patches/settings are unavailable.')


if __name__ == '__main__':
    main()
