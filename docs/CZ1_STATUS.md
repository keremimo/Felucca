# CZ-1 implementation status

This branch is an experimental CZ-style engine. It does not yet meet the
requirement that listeners should be unable to distinguish it from a Casio CZ-1.
The engine is included in `next` at the user's request. The present evidence does
not justify describing it as a faithful CZ-1 emulation.

## What the current checks establish

- The corrected renderer passed 18 factory checks (nine presets at low/high
  pitch) and 36 native stress cases (all line/modulation modes with moving
  envelopes, resonance, double-sine, alternating basic/resonance waves and live
  expression). Every case held eight voices, with zero new shedding or missed
  deadlines. The heaviest observed case reached 75.4% CPU and 2433 us per audio
  block, below the approximately 2902 us deadline for 128 samples at 44.1 kHz.
- A separate four-part run held two voices per part, eight total, with no new
  missed deadlines or shedding over five observations. It reached
  2176 us. The first four-part assertion incorrectly compared the retained
  lifetime deadline counter with zero; the repeat correctly compared before/after
  values. `melodee_dbg.late` persists across firmware restarts.
- After native carrier/window support, 18 factory checks and 60 hidden-mode
  stress cases held eight voices, with zero new shedding or missed deadlines.
  Peak CPU was 73.8%; the longest measured block was 2389 us (factory case),
  versus the approximately 2902 us deadline. The four-part run reached 2150 us.
  Hidden-mode cases cover all line/modulation modes, moving envelopes, detune,
  expression and live edits. A further four-part run held two voices per part
  with the double-saw window. Native import/export was checked for 24 hidden-mode
  cases. The benchmark image preceded only a window-caption shortening; the DSP
  source and factory audio renders are unchanged by that final UI correction.
- These are measurements of this approximate model, with USB MIDI and the audio
  interface available but no active USB audio streaming. They do not establish
  the cost of further fidelity corrections or the entire effects/USB workload.
- The final native-window build and full suite reached `ALL HOST TESTS PASSED`.
  All 113 audio goldens were unchanged in this update, both UI render variants
  had zero layout findings, and old/native storage and protocol tests passed.
  Normal next firmware and all nine freshly backed-up data objects were restored
  and verified; device identity is FM-1_900.
- Firmware build and all host tests passed after the earlier oscillator correction. Only the three
  intentionally affected CZ audio goldens were changed; non-CZ goldens and CPU
  baselines were retained. Device data was backed up freshly before testing and
  normal next firmware/data restored afterward.
- Native Casio tone fields include the two line levels, six velocity sensitivities
  and 16-character name. Independent fixtures check their documented byte offsets;
  envelope and vibrato conversion fixtures check documented machine values.
- Native framed tone import/export was exercised over the device's USB MIDI.
  Host tests cover the open transfer handshake, timeout and malformed transfers.
  The native carrier/window update passed 24 device transfers: each hidden carrier
  (NULL, unwindowed MULTI-SINE and PULSE2) with every window code, read back through
  both the Melodee patch API and native Casio SysEx. Actual Patch Base on iPad has
  not been tested.
- All eight native carriers and all eight window codes are represented. Windows
  are independent of the carrier and shared by alternating waves. NULL settles
  to silence after its DC-step attack at full DCW; PULSE2 repeats twice per carrier
  period; MULTI-SINE can run without a window. The additional windows implement a
  half-period falling ramp and two rising ramps. Codes 6/7 preserve their native
  selectors but render the same window as 5, as observed on hardware.
- All 576 waveform/window combinations (512 enabled pairs and 64 single waves)
  preserve their native selector bits in codec tests. All 35 free mu:zines tones
  now import/render, including Rebecca. The earlier two false rejections were an
  importer bug: fine detune occupies bits 7–2, not 5–0. Independent manufacturer
  table fixtures check the corrected placement. Arbitrary noncanonical machine
  bytes are still canonicalized rather than preserved byte for byte.
- Native controls, line copying, compare, names, user presets, templates, projects
  and web libraries retain the new windows. Older 163-byte CZ tones and their
  storage formats are converted. The 165-byte tone fits the existing preset
  record; final firmware image is 557192 bytes, ordinary RAM 91980/98304 bytes, and pool
  335388/344064 bytes with the required 8192-byte reserve maintained.
- Native edit-buffer transfers are supported. Original CZ-1 bank management and
  operation-memory dumps are not implemented. Melodee has its own preset storage.

## What the current checks do not establish

Correctly transporting patch values does not establish their acoustic effect.
The renderer uses fitted envelope timing, chosen gain/depth and key-follow curves,
an approximate phase-distortion oscillator, approximate vibrato and detune behavior,
and Melodee's existing chorus (the native switch gates the Melodee chorus send,
which must also have a nonzero level). Double-sine and resonance structures have now been
corrected using real CZ-1 waveform recordings, including the doubled carrier,
unipolar resonance core, trapezoid shape and shared alternating-wave window.
Those provisional comparisons do not establish the full DCW response or validate
the remaining envelope, expression, modulation and output-path behavior.
The authored presets are not authenticated Casio factory tones.

Host regressions establish consistency with this implementation; they are not
reference comparisons against Casio audio. A device capture verified a 440 Hz
test tone. The [free reference audit](CZ1_REFERENCE.md) now includes external
patch auditions and provisional comparisons with real CZ-1 waveform recordings,
but no fully matched hardware tone/performance pair or blind listening test exists.

## Acceptance work for the requested fidelity

Obtain reference recordings from a real CZ-1, with the exact tone dump, keyboard
function settings and MIDI sequence for each capture. First isolate individual
waveforms, envelope stages, line levels, velocity/key follow and modulation with
chorus off. Then characterize detuned lines, ring/noise modes and chorus in stereo.
Compare pitch, spectra, envelope trajectories and modulation against these
references; calibrate the renderer and recheck the eight-voice device budget.

Finally run randomized, level-matched blind listening trials across representative
patches and playing styles. Agree a test protocol and success criterion before
claiming perceptual equivalence. An exact digital model and a successful perceptual
test are different claims; neither is established by this branch.

The documented tone format is available in
[Casio's CZ-1 System Exclusive specification](https://cz.brusi.com/CASIO-CZ-1-SYSTEM-EXCLUSIVE-SYSEX.pdf).
