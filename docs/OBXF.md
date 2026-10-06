# OBXF on the FM-1

OBXF is a mono-part port of the GPL-3.0-or-later OB-Xf engine, appended as engine 14. The reference checkout used for validation was `b08ffb6ab6cfa0f66cb057e855149ab640de0f78` from https://github.com/surge-synthesizer/OB-Xf. It is not vendored. The 75 curated factory patches are CC0; each author is retained in `eng_obxf_rom.h`.

EDIT's eight macros are neutral at zero. The 26 OBXF pages after EDIT 2 edit the underlying patch, covering all supported parameters. Projects, startup templates, full backups and sound undo/redo retain all 95 stored values and the patch name. Loading a saved edit does not reload its factory PTCH over it. User preset slots keep PTCH and macros, as FM6 does.

The device supports **two native OBXF voices across all tracks** when one selected OBXF part uses a steady patch: two polyphonic keys or one key with two-voice unison. Several selected OBXF parts reserve the budget for one native voice, because each has independent LFO and smoother overhead. Patches with layered saw/pulse waveforms, oscillator sync, crossmod, ring modulation, envelope pitch/PW modulation or LFO pitch/PW modulation reserve the shared budget for **one native voice**. Patch polyphony and unison settings are retained, but playback is bounded by these measured limits, with unison level compensation. OB-Xf's larger unison stacks are therefore not reproduced at their original voice count.

VoiceMatrix, MPE, microtuning, per-voice stereo pans and HQ oversampling are excluded. HQ remains stored for compatibility and is ignored. Dedicated web patch pages, OBXF SysEx patch transfer, writable patch banks and HQ are deferred.

## DSP and build validation

The target builds with `-mcpu=r3` at both compiler stages. Float operations use general registers; IRQ initialization explicitly disables float traps while retaining the integer divide trap. The disassembly has no float/double helper calls or `__udivdi3`.

The default image is 574,932 of 581,564 bytes. Ordinary RAM is 92,220 of 98,304 bytes; the pool is 333,188 of 344,064 bytes, leaving 10,876 bytes. The OBXF part is 4,308 bytes in the existing 12,888-byte engine-state union.

Run `OBXF_SRC=/path/to/OB-Xf sh tests/obxf_parity.sh`. It builds the reference's actual oscillator, filter, envelope, LFO and voice headers against dependency stubs. Matrix adjustments pass through native values and tuning is identity. Seeds, oscillator phase and deterministic detune are aligned. The port and reference are compiled with `-ffp-contract=off`.

Both firmware approximations and a libm variant pass 39 deterministic feature cases at a minimum 70 dB SNR and maximum absolute error 0.002. Observed worst error is 0.001313, with minimum SNR approximately 75 dB. Coverage includes saw/pulse/triangle, sync, crossmod, ring modulation, three noise colors, two- and four-pole filters, all 15 Xpander modes, envelopes, both LFOs, pulse-width and volume modulation, random LFO, tempo sync, vibrato, glide and unison detune. This tests individual DSP features; hardware voice-count limits and the omitted features remain differences from the plugin.

The parity check caught and corrected the BLEP residual sign, sample-by-sample release coefficient update, and single-rounding TPT filter-state update. Pitch drift and cutoff drift use cached native coefficients with small-argument expansions; modulated native parameters still update at sample rate.

Output gain is 32768. At velocity 100 / level 100, the dry A4 OBRIGHT measurement is peak 16,235 / RMS 7,895, versus ANALOG SAW LEAD 24,680 / 11,674 and FM6 TINE EP 3,917 / 1,656. The full preset regression passes clipping, DC, release and silence checks. Native OB-Xf pad releases receive a duration-based test bound rather than the old 12-second bound.

## Storage compatibility

FUN9 is 4,396 bytes. It retains FUN8's FM6 offsets and appends four 190-byte patches and 13-byte names before the project name. TPL7 adds the same 812 bytes to TPL6. FBKA is 19,244 bytes, fitting the existing five-sector A/B flash objects. Its inactive steps use eight bytes: four seven-bit notes; `n + 5*time` in four bits; two flags; seven-bit velocity; eight-bit hits and accents; seven-bit probability. Packing uses 32-bit operations.

FUN1–FUN8, TPL5/6 and FBK9 remain readable. Missing OBXF data initializes to INIT. Legacy FBK9 upgrades in main-loop staging before saving, retaining its inactive banks, motion, timing, assignments and FM6 function settings. Tests verify the frozen legacy offsets and upgrade, edited-patch round trips, corrupt patch rejection and every supported device page. Older firmware rejects the new magics.

## Hardware checks

The FM-1's current 128-frame half-buffer deadline is **2,902 microseconds**, rather than the older 5,805-microsecond figure. Before limiting voices, two ordinary voices consumed about 75% CPU; larger unison stacks exceeded the deadline. JET's large pitch sweep also exceeded the deadline with two voices, which motivated its one-voice allowance. Representative bounded patches rendered in about 2.2–2.4 ms before the final idle-path optimization.

USB capture of factory OBRIGHT at MIDI A4 measured 440.41 Hz (with its native drift). The default build and full host suite pass, including the reference parity harness. A 150-window hardware sweep (all 75 presets, two simultaneous requests at MIDI 60/64 and 96/100) measured a maximum 2,901 microseconds and 78.9% windowed CPU. It recorded two isolated late renders on high-note onsets (MELLOWOW and SAW PLUCK), despite the measured render duration remaining below the deadline. This is not a guarantee against an onset glitch; Continuous holds of OBRIGHT, MELLOWOW, MUUGER, 8VM LEAD, ROTARY ORGAN, FLUTE and JET at high notes had zero late renders in their one-second windows. Four idle OBXF parts consumed 28.5% CPU; two sounding voices then overloaded, motivating the additional one-voice limit for several selected OBXF parts. The final multi-part policy was verified with one through eight simultaneous MIDI note requests: 56–59% CPU, maximum 2,041 microseconds and zero late renders while playback was bounded to one native voice. A separate single-part two-voice hold measured 73.4% CPU, maximum 2,246 microseconds and zero late renders. Physical knob feel and subjective listening remain separate from these measurements.
