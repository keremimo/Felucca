# Melodee

[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)](LICENSE)

**TL;DR:** connect your FM-1 to a computer by USB, open the
[web installer](https://keremimo.github.io/melodee/) in Chrome or Edge, and press Install.
No extra hardware is needed. Use at your own risk; M-VAVE's own updater or the installer's
**Return to official V15** takes you back to the official firmware.
It saves a complete backup first. If your firmware cannot export one, you can select
**Skip backup** and confirm that Melodee music, sounds and settings may be lost.

Multi-engine synthesizer firmware for the M-VAVE FM-1. Current release: **Melodee 0.10**
([what's new](#whats-new-in-010)).

Melodee is a modified version of [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita
([@kurogedelic](https://github.com/kurogedelic)), [Hügelton Instruments](https://hugelton.com), and
follows Felucca 1.0's design. Melodee is not affiliated with or endorsed by Hügelton Instruments;
please report Melodee problems here, not to Felucca. The installer also finds an FM-1 that still
runs Felucca.

- Install: [web installer](https://keremimo.github.io/melodee/) (Chrome or Edge, USB), or `tools/fm1_install.py` from a terminal
- Editor: [web editor](https://keremimo.github.io/melodee/webapp/editor/)
- Build: [BUILDING.md](BUILDING.md)

## What's new in 0.10

Melodee 0.10 is built on Felucca 1.0: its screen design, quick layers, sequencer and file formats.
On top of Felucca 1.0 it adds:

- **FM6** renders DX7 voices sample for sample as Dexed, with up to 16 voices, operator pages on the
  device and DX7 SysEx import. Each track keeps its own function settings (MODERN / MARK I / OPL,
  pitch bend, portamento and controllers), saved with the project
- **KIT 808** on the DRUM engine: TR-808 circuit models on the eight lanes
- **USB audio:** Melodee Out plays the computer through the FM-1, Melodee In records the four tracks
  as separate channels; each can be switched off
- **Patterns:** eight banks per track, with songs that pick a bank for each track
- **Step editing:** set a note's length while its key is held, resize it with ENV, move it with SCL,
  delete it with EDIT, and undo or redo up to eight edits; MIDI step entry; live recording onto the
  playing step, with the cursor following it
- **Startup:** a BOOT project loaded at power-on and a template for new projects; CLK, TUNE, MIDI and
  ROUT kept between starts
- **Panel:** HOME names the notes and chords you play; key lights for the scale and the sounding
  notes (the menu's LIGHTS); REC + PLAY records at once and REC held opens the MIXER; SAVE + REC
  saves the project to its slot; BPM and swing on SEQ > TEMPO, saved with the project
- **MIDI:** a DRUM channel (10 by default) for the first DRUM track; QNT ALL and MPC pad layouts that
  incoming MIDI follows too; a more reliable TRS input; GLO > SYSTEM shows the input's activity

**Upgrading from an earlier Melodee:** device data starts fresh. Projects, settings, templates and
user presets saved by earlier Melodee versions are not imported, and the user sample slots are gone
(see [Startup and compatibility](#startup-and-compatibility)). Felucca 1.0 projects load.

## Features

- **Thirteen engines** (below), each with its own factory presets
- **Four tracks**, one synth part each with its own engine and sound (drums are the DRUM engine or
  the SAMPLE engine's GM kit); up to 16 FM6 voices or 8 voices from the other engines shared between
  them (an FM6 voice uses one budget unit, another engine's voice two; 16 units total).
  ALGORITHM selects the track on every page
- **Sequencer:** 64 steps per track with chords, ties, accent, slide and per-step chance; a piano
  roll of the steps; a drum grid (white keys = steps, black keys = lanes); motion recording of knob
  moves; live loop recording onto the playing step with overdub; MIDI step entry; tied-note length,
  movement and deletion; eight-level manual step undo/redo; divisions from 1/32 to 4 bars;
  loading a sound never touches your patterns
- **Patterns:** eight independent 64-step banks per track, with up to four notes per step, ties,
  chance, drum hits and bank-specific motion (64 automation events shared across the project)
- **Songs:** up to 16 rows, each choosing a bank for every track and repeating 1–16 times
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
  a startup project and a template for new projects; compatible upstream projects from earlier versions load
- **Screen:** flat UI with Inter Tight and Fukiai icons, 8 palettes including grayscale and high contrast;
  HOME shows the played notes and recognized chords above the live waveform, retaining the last voicing after release
- **Lights:** the keys that play glow (the scale's notes with QNT OFF, every key of a kit), a key lights up while
  its note sounds, MIDI in too, and the idle buttons glow; the HOME-held menu's LIGHTS sets the level (OFF: only
  what is pressed or engaged)
- **USB:** class-compliant MIDI in and out; **Melodee Out** plays the computer through the FM-1,
  **Melodee In** records four mono tracks (one channel per track, after level and before pan, sends
  and master effects). Both support 16/24-bit audio at 44.1 kHz; each can be disabled in the
  HOME-held menu's USB AUDIO setting. No driver needed
- **MIDI:** USB and TRS MIDI in; channels 1–4 play tracks 1–4, channel 10 the first DRUM track (GLO >
  SYSTEM **DRUM**: any channel or OFF; no DRUM track: the selected one), other channels the selected track,
  and the keys send on the track's channel; pitch bend, sustain, panic; clock from internal, USB or TRS;
  GLO > SYSTEM KNOB 1 shows the USB or the TRS input's status (RX while it receives; both always play)
- **Web:** editor for every parameter (with a 6-operator FM patch editor), step grid, mixer,
  preset library and FM6 bank; full backup and restore; return to the official firmware

## Controls

![FM-1 controls](docs/panel.jpg)

- **SELECT** turns the pages of the open section (both ways), on STEP too (KNOB 1 moves its cursor); **MASTER** the
  volume, **ALGORITHM** picks the track (T1–T4) and **PRESETS** its sound. **KNOB 1–4** edit the four columns
  of the page
- **BPM** and the song's **SWG** are on **SEQ > TEMPO**: the project's, saved and loaded with it (power-on: the
  BOOT project's, the template's or 120); GLO > GLOBAL keeps CLK and TUNE, the device's
- FX, SCL, ENV, LFO, EDIT, GLO, SAVE, ARP and SEQ open their pages; press again for the next page.
  HOME returns home
- **Held:** FX, GLO, SCL and EDIT open their quick layers; SAVE is undo, HOME the menu, SEQ the song,
  REC the MIXER.
  When editing synth steps, SCL and EDIT use the editing controls below instead; FX keeps its layer
- PLAY starts and stops all four tracks; REC arms the selected track, on every page (and starts the
  transport when it is stopped); **REC + PLAY** arms it and starts recording in one gesture
- OCT− / OCT+ shift the octave (both: reset). On action pages, in dialogs and the menu, OCT+ does it
  and OCT− goes back. During synth step editing, OCT− / OCT+ move the step cursor
- Save a sound: stop, tap SAVE, pick a slot with KNOB 1, then OCT+ and OCT+ again (name it with the keys)
- **SAVE + REC** saves the project back to the slot it was loaded from or last saved to (`SAVED B`), stopping
  the transport first; a new project opens SAVE > PROJECT on a free slot

### Patterns and songs

Hold **SEQ** and press one of the first eight white keys to pick pattern 1–8. Hold a pattern key
and press a second one to copy its notes, ties, timing, chance, drum hits and automation. **SEQ + SELECT**
also chooses a pattern. While playing, each track changes at its own loop end; the queued key blinks.
Selecting the active pattern cancels a queued change. STOP applies pending choices.

On **SEQ > SONG**, KNOB 1 chooses the row, **ALGORITHM** chooses the track, KNOB 2 chooses that
track's pattern, and KNOB 3 sets repeats. PLAY runs the arrangement; rows change all four tracks
at track 1's loop boundary and the last row stops. STOP returns to the patterns selected before
SONG. Project saves and complete backups include all 32 banks and the arrangement.

### Step editing

On **SEQ > STEP**, KNOB 1 picks the step, KNOB 2 changes its note, KNOB 3 **TIME** picks
NOTE / TIE / REST, and KNOB 4 sets accent and slide. On a synth track:

- Hold a key or MIDI chord, turn **SELECT** to set its length, then release to advance past the whole note.
- Hold **ENV** and turn **SELECT** to resize an existing note, from its onset or any of its ties.
- Hold **SCL** and turn **SELECT** to move the whole note, with its ties. Movement and lengthening
  stop before another note; both work across the pattern's loop.
- **KNOB 1** does the same as SELECT while ENV, SCL or a key is held. These gestures also work while the
  track is armed and playing: the cursor stops following the play head while ENV or SCL is held, and
  OCT− / OCT+ keep shifting the octave while you record live.
- Tap **EDIT** to delete the selected note and its ties. A DRUM grid EDIT clears just that step.
- Hold **SAVE** to undo the last manual edit. With **EDIT** or **SAVE** down, **OCT−** undoes and **OCT+**
  redoes, up to eight edits. A held entry or move counts as one edit. A new edit clears redo;
  recording, loading another pattern, changing track or pattern length, or external step edits start
  a fresh history. With no manual edit to undo, held SAVE retains the sound/pattern-load undo.

Using SELECT consumes the ENV/SCL page tap; a tap without an edit still opens that page. A final
SELECT turn arriving with the key or modifier release is included. With no note or editing modifier
held, SELECT turns the pages; KNOB 1 moves the cursor. PRESETS continues to browse sounds on HOME and PRESETS,
and selects FM6 operators on their pages.

Armed live recording writes the step currently playing, and the STEP cursor follows it. The DRUM
grid keeps its white-key step and black-key lane controls.

### Startup and compatibility

On **SAVE > PROJECT**, KNOB 2 **BOOT** selects OFF or project A–D to load at power-on.
KNOB 1 **SLOT** also offers **TMPL**: save your sounds and settings there as the template for new
projects, with empty patterns. BOOT OFF uses the template when one is saved. CLK, TUNE, MIDI
and ROUT persist between starts; loading a project or template applies its own settings. DRUM is the
device's own setting.

Projects use the FBK9 format: all 32 banks, their timing, arrangement and automation. Felucca 1.0
projects load into pattern 1; their old project-based SONG rows are cleared. Pre-1.0 Melodee's
multi-pattern projects/settings/templates and incompatible 58/62-parameter user presets are not imported.
User sample slots USR1–3 and sample uploads are removed; their flash space now stores projects.
Built-in samples, drum kits and BREAK remain available. Earlier sample data is overwritten as projects
are saved. Complete backups with nonempty user samples require firmware that supports those slots.
The FM6 bank has explicit conversion for earlier Melodee and Felucca banks.

## Engines

In the order the device lists them:

- **ANALOG**: virtual analog; two oscillators, noise, drive, resonant low-pass filter
- **FM6**: classic 6-operator FM (Dexed-based): 32 algorithms, a full patch per track edited in the
  web editor or on the device; operator frequency, levels, envelopes and scaling, pitch envelope,
  LFO, STORE and DX7 SysEx; an algorithm chart on screen. PRESETS selects the operator on operator
  pages. Each track keeps its own patch and function settings (MODERN / MARK I / OPL, pitch bend,
  portamento, wheel, foot, breath and aftertouch), saved with the project and the template
- **PHASE**: phase distortion (ported from CrispyZebra)
- **LOFI**: chiptune; pulse, triangle, saw, noise and a 4-bit wave RAM, stepped envelope, sweep, arpeggio
- **SAMPLE**: multisampled instruments and a GM percussion set
- **VOICE**: formant oscillator, sung vowels
- **TRIO**: 3 oscillators with ring modulation and sync, multimode filter
- **WHEEL**: tonewheel-style organ; drawbar registrations, percussion, key click, drive, rotary speaker
- **GRAIN**: granular textures from the built-in samples
- **PHYS**: physical models: modal resonators, strings, struck membranes, sympathetic strings
- **NOISE**: noise from analog to digital: colours, crackle, shift-register and metallic tones
- **SLICE**: the built-in drum break cut into slices, one per key
- **DRUM**: an 8-lane kit of Felucca's own drum voices on the General MIDI key map; **KIT 808**
  plays TR-808 circuit models instead

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

QNT **ALL** plays the next scale note on every key, black keys included. QNT **MPC** maps an
MPC's Bank H pads (MIDI 20–35, H01–H16 of MPC Sample's default map) to successive scale notes;
the **MPC** page in the SCL family sets **DEG**, the degree pad H02 plays. Incoming MIDI follows
WHITE, ALL and MPC the way the keys do (without the octave buttons). SCL, QNT and DEG are shared by
all four tracks; ROOT and TRN stay per track. Drum kits and slices keep their own note mapping.

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

Melodee lives at <https://github.com/keremimo/melodee>: bug reports, ideas and pull requests are
welcome there.

Felucca, which Melodee is built on, is Leo Kuroshita's work. If Melodee is useful to you, consider
[sponsoring him on GitHub](https://github.com/sponsors/hugelton) or supporting Felucca on
[itch.io](https://hugelton.itch.io/felucca).

## Credits

- Melodee by Ellic Studio (Kerem Kilic, [@keremimo](https://github.com/keremimo))
- Based on [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita ([@kurogedelic](https://github.com/kurogedelic)), [Hügelton Instruments](https://hugelton.com)
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

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments. M-VAVE and FM-1 are
trademarks of their respective owners. Melodee is not affiliated with or endorsed by any of them.

Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments\
Modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio)
