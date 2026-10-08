# Prophet-5 Rev-4 playable prototype

Work on `codex/prophet5-prototype`, based on local `next` at `2688403`.
The playable engine, native patch storage and editor are implemented. This is
an engineering prototype; reference calibration and human listening/control
approval remain release gates. Nothing here establishes calibrated Rev-4 fidelity.

The user reported a substantial audible mismatch on the first installed factory
program, **It's a Prophet 5**. The supplied
[Rev-4 factory demo](https://www.youtube.com/watch?v=C3rYt4OdqzQ) has since been
measured; see **Reference calibration** below. The DSP is now fitted to that
recording, which is one dry instrument and not a hardware calibration.

## Engine and performance

PROPHET has persistent engine ID **19** and replaces ANALOG in the new-sound
browser. ID 0 still renders legacy ANALOG projects and presets. Retired TRIO,
WHEEL and PHYS IDs 6, 7 and 9 remain reserved and preserve saved data; they do
not render audio. IDs 16–18 are reserved. Engine count is 20. General, FM6 and
CZ favorites retain frozen collection IDs 16, 17 and 18; Prophet users use 20.
The web selector identifies a currently loaded hidden engine as legacy.

Each Prophet part has five local voices. Each costs three units of the existing
sixteen-unit shared budget: five Prophet voices cost fifteen. Adding one
ordinary voice on each other track leaves three Prophet plus three ordinary
voices, costing fifteen units. Two allocation steals are expected in that mix.
Overload shedding is separately counted and is a failed performance gate.
This does not provide five simultaneous voices on all four Prophet tracks.

Implemented DSP:

- Independent A/B tuning, levels and pulse widths, simultaneous waveforms,
  B triangle, low-frequency and keyboard-tracking switches, mixer noise,
  band-limited saw/pulse generation and fractional hard-sync correction.
- Separate native filter/amplifier ADSRs, velocity switches and envelope
  ownership; the common amp ADSR does not shape Prophet a second time.
  Native filter self-oscillation starts with the oscillator mixer closed.
- Distinct four-pole SSI and Curtis character models, resonance, saturation,
  keyboard tracking and native filter-envelope modulation. SSI uses distributed
  stage saturation; Curtis uses concentrated saturation and different gain loss.
- Audio-rate Poly-Mod from B plus filter envelope to A frequency (exponential,
  as on the CEM3340), A pulse width and cutoff.
- Shared audio-rate wheel modulation per part, combined native LFO waveforms,
  0.022–500 Hz rate, initial depth, noise blend and all native destinations.
- Per-note Vintage tuning/filter/envelope/amplifier mismatch, native glide,
  unison count/detune, priority/retrigger, release switch, sustain, channel
  aftertouch and one-to-twelve-semitone native pitch bend.

Keyed oscillator coarse tuning selects semitone detents: adjacent raw values
share a detent, raw 24/25 select the root anchor, and raw 96 and above select
the top of the four-octave range. Keyboard-off B retains its separate continuous
nine-octave range. Full filter keyboard tracking doubles the model's cutoff
frequency per keyboard octave; half tracking multiplies it by sqrt(2).
These fix the previous half-semitone interpretation and excessive tracking slope.
Wheel pitch is exponential, so a triangle goes equally sharp and flat in cents.

The [Sequential user guide](https://www.sequential.com/wp-content/uploads/2021/02/Prophet-5-Users-Guide-1.3.pdf)
documents semitone coarse tuning and equal sharp/flat triangle vibrato.
Hardware observations in the independent
[Rev-4 conversion project](https://github.com/FunkybotsEvilTwin/ToneXCaptures/tree/main/Prophet%205%20Rev%204%20to%20RePro-5%20Patch%20Converter)
corroborate the adjacent raw tuning detents. No converter source or profiles
are included in this repository; plugin-control translation curves are not
used as physical Hz, time or modulation-depth measurements.

Native knob values are interpreted on 0–127: Sequential's factory data uses the
whole range (levels and sustains of 127, values 121–126), although the MIDI
guide documents 0–120. Imported values remain byte-exact. Rev-10 unison counts
above five are preserved but play at the five-voice cap.
Default synthesis uses **1x** sampling. Experimental `P5_OVERSAMPLE=2` remains
available and has host stability coverage; its earlier device costs exceeded
budget. Strong sync/audio-rate modulation can alias. Band limiting alone does
not establish acceptable aliasing or reference fidelity.

The sample path and coefficient/saturation tables live in the existing RAM
DSP sections. The saturation lookup has less than 18 Q15 units of absolute
error against the intended tanh knee. Inside the filter that error is filtered
by the following poles; the last SSI stage and the filter output interpolate
the table, since their 32-unit steps reached the VCA as a grain about 50 dB
below a closed filter's sustain (Internalized). Host tests check odd symmetry,
monotonicity, endpoints and stability. The common renderer's structural cost
check passes. Hardware timing, rather than host speed, decides capacity.

## Subsequent fidelity corrections and current limits

The coarse tuning, keyboard tracking, wheel-pitch symmetry and arithmetic
endpoint repairs above are covered by host playback and strict ASan/UBSan.
The wheel depth, envelope times and filter mapping were later fitted to the
supplied demo (see **Reference calibration**). The device measurements below
predate that change; its exponential Poly-Mod costs ~8 % more on the host's
worst-case five-voice stress and has not been timed on the device.

Updated synthesis candidates failed some worst-case device gates:
`fidelity-fast-device-84.json` reports SSI overload shedding (one late block
in the Prophet-only row) and a Curtis mixed peak of 2557 us above the 2466 us
gate. Earlier passing baseline measurements below do not validate this revised
DSP. A later RAM control-path experiment failed target branch relocation
checks and was reverted. No further silent timing images will be installed:
the user explicitly requires that audio remain enabled.

`playable-final.fwsc` was reinstalled to restore output, followed by exact
runtime restoration. Readback confirmed all persistent musical object CRCs
and all 128 installed factory records against Sequential's source. The new
factory-default normal build has a 406548-byte app, unchanged static RAM/pool
extents, and 780 bytes of state per active Prophet track. Its output is enabled
(`MELODEE_BENCH_SILENT=0`). Default availability and musical fidelity are
separate acceptance checks.

## Device and web editing

Sixteen device pages cover STORE, OSC A/B, B waves, mixer, filter, both ADSRs,
velocity/envelope modulation, Poly-Mod, LFO/wheel and performance settings.
STORE offers 128 slots, naming up to twenty characters, native USB dump and
INIT. Store requires the normal stopped-transport/overwrite workflow.

All 200 Sequential v1.03 programs are PROPHET's factory presets (1–200, after
INIT at 0), always present whatever the user collection holds; they are dry, as
the Prophet. Only INIT can be a factory favorite (the favorites store has no room
for 200 more); copies in user slots can be starred. The default native P001–P128
collection also contains the first 128. `assets/prophet5-factory/README.md` records
the original download and SHA-256; `tools/gen_prophet_factory.py` verifies that
source before generating firmware and mock-editor data. Missing/invalid banks
use those ROM defaults without writing flash. Valid saved banks take precedence,
including deliberately empty slots. Selecting Prophet starts on user P001 when it
is used, otherwise on factory preset 1 (It's a Prophet 5).

The editor protocol carries preset numbers past 127 as trailing high bits on
`DUMP`, `PRESET`, `RELOAD`, `TRACK` and `TRACK_DUMP`, and pages `NAMES`
(`web/EDITOR_PROTOCOL.md`, "Presets past 127"). Older editors see the first page.

The matching web panel edits the known native fields and name, reads/sends
complete patches, imports single dumps or whole `.syx` libraries, and exports
patches/libraries with original wire layout and opaque bytes. Both official
200-program banks remain browsable after import. The separate 128-slot native
user collection supports list/load/store/rename/delete, favorites and native
collection import/export. Larger libraries are not silently truncated into
those 128 slots. Complete sound JSON exports/auditions retain the native patch.

Native loads initialize native voice/performance behavior and reset eight
common engine macros. They retain Melodee effects, modulation matrix, scales,
chords and sequence. Common CUT/RES, A/B tuning, A/B width and A/B mixer macros
act as offsets; matrix PITCH/CUT/SHP/AMP are supported. Matrix ENV follows the
native amplifier envelope. Common ENV destination controls are inactive because
Prophet owns both envelopes. Native bend overrides common RPN bend range.
Melodee VOICE/PRIORITY/DETUNE may subsequently override allocator settings.

## Reference calibration (2026-10-08)

The demo's audio (dry, 44.1 kHz mono analysis copy) was mapped to programs by
reading the panel display in the video: 111–118, 121–128, 131–138, 148,
151–157, 211–218, 221–228, 231, 311–318, 411–418 and 511–517 (75 programs).
Group/bank/program `GBP` is factory index (G−1)·40+(B−1)·8+(P−1).
Measurements use the first seconds of each program, before the player turns
knobs. Fine B 24 measured +18 cents, as rendered; the parameter layout matched.

| What | Prototype before | Measured on the demo | Now |
|---|---|---|---|
| LFO rate | 0.022–500 Hz exponential: raw 80 = 17.6 Hz | raw 74/80/89/94/97 = 2.40/3.37/5.48/7.10/8.39 Hz | ln Hz = −3.10 + 0.0539·raw (±2 %), bent to .022/500 Hz at the ends |
| LFO amount (triangle, pitch) | linear, ±1 octave at full: 18 = ±241 cents | 18/24/30/42/55 = ±7.5/13/20/36/50 cents | 0.052·raw^1.74 cents (±235 at full); saw/square 0..2× (same peak-to-peak) |
| Filter key tracking | pivot MIDI 60 | cutoff = constant × note frequency, knob value at the lowest C (keyboard 0 V) | pivot MIDI 36 (2 octaves brighter at middle C) |
| Cutoff knob (Curtis) | 30 Hz·533^(raw/127) at C4 | LOW STRINGS 67 → 12× note; Full Hexagon 84 → 26× | ~870 Hz·2^((raw−67)/15.2) at C2; SSI +0.7 oct below mid-knob |
| Filter env amount | 0.053 oct/step × velocity | Cars Strings 76 → ~+3.3 oct; It's a Prophet 5 sweep 10.7 → 2.5 kHz | 0.046 oct/step × velocity |
| Decay/release | 99 % point at 10000^(v/127) ms | Antique Bell D98 ≈ 1.2 s τ, Gamelan R82 ≈ 0.4 s τ | time constant 10000^(raw/127) ms (4.6× longer) |
| Rev 1/2 filter envelope | exponential | It's a Prophet 5 decay is nearly linear (as the guide says) | linear decay/release, full scale in 3.6 τ |
| Poly-Mod → osc A | linear FM, ratio up to 2 | Whiny Opener env 96 sweeps A +2 octaves; Small Gong B 127 shifts A ~+3 semitones | exponential: env 2.65 oct, osc B ±1.1 oct at 127 |
| Vintage | 0 = loosest | 63 factory programs use 0, 48 use 42, 12 use 127; guide: 4 (stable) to 1 | 0 = stable Rev 4, 127 = Rev 1 |
| Osc B Lo Freq | 10 octaves down, every new voice at the bottom of the ramp | Pickle Pincher (B 48, keyboard off: 173 Hz; LFO is noise only) moves level and brightness at 1.31–1.39 Hz with harmonics | 7 octaves down (1.35 Hz), free-running phase |

After the change, It's a Prophet 5 at middle C renders 3.35 Hz ±7.9 cents and a
cutoff of 10.5/8.9/6.1/4.2/2.5 kHz at 0/0.2/0.6/1.0/1.6 s (demo: 3.37 Hz
±6.5–8 cents, 10.8/8.2/5.4/4.1/2.5 kHz). LOW STRINGS renders within 0.1 octave
of the demo from A2 to C5. `tests/prophet_test.c` checks the measured rate and
depth points.

Still estimated, not measured: LFO→filter (±4 octaves at full) and LFO→PW
scales, Poly-Mod → filter (8 octaves at 127) and → PW, the SSI cutoff offset
(data spread ±1 octave), attack times, glide, unison count 0, and velocity curves.
The demo bank (2020) may differ from v1.03 in places; Nylon Fingers, for
example, showed no Poly-Mod sidebands at all.

## SysEx evidence and encoding

Sources: [Sequential specifications](https://sequential.com/classics-reissued/prophet-5-10/),
[MIDI implementation 1.4](https://sequential.com/wp-content/uploads/2021/03/Prophet-5-MIDI-Implementation-1.4.pdf),
[official sounds](https://sequential.com/support/download/prophet-5-10-sounds/) and
[v1.03 archive](https://sequential.com/wp-content/uploads/2025/03/Prophet-510-Factory-Programs-ReadMe1.03.zip).
The unmodified USER SysEx bank is included under `assets/prophet5-factory/`;
factory patch data is attributed to Sequential separately from the software.

Both banks have 200 single-program frames, 159 bytes each: manufacturer 01,
model **32**, command 02, group/program, and 152 packed bytes for **133 raw bytes**.
The guide describes model 31 and 128 raw bytes (147 packed bytes). Both explicit
models/layouts are accepted. The 138-byte native record contains 133 raw bytes
plus original size/model/command/group/program. Program/edit dumps preserve
original metadata and every unused byte, including factory FF at raw 97/98.
Name bytes are 65–84. A panel edit changes only its field. Export reproduces
original frames exactly unless the user edits the patch.

| Bank | SHA256 |
| --- | --- |
| FACTORY v1.03 | `9b5beaae2953deed5a50ed5f3cbd78c589e704c35a694fa188e9b27f693b73ca` |
| USER v1.03 | `1d6e9dec2753a08ac6d07b98f0f4e755e33b41de11dc3bed46fa23217619d232` |

USB native single/edit dumps load the selected track in RAM without an automatic
flash write. Edit-buffer requests return a native dump; addressed program
requests read used user slots 1–128. Native parameter NRPN/CC editing is outside
this prototype; editor command 95 edits the native record. TRS retains its
existing MIDI-input policy and does not decode native SysEx.
A real Rev-4 edit-buffer capture is still needed; edit-buffer fixtures are
synthetic transformations of the validated factory payloads.

## Storage and compatibility

128 complete patches occupy five A/B-protected 3600-byte objects, twenty-six
slots per object (twenty-four in the last). Each contains magic, used and favorite
masks plus 26 × 138-byte records. Interrupted saves retain the previous commit
and restore the previous RAM/cache state. Invalid masks/records are rejected
before erase/write. Playback prevents musical flash saves.

The app partition ends at **0x89000**; ten 4-KiB sectors at 0x89000–0x92FFF
hold the native collection. Linker, package, loader, musical write whitelist
and cache invalidation agree on this boundary. The loader rejects a package whose
app extents differ from its own before app writes. OTA still accepts official V15
and earlier Melodee packages (app area to 0x93000), so leaving or downgrading
works; they overwrite these sectors, whose storage objects then fail their
checks, and a later Melodee starts again from the factory programs.

Existing collection/object IDs remain fixed. Backup inventories have 28 objects:
0–22 retain their meanings; 23–27 are native Prophet banks. Older 23-object
archives restore without deleting missing Prophet banks. The web editor
preflights nonempty Prophet archives against device capability before writes.
Projects **FUN14** (12904 bytes), banks **FBKH** (27752) and templates **TPLC**
embed all four complete native records. Legacy FUN13/FBKG/TPLB and earlier
projects remain readable. Sound undo/redo includes the complete native record.
Older firmware cannot interpret the new project/template layouts.

Resource figures and measured performance are in the validation section below.

## Validation and remaining release gates

Host coverage includes malformed frames/masks, both real 200-program banks,
byte-exact import/export, all 128 slots, last-slot naming/favorites, simulated
torn saves, stopped-transport guards, independent track-owned patches, projects,
legacy projects, runtime backup, templates, sound undo/redo and old-partition
OTA refusal. Every program in both official banks is rendered with its original
native settings; all 400 are audible and bounded. That is host playback, not
400 device auditions. Existing engine audio regression hashes remain unchanged.

Silent timing images use `MELODEE_BENCH_SILENT=1`: complete synth/effects/scope
and USB capture/playback work runs, then only the physical DAC output is zeroed.
Normal builds leave output audible. Host checks prove the active synth advances
in the silent image. Timing stress patches use zero mixer noise, but maximum
initial wheel depth with a 50% modulation-noise blend, all wheel routes, a
500 Hz combined-wave LFO, resonance, sync, audio-rate Poly-Mod and effects.
They are intentionally harsh diagnostic patches, not factory presets.

Earlier integration runs failed the SSI overload gate; their logs remain under
`build/prophet5/`. Passing later runs do not erase those failures. The final evidence below records the passing configuration and its limits.

### Final device measurements (2026-10-08)

The normal image is `build/prophet5/playable-final.fwsc`, flashed successfully as
FM-1_900 (`playable-final-flash.log`). App size is **387388 bytes**; loader is
6718 bytes; package is 568914 bytes. Static RAM `.data + .bss` is
**92092 / 98304 bytes**; pool allocation is **169184 / 344064 bytes**, leaving
174880 bytes for the on-demand audio arena (required minimum 114688).
Each active Prophet part adds 696 bytes within that arena.

The final silent timing image has the same synthesis configuration and a
387292-byte app. Eight-second runs include note attacks, distortion 30,
chorus/delay/reverb 70 and simultaneous USB playback/capture at alternate 2/2.
The half-buffer deadline is **2902 µs**; the prototype gate is 85% (2466 µs).
`half_render_max_us` excludes nested timer work; the late counter covers the
complete deadline. Each row passed with no new late blocks, overload shedding,
USB underruns/overruns or missed frames.

| Filter/workload | Base note | Peak render µs | Max reported CPU % | Voices |
|---|---:|---:|---:|---:|
| SSI, Prophet only | 60 | 2158 | 69.53 | 5 |
| SSI, mixed engines | 60 | 2283 | 70.70 | 6 |
| Curtis, Prophet only | 60 | 2009 | 66.41 | 5 |
| Curtis, mixed engines | 60 | 2206 | 68.75 | 6 |
| SSI, Prophet only | 84 | 2143 | 71.09 | 5 |
| SSI, mixed engines | 84 | 2230 | 71.48 | 6 |
| Curtis, Prophet only | 84 | 2051 | 67.97 | 5 |
| Curtis, mixed engines | 84 | 2249 | 69.53 | 6 |

Mixed runs use three Prophet voices plus LOFI, PHASE and VOICE, with two
expected allocation steals when the requested chord exceeds the shared budget.
These are distinct from overload shedding. Five Prophet voices cost fifteen
of sixteen shared units; this does not promise five voices on each track.
Evidence: `playable-lut-device-60.json`, `playable-lut-device-84.json` and their
`playable-lut-bench-*.log` files. The 1× filter configuration is measured;
experimental 2× oversampling has not passed this final budget gate.

### Patch, persistence and audio evidence

`native-device-qa.json` confirms last slot P128, twenty-character names, opaque
bytes, favorites, project reload and flash/reboot persistence. Temporary slot
and project writes were restored; all 28 persistent objects retained their
original sizes and checksums after restoration. `playable-device-ready.json`
records the final RAM-only setup: track 1 is native **Forever Keys**, selected,
with effects off and other tracks muted; saved projects and collections remain
unchanged.

`factory-device-recordings/manifest.json` accompanies fourteen dry USB stems:
It's a Prophet 5, Forever Keys, Funk Bass II, Stabby Brass, Reedy String Pad,
Synchrotrill and Synctink, each through both filters. Original native bytes are
preserved except the filter selector; every recording has a native `.syx`
file and exact device readback verification. Capture level is 64, other tracks
are muted and effects are off. All fourteen have zero clipped samples and
zero USB stream errors. USB stems are mono track output, not stereo DAC
recordings. The user reported that overpowering noise occurred only during
the earlier stress test, not these factory auditions. This is useful listening
feedback, not reference calibration of the two filter models.

`integration-tests-final.log` ends with ALL HOST TESTS PASSED; all 140 golden
renders pass, including the new Prophet INIT, with previous hashes unchanged.
`integration-final-asan.log` and `dsp-final-asan.log` cover integration and
400 original factory programs under sanitizers. `web-final-check.log` verifies
the final editor helpers and codecs. Browser mock QA imported 200 programs,
sent Stabby Brass to a track and read it back with the correct engine/name
(`editor-native-proof.jpg`). The in-app browser did not expose its Blob export
as a download event, so byte-exact exported files are verified by codec tests,
not by claiming a captured browser download.

`osc-alias-check.json` measures standalone saw/pulse oscillators at 687.04,
3028.78 and 6056.89 Hz, normalized to equal fundamental amplitude. BLEP reduces
folded spectral energy by **15.1–20.7 dB** relative to naive oscillators. This
isolated test does not establish aliasing performance for sync, Poly-Mod or
modulated pulse width.

Before publication: listen to both filters, confirm native control directions
on the device, assess aliasing/sync/Poly-Mod musically, calibrate against Rev-4
reference material, validate a real edit-buffer capture, and approve the
firmware/editor release. No public firmware/editor release was made here.
