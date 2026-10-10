# Recording and performance controls

REC taps arm or disarm the selected track. They leave the transport alone.
PLAY starts/stops playback; an armed track records while playback runs.
REC + PLAY still arms and starts in one gesture. Holding REC opens the mixer.

Hold HOME to open the menu. The following device preferences also appear in
the web editor's settings and survive power-off:

- **AUDIO CLICK:** OFF, REC (while a track is armed and playback runs), or ON.
- **CLICK LEVEL:** LOW, MID, HIGH. The first beat of each four-beat bar is accented.
  This sets both metronome and count-in volume independently of mix MASTER,
  including when MASTER is zero. The click goes to headphones/line out and
  speaker, after effects and USB capture, so recordings contain no click.
- **COUNT-IN:** OFF, 1 BAR, 2 BARS. Starting an armed track with internal clock
  counts four or eight beats before the first step. Count-in clicks sound even
  with AUDIO CLICK off. PLAY cancels it. External clock and song-chain starts
  follow their existing transport timing without a count-in. Notes pressed in
  the final half-beat and still held at the downbeat enter step one without
  retriggering their live sound; earlier presses are for monitoring only.
- **NOTE PREVIEW:** OFF/ON. Moving the stopped STEP/NOTES cursor auditions its
  note or drum hits. NOTES selects an individual note; STEP previews a chord.
  Preview lasts about 167 ms, does not record or transmit MIDI, and respects
  notes already held by the player or arpeggiator.
- **CHORD ENTRY:** HOLD keeps the existing simultaneous-key entry and automatic
  advance. ADD lets separate taps add/remove pitches at the same cursor.
  Up to four pitches fit a polyphonic step; a full chord shows CHORD FULL.
  Mono voice modes keep one note. Cursor movement and length editing remain
  available; each completed tap can be undone.

DRUM has **LANES** and **LANES2** pages in EDIT, with individual KICK, SNARE,
CLAP, HATCL, HATOP, TOM, RIM and BELL levels. Zero silences a lane and 100%
retains its original level. These levels save in sounds, projects, templates
and backups and can be recorded as motion.

Hold **LFO** while turning sound/effect knobs to make temporary changes.
Release LFO to restore the values; press **OCT+** while holding it to keep them.
This works on HOME, sound pages, effect settings and native FM6/CZ-1/PROPHET
pages. Temporary changes are excluded from motion recording. Changing tracks,
loading/saving a sound/project, opening the menu, or receiving an editor sound
edit restores temporary values first. Navigation, storage actions and shared
scale settings keep their ordinary behavior.

USB and TRS MIDI share these controls, using the existing channel routing:

| CC | Control |
| --- | --- |
| 5 | Glide (FM6 also retains its native portamento-time mapping) |
| 7 | Track level |
| 10 | Pan; 64 is centre |
| 71 | Resonance, or Q where the engine exposes it |
| 72 / 73 / 75 | Release / attack / decay |
| 74 | Engine cutoff or brightness |
| 91 / 93 | Reverb / chorus send |

Cutoff/brightness uses ANALOG CUT, PHASE DCW, SID CUT, DRUM TONE, NOISE FREQ,
FM6 MLVL and PROPHET CUT. CZ-1 uses its filter-envelope amount. Engines without
a resonance control ignore CC71. Native FM6 envelope messages change all six
operators' corresponding rates. CZ-1 changes both amplitude envelopes' first,
second or END rate; PROPHET changes its amplitude envelope. Those native edits
save with the native sound, as panel edits do; common controls and engine
macros record as motion when the selected track is recording. Existing wheel,
expression, sustain, bend, RPN and panic controls remain available.

New data uses 100 parameters (engine parameters start at 92), FUN15 projects,
FBKI pattern banks and TPLD templates. Older layouts import with drum levels
at 100%, retaining native voices, notes, timing and automation. Recording
preferences use tagged spare bits in PER5's unused zoom word; older settings
load as click OFF, level MID, count-in OFF, preview OFF and chord entry HOLD.
