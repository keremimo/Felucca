# Melodee

[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)](LICENSE)

**TL;DR:** connect your FM-1 to a computer by USB, open the
[web installer](https://keremimo.github.io/melodee/) in Chrome or Edge, and press Install.
No extra hardware is needed. Beta: use at your own risk; M-VAVE's own updater takes you back
to the official firmware.

Multi-engine synthesizer firmware for the M-VAVE FM-1.

Melodee is a modified version of [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita
([@kurogedelic](https://github.com/kurogedelic)), [Hügelton Instruments](https://hugelton.com). It
started as a fork and has since gone its own way: USB audio, TRS MIDI in, MIDI clock, the FM6 engine,
the TR-808 drum kit, multiple patterns per track and the STUDIO workspaces, among others. Melodee is
not affiliated with or endorsed by Hügelton Instruments; please report Melodee problems here, not
to Felucca. The installer also finds an FM-1 that still runs Felucca.

![FM-1 controls](docs/panel.jpg)

## Features

- **Ten engines** (below), each with its own factory presets
- **Four tracks:** three synth parts, each with its own engine and sound, plus an analog drum track;
  8 voices shared between the parts. ALGORITHM selects the track on every page
- **Sequencer:** 8 patterns of 64 steps per track with chords, ties, accent and slide; live loop
  recording with overdub and held notes; each track loops on its own length and switches
  patterns at the end of its loop
- **Arpeggiator**, scales and quantize, glide, MONO / LEGATO / UNISON voice modes
- **Effects:** distortion and the SLICER per track; chorus, delay and reverb sends; master limiter
- **Presets:** factory presets with their own patterns, 32 user preset slots, 4 project slots
- **Web editor:** every parameter of every track, step grid, track mixer, preset library, sample upload
- **USB:** class-compliant MIDI in and out (channels 1–3 for the parts, 10 for drums);
  updates over the same USB cable
- **Experimental USB audio (enabled by default):** four isolated mono inputs (three synth tracks and drums)
  and a separate stereo output device (computer playback), each host-selectable 16/24-bit
  at native 44.1 kHz, alongside MIDI. Tested on macOS; the USB serial console is disabled.
  GLO > SYSTEM knobs 2 and 3 switch the output and input devices on or off (saved; USB reconnects).
  See [USB audio build instructions](BUILDING.md#experimental-usb-audio-mode).
- **TRS MIDI IN:** enabled by default, with the same channel routing and scale
  mapping as USB MIDI; supports pitch bend, mod-wheel vibrato, sustain and MIDI panic;
  [controls and limits](docs/MIDI-EXPRESSION.txt)
- **Note readout:** HOME opens the notes or chord last played on the synth parts (keys, USB and TRS MIDI),
  as they sound after the scale, above the live output waveform; white while held, kept after the release,
  including with FM6 sounds
- **MIDI status:** GLO > SYSTEM knob 1 switches the first column between USB status and TRS status/activity; both inputs remain active

## Device workspaces

**EDIT** opens the instrument. Tracks 1–3 are synths; track 4 is the drum kit. Turn **ALGORITHM**
to pick a track and **PRESETS** to pick a sound. Hold EDIT and tap one of the first eight white
keys to recall that engine's first eight sounds; on FM6 those keys pick operators instead. The
black keys jump to deeper sound controls. **ENV**, **FX**, **LFO**, **SCL** and **ARP** open their
modules directly; tap a module button again for its next control surface.

**SEQ** opens LOOP, a four-track view of the current 16-step bank. Turn **PRESETS** to queue the
selected track's next pattern, **ALGORITHM** to change track, and **OCT−/OCT+** to move the step
cursor. Tap SEQ again for STEP entry; **FX** clears the current step there. Hold SEQ and use the
lit white keys to pick or copy patterns. The patterns remain note data, so changing a sound also
changes what its recorded notes play.

**HOME** opens the note/chord view and live output waveform, with one track level per knob. Tap HOME again for the four-track
mixer, then for pan controls, then for master delay and reverb controls with a scope. Turn **ALGORITHM** or **PRESETS** to
select a track. Tap **REC** in
any musical workspace to arm that track and start playback if stopped, or press **REC + PLAY**
to begin recording in one gesture. **PLAY** stops or starts the transport. Hold REC to open
TRACKS for length, pan and the full pattern mixer. Hold HOME for Settings. **SAVE** opens the
sound and project library; its next pages are USER, PROJECT and TOOLS.

The center drawing is the focus of each workspace, with four knob values below it. The STUDIO
look gives knobs 1–4 the same blue, coral, yellow and green cues on every page. The instrument
has distinct synth and drum scenes, LOOP shows all four tracks, and the mixer shows levels and
activity. The top strip shows the track, sound and page; the narrow bottom strip shows the
current 16-step bank.

STUDIO is selected on a fresh installation. If the FM-1 already has a saved colour choice, hold
**HOME**, turn **PRESETS** to **COLOR**, turn **knob 1** until **STUDIO** appears, then press
**OCT-** to save and leave Settings. **ZOOM** in the same menu controls the large value readout
that appears when a knob turns; **KEYS** sets how bright the unplayed keys glow.

Every button glows dimly; it lights fully while held and while it is engaged: the open page's
button (HOME, ENV, FX, SAVE, GLO ...), EDIT on the instrument pages, PLAY (blinking while playing),
REC while recording, OCT- / OCT+ while the octave is shifted.

## Engines

- **ANALOG**: virtual analog; two oscillators (saw, square, triangle, sine, PWM), noise, drive, resonant low-pass filter
- **DIGITAL**: 4-operator FM, 8 algorithms, feedback
- **PHASE**: phase distortion (ported from CrispyZebra)
- **LOFI**: chiptune; pulse, triangle, saw, noise and a 4-bit wave RAM, stepped envelope, sweep, arpeggio
- **SAMPLE**: multisampled instruments and a user sample slot
- **VOICE**: formant oscillator, sung vowels
- **TRIO**: 3 oscillators with ring modulation and sync, multimode filter (LP / BP / HP / notch)
- **WHEEL**: tonewheel-style organ; drawbar registrations, percussion, key click, drive, rotary speaker
- **GRAIN**: granular textures from the built-in samples or a user slot
- **FM6**: six-operator FM that plays DX7 voices sample for sample as Dexed does (its MODERN,
  MARK I and OPL engines, 16 voices, the DX7 functions: bend, portamento, wheel / foot / breath /
  aftertouch); every operator edited on the device; DX7 voices and banks over USB-MIDI ([below](#fm6))

**SLICER** (FX page, every track including drums): a tempo-synced 16-step gate or stutter, with 16 patterns.

**Drums** (track 4): an analog drum kit modelled on the TR-808's voice circuits, synthesized live: bass
drum, snare, low / mid / high tom and conga, rim shot, claves, hand clap, maracas, cowbell, cymbal, open and
closed hi-hat, on the General MIDI percussion keys (MIDI channel 10 by default). As on the original, each
instrument is one circuit: a hit kicks it again while it rings, the bass drum's pitch rises on its first
millisecond and sighs as it decays, and velocity is the accent (louder hits are also brighter). EDIT 1 / 2 of
the drum track: bass drum TONE, DECAY and TUNE, snare TONE and SNAPPY, tom / conga tuning, open hi-hat and
cymbal DECAY (0 is the stock setting).

- Install: [web installer](https://keremimo.github.io/melodee/) (Chrome or Edge, USB), or `tools/fm1_install.py` from a terminal
- Editor: [web editor](https://keremimo.github.io/melodee/webapp/editor/)
- Build: [BUILDING.md](BUILDING.md)

## FM6

FM6 plays DX7 voices the way Dexed (Pascal Gauthier's DX7 emulation, on Raph Levien's MSFA) plays
them: six operators, the 32 DX7 algorithms, four-rate / four-level envelopes with the DX7's attack
curve and static times, level and rate key scaling, velocity, feedback, pitch envelope, LFO, pitch
bend, portamento and the controllers, in Dexed's code restated for the FM-1 (fixed point, its
tables, its 64-sample blocks). A part renders the same samples Dexed renders: `tests/fm6_parity.sh`
plays hundreds of scores (the factory voices and random DX7 voices; chords, voice stealing, bend,
the controllers, portamento, mono) through Dexed's own code and through FM6 and compares them sample
by sample; they are equal. **ENGINE** (EDIT 2) is Dexed's engine resolution: **MARK I** (the DX7's
log-sine and exponent tables and its 2- and 3-operator feedback loops in algorithms 6 and 4; the
default, as in Dexed), **MODERN** (MSFA's 24-bit sine) or **OPL**. FM6 has 16 voices, chosen and
handed over as Dexed chooses them. 16 factory voices (VOICE R01–R16) come with presets of their own;
32 user voices (U01–U32) live in flash.

The DX7 functions are the part's (as Dexed's are the plugin's), saved with the project:

- **FM BEND**: BEND+ / BEND- (pitch-bend range up and down, semitones), STEP (0 = smooth, else
  steps of that many semitones), DX VEL (Dexed's velocity scaling to the DX7's range).
- **FM PORTA**: PORTA (PEDAL: while CC 65 is down, as in Dexed; ON: always), TIME (CC 5 sets it),
  GLISS (glissando: semitone steps).
- **FM WH/FT**, **FM BR/AT**: the range (0–99) and target of the mod wheel, foot controller (CC 4),
  breath controller (CC 2) and channel aftertouch: **P** pitch (LFO depth), **A** amplitude (LFO
  depth), **E** EG bias, or a mix. The wheel starts at 99 to pitch: vibrato for voices with a PMS.
- **MONO**: the part's VOICE mode **LEGATO** with PRIO **HIGH** is Dexed's mono mode.

On FM6 parts the generic wheel vibrato and the part's bend range give way to these.

**EDIT** steps through the pages. **PATCH**: VOICE, then MOD (the modulators'
levels: brightness), M.TIM and C.TIM (the modulators' and carriers' envelope times); these are
Melodee's own and neutral at 0. Then **STORE**, and the voice itself: **ALGO** (algorithm, feedback,
key sync, transpose), six operator pages (**FREQ**, **OUT**, **EG RATE**, **EG LVL**, **SCALE**,
**CURVE**), the pitch envelope and two LFO pages. On the operator pages **PRESETS** picks the
operator (OP1–OP6); the graph shows the algorithm with it highlighted.

Hold **EDIT** to jump instead: the black keys light up as a map of the EDIT pages, in page order
from F#3 — PATCH, STORE, ALGO, FREQ, OUT, EG RATE / LVL, SCALE / CURVE, PITCH EG / LV, LFO 1 / 2,
VOICE / VOICE 2, FM BEND / FM PORTA (a key that holds two pages switches between them when pressed
again; the controller pages follow FM PORTA). The white
keys F3–D4 pick OP1–OP6. The key of the current page and operator blinks. While EDIT is held the
keys play nothing; let go and they play again, so edits can be heard right away. A short tap of
EDIT still goes to the next page. Other engines get their EDIT pages on the same keys.

Edits change the part's voice at once (held notes go on from their third envelope segment, as in
Dexed) and stay until another VOICE or preset is loaded; a new VOICE or voice dump stops the notes,
as a program change does in Dexed. **STORE**
keeps them: SLOT picks a user voice, STORE writes it there (two detents, like the other GO
buttons) and VOICE follows it. SLOT starts on the user voice the part plays (when the voice still
has its name), else on the first free (INIT VOICE) slot. SEND sends the voice as a DX7 single-voice
dump, INIT starts from the DX7 init voice. Projects save each FM6 part's voice with its edits; user
presets save its VOICE number.

Over USB-MIDI FM6 takes DX7 SysEx on any channel, so Dexed or any DX7 librarian can edit a
voice live or load a cartridge:

| SysEx | What FM6 does |
| --- | --- |
| `F0 43 0n 00 01 1B` + 155 bytes + checksum `F7` (a voice) | into the FM6 part's voice |
| `F0 43 0n 09 20 00` + 4096 bytes + checksum `F7` (32 voices) | into U01–U32, saved in flash |
| `F0 43 1n gg pp dd F7` (a voice parameter) | into the FM6 part's voice |
| `F0 43 1n 08 pp dd F7` (a function parameter: 64 mono, 65 bend range, 66 step, 68 glissando, 69 portamento time, 70–77 wheel / foot / breath / aftertouch range and target) | into the FM6 part's functions |
| `F0 43 2n 00 F7` / `F0 43 2n 09 F7` (dump requests) | sends the part's voice / the user bank |

The FM6 part is the selected track when it plays FM6, else part n + 1, else the first FM6 part.

In the web editor, **Library → Import** accepts DX7 `.syx` single voices and 32-voice banks
alongside Melodee JSON files. Each bank voice becomes a separate named library entry; importing
works offline and checks the dump's length and checksum. Library JSON exports retain the voice data.
Select a synth track and **Audition** to send a voice to FM6; **Keep in U03** (the selected FM6 slot,
else the first free one) stores it in the user bank, and the auditioned track goes on playing it from
there, so user presets and projects recall it.

**FM6 voices on the device** lists U01–U32 by name: drag a DX7 library voice onto a slot to store
it, drag a slot to the library to copy it, **Play on track** (or a double click) sets the selected
track's VOICE to it. **Import .syx** replaces the whole bank with a 32-voice dump (after asking) or
puts single voices into free slots; **Save as .syx** downloads the bank. Every change reads the bank,
writes it back whole with the 32-voice dump and checks the device's readback. The **Sound** tab's
VOICE shows the user voices by name too. DX7 voices do not go into the user presets (those store
only a VOICE number, not the voice data).

## Scale keyboard

On the **SCL** page, **QNT** selects the keyboard layout:

- **OFF:** normal chromatic keyboard.
- **SNAP:** every key plays, rounded down to the scale. This is the previous ON
  mode; existing saved sounds and projects retain it.
- **WHITE:** consecutive white keys play consecutive scale notes; black keys are
  silent, including during live recording and step entry.
- **ALL:** every key, white or black, advances one scale degree without duplicate
  pitches. For C major, C, C#, D, D#, E, F, F#, G play C, D, E, F, G, A, B, C.
- **MPC:** the FM-1 keys use WHITE. Incoming MIDI notes 20–35 from MPC Sample
  Bank H play consecutive scale degrees. H02 (MIDI note 21) defaults to ROOT in
  the octave selected on the FM-1; H01 plays the degree below it. All other MIDI notes are
  ignored, including during live recording and step entry.

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
During normal playing, the key LEDs show the selected track's layout, dimly lit.
OFF highlights notes in the chosen ROOT/SCL (all keys for CHR) without changing
their pitches; SNAP and ALL light every sounding key; WHITE and MPC light the
white keys. A key turns bright while it is held or while its pitch plays from
USB or TRS MIDI on the selected track. Hold **HOME** and set **KEYS** to choose
how bright the rest of the layout is: OFF, LOW, MID (default), HIGH or FULL (as
bright as a played key). Holding EDIT or SEQ shows their shortcut lights instead.
**GLO > LIGHTS** knob 1 turns the idle lights off (saved): no layout and no idle
button glow. Keys still light while they play (held or from MIDI); holding EDIT or
SEQ, the keys that do something there glow dimly and a pressed key lights. Engaged
and held buttons still light. Turning it back ON, or
setting **KEYS**, brings the rest back.

Incoming MIDI uses the receiving track's **WHITE** or **ALL** layout: MIDI note 60
(C4) plays ROOT, and each participating key advances one scale degree.
**TRN** applies to the mapped notes. In WHITE and ALL, the FM-1's octave buttons
affect only its own keys; use the external keyboard's octave controls for MIDI input.
In MPC mode, the FM-1 octave buttons also shift the mapped H-bank notes.
With QNT at OFF or SNAP, incoming MIDI notes pass through unchanged.

For MPC Sample, set **PAD MIDI OUT** to **Empty** (or **Always**) in its MIDI
Configuration, select the empty **H** bank, and set the FM-1's **QNT** to **MPC**.
Select **ROOT**, **SCL**, and the FM-1 octave first; **TRN** still transposes the
result. The H bank mapping uses the MPC Sample's default pad assignments:
H01–H16 send MIDI notes 20–35 in order. A project with a custom pad note map
needs its H bank restored to those assignments.
Out-of-range mapped notes are silent. MPC mode follows the usual MIDI channel
routing, so use a synth-part channel or one that follows the selected track.
The H-bank input filter also applies to the drum channel, GM sample kits and
SLICE; those destinations retain their existing mapping for accepted notes.

With **QNT = MPC**, tap **SCL** again to open the **MPC** subpage. **Knob 1
(DEG)** selects which scale degree H02 plays; the display previews the resulting
note, for example `H02: 3 -> E4` in C major. H01 is one degree below H02 and
H03–H16 continue upward through the scale. ROOT stays unchanged. DEG ranges from
1 to the number of notes in the scale (7 for major/minor, 5 for pentatonic,
12 for chromatic); switching to a shorter scale clamps it to that scale's last
degree. DEG is shared across the synth parts, saved in projects and retained
when changing presets. Older projects default to degree 1. This subpage is hidden
in other quantization modes and on the drum track. The panel keys keep their
WHITE layout; DEG affects mapped MPC notes, including recording and STEP entry.

Channels 1–3 play parts 1–3; the configured drum channel plays drums; other
channels play the selected track. Set ROOT, SCL and QNT on that receiving part.
Mapped notes feed its arpeggiator, live recording and **SEQ > STEP** entry.
Note-offs release the pitch and part chosen at note-on, even if settings or the
selected track change.

This applies to USB MIDI routed from a computer and to TRS MIDI input, enabled
by default (`MELODEE_UART=1`). TRS also accepts MIDI clock and transport;
system common and SysEx remain unsupported. End-to-end TRS timing still needs
verification with an external MIDI source.

## Sequencer note length

Tap **SEQ** twice from another workspace to reach STEP, then play notes from the FM-1 keys or
external USB/TRS MIDI. Notes
enter at the cursor; chords use up to four notes in POLY mode. Hold a key or chord
and turn **PRESETS** to change its length. Clockwise extends it;
counterclockwise shortens it to a minimum of one step. Release all keys to advance
the cursor past the note. When no note is held, STEP moves the cursor and PRESETS selects the
track's pattern. **TIME** on knob 3 still selects NOTE, TIE or REST.
The readout above the piano roll shows `HOLD + PRESETS: 4 STP`, for example. Ties
are added and removed automatically.

Lengths can cross a 16-step bank or the pattern's loop boundary, up to one full
pattern. Extensions stop before another note; shortening clears only the removed
ties. Drum hits remain one-shot. The piano roll shows sustained chords across
banks, and existing projects keep using the same NOTE/TIE representation.

## Patterns

Each track has 8 patterns. Each pattern has its own steps and its own **LEN**, **DIV**,
**SWING** and **GATE** (SEQ > LOOP). Press **SEQ** for the four-track LOOP overview. Hold **SEQ**:
the white keys F3–F4 are
patterns 1–8 of the selected track. A lit key holds notes, the playing pattern blinks, and a
queued pattern blinks fast.

- **Pick:** tap a key (on release). Stopped, the pattern changes at once. Playing, it changes
  when the track's loop ends, so each track switches on its own length. Picking the playing
  pattern cancels a queued switch.
- **Copy:** hold one key and press another: the held pattern is copied there, with its LEN etc.
- A pattern never played starts with the LEN etc. of the pattern it follows.

While SEQ is held the keys play nothing. A short tap of SEQ switches LOOP and STEP. The top
line of the SEQ pages shows the pattern, with the queued one after it: `P2>5 STEP`. Projects
store every pattern of every track. Hold REC to open TRACKS; holding it there offers to clear
all 8 patterns of the track. The web editor shows and edits the playing pattern.

Projects from older firmware load with their pattern as pattern 1. Projects now live where user
sample slots 2 and 3 were: one user sample slot (USR1) remains.

## MIDI clock and transport

In **GLO > GLOBAL > CLK**, select **INT**, **USB**, or **TRS**. USB and TRS follow
MIDI Clock (24 pulses per quarter note) and Start, Stop, and Continue from the
selected input; the other input can still play notes and expressive controls.
Start resets the patterns to step 1, while Continue resumes their current steps.
The sequencer stops and releases its notes if clock disappears for 500 ms. The
displayed BPM, arpeggiator, delay, and SLICER follow the measured tempo. TRS MIDI
IN is enabled by default (`MELODEE_UART=1`).

## Layout

| Path | What |
| --- | --- |
| `firmware/` | firmware sources: `src/` app, `hal/` hardware layer, `loader/` update loader |
| `tools/` | build script, generators, package maker, installer and sample uploader |
| `assets/` | icon atlas, font, CC0 instrument samples |
| `web/` | web installer and editor sources |
| `tests/` | tests that run on the build machine |

## Support

Melodee lives at <https://github.com/keremimo/melodee>: bug reports, ideas and pull requests are
welcome there.

Felucca, which Melodee is built on, is Leo Kuroshita's work. If Melodee is useful to you, consider
[sponsoring him on GitHub](https://github.com/sponsors/hugelton) or supporting Felucca on
[itch.io](https://hugelton.itch.io/felucca).

## Credits

- Melodee by Ellic Studio
- Based on [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita ([@kurogedelic](https://github.com/kurogedelic)), [Hügelton Instruments](https://hugelton.com)
- Font: [Terminus](https://terminus-font.sourceforge.net/) by Dimitar Toshkov Zhekov, [SIL OFL 1.1](assets/fonts/Terminus-LICENSE.txt)
- Samples: [Versilian Studios](https://versilian-studios.com/) [VSCO-2 Community Edition](https://github.com/sgossner/VSCO-2-CE) and [VCSL](https://github.com/sgossner/VCSL), CC0 1.0 ([attribution](assets/samples-cc0/ATTRIBUTION.txt))
- PHASE engine: oscillator ported from [CrispyZebra](https://github.com/hugelton/CrispyZebra) by Leo Kuroshita (GPL-3.0)
- VOICE engine: after [klattsch](https://github.com/tgies/klattsch) by Tony Gies (MIT); formant data from Klatt (1980) and Hillenbrand et al. (1995)
- FM6 engine: [Dexed](https://github.com/asb2m10/dexed) by Pascal Gauthier and MSFA (Google), restated in fixed point (GPL-3.0-or-later, Apache-2.0)
- Web editor icons: Fukiai by [Hügelton Instruments](https://hugelton.com), [MIT](web/FUKIAI-LICENSE.txt)
- Package format and boot files: [JieLi AC79 SDK](https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK) (Apache-2.0, not included)

## Licence

Code: [GPL-3.0-only](LICENSE). Third-party material: [LICENSING.md](LICENSING.md).

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments. M-VAVE and FM-1 are
trademarks of their respective owners. Melodee is not affiliated with or endorsed by any of them.

Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments\
Modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio)
