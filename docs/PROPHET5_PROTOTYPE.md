# Prophet-5 Rev-4 playable prototype

Work on `codex/prophet5-prototype`, based on local `next` at `2688403`.
The playable engine, native patch storage and editor are implemented. This is
an engineering prototype; reference calibration and human listening/control
approval remain release gates. Nothing here establishes calibrated Rev-4 fidelity.

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
- Audio-rate Poly-Mod from B plus filter envelope to A frequency, A pulse width
  and cutoff. Frequency modulation is bounded, without through-zero reversal.
- Shared audio-rate wheel modulation per part, combined native LFO waveforms,
  0.022–500 Hz rate, initial depth, noise blend and all native destinations.
- Per-note Vintage tuning/filter/envelope/amplifier mismatch, native glide,
  unison count/detune, priority/retrigger, release switch, sustain, channel
  aftertouch and one-to-twelve-semitone native pitch bend.

Native knob values are interpreted on their documented 0–120 scale, except
Fine B, Poly-Mod envelope and Vintage (0–127). Imported out-of-panel values
remain byte-exact; DSP clamps them. Rev-10 unison counts above five are preserved
but play at the five-voice cap. Frequency anchors, envelopes, modulation depths
and both filters still need calibration against reference recordings/hardware.
Default synthesis uses **1x** sampling. Experimental `P5_OVERSAMPLE=2` remains
available and has host stability coverage; its earlier device costs exceeded
budget. Strong sync/audio-rate modulation can alias. Band limiting alone does
not establish acceptable aliasing or reference fidelity.

The sample path and coefficient/saturation tables live in the existing RAM
DSP sections. The saturation lookup has less than 18 Q15 units of absolute
error against the intended tanh knee; host tests check odd symmetry,
monotonicity, endpoints and stability. The common renderer's structural cost
check passes. Hardware timing, rather than host speed, decides capacity.

## Device and web editing

Sixteen device pages cover STORE, OSC A/B, B waves, mixer, filter, both ADSRs,
velocity/envelope modulation, Poly-Mod, LFO/wheel and performance settings.
STORE offers 128 slots, naming up to twenty characters, native USB dump and
INIT. Store requires the normal stopped-transport/overwrite workflow.

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

## SysEx evidence and encoding

Sources: [Sequential specifications](https://sequential.com/classics-reissued/prophet-5-10/),
[MIDI implementation 1.4](https://sequential.com/wp-content/uploads/2021/03/Prophet-5-MIDI-Implementation-1.4.pdf),
[official sounds](https://sequential.com/support/download/prophet-5-10-sounds/) and
[v1.03 archive](https://sequential.com/wp-content/uploads/2025/03/Prophet-510-Factory-Programs-ReadMe1.03.zip).
Downloaded factory data stays in ignored build storage and is not redistributed.

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
and cache invalidation agree on this boundary. OTA rejects packages with the
old larger app partition before staging a loader; the loader independently
rejects mismatched app extents before app writes. A stock recovery tool using
an older external loader can still overwrite the new reserved sectors.

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
