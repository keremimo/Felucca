# Free CZ reference audit

Run from the checkout, after generating the normal build tables:

```sh
python3 tools/cz1_reference.py --fetch
```

Requires a C compiler, ffmpeg and NumPy. This compiles the firmware's renderer
into a host audition tool, downloads external assets into the ignored
`build/cz1-reference/source` directory, and writes dry WAV auditions plus JSON
reports. It does not flash the device or alter its saved data. Repeating without
`--fetch` uses the downloaded files. `sources.json` records URLs and SHA-256 hashes.
The downloaded audio/patches are not included in the repository or firmware.

## Bank results, 6 October 2026

The free [mu:zines Magazine Patches bank](https://www.muzines.co.uk/blog/the-long-lost-patch/20?theme=1)
contains 35 complete native CZ-1 tone frames. After the native waveform/window implementation, all 35 decode
and render. Fine-detune bytes 0x50 (Martha Going Oh and Ah) and 0xfc (Timpani Drums)
were previously, incorrectly rejected. Casio's p. 84 places the six-bit fine-detune
value in bits 7–2 of PDETL. The old importer read bits 5–0 and the exporter omitted
the shift. Both now use the documented high-six-bit placement. Independent table
fixtures check both directions; these two patches decode to display values 19 and
60 respectively. The correction also fixes detune on other imported patches.

Rebecca uses raw MULTI-SINE with no window. The patch model now stores the
native carrier and independent window separately, so it imports as MULTI-SINE
rather than being substituted with RESONANCE I. NULL, PULSE2, both hidden window
shapes, all window aliases, and arbitrary alternating carrier combinations are
also implemented. All 576 first/second/window choices (512 combinations and 64
single-wave choices) retain their native selector bits on re-export; duplicate
waves are retained too. This establishes selector compatibility, not exact audio.

The external files exposed a vibrato export bug: the two bytes of each machine
value were reversed. They now follow Casio's wire order: display value, machine
low byte, machine high byte. For example, vibrato rate 50 is `32 e0 09`.
An independent fixture now checks this order rather than just codec round trips.

String Bass originally failed because two END steps have nonzero stored levels.
Import now normalizes these to the zero targets specified by Casio's envelope
semantics. Invalid END indices are still rejected. Its audition is
`build/cz1-reference/String Bass.wav` (C4, velocity 100, two seconds held and two
seconds release, with a short leading silence).

These are host auditions at explicit note/velocity/duration settings, with neutral
Melodee macros and no effects sends. Keyboard function settings are absent from
tone SysEx and use our INIT defaults. They are not reproductions of the original
demo's undocumented performance. The String Bass demo download returned HTTP 403;
its hardware versus software origin is also not established.

Imported display values can re-encode to different machine bytes in noncanonical
files. `canonicalized_bytes` reports this; `CZ_DIFF=1` on the C audition tool shows
each changed offset. The current codec does not preserve arbitrary raw machine
bytes or undocumented intermediate values. Byte-exact preservation of those is
an unresolved requirement for comprehensive native-patch compatibility.

## Provisional oscillator comparison

Thirteen freely accessible [Kasploosh recordings](https://www.kasploosh.com/cz/11800-spelunking/2-basic_waves/)
are from the author's real CZ-1. He specifies nominal A = 220 Hz, fixed level,
and no envelope or velocity effects. Our constructed probes use MIDI 57,
velocity 127, a single line, maximum DCW, fixed pitch and amplitude, and no effects.
They are separate constructed patches; the exact source patch/settings are
unavailable. Consequently these results are diagnostic and not parity tests.

The analysis uses seconds 1–3, a Hann window, and 32 integrated harmonic bands that
allow pitch drift/modulation. Levels are relative to each recording's fundamental (440 Hz for PULSE2).
NULL has no steady pitched output; its spectral report is not a meaningful
harmonic comparison. The raw MULTI-SINE peak is around the fifteenth harmonic,
so normalization against its very weak fundamental also needs care.

| Waveform | CZ-1 H2 | Our H2 | CZ-1 H3 | Our H3 |
| --- | ---: | ---: | ---: | ---: |
| Saw | −7.9 dB | −8.1 dB | −11.8 dB | −11.9 dB |
| Square | −43.8 dB | −116.9 dB | −9.4 dB | −9.6 dB |
| Pulse | −0.3 dB | −0.0 dB | −0.7 dB | −0.1 dB |
| Double sine | −21.9 dB | −21.0 dB | −24.1 dB | −22.3 dB |
| Saw pulse | −5.9 dB | −5.5 dB | −11.9 dB | −12.0 dB |

The table above records the corrected oscillator. Saw and saw-pulse
are closer at this operating point. Even-harmonic differences in square may
include the hardware's output path/noise. This analysis does not isolate those
effects, compare complete envelopes/velocity/chorus, or establish blind-test
equivalence.

The current correction replaces the double-sine transfer with two equal carrier
cycles at zero DCW, then compresses the first cycle into a short pulse. The minimum
pulse width of 1/32 cycle is a provisional fit to the hardware trace; the full DCW
response curve remains unmeasured. At maximum DCW its H2/H3 are now −21.0/−22.3 dB,
compared with −21.9/−24.1 dB in the recording. The earlier H2 difference was about
24 dB; it is now about 0.9 dB at this one operating point.

The resonance model now reaches approximately 15 carrier cycles per fundamental
period, uses a unipolar carrier before windowing, and has the documented trapezoid
(hold at full level for the first half, then fall to zero). The unipolar carrier
restores the window's low harmonics. A selected resonance window applies to both
alternating waves. These are structural corrections grounded in the author's
[window measurements](https://www.kasploosh.com/cz/11800-spelunking/4-window/)
and [combination-wave research](https://www.kasploosh.com/cz/11800-spelunking/3-combo_waves/).

| Resonance window | CZ-1 H2 | Our H2 | CZ-1 H3 | Our H3 | CZ-1 H15 | Our H15 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Saw | −5.9 | −5.9 | −9.2 | −9.3 | +3.0 | +2.3 |
| Triangle | −48.3 | −46.6 | −18.9 | −19.2 | +1.0 | +0.3 |
| Trapezoid | −7.3 | −7.4 | −10.4 | −10.6 | +5.1 | +4.4 |

All values are dB relative to the fundamental. Significant sideband differences
remain around the carrier peak (for example H16), as do uncertainty about source
settings and the original output path. These comparisons are provisional, not a
fidelity pass. Regression checks guard coarse wave structure, including the
zero-DCW doubled carrier and the resonance window, without asserting exact audio
parity. CC121 now also clears the native CZ portamento flag.

## Native patch storage and migration

The human-facing packed tone has 165 bytes: 147 existing controls, independent
window bytes at 147/148, and 16 name characters at 149. Carrier selectors are raw
0..7; the second wave is OFF (0) or raw selector + 1. Windows are 0..7, with 6/7
rendering the same shape as 5 while preserving their original codes. All controls
are available in the device editor, including a separate CZ WINDOW page. Copying
one line copies its window too.

Native presets use record version 7 and 983 tone bits plus 528 common-parameter
bits, still inside the existing 238-byte record. Older v6 presets, FUNA projects,
FBKB pattern banks, TPL8 templates, 163-byte CZ editor PUTs and version-1 librarian
extensions are converted from panel waves to native carriers/windows. New writes
use FUNB (4244 bytes), FBKC (19092), TPL9, and a version-2 librarian extension.
Legacy FUN8/FBK9 records remain supported. The external Casio frame size and
nibble order are unchanged.

The hidden-mode comparison includes PULSE2 and the half-saw/double-saw windows.
At this operating point PULSE2 H2/H3 are about −0.2/−0.4 dB on hardware and
−0.1/−0.3 dB in the renderer. The half-saw window is approximately −7.3/−10.5 dB
on hardware and −7.4/−10.6 dB in the renderer. Double-saw still differs substantially:
H2 relative to its weak fundamental is +19.4 dB on hardware versus +40.9 dB in the
renderer. Its detailed carrier/window response needs calibration. These results
are provisional comparisons, not claims of faithful reproduction.
