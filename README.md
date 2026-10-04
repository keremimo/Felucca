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

- **Ten engines** (below), each with its own factory presets
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
- **Experimental USB audio build:** a stereo input device (synth recording) and a
  separate stereo output device (computer playback), each host-selectable 16/24-bit and 44.1/48 kHz,
  alongside MIDI. Opt-in build mode; tested on macOS, listening tests pending.
  See [USB audio build instructions](BUILDING.md#experimental-usb-audio-mode).
- **TRS MIDI IN:** enabled by default, with the same channel routing and scale
  mapping as USB MIDI; supports pitch bend, mod-wheel vibrato, sustain and MIDI panic;
  [controls and limits](docs/MIDI-EXPRESSION.txt)
- **MIDI status:** GLO > SYSTEM knob 1 switches the first column between USB status and TRS status/activity; both inputs remain active

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
- **FM6**: six-operator FM that plays DX7 voices: 32 algorithms, DX7 envelopes, key scaling,
  pitch envelope and LFO; every operator edited on the device; DX7 voices and banks over USB-MIDI
  ([below](#fm6))

**SLICER** (FX page, every track including drums): a tempo-synced 16-step gate or stutter, with 16 patterns.

- Install: [web installer](https://hugelton.github.io/Felucca/) (Chrome or Edge, USB), or `tools/fm1_install.py` from a terminal
- Editor: [web editor](https://hugelton.github.io/Felucca/webapp/editor/)
- Build: [BUILDING.md](BUILDING.md)

## FM6

FM6 plays DX7 voices: six operators, the 32 DX7 algorithms, four-rate / four-level envelopes,
level and rate key scaling, velocity, feedback, pitch envelope and LFO, as measured on a DX7 by the
MSFA and Dexed projects. 16 factory voices (VOICE R01–R16) come with presets of their own; 32 user
voices (U01–U32) live in flash.

**EDIT** steps through the pages. **PATCH** (also HOME's knobs): VOICE, then MOD (the modulators'
levels: brightness), M.TIM and C.TIM (the modulators' and carriers' envelope times). Then the voice
itself: **ALGO** (algorithm, feedback, key sync, transpose), six operator pages (**FREQ**, **OUT**,
**EG RATE**, **EG LVL**, **SCALE**, **CURVE**), the pitch envelope, two LFO pages and **STORE**. On
the operator pages **PRESETS** picks the operator (OP1–OP6); the graph shows the algorithm with it
highlighted.

Edits change the part's voice at once and stay until another VOICE or preset is loaded. **STORE**
keeps them: SLOT picks a user voice, STORE writes it there (two detents, like the other GO
buttons). SEND sends the voice as a DX7 single-voice dump, INIT starts from the DX7 init voice.
Projects save each FM6 part's voice with its edits; user presets save its VOICE number.

Over USB-MIDI FM6 takes DX7 SysEx on any channel, so Dexed or any DX7 librarian can edit a
voice live or load a cartridge:

| SysEx | What FM6 does |
| --- | --- |
| `F0 43 0n 00 01 1B` + 155 bytes + checksum `F7` (a voice) | into the FM6 part's voice |
| `F0 43 0n 09 20 00` + 4096 bytes + checksum `F7` (32 voices) | into U01–U32, saved in flash |
| `F0 43 1n gg pp dd F7` (a voice parameter) | into the FM6 part's voice |
| `F0 43 2n 00 F7` / `F0 43 2n 09 F7` (dump requests) | sends the part's voice / the user bank |

The FM6 part is the selected track when it plays FM6, else part n + 1, else the first FM6 part.

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
The drum track and GM sample kit retain their existing note mapping.

Incoming MIDI uses the receiving track's **WHITE** or **ALL** layout: MIDI note 60
(C4) plays ROOT, and each participating key advances one scale degree.
**TRN** applies to the mapped notes. The FM-1's octave buttons affect
only its own keys; use the external keyboard's octave controls for MIDI input.
With QNT at OFF or SNAP, incoming MIDI notes pass through unchanged.

Channels 1–3 play parts 1–3; the configured drum channel plays drums; other
channels play the selected track. Set ROOT, SCL and QNT on that receiving part.
Mapped notes feed its arpeggiator and live recording. Note-offs release the pitch
and part chosen at note-on, even if settings or the selected track change.

This applies to USB MIDI routed from a computer and to TRS MIDI input when it is
enabled (`FELUCCA_UART`). TRS ignores MIDI clock, transport, system common and
SysEx. End-to-end TRS reception still needs verification with an external MIDI
source.

## Sequencer note length

On **SEQ > STEP**, turn **PRESETS** to change the selected note or chord's length
in steps. The readout above the piano roll shows `PRESETS: LENGTH 4 STP`, for
example. **TIME** on knob 3 still selects NOTE, TIE or REST.

To enter a long note, hold its key (or chord), turn PRESETS to the desired length,
then release the keys. The cursor advances past the whole note, ready for the next
one. For an existing note, select its onset or any of its tied steps with **STEP**
(knob 1) and turn PRESETS. Clockwise extends it; counterclockwise shortens it to a minimum
of one step. Ties are added and removed automatically.

Lengths can cross a 16-step bank or the pattern's loop boundary, up to one full
pattern. Extensions stop before another note; shortening clears only the removed
ties. Drum hits remain one-shot. The piano roll shows sustained chords across
banks, and existing projects keep using the same NOTE/TIE representation.

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
