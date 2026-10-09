# Native CZ-1 tones

The web editor accepts Casio `.syx` tone and concatenated bank files. Choose
CZ-1 in Sound, import the file in **CZ-1 native patches**, select its tone and
send it to the selected track. Direct Casio MIDI tone dump/read requests are supported too. Reading/exporting a native tone preserves its
original bytes. The library accepts `.syx` too, including before connecting a
device. Native tones require firmware advertising the CZ capability.

Each native tone is 144 bytes: 128 synthesis bytes and a 16-byte name. It keeps
both waveform words (including hidden waveform/window combinations), six
independent eight-point envelopes with sustain/end points, detune, octave,
vibrato, both line levels, key follow and per-envelope velocity sensitivity.
Editing one rate changes that rate only; unedited native data is not normalized.
END targets zero during playback as specified by Casio. Rates are speeds rather
than ADSR segment durations. PHASE is a separate engine with LINK/SPLIT controls and its own presets. Selecting CZ-1 opens a native INIT TONE; importing a
native tone selects the CZ-1 engine and its native renderer.

The parser accepts `F0 44 00 00 7n 30 <data> F7` dumps and receive-request
`20 <slot>` (128 bytes) / `21 <slot>` (144 bytes) frames. Casio's low nibble
comes first. There is no checksum. Request/acknowledgement frames and MIDI
realtime bytes in captures are skipped. Truncated, foreign or invalid payloads
are rejected before sending anything to the device. For 128-byte compatible
tones only, missing CZ-1 line levels, velocity and name are initialized and the
DCW key-follow table is translated. Native 144-byte tones are retained verbatim.

Editor command 78 transfers tones to the separate native CZ user collection, storing only the
144 native bytes. Commands 75/76 still transfer track tones and read/write legacy general slots
for older clients. Legacy preset version 8 stores the complete native tone in its compact record.
Projects and templates store both the native tone and all ordinary controls.
FUN10 / FBKD / TPLA add native tones and continue to read deployed FUN8/FBK9/TPL6 and earlier formats.
Native tone names remain 16 bytes even though the preset browser shows 12.

Playback evaluates the uPD933 model's phase functions continuously using
native fp32 arithmetic. Its logarithmic amplitude law and chip-rate envelope
rules are retained, with rates adjusted from 40 kHz to the current audio/control
clock. DCO (semitones), DCW (phase units) and DCA (0..127) have separate domains.
Fractional phase, envelope time and amplitude are preserved until PCM output.
The output includes DC removal and headroom for asymmetric carriers.

This is documented-behavior implementation, not measured audio parity. The
200 Hz vibrato control clock, velocity interpolation and key-follow response
are provisional approximations. Hardware DAC quantization, analog filtering,
absolute output gain, noise modulation and envelope stepping at audio rather
than control rate still need comparison against an actual CZ-1. Native bytes
are preserved so calibration can improve without reimporting patches.

Sources:

- [Casio CZ-1 System Exclusive specification](https://cz.brusi.com/CASIO-CZ-1-SYSTEM-EXCLUSIVE-SYSEX.pdf), tone formats and parameter tables.
- [Casio CZ-1 owner's manual](https://manuals.fdiskc.com/tree/Casio/Casio%20CZ-1%20Owners%20Manual.pdf), waveforms and rate/level envelope behavior.
- [Devin Acker's uPD933 device model](https://github.com/mamedev/mame/blob/master/src/devices/sound/upd933.cpp), chip phase functions, envelope rate domains and logarithmic DCA; BSD-3-Clause.
- [Michael Rickard's CZ-1 hardware investigation](https://www.kasploosh.com/cz/11800-spelunking/), hidden waveform/window combinations.

## Native user presets

CZ-1 has 128 separate native user slots, Z001–Z128. In the editor's User presets section,
choose CZ-1 and import a .syx file into free slots, save the current track tone, or upload a library tone.
Read, load, save, erase and export use this collection; export concatenates complete Casio tone frames.
Each save commits atomically. A multi-tone import commits one tone at a time, so an interrupted import
may have saved earlier tones. Capacity is checked before the import starts, and occupied slots stay intact.

The device scrolls used slots after CZ-1 factory tones in PRESETS and the engine's preset knob.
Favorites work for these slots. SAVE > USER opens this collection when the selected track is CZ-1.
Loading recalls only the native tone: Felucca effects, modulation and patterns remain the track's.
There is no BANK / PTCH page. Full backups retain all native bytes and empty slots.

Existing saved CZ banks migrate with their tone indices intact. Embedded general CZ presets move into
free native slots; if the collection is full, unmatched general records remain accessible. Factory tones
stay in the factory preset list, and unsaved user slots start empty.

## Factory tones

Casio's 64 CZ-1 preset tones are built into the firmware (`assets/cz1-factory/`, see its README for
where they come from). PRESETS 1–64 load these factory tones independently of the native user collection.

## Device pages: dim values

The CZ-1 EDIT pages show every panel value of the tone; a value drawn dim (grey) has no effect on the tone as it
is, exactly as the CZ plays it:

- line 2's own values sound only in LINE2 and 1+2' (1+1' plays line 1's values twice); line 1's not in LINE2
- DETUNE (SIGN, OCT, NOTE, FINE) and MOD act on line 2 (SIGN only on a nonzero detune)
- vibrato WAVE, RATE and DELAY need a DEPTH above 0
- an envelope runs its steps up to END: the rates up to END's (the release to 0), the levels before END
  (END's own level is always 0), and SUS needs a step before END

Velocity amounts (V.PIT, V.WAV, V.AMP) follow MIDI note velocity; the FM-1's keys play at a fixed velocity.
`tests/cz1_knob_test.c` renders every value at its minimum and maximum: all change a tone that uses every
part of the CZ, and every dim one leaves INIT, factory tones and a LINE2 tone bit for bit.

These tones have no sustain point in several DCA envelopes (BELLS, SITAR, JET ROAR): as on the CZ, the
whole envelope plays regardless of the key, so such notes ring long after release.

Earlier next FUNA/FUNB, FBKB/FBKC and TPL8/TPL9 CZ sounds are migrated to raw native tones; common sound settings and patterns are retained.
