# Prophet-5 Rev-4 prototype and capacity audit

Work based on `next` at `df03d93`, measured on the connected FM-1 on 2026-10-08.
This is a measurement prototype, not a finished Prophet engine or a release.

## Current scope

`MELODEE_PROPHET_PROTOTYPE=1` substitutes P5TEST for engine 0 **only in test
firmware**. Musical flash erases and writes are refused in that build. The
normal firmware retains ANALOG at ID 0. Prophet needs a new persistent ID
after the performance gate and the collection/favourite namespace work.

Implemented for measurement: native single-program/edit-buffer frame decoding
and exact encoding, five local voices within the existing shared budget,
independent oscillators and simultaneous waves, B triangle/low-frequency/key
tracking, noise, band-limited waveforms with fractional sync correction,
separate amp/filter ADSRs, velocity switches, audio-rate Poly-Mod to A frequency,
A pulse width and filter, provisional Vintage mismatch, and two separate
four-pole filter character models with saturation and self-oscillation.

The filters are engineering approximations. Cutoff, resonance, envelope and
oscillator ranges have not been calibrated to a real Rev-4. Native wheel/noise
mix, aftertouch, full native LFO range/behaviour, glide, unison and bend behaviour
are not complete. These settings remain intact in the raw patch. The existing
Melodee envelope-ownership path is used; the common voice renderer is unchanged.

The measured hot path uses the existing RAMTEXT region and local coefficient
and saturation tables. Startup already copies these sections. An indirect
call crosses the device's XIP/RAM branch-distance limit. `P5_OVERSAMPLE=1` is
the default measurement configuration; `P5_OVERSAMPLE=2` remains available to
compare cost and aliasing. No claim of acceptable aliasing or musical fidelity
follows from the timing or stability tests.

## Native wire evidence

Sources: [Sequential specifications](https://sequential.com/classics-reissued/prophet-5-10/),
[MIDI implementation 1.4](https://sequential.com/wp-content/uploads/2021/03/Prophet-5-MIDI-Implementation-1.4.pdf),
[official factory downloads](https://sequential.com/support/download/prophet-5-10-sounds/).
The official v1.03 archive tested was
[Prophet-510-Factory-Programs-ReadMe1.03.zip](https://sequential.com/wp-content/uploads/2025/03/Prophet-510-Factory-Programs-ReadMe1.03.zip).
Factory data is downloaded into ignored build storage, not distributed with the source.

Both official `.syx` banks contain 200 single-program frames, 159 bytes each.
Each uses manufacturer 01, model **32**, command 02, group/program, then 152
packed bytes representing **133 raw bytes**. The guide instead describes
model 31 and 128 raw bytes; 128 bytes pack to 147, not 152. The codec therefore
accepts both explicit lengths and model IDs, preserving the original length,
model, command, address and every opaque byte on export. It rejects ambiguous
lengths, invalid addresses, embedded MIDI status bytes and invalid final masks.

Raw name bytes are 65–84. The real files include FF bytes at offsets 97 and 98,
so clearing unknown fields or treating the patch as seven-bit data loses information.
The codec tests re-encode every frame byte for byte in both official banks.
Edit-buffer tests are synthetic frames derived from the same payload layouts;
a real Rev-4 edit-buffer capture is still needed.

File SHA256:

| File | SHA256 |
| --- | --- |
| FACTORY v1.03 | `9b5beaae2953deed5a50ed5f3cbd78c589e704c35a694fa188e9b27f693b73ca` |
| USER v1.03 | `1d6e9dec2753a08ac6d07b98f0f4e755e33b41de11dc3bed46fa23217619d232` |

`prophet_patch.c` uses a 138-byte patch record with 133 raw bytes and five
wire-layout fields. Interpreting DSP values never rewrites raw imported bytes.
The measurement-only editor command 95 transfers these patches in RAM and
refuses replacement while the selected track has voices. It does not add
direct native Sequential reception or a production editor protocol.

## Firmware, RAM and storage

| Resource | Original baseline | Default with three engines retired |
| --- | ---: | ---: |
| App image | 383,204 B | 367,140 B |
| `.data` + `.bss` | 90,812 / 98,304 B | 90,812 / 98,304 B |
| Permanent pool | 160,588 / 344,064 B | 160,588 / 344,064 B |
| On-demand audio arena | 183,476 B | 183,476 B |

The prototype adds 552 bytes of permanent native patches and 16 bytes of small
RAM state. Each active Prophet track requests 388 bytes of working state.
Five Prophet voices cost fifteen of the sixteen existing shared voice units.
The existing per-engine cost callback charges three units per Prophet voice.
Adding one ordinary voice on each of the other three tracks leaves three
Prophet voices plus those three ordinary voices (fifteen units total).
These are planned allocation steals, distinct from overload shedding.
This is not five voices simultaneously on each of four Prophet tracks.

The app slot is currently `0x8DFBC` bytes; code size alone is not the storage
limitation. The allowed musical flash regions already hold legacy project/FM
migration copies, bank projects, CZ and native FM collections, general presets,
project extensions and settings. OTA staging is E0000–E4FFF. FB000 is not in
the musical write whitelist; FF000 holds boot/vendor metadata and is not free.

A 128-slot Prophet collection with preserved wire metadata and A/B protection
needs approximately **ten 4-KiB sectors (40 KiB)**. With 3,840 bytes per protected
sector and a small collection header, 26 aligned records of 144 bytes fit;
five sectors per copy cover 128 slots. The final record/header format must be
checked before assigning addresses.

Removing engines frees image bytes and their active DSP working allocations;
it does not itself free musical sectors. One possible repartition is to reduce
the app slot by 40 KiB and reserve 89000–92FFF for Prophet. That requires a
coordinated linker/package/loader/write-whitelist change, an OTA migration and
backup/downgrade tests. **That repartition has not been implemented.** Existing
loaders can overwrite the proposed range; it must not hold patches yet.

## Requested engine removals

TRIO (6), WHEEL (7) and PHYS (9) are retired in the default firmware and editor.
Their IDs remain reserved. Existing projects and general user patches retain
these IDs, their edit values and preset references. Those tracks render silence;
they are not relabelled as another synth. Existing favourite bits are retained.
ANALOG and all other engine IDs remain unchanged.

Separate descriptor-removal measurements saved 8,916 B for PHYS, 3,580 B for
TRIO and 2,272 B for WHEEL. Removing all three, their runtime allocations and
browser UI together saves **16,064 B** in the final default build. This total
includes the small compatibility-preservation changes.

Their source and reference tests remain available under
`MELODEE_LEGACY_EXTRAS=1`. Nineteen removed preset golden hashes are archived in
`tests/golden_retired_extras.txt`; surviving expected audio hashes were not
regenerated. Resource tests now measure the largest available engine instead
of assuming four PHYS parts.

## Device timing gate

The benchmark uses USB playback and four-channel capture at 44.1 kHz, distortion,
chorus and reverb sends, and checks actual active voices. The Prophet
stress patch enables simultaneous waves, resonance 127, sync, all three
audio-rate Poly-Mod destinations and maximum Vintage variation. Tests reset
counters **before note attack** so startup shedding cannot be hidden by a
steady-state load reading. A pass requires every voice admitted by the shared
allocator to remain,
render maxima below 85% of the 2.902-ms half-buffer deadline, and no audio,
overload shedding or USB underrun/overrun errors. The mixed case expects two
planned allocation steals; the solo case expects none. Reported render time
excludes nested
TIMER5 work; the device's protection includes it, so a low render maximum alone
is insufficient. USB alternate settings 2/2 confirm native 24-bit streams;
the host converts to/from 16-bit PCM for the probes.

The original ANALOG baseline retained five voices (1,035 µs maximum) and eight
voices across four tracks (1,598 µs maximum), with no counter errors. Its mixed
tracks were LOFI/PHASE/TRIO. After TRIO's removal, prototype mixes use
LOFI/PHASE/VOICE; the original mixed number is context, not an identical mix
comparison.

Initial twice-rate builds failed: overload protection reduced five Prophet
voices to one or two. Early measurements after warmup concealed the attack
counter errors; those rows were marked failed by their low voice count, and
the measurement window was corrected. Subsequent builds reduced arithmetic,
waveform duplication and table-access costs. The initial normal-rate build
charged only two units per Prophet voice and measured:

| Scenario | Requested / minimum retained voices | Max render | Peak averaged CPU | Result |
| --- | --- | ---: | ---: | --- |
| SSI, Prophet alone | 5 / 5 | 2,163 µs | 70.31% | pass |
| Curtis, Prophet alone | 5 / 5 | 2,001 µs | 64.45% | pass |
| SSI, four-track mix | 8 / 6 | 2,560 µs | 67.58% | fail: 2 voices shed |
| Curtis, four-track mix | 8 / 6 | 2,576 µs | 64.45% | fail: 2 voices shed |

The chord is MIDI 60/63/66/69/72, velocity 110. Each window covers note
attack and 12 seconds of sustained playback, sampled
about twice per second. All final USB/audio-late counter deltas were zero.
Both five-voice cases had zero shedding/given-up counters. The lower mixed CPU
values are **after protection shed voices**, and must not be presented as
headroom for eight voices. The eight-voice shared allocation is not a safe
performance promise for this DSP. The prototype now charges three shared
units per Prophet voice, preventing
that over-admission before overload protection is needed.

A second pitch set at MIDI 84/87/90/93/96 retained all five voices for eight
seconds: SSI maximum 2,176 µs (71.09% peak averaged CPU), Curtis 2,033 µs
(66.02%), with zero audio/USB/shedding errors. The original two-unit mixed
cases still shed voices; SSI also recorded one late callback. These two pitch
sets do not replace a full pitch, effect and mixed-engine sweep.

The final three-unit allocation build passed all four scenarios:

| Scenario | Requested / allocated / retained | Max render | Peak averaged CPU | Overload/USB errors |
| --- | --- | ---: | ---: | --- |
| SSI, Prophet alone | 5 / 5 / 5 | 2,146 µs | 69.92% | zero |
| SSI, four-track mix | 8 / 6 / 6 | 2,139 µs | 67.58% | zero |
| Curtis, Prophet alone | 5 / 5 / 5 | 1,993 µs | 64.45% | zero |
| Curtis, four-track mix | 8 / 6 / 6 | 2,054 µs | 64.84% | zero |

Each mixed window counted exactly two planned allocation steals and zero
overload shedding. Each solo window counted neither. Thus five Prophet voices
are practical in the tested configuration; the four-track mix intentionally
admits three Prophet voices and three other voices. The shared budget remains
sixteen units and the common allocator is unchanged. Measurements are in
`build/prophet5/prototype-weighted-device.json`; the earlier failure and
higher-pitch files are retained alongside it.

The final prototype image is 371,424 B. This cannot be interpreted as the
complete engine's cost: it replaces ANALOG and disables musical saves, and
production editing/storage/performance code is absent. The normal build with
only the requested retirements is 367,140 B.

## Comparison recordings

Ten dry recordings captured the connected device: pad, brass, bass, sync and
Poly-Mod examples through each filter mode. Each contains approximately two
seconds held and four seconds of release. Raw mono stems are retained in
`build/prophet5/device-recordings`; none clipped and the host reported no
callback xruns. Listening copies apply the same gain of eight (+18.06 dB) to
every clip, preserving relative levels, in
`build/prophet5/prophet-filter-comparisons.zip`. These are prototype examples,
not comparisons against a real Prophet. Human listening and musical acceptance
are outstanding.

## Verification and remaining gates

Host coverage includes malformed/atomic decoding, exact random and factory
round trips, shared voice limits, native release/velocity ownership, both filter
stability sweeps and endpoint/high-pitch torture, and seeded self-oscillation.
ASan/UBSan checks cover both sample-rate configurations, including maximum
pitch and cutoff/resonance endpoints. Self-oscillation is
tested with an initial impulse; noise-start behaviour is not calibrated.

The full normal firmware/editor regression suite passed, with all 139 remaining
golden renders unchanged. It covers persistence, interrupted
saves, old projects, backups, browser order, retired identity preservation,
UI rendering, USB audio, audio golden hashes and render budgets.

The connected device was returned to the normal retirement build (MELODEE
v0.12 / FM-1_900); its engine list reports ANALOG at 0 and reserved placeholders
at 6, 7 and 9. The original runtime was restored. All 22 persistent musical
objects matched their original lengths and CRCs after installation. The
normal package SHA256 is
`270aea828a08d6c1af66eb2f7f0a51e6038496284eed8ac924d530d0c2265674`;
the measured prototype package is
`138c6daa198bd1ee1a6343bb1fff2e9d1cc637a2b1cfeda0ad6533ab99302026`.
Development packages share the same firmware identity; filenames and hashes
distinguish these builds. Changes are local and have not been published.

Release work remains: final efficient DSP design and aliasing checks; actual
Rev-4 comparisons and parameter calibration; the permanent engine/collection
IDs; storage repartition and torn-write tests; complete native controls and
modulation interactions; device and web editing/library import/export; full
project/backup/undo patch persistence; listening and physical control checks.
Firmware and editor publication remain gated on those checks.

## Reproducing the measurements

```sh
# Build normal firmware, then run the repository regression suite.
./build.sh
tests/run_tests.sh

# Prototype only: no musical flash writes. Back up before installing.
MELODEE_PROPHET_PROTOTYPE=1 P5_OVERSAMPLE=1 ./build.sh
python tools/prophet_device.py backup --backup build/prophet5/device-backup
python tools/fm1_install.py build/melodee.fwsc --yes
python tools/prophet_device.py bench --factory build/prophet5 --usb --seconds 12 \
  --backup build/prophet5/device-backup --output build/prophet5/device-performance.json
```

On this Mac, the build/test Python runtime was `/tmp/melodee-obxf-venv/bin/python`.
Raw packages, backups, build logs, timing JSON and WAV files are in ignored
`build/prophet5`. Restore the saved normal package and runtime after testing,
then verify every persistent object's original length and CRC. The prototype
must not be published as ordinary firmware.
