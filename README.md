# Felucca

[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)](LICENSE)
[![Sponsor](https://img.shields.io/badge/Sponsor-ea4aaa?logo=githubsponsors&logoColor=white)](https://github.com/sponsors/hugelton)

**TL;DR:** connect your FM-1 to a computer by USB, open the
[web installer](https://hugelton.github.io/Felucca/) in Chrome or Edge, and press Install.
No extra hardware is needed. Beta: use at your own risk; M-VAVE's own updater takes you back
to the official firmware.

Multi-engine synthesizer firmware for the M-VAVE FM-1.

![FM-1 controls with Felucca](docs/panel.jpg)

## Features

- **Nine engines** (below), each with its own factory presets
- **Four tracks:** three synth parts, each with its own engine and sound, plus a GM drum track;
  8 voices shared between the parts. ALGORITHM selects the track on every page
- **Sequencer:** 64 steps per track with chords, ties, accent and slide; live loop recording
  with overdub and held notes; each track loops on its own length
- **Arpeggiator**, scales and quantize, glide, MONO / LEGATO / UNISON voice modes
- **Effects:** distortion and the SLICER per track; chorus, delay and reverb sends; master limiter
- **Presets:** factory presets with their own patterns, 32 user preset slots, 4 project slots
- **Web editor:** every parameter of every track, step grid, track mixer, preset library, sample upload
- **USB:** class-compliant MIDI in and out (channels 1–3 for the parts, 10 for drums);
  updates over the same USB cable

## Engines

- **ANALOG**: virtual analog; two oscillators (saw, square, triangle, sine, PWM), noise, drive, resonant low-pass filter
- **DIGITAL**: 4-operator FM, 8 algorithms, feedback
- **PHASE**: phase distortion (ported from CrispyZebra)
- **LOFI**: chiptune; pulse, triangle, saw, noise and a 4-bit wave RAM, stepped envelope, sweep, arpeggio
- **SAMPLE**: multisampled instruments and 3 user sample slots
- **VOICE**: formant oscillator, sung vowels
- **TRIO**: 3 oscillators with ring modulation and sync, multimode filter (LP / BP / HP / notch)
- **WHEEL**: tonewheel-style organ; drawbar registrations, percussion, key click, drive, rotary speaker
- **GRAIN**: granular textures from the built-in samples or a user slot

**SLICER** (FX page, every track including drums): a tempo-synced 16-step gate or stutter, with 16 patterns.

- Install: [web installer](https://hugelton.github.io/Felucca/) (Chrome or Edge, USB), or `tools/fm1_install.py` from a terminal
- Editor: [web editor](https://hugelton.github.io/Felucca/webapp/editor/)
- Build: [BUILDING.md](BUILDING.md)

## Scale keyboard

On the **SCL** page, **QNT** selects the keyboard layout:

- **OFF:** normal chromatic keyboard.
- **SNAP:** every key plays, rounded down to the scale. This is the previous ON
  mode; existing saved sounds and projects retain it.
- **WHITE:** consecutive white keys play consecutive scale notes; black keys are
  silent, including during live recording and step entry.
- **ALL:** every key, white or black, advances one scale degree without duplicate
  pitches. For C major, C, C#, D, D#, E, F, F#, G play C, D, E, F, G, A, B, C.

In WHITE and ALL, C4 plays **ROOT** and the layout continues above and below
it. **TRN** transposes the resulting notes; the octave buttons shift them by full
octaves. In ALL, degrees outside MIDI's 0–127 pitch range are silent rather than
clamped to duplicate end notes; use a different input octave to return to range.

Available scales: chromatic (CHR), major (MAJ), natural minor (MIN), Dorian (DOR),
Mixolydian (MIX), major pentatonic (PEN), minor pentatonic (MPEN), harmonic minor
(HARM), Phrygian (PHRY), Lydian (LYD), Locrian (LOC), ascending melodic minor (MEL),
minor blues (BLUES), whole tone (WHOLE), half-whole diminished (DIMHW), and
whole-half diminished (DIMWH). Scale degrees continue across keyboard octave
boundaries; roots need not fall on every C key.
The drum track, GM sample kit and incoming MIDI retain their existing note mapping.

## Layout

| Path | What |
| --- | --- |
| `firmware/` | firmware sources: `src/` app, `hal/` hardware layer, `loader/` update loader |
| `tools/` | build script, generators, package maker, installer and sample uploader |
| `assets/` | icon atlas, font, CC0 instrument samples |
| `web/` | web installer and editor sources |
| `tests/` | tests that run on the build machine |

## Support

If Felucca is useful to you, [sponsoring on GitHub](https://github.com/sponsors/hugelton) or a donation
on [itch.io](https://hugelton.itch.io/felucca) helps keep its development going.

Pull requests are welcome, and so are ideas and requests: post them in
[Discussions](https://github.com/hugelton/Felucca/discussions) or on X ([@kurogedelic](https://x.com/kurogedelic)).

## Credits

- Felucca by Leo Kuroshita ([@kurogedelic](https://github.com/kurogedelic)), [Hügelton Instruments](https://hugelton.com)
- Font: [Terminus](https://terminus-font.sourceforge.net/) by Dimitar Toshkov Zhekov, [SIL OFL 1.1](assets/fonts/Terminus-LICENSE.txt)
- Samples: [Versilian Studios](https://versilian-studios.com/) [VSCO-2 Community Edition](https://github.com/sgossner/VSCO-2-CE) and [VCSL](https://github.com/sgossner/VCSL), CC0 1.0 ([attribution](assets/samples-cc0/ATTRIBUTION.txt))
- PHASE engine: oscillator ported from [CrispyZebra](https://github.com/hugelton/CrispyZebra) by Leo Kuroshita (GPL-3.0)
- VOICE engine: after [klattsch](https://github.com/tgies/klattsch) by Tony Gies (MIT); formant data from Klatt (1980) and Hillenbrand et al. (1995)
- Web editor icons: Fukiai by [Hügelton Instruments](https://hugelton.com), [MIT](web/FUKIAI-LICENSE.txt)
- Package format and boot files: [JieLi AC79 SDK](https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK) (Apache-2.0, not included)

## Licence

Code: [GPL-3.0-only](LICENSE). Third-party material: [LICENSING.md](LICENSING.md).

M-VAVE and FM-1 are trademarks of their respective owners. Felucca is not affiliated with or endorsed by them.

Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
