# Felucca

[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)](LICENSE)
[![Sponsor](https://img.shields.io/badge/Sponsor-ea4aaa?logo=githubsponsors&logoColor=white)](https://github.com/sponsors/hugelton)

![Felucca 1.0](docs/felucca-1.0.png)

**TL;DR:** Felucca 1.0 — Big New Features, field testing. Connect your FM-1 to a computer by USB,
open the [web installer](https://hugelton.github.io/Felucca/) in Chrome or Edge, and press Install;
no extra hardware is needed. Installing is at your own risk: M-VAVE's updater or the installer's
**Return to official V15** takes you back.

Multi-engine synthesizer firmware for the M-VAVE FM-1. Please report what you find in
[Issues](https://github.com/hugelton/Felucca/issues).

- Install: [web installer](https://hugelton.github.io/Felucca/) (Chrome or Edge, USB), or `tools/fm1_install.py` from a terminal
- Editor: [web editor](https://hugelton.github.io/Felucca/webapp/editor/)
- Build: [BUILDING.md](BUILDING.md)

## Features

- **Thirteen engines** (below), each with its own factory presets
- **Four tracks**, one synth part each with its own engine and sound (drums are the DRUM engine or
  the SAMPLE engine's GM kit); 8 voices shared between them. ALGORITHM selects the track on every page
- **Sequencer:** 64 steps per track with chords, ties, accent, slide and per-step chance; a piano
  roll of the steps; a drum grid (white keys = steps, black keys = lanes); motion recording of knob
  moves; live loop recording with overdub; divisions from 1/32 to 4 bars; loading a sound never
  touches your patterns
- **Songs:** chain patterns A–D
- **Chord keys:** one finger plays an in-key chord (triads or sevenths of the scale, or fixed chord
  shapes), with voicings; on the keys, MIDI in, recording and the arpeggiator
- **Arpeggiator** with REPEAT and a beat LED, 16 scales with a white-key mode, glide,
  MONO / LEGATO / UNISON
- **Modulation matrix:** 4 slots per track, MIDI controllers as sources
- **Effects:** distortion and the SLICER per track; chorus, delay and reverb sends (the reverb as
  ROOM or SPRING); master limiter
- **FX layer:** hold FX for repeat, reverse, filter sweeps, tape stop, freeze and a harmonizer
  (OCT UP / OCT DN with shimmer), and mutes on the black keys
- **Quick layers:** hold FX, GLO, SCL or EDIT for shortcuts on the keys and knobs; one-step undo
  (SAVE held); REC on every page; OCT+ confirms, OCT- goes back
- **Presets:** factory presets, 32 user preset slots and 4 projects, named on the device;
  projects from every earlier version load
- **Screen:** flat UI with Inter Tight and Fukiai icons, 8 palettes including grayscale and high contrast
- **USB:** class-compliant MIDI in and out, and a 44.1 kHz stereo audio input ("Felucca") that
  records the master output on the computer, no driver needed
- **MIDI:** USB and TRS MIDI in; channels 1–4 play tracks 1–4 (other channels the selected track),
  and the keys send on the track's channel; pitch bend, sustain, panic; clock from internal, USB or TRS
- **Web:** editor for every parameter (with a 6-operator FM patch editor), step grid, mixer,
  preset library, sample upload and recording with trim; full backup and restore; return to the
  official firmware

## Controls

![FM-1 controls with Felucca 1.0](docs/panel.jpg)

- **SELECT** sets the BPM, **MASTER** the volume, **ALGORITHM** picks the track (T1–T4) and
  **PRESETS** its sound. **KNOB 1–4** edit the four columns of the page
- FX, SCL, ENV, LFO, EDIT, GLO, SAVE, ARP and SEQ open their pages; press again for the next page.
  HOME returns home
- **Held:** FX, GLO, SCL and EDIT open their quick layers; SAVE is undo, HOME the menu, SEQ the song
- PLAY starts and stops all four tracks; REC arms the selected track, on every page
- OCT− / OCT+ shift the octave (both: reset). On action pages, in dialogs and the menu, OCT+ does it
  and OCT− goes back
- Save a sound: stop, tap SAVE, pick a slot with KNOB 1, then OCT+ and OCT+ again (name it with the keys)

## Engines

In the order the device lists them:

- **ANALOG**: virtual analog; two oscillators, noise, drive, resonant low-pass filter
- **FM6**: classic 6-operator FM (Dexed-based): 32 algorithms, a full patch per track edited in the
  web editor, macros on the device, an algorithm chart on screen
- **PHASE**: phase distortion (ported from CrispyZebra)
- **LOFI**: chiptune; pulse, triangle, saw, noise and a 4-bit wave RAM, stepped envelope, sweep, arpeggio
- **SAMPLE**: multisampled instruments, a GM percussion set and 3 user sample slots
- **VOICE**: formant oscillator, sung vowels
- **TRIO**: 3 oscillators with ring modulation and sync, multimode filter
- **WHEEL**: tonewheel-style organ; drawbar registrations, percussion, key click, drive, rotary speaker
- **GRAIN**: granular textures from the built-in samples or a user slot
- **PHYS**: physical models: modal resonators, strings, struck membranes, sympathetic strings
- **NOISE**: noise from analog to digital: colours, crackle, shift-register and metallic tones
- **SLICE**: a drum break or your own sample cut into slices, one per key; set the slices by hand
  on the SLICES page
- **DRUM**: an 8-lane kit of Felucca's own drum voices on the General MIDI key map

The DIGITAL engine of 0.9 has been replaced by FM6: projects and presets with DIGITAL sounds load
as FM6 sounds converted from them.

**SLICER** (FX page, every track): a tempo-synced 16-step gate or stutter, with 16 patterns.

## Scale keyboard

On the **SCL** page, set **QNT** to WHITE to play the selected scale using only the
white keys (SNAP keeps every key and rounds it down to the scale). C4 plays **ROOT**; consecutive white keys play consecutive scale notes
above and below it. Black keys are silent, including during live recording and
step entry. **TRN** transposes the resulting notes; the octave buttons shift them
by full octaves. Set QNT to OFF for the normal chromatic keyboard.

Available scales: chromatic (CHR), major (MAJ), natural minor (MIN), Dorian (DOR),
Mixolydian (MIX), major pentatonic (PEN), minor pentatonic (MPEN), harmonic minor
(HARM), Phrygian (PHRY), Lydian (LYD), Locrian (LOC), ascending melodic minor (MEL),
minor blues (BLUES), whole tone (WHOLE), half-whole diminished (DIMHW), and
whole-half diminished (DIMWH). Scales with other than seven notes continue across
the white keys without repeating notes; their roots need not fall on every C key.
Drum kits and incoming MIDI keep their own note mapping.

Press **SCL** again for the **CHORD** page: CHRD picks the chord keys (OFF, the scale's triads or
sevenths, or a fixed shape) and VOIC the voicing.

## Layout

| Path | What |
| --- | --- |
| `firmware/` | firmware sources: `src/` app, `hal/` hardware layer, `loader/` update loader |
| `tools/` | build script, generators, package maker, installer and sample uploader |
| `assets/` | UI font, icon names, CC0 instrument samples |
| `web/` | web installer and editor sources |
| `tests/` | tests that run on the build machine |
| `LICENSES/` | licence texts of the bundled font, icons, ported DSP and SDK files |

## Support

If Felucca is useful to you, [sponsoring on GitHub](https://github.com/sponsors/hugelton) or a donation
on [itch.io](https://hugelton.itch.io/felucca) helps keep its development going.

Pull requests are welcome, and so are ideas and requests: post them in
[Discussions](https://github.com/hugelton/Felucca/discussions) or on X ([@kurogedelic](https://x.com/kurogedelic)).

## Credits

- **[Hügelton Instruments](https://hugelton.com)** (Leo Kuroshita, [@kurogedelic](https://github.com/kurogedelic)):
  Felucca itself; the PHASE engine's waveforms (a C port of the oscillator of
  [CrispyZebra](https://github.com/hugelton/CrispyZebra), GPL-3.0); the DRUM voices; the Hügelton Sample
  Pack (the drum samples, GPL-3.0-only, not CC0); the [Fukiai](https://github.com/hugelton/Fukiai) icon
  font ([MIT](LICENSES/MIT-Fukiai.txt))
- Font: [Inter Tight](https://github.com/rsms/inter-tight) by The Inter Project Authors, [SIL OFL 1.1](LICENSES/OFL-InterTight.txt)
- Samples: [Versilian Studios](https://versilian-studios.com/) [VSCO-2 Community Edition](https://github.com/sgossner/VSCO-2-CE) and [VCSL](https://github.com/sgossner/VCSL), CC0 1.0 ([attribution](assets/samples-cc0/ATTRIBUTION.txt))
- VOICE engine: after [klattsch](https://github.com/tgies/klattsch) by Tony Gies (MIT); formant data from Klatt (1980) and Hillenbrand et al. (1995)
- PHYS engine: models ported from [DaisySP](https://github.com/electro-smith/DaisySP) by Electrosmith and Emilie Gillet ([MIT](LICENSES/MIT-DaisySP.txt)) and from Emilie Gillet's [eurorack](https://github.com/pichenettes/eurorack) code ([MIT](LICENSES/MIT-Rings.txt))
- FM6 engine: msfa from [Dexed](https://github.com/asb2m10/dexed) by Google Inc. and Pascal Gauthier ([Apache-2.0](LICENSES/Apache-2.0-msfa.txt))
- Package format and boot files: [JieLi AC79 SDK](https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK) ([Apache-2.0](LICENSES/Apache-2.0.txt); three of its files are in every package, none in this tree)
- Contributions: [keremimo](https://github.com/keremimo) (white-key scales, #2), [ChanceTheMaker](https://github.com/ChanceTheMaker)
  (TRS MIDI, bend, sustain and clock, palettes, favourites, editor display settings: #8, #10, #11, #12),
  [andreahaku](https://github.com/andreahaku) (sample recording and trim, #29; SLICE manual slices and tests, #27, #22)

## Licence

Free software: [GPL-3.0-only](LICENSE), the Hügelton Sample Pack included. The bundled font and the
ported DSP keep their own licences ([LICENSES/](LICENSES/)); details in [LICENSING.md](LICENSING.md).

M-VAVE and FM-1 are trademarks of their respective owners. Felucca is not affiliated with or endorsed by them.

Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
