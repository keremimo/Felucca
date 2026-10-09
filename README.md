# Melodee

[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)](LICENSE)

**TL;DR:** connect your FM-1 to a computer by USB, open the
[web installer](https://keremimo.github.io/melodee/) in Chrome or Edge, and press Install.
No extra hardware is needed. Use at your own risk; M-VAVE's own updater or the installer's
**Return to official V15** takes you back to the official firmware.
It saves a complete backup first. If your firmware cannot export one, you can select
**Skip backup** and confirm that Melodee music, sounds and settings may be lost.

Multi-engine synthesizer firmware for the M-VAVE FM-1. Current release: **Melodee 0.13.1**
([what's new](#whats-new-in-0131)).

Melodee is a modified version of [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita
([@kurogedelic](https://github.com/kurogedelic)), [Hügelton Instruments](https://hugelton.com), and
follows Felucca 1.0's design. Melodee is not affiliated with or endorsed by Hügelton Instruments;
please report Melodee problems here, not to Felucca. The installer also finds an FM-1 that still
runs Felucca.

- Install: [web installer](https://keremimo.github.io/melodee/) (Chrome or Edge, USB), or `tools/fm1_install.py` from a terminal
- Editor: [web editor](https://keremimo.github.io/melodee/webapp/editor/)
- Build: [BUILDING.md](BUILDING.md)

## What's new in 0.13.1

- **PROPHET Lo Freq:** oscillator B's Lo Freq switch drops it seven octaves instead of ten, as measured
  on a Rev 4 recording, and a new note finds it anywhere in its cycle, as on the free-running original.
  Programs that use it as a slow modulator (Pickle Pincher, FLUTES, TRUMPET FLUTE and others) move at
  their intended rate; Pickle Pincher no longer falls silent after the first note
- **PROPHET filter:** smoother saturation at the filter's last stage and output removes a faint grain
  that became audible as the filter closed, for example in Internalized's sustain
- **Web editor:** importing more native .syx programs than there are empty slots offers to overwrite
  them from the selected slot onwards

## What's new in 0.13

- **PROPHET**, a new engine for Sequential Prophet-5 Rev 4 programs: two oscillators with hard sync,
  Poly-Mod, LFO and noise wheel modulation, the SSI (Rev 1/2) and Curtis (Rev 3) four-pole filters,
  separate filter and amplifier envelopes, Vintage, glide, unison and five voices per track.
  **All 200 programs of Sequential's v1.03 factory bank are its presets.** LFO rates, vibrato depth,
  filter tracking and envelope times are fitted to recordings of a Rev 4. Sixteen device pages edit
  every native parameter, and 128 native user slots (P001–P128) start with the first 128 factory
  programs. The web editor imports and exports Prophet-5/10 .syx programs and banks; the device also
  accepts single and edit-buffer dumps over USB. PROPHET takes ANALOG's place in the engine list
- **Note editing:** SEQ > NOTES is now the single editor for entered steps and recorded takes:
  SELECT walks the notes, the knobs change pitch, length and velocity, SCL moves and EDIT deletes;
  hold EDIT while recording to erase as the playhead passes
- **FM6:** DX7 librarians can send 32-voice banks straight to the device over USB, into F001–F032
  or F033–F064 (EDIT > STORE > SLOT), and bank dump requests answer from there; STORE > SEND sends
  the track's voice. The fourth main knob is DTUN
- **Performance:** engine and bank memory is allocated when it is used, idle USB capture and silent
  tracks cost no processing, and the screen draws while the LCD transfers
- **Retired engines:** PHYS, TRIO and WHEEL. Tracks that use them are silent; their saved values are kept

**Upgrading from 0.12:** projects, templates, banks and presets migrate. Projects and templates saved
by 0.13 include their Prophet programs and need 0.13. Installing 0.12 or the official V15 afterwards
erases the Prophet user slots: export them, or keep the installer's backup, first.

## What's new in 0.12

- **Microtonal scales:** 54 new tunings, for 70 scales in total, including equal divisions,
  just intonation, historical temperaments, maqam models and Bohlen–Pierce. The SCALES browser
  groups them by family, shows full names and note counts, and saves device favorites; the web
  editor adds filtering, search and browser favorites. Chords, ARP and recordings retain scale degrees
- **Concert pitch:** set A4 from **400–480 Hz** on GLO > GLOBAL, saved independently of projects;
  440 Hz remains the default and TUNE still adds a fine offset in cents
- **Recording:** retain the original note timing and gate lengths, with **1,024 timed notes across
  all 32 pattern banks**. SEQ > TIMING > QNT applies reversible playback quantization and defaults OFF
- **Recorded-note editing:** SEQ > NOTES selects individual synth or drum hits, zooms the original
  take, deletes selected hits and supports undo without changing neighbouring notes
- **Native user presets:** 64 FM6 voices and 128 CZ-1 tones in separate collections, alongside 64 general
  user presets. Scroll and favorite them in PRESETS; import/export Dexed/DX7 and Casio .syx in the editor.
  Native slots store only the tone, so loading keeps the track's effects and patterns
- **FM6 diagrams:** MARK I algorithms 4 and 6 show their longer feedback paths
- **FX change:** the shared delay is retired to make room for expanded recording; THROW now feeds reverb

**Upgrading from 0.11 / 0.11.1:** existing projects, templates, settings and presets migrate.
Expanded recordings and new scale IDs need 0.12; older firmware cannot reproduce those additions.
Microtonal MIDI OUT sends degree addresses, so an external synth needs the same tuning and mapping.

## What's new in 0.11.1

- **Web editor connects again:** with 0.11 it stopped after "Reading" the parameters, because the
  device cut the list of CZ-1's 65 preset names short. The device now sends the whole list, and the
  editor also connects to an FM-1 that still runs 0.11 (the last CZ-1 presets show as numbers there)

## What's new in 0.11

- **CZ-1**, a new engine playing native Casio CZ-1 tones: two lines, each with its own eight-step pitch,
  timbre and volume envelopes, the CZ's waveforms and windows, ring and noise modulation, detune, vibrato,
  key follow, line levels and velocity sensitivity (from MIDI)
- **Casio's 64 CZ-1 preset tones** (A-1 BRASS 1 to H-8 TYPHOON SOUND) as CZ-1's factory presets
- **Every tone value on the device:** 38 EDIT pages for the lines, detune, vibrato, windows and the six
  envelopes, and CZ TOOLS (NAME, copy line 1 > 2 or 2 > 1, COMPARE). A value that has no effect on the
  tone as it is (line 2 in LINE1, steps after END, vibrato without DEPTH) is drawn dim
- **Casio SysEx over MIDI:** CZ editors and librarians can send tones to a CZ-1 track and request them
- **PHASE** keeps its own six presets and gains LINK / SPLIT: separate native DCO, DCW and DCA envelopes
- **Installer:** **Return to official V15** can skip the backup when the firmware cannot export one
- **Removed:** the SAMPLE, GRAIN and SLICE engines and the custom drum kit (DRUM plays the 808)

**Upgrading from 0.10:** projects, templates, settings and user presets load. Tracks and user presets
that used SAMPLE, GRAIN or SLICE need another sound; a track with the custom drum kit plays the 808.

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
  delete it with EDIT, and undo or redo up to eight edits; MIDI step entry; unquantized live recording with reversible timing quantization
  and the cursor following playback
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

- **Eleven engines** (below), each with its own factory presets
- **Four tracks**, one synth part each with its own engine and sound (DRUM plays the synthesized 808); up to 16 FM6 voices or 8 voices from the other engines shared between
  them (an FM6 voice uses one budget unit, another engine's voice two; 16 units total).
  ALGORITHM selects the track on every page
- **Sequencer:** 64 steps per track with chords, ties, accent, slide and per-step chance; a piano
  roll of the steps; a drum grid (white keys = steps, black keys = lanes); motion recording of knob
  moves; unquantized live loop recording with overdub and optional playback quantization; MIDI step entry; tied-note length,
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
- **Effects:** distortion and the SLICER per track; chorus and reverb sends (the reverb as
  ROOM or SPRING); master limiter
- **FX layer:** hold FX for repeat, reverse, filter sweeps, tape stop, freeze and a harmonizer
  (OCT UP / OCT DN with shimmer), and mutes on the black keys
- **Quick layers:** hold FX, GLO, SCL or EDIT for shortcuts on the keys and knobs; one-step undo
  (SAVE held); REC on every page; OCT+ confirms, OCT- goes back
- **Presets:** factory presets, 64 general slots, 64 native FM6 slots, 128 native CZ-1 slots, 128 native Prophet slots and 4 projects, named on the device;
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
  preset library and FM6 voice editor; full backup and restore; return to the official firmware

## Controls

![FM-1 controls](docs/panel.jpg)

- **SELECT** turns the pages of the open section (both ways); in NOTES it selects individual notes; **MASTER** the
  volume, **ALGORITHM** picks the track (T1–T4) and **PRESETS** its sound. **KNOB 1–4** edit the four columns
  of the page
- **BPM** and the song's **SWG** are on **SEQ > TEMPO**: the project's, saved and loaded with it (power-on: the
  BOOT project's, the template's or 120); GLO > GLOBAL keeps CLK and TUNE, the device's
- FX, SCL, ENV, LFO, EDIT, GLO, SAVE, ARP and SEQ open their pages; press again for the next page.
  HOME returns home
- **Held:** FX, GLO, SCL and EDIT open their quick layers; SAVE is undo, HOME the menu, SEQ the song,
  REC the MIXER.
  When editing synth steps, SCL and EDIT use the editing controls below instead; FX keeps its layer
- PLAY starts and stops all four tracks; REC arms or disarms the selected track without starting the
  transport; **REC + PLAY** arms it and starts recording in one gesture
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

### Note editing

**SEQ > NOTES** is the single editor for entered steps and recorded performances. Press SEQ to
open it; tap SEQ again to reach PATTERN, TEMPO and the other sequence pages. The piano roll
shows recorded notes at their original timing alongside ordinary notes and continuous tie tails.
The highlighted note has a bright outline; the playhead is a separate moving line.

- **SELECT** moves through individual notes in time order, including repeated hits and separate
  chord pitches. It stays inside the editor and skips empty intervals. **KNOB 1 STEP** reaches
  any interval, including empty space for entering notes.
- **KNOB 2 PITCH** changes the highlighted pitch. On an empty manual step, it inserts a note;
  playing keys or MIDI enters a note or chord, then advances when released. Playing on a recorded
  interval auditions without replacing the take. During live recording, keys and MIDI record normally.
- **KNOB 3 LENGTH** extends or shortens a note. Recorded durations change by one nominal step,
  preserving their fractional length; hold **ENV** for fine adjustments of 1/16 step. Ordinary step
  notes resize their tie tails and stop before another note. Entered chord pitches share a step's
  length and velocity (marked **ALL** on those cards); recorded chord pitches have independent lengths
  and velocities. Adjusting a shared property highlights the affected chord.
- **KNOB 4 VEL** adjusts velocity from 1 to 127. Turning it on an accented manual note makes the
  displayed velocity its playback velocity. Hold **ENV** and turn KNOB 4 to switch a manual note's
  **SLIDE** on or off; the card changes to SLIDE while ENV is held.
- **PRESETS** zooms around the selected interval from 16 steps down to one. Selection stays put.
- Hold **SCL** and turn **SELECT** or **KNOB 1** to move the selected note. Recorded notes keep their
  fractional onset, duration and velocity; manual notes move with their ties. Moving into an occupied
  manual note or tie is blocked. Hold **ENV** and turn SELECT or KNOB 1 to resize instead.
- Tap **EDIT** to delete the highlighted pitch. Recorded deletion affects just that hit; manual
  chords retain their other pitches and ties. Deleting the final manual pitch clears its tie tail.
  Using another control while EDIT is held consumes the tap.
- While the selected track is recording and playing, **hold EDIT to erase**. The current step and
  each step the playhead crosses lose their notes, including recorded hits, drum hits and tied
  notes already sounding. The header reads **ERASING**; release EDIT to stop immediately.
  Keys and MIDI still audition while erasing, but do not record new notes. The gesture stays on
  the selected track and bank; leaving NOTES, changing track/bank or stopping recording cancels it.
  This is live recording: it starts a fresh edit history, so the eight-edit undo does not restore
  an erased pass.
- Hold **SAVE** to undo. **EDIT/SAVE + OCT− / OCT+** undo/redo up to eight edits, restoring note data
  and focus. A new edit clears redo. Recording, external edits, changing track, bank or loop length
  start a fresh history. With no manual edit to undo, held SAVE retains sound/pattern-load undo.

Recorded properties and deletion are editable during playback; stop live recording before editing
those notes. Live recording follows the playhead; ordinary playback leaves the editing focus alone.
The screen shows the four knob controls plus selection, zoom, movement and deletion hints.
The DRUM grid retains white-key step entry and black-key lane selection for manual patterns;
recorded drum performances use the same precise NOTES piano roll and individual-hit controls after
recording stops. During live drum recording, the grid and lane keys stay in place.

Armed live recording preserves played timing, velocity and each note's held duration, including
repeated hits within one step and overlapping chord notes with different releases.
**SEQ > TIMING > QNT** defaults to **OFF**. Choose a division to snap playback to a swung grid;
switch back to OFF to hear original timing again. The piano roll continues to show the original take.
This is independent of scale/key-map QNT on SCL. Track GATE controls manually entered steps.

Project saves, backups and bank copies preserve edited performance events and original timing.
User-preset patterns and the ordinary step-edit protocol carry only the grouped step overview;
external overview edits can replace a recording group with ordinary step sequencing.

A project holds **1,024 timed notes shared across all 32 banks**. A chord uses one entry per note.
Distinct repeated hits are separate entries; repeating the same pitch at exactly the same time
replaces that event. **RECORDING FULL** leaves existing recordings intact; clearing or replacing
recorded steps makes space for new takes. PHASE remains available. Notes held longer than 128
nominal steps are capped at that duration.

The shared FX delay is retired to free 128 KiB for recording and future capacity. Its old
parameter IDs remain reserved, so existing projects, presets and automation can still load.
THROW now feeds reverb. All synth engines, chorus, reverbs, SLICER and other live FX remain.
Older 152-note recordings keep their original timing when loaded and saved in the expanded format.

With **CLK TRS** or **CLK USB**, musical timing follows MIDI clock pulses directly, so tempo
changes do not shift the pattern. DIV sets the pattern's step length, while TIMING QNT independently
selects the optional playback grid. The DRUM grid keeps its white-key step and black-key lane controls.

### Startup and compatibility

On **SAVE > PROJECT**, KNOB 2 **BOOT** selects OFF or project A–D to load at power-on.
KNOB 1 **SLOT** also offers **TMPL**: save your sounds and settings there as the template for new
projects, with empty patterns. BOOT OFF uses the template when one is saved. CLK, TUNE, MIDI
and ROUT persist between starts; loading a project or template applies its own settings. DRUM is the
device's own setting.

For handpans and other instruments with a different concert pitch, open **GLO > GLOBAL** and turn
**KNOB 1 A4**: **400–480 Hz**, in 1 Hz steps, with **440 Hz** as the default. Set **432 Hz** for an
A=432 instrument and leave **TUNE** at 0; TUNE still adds a fine offset in cents. A4 retunes all
pitched engines, including FM6 and CZ-1, and takes effect on held notes. It is saved with device
settings and stays selected when loading projects, templates or presets. MIDI note numbers and
incoming computer audio are unchanged; external MIDI instruments need their own tuning adjustment.

Projects use the FBKG format: all 32 banks, their timing, arrangement and automation. Felucca 1.0
projects load into pattern 1; their old project-based SONG rows are cleared. Pre-1.0 Melodee's
multi-pattern projects/settings/templates and incompatible 58/62-parameter user presets are not imported.
User sample slots USR1–3 and sample uploads are removed; their flash space now stores projects.
SAMPLE, GRAIN, SLICE, OBXF and the custom drum kit are removed; only synthesized 808 drums remain. Earlier sample data is overwritten as projects
are saved. Complete backups with nonempty user samples require firmware that supports those slots.
FM6 and CZ-1 have independent native user collections: F001–F064 store the 128-byte Dexed/DX7
voice, and Z001–Z128 store the complete 144-byte Casio tone. They appear alongside factory tones
in normal global and engine-specific preset scrolling, and can be favorited. SAVE > USER selects
the current engine's collection; FM6 > STORE uses those same FM6 slots. Native saves exclude
Felucca effects, envelopes, modulation settings and patterns. Loading keeps these track settings;
when changing engines, only engine-specific controls receive their defaults.

The 64 general U01–U64 slots remain available for other engines. First boot copies saved FM6
voices and saved CZ banks into their native collections, imports embedded CZ user tones into free
CZ slots, and releases successfully migrated general slots. If CZ's collection is full, unmatched
legacy tones stay in their general slots. Migration and saves use atomic flash writes; full backups
include both collections. The editor's User presets collection selector imports/exports .syx directly.
Going back to older firmware cannot access these new native FM6 slots or U33–U64.

## Engines

In the order the device lists them:

- **PROPHET**: Sequential Prophet-5 Rev 4 programs, five voices per track: oscillators A and B (saw,
  pulse, B triangle, low-frequency and keyboard switches), hard sync, Poly-Mod, LFO and noise wheel
  modulation, the SSI (Rev 1/2) or Curtis (Rev 3) four-pole filter, filter and amplifier envelopes,
  Vintage, glide and unison. Each voice uses three of the shared voice slots
- **FM6**: classic 6-operator FM (Dexed-based): 32 algorithms, a full patch per track edited in the
  web editor or on the device; operator frequency, levels, envelopes and scaling, pitch envelope,
  LFO, STORE and DX7 SysEx; an algorithm chart on screen. The main knobs control MLVL, MRAT,
  MEG and DTUN. PRESETS selects the operator on operator pages. Algorithms 4 and 6 use three- and two-operator feedback loops in MARK I; MODERN and OPL
  use OP6 self-feedback, so 4 / 6 sound like 3 / 5 in those engines. Set ENGINE to MARK I on
  FM PORTA and raise FB above 0 to hear the longer loops; the algorithm chart follows ENGINE.
  Each track keeps its own patch and function settings (MODERN / MARK I / OPL, pitch bend,
  portamento, wheel, foot, breath and aftertouch), saved with the project and the template
- **PHASE**: phase distortion with LINK/SPLIT envelopes and its own six presets.
- **CZ-1**: native Casio tones, with separate eight-point pitch, timbre and volume envelopes on each line
- **LOFI**: chiptune; pulse, triangle, saw, noise and a 4-bit wave RAM, stepped envelope, sweep, arpeggio
- **VOICE**: formant oscillator, sung vowels
- **NOISE**: noise from analog to digital: colours, crackle, shift-register and metallic tones
- **DRUM**: synthesized TR-808 circuit models on eight lanes with the General MIDI key map.
  The 808 is the only kit; Felucca’s custom kit has been removed.

TRIO, WHEEL and PHYS are retired. Their engine IDs, saved edit values and favourite bits remain reserved; old tracks using these engines are silent. PROPHET replaces ANALOG in the engine list; ANALOG still renders existing sounds.

PROPHET opens on It's a Prophet 5 (or on user slot P001 once it holds your own program). PRESETS lists
INIT and Sequential's 200 factory programs, then the native user slots P001–P128. In the web editor,
choose PROPHET to edit a track's program field by field, read and send it, import or export .syx
programs and banks in Sequential's format, and manage the user slots. Calibration, storage and format
notes: [docs/PROPHET5_PROTOTYPE.md](docs/PROPHET5_PROTOTYPE.md).

The DIGITAL engine of 0.9 has been replaced by FM6: projects and presets with DIGITAL sounds load
as FM6 sounds converted from them.

**SLICER** (FX page, every track): a tempo-synced 16-step gate or stutter, with 16 patterns.

CZ-1 opens with a native INIT TONE; PRESETS then lists Casio's 64 CZ-1 preset tones (A-1 BRASS 1 to
H-8 TYPHOON SOUND). The separate 128-slot user collection starts empty. Imported Casio tones use
their original oscillator and six eight-point envelope parameters. PHASE remains independently selectable with its
LINK/SPLIT controls and six factory presets.

**Native CZ-1 SysEx**: choose CZ-1 in the web editor, then **CZ-1 native patches → Import .syx**.
Select a tone from the imported bank and **Send to track**. **Read track** retrieves its original tone;
**Export .syx** writes a CZ-1-compatible tone. **Add to library** retains it for later use.
In **User presets**, choose **CZ-1**, then import .syx into free native slots or save the current track's
native tone. Export writes the collection as Casio frames. The device scrolls these tones in PRESETS;
there is no BANK / PTCH selection page. Full backups include all 128 slots.
Both lines have separate eight-point DCO/DCW/DCA envelopes, including sustain,
end points, velocity sensitivity and key follow. The original 144-byte CZ-1 tone survives user presets,
projects, templates and library export; it is not reduced to common ADSR values. Compatible 128-byte
CZ tones are accepted with CZ-1 defaults for fields they lack. See [format and fidelity notes](docs/CZ1_SYSEX.md).

Native mode uses the documented uPD933 phase functions, rate law and logarithmic DCA response.
Vibrato timing, key follow, velocity response, DAC behavior and output filtering still need hardware
calibration; this build does not establish near-identical CZ-1 audio.

SAMPLE, GRAIN and SLICE and all recorded sample material have been removed. Engine numbers 4, 8
and 13 remain reserved so existing projects do not accidentally select another instrument. Old tracks
using those engines are silent until another sound is chosen. Historical separate GM drum parts load
as the synthesized 808 kit. SLICER remains a gate/stutter effect on synthesized audio.


## Scale keyboard

The first **SCL** page is **SCALES**, a browser with full names and note counts.
**KNOB 1** selects a family (or ALL / FAV), **KNOB 2** browses that list,
**KNOB 3** sets ROOT beside SCALE, and **KNOB 4** sets QNT.
The **PRESETS** knob also browses scales on this page. Family changes select the first
matching scale when the current one is outside the family; an empty favorites list keeps
that scale and shows how to add it. Device favorites survive power-off.
Press **SCL** again for the settings page: ROOT, FAV, QNT and TRN. **KNOB 2** there
saves or removes the active scale as a favorite without changing its tuning.
Press **SCL** once more for CHORD.
The web editor adds family filtering, search by full or short name, and favorites saved
in that browser. Its filters keep the active tuning until a scale is selected;
browser favorites and device favorites are separate.

On the **SCL** page, set **QNT** to WHITE to play the selected scale using only the
white keys (SNAP keeps every key and rounds it down to the scale). C4 plays **ROOT**; consecutive white keys play consecutive scale notes
above and below it. Black keys are silent, including during live recording and
step entry. **TRN** transposes the resulting notes; the octave buttons shift them
by full repeat periods (octaves for the original scales). Set QNT to OFF for the normal chromatic keyboard.

Available scales: chromatic (CHR), major (MAJ), natural minor (MIN), Dorian (DOR),
Mixolydian (MIX), major pentatonic (PEN), minor pentatonic (MPEN), harmonic minor
(HARM), Phrygian (PHRY), Lydian (LYD), Locrian (LOC), ascending melodic minor (MEL),
minor blues (BLUES), whole tone (WHOLE), half-whole diminished (DIMHW), and
whole-half diminished (DIMWH). Scales with other than seven notes continue across
the white keys without repeating notes; their roots need not fall on every C key.

The list also includes **54 microtonal selections**, for **70 scales in total**:
selected equal divisions from 5 to 53, just intonation, harmonic scales, Partch, historical temperaments,
maqam models, shruti, gamelan examples and Bohlen–Pierce. See the [complete catalogue and
playing notes](assets/scales/README.md). QNT WHITE, ALL and MPC play consecutive degrees;
SNAP rounds the keyboard and incoming MIDI pitches down to a scale degree. QNT OFF bypasses
microtonal tuning. ROOT and TRN shift pitch in ordinary semitones. The octave buttons,
ARP OCT and chord voicings move by the scale's repeat period (3:1 for Bohlen–Pierce).
Microtonal notes display as degrees, such as D1 and D3+1, and retain separate identities
when several degrees fit inside one semitone. Recordings and projects retain these degrees.
MIDI OUT carries degree addresses; an external synth needs the same tuning and mapping to
reproduce these pitches.

QNT **ALL** plays the next scale note on every key, black keys included. QNT **MPC** maps an
MPC's Bank H pads (MIDI 20–35, H01–H16 of MPC Sample's default map) to successive scale notes;
the **MPC** page in the SCL family sets **DEG**, the degree pad H02 plays. Incoming MIDI follows
WHITE, ALL and MPC the way the keys do (without the octave buttons). SCL, QNT and DEG are shared by
all four tracks; ROOT and TRN stay per track. Drum kits keep their own note mapping.

On the **CHORD** page, CHRD picks the chord keys (OFF, the scale's triads or
sevenths, or a fixed shape) and VOIC the voicing.

## Layout

| Path | What |
| --- | --- |
| `firmware/` | firmware sources: `src/` app, `hal/` hardware layer, `loader/` update loader |
| `tools/` | build script, generators, package maker, installer and sample uploader |
| `assets/` | UI font and icon names |
| `web/` | web installer and editor sources |
| `tests/` | tests that run on the build machine |
| `LICENSES/` | licence texts of the bundled font, icons, ported DSP and SDK files |

## Support

You can leave a tip with [ko-fi](https://ko-fi.com/keremimo).

## Credits

- Melodee by Ellic Studio (Kerem Kilic, [@keremimo](https://github.com/keremimo))
- Based on [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita ([@kurogedelic](https://github.com/kurogedelic)), [Hügelton Instruments](https://hugelton.com)
- **[Hügelton Instruments](https://hugelton.com)** (Leo Kuroshita, [@kurogedelic](https://github.com/kurogedelic)):
  Felucca itself; the PHASE engine's waveforms (a C port of the oscillator of
  [CrispyZebra](https://github.com/hugelton/CrispyZebra), GPL-3.0); the synthesized 808; the [Fukiai](https://github.com/hugelton/Fukiai) icon
  font ([MIT](LICENSES/MIT-Fukiai.txt))
- Font: [Inter Tight](https://github.com/rsms/inter-tight) by The Inter Project Authors, [SIL OFL 1.1](LICENSES/OFL-InterTight.txt)
- VOICE engine: after [klattsch](https://github.com/tgies/klattsch) by Tony Gies (MIT); formant data from Klatt (1980) and Hillenbrand et al. (1995)
- Native CZ engine: uPD933 model by Devin Acker in [MAME](https://github.com/mamedev/mame/blob/master/src/devices/sound/upd933.cpp) ([BSD-3-Clause](LICENSES/BSD-3-Clause-uPD933.txt))
- PHYS engine: models ported from [DaisySP](https://github.com/electro-smith/DaisySP) by Electrosmith and Emilie Gillet ([MIT](LICENSES/MIT-DaisySP.txt)) and from Emilie Gillet's [eurorack](https://github.com/pichenettes/eurorack) code ([MIT](LICENSES/MIT-Rings.txt))
- FM6 engine: msfa from [Dexed](https://github.com/asb2m10/dexed) by Google Inc. and Pascal Gauthier ([Apache-2.0](LICENSES/Apache-2.0-msfa.txt))
- Package format and boot files: [JieLi AC79 SDK](https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK) ([Apache-2.0](LICENSES/Apache-2.0.txt); three of its files are in every package, none in this tree)
- Contributions: [keremimo](https://github.com/keremimo) (white-key scales, #2), [ChanceTheMaker](https://github.com/ChanceTheMaker)
  (TRS MIDI, bend, sustain and clock, palettes, favourites, editor display settings: #8, #10, #11, #12),
  [andreahaku](https://github.com/andreahaku) (sample recording and trim, #29; SLICE manual slices and tests, #27, #22)

## Licence

Free software: [GPL-3.0-only](LICENSE). The bundled font and the
ported DSP keep their own licences ([LICENSES/](LICENSES/)); details in [LICENSING.md](LICENSING.md).

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments. M-VAVE and FM-1 are
trademarks of their respective owners. Melodee is not affiliated with or endorsed by any of them.

Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments\
Modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio)

See [Recording and performance controls](docs/RECORDING_PERFORMANCE.md) for count-in, metronome, drum levels, temporary knob edits, additive chords and MIDI parameter control.
