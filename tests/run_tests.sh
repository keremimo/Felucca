#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Host tests (no hardware). Run from the repo root after ./build.sh:
#   tests/run_tests.sh
#
# Regression suite (tests/regress.c, tests/target_budget.py; details at the top of regress.c):
#   golden renders  every engine x preset, the drum kit, voice modes, FX sends, a 4-track mix: one hash
#                   each in tests/golden.txt. A change of the sound fails with the list of renders.
#   health          clipping, DC, peak level, voices free after the release, silence at the end.
#   CPU             instructions / sample per preset and mix (tests/cpu_baseline.txt, +25 %), ns printed;
#                   target: loop instructions of the render functions in build/melodee.dis
#                   (tests/target_budget.txt, +10 %; exact, static).
#   voices          the budget of 8, steal fades, MONO / LEGATO / UNISON keep their note, the VOICE cap,
#                   no hanging notes on any MIDI / key routing.
# After an intended change of the sound: GOLDEN_UPDATE=1 sh tests/run_tests.sh, review the diff
# of tests/golden.txt, commit it with the change. After an intended change of the cost (or a new
# compiler): BUDGET_UPDATE=1 (rewrites cpu_baseline.txt and target_budget.txt). VERBOSE=1: every render.
set -e
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
cd "$(dirname "$0")/.."
OUT=build/host
mkdir -p "$OUT"
CC="${CC:-cc} -O1 -Wall -Wno-unused-function"
fail=0
run() { echo "== $1"; shift; "$@" || fail=1; }

[ -f build/melodee.fwsc ] || { echo "run ./build.sh first"; exit 1; }

$CC -w -Ifirmware/hal -o "$OUT/encoder_test" tests/encoder_test.c
run "encoders: first click, direction and reversed transitions" "$OUT/encoder_test"

$CC -o "$OUT/storage_test" tests/storage_test.c
run "flash storage (A/B, torn writes)" "$OUT/storage_test"

$CC -o "$OUT/upreset_test" tests/upreset_test.c
run "user presets (UP_PUT parser, bank round trip, versions)" "$OUT/upreset_test"

$CC -o "$OUT/midi_uart_test" tests/midi_uart_test.c
run "TRS MIDI parser" "$OUT/midi_uart_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/midi_clock_test" tests/midi_clock_test.c -lm
run "USB/TRS MIDI clock and transport" "$OUT/midi_clock_test"

$CC -o "$OUT/usb_audio_test" tests/usb_audio_test.c -lm
run "USB audio: routing, clock drift and stream recovery" "$OUT/usb_audio_test"
$CC -o "$OUT/usb_audio_driver_test" tests/usb_audio_driver_test.c
run "USB audio: endpoint lifecycle and packet ownership" "$OUT/usb_audio_driver_test"
run "USB descriptors: MIDI, CDC and audio configurations" python3 tests/usb_audio_desc_test.py
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/usb_audio_tracks_test" tests/usb_audio_tracks_test.c -lm
run "USB audio: four isolated track stems through the real mixer" "$OUT/usb_audio_tracks_test"

$CC -o "$OUT/ota_test" tests/ota_test.c
run "M-UPGRADE entry" "$OUT/ota_test" build/melodee.fwsc

head -c 200000 build/melodee.bin > "$OUT/old_app.bin"
python3 tools/fm1pkg_make.py "$OUT/old_app.bin" build/loader/ota.bin "$OUT/old.fwsc" >/dev/null
$CC -o "$OUT/ldr_test" tests/ldr_test.c
run "update loader: other app -> this build" "$OUT/ldr_test" "$OUT/old.fwsc" build/melodee.fwsc

$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/hostsim" tests/hostsim.c -lm
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/fm6_test" tests/fm6_test.c -lm
run "FM6: DX7 algorithms, voices, pitch, levels, envelopes, modulation" "$OUT/fm6_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/fm6_ams_test" tests/fm6_ams_test.c -lm
run "FM6: AMS as Dexed's doubles figure it, every modulation" "$OUT/fm6_ams_test"
if [ -n "$DEXED_SRC" ]; then
    run "FM6 vs Dexed: sample-exact renders (DEXED_SRC)" sh tests/fm6_parity.sh --quick
else
    echo "== FM6 vs Dexed: skipped (DEXED_SRC=<dexed>/Source to run tests/fm6_parity.sh)"
fi
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/scale_test" tests/scale_test.c -lm
run "scales: white-key mapping and note lifecycle" "$OUT/scale_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/midi_scale_test" tests/midi_scale_test.c -lm
run "MIDI scales: mapping, recording, routing and held notes" "$OUT/midi_scale_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/seq_edit_test" tests/seq_edit_test.c -lm
run "sequencer: note length, panel gestures, playback and display" "$OUT/seq_edit_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/chord_test" tests/chord_test.c -lm
run "chords: voicings, panel releases, MIDI out burst, editor and FUN8/TMP2 persistence" "$OUT/chord_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/midi_expression_test" tests/midi_expression_test.c -lm
run "MIDI expression: pitch bend, mod wheel, panic, sustain (USB and TRS)" "$OUT/midi_expression_test"
run "DSP render (ANALOG preset 0)" "$OUT/hostsim" 0 0 1 "$OUT/render.wav"
mkdir -p build/tracks_demo
run "TRACKS: 4-track pattern, live recording (lengths, swing), voice budget, engine switch, cost" env TRACKS=build/tracks_demo "$OUT/hostsim" 0 0 1 "$OUT/tracks.wav"
$CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/project_test" tests/project_test.c -lm
run "project formats (FUN1..FUN7 -> FUN8: patterns, parameters, FM6 voices and functions)" "$OUT/project_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/slicer_test" tests/slicer_test.c -lm
mkdir -p build/slicer_demo
run "SLICER: no clicks, timing, sync with the sequencer, STUT, cost, demos" "$OUT/slicer_test" build/slicer_demo
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/regress" tests/regress.c -lm
run "regression: golden renders, health, voices, CPU budget" "$OUT/regress" tests/golden.txt tests/cpu_baseline.txt
# SLICE (tests/slice_test.c) needs a MELODEE_SLICE=1 build; the engine is not built by default

run "regression: target cost of the render loops" python3 tests/target_budget.py \
    build/melodee.dis tests/target_budget.txt

run "installer CLI (fm1_install.py) against a simulated FM-1" python3 tests/install_test.py

if command -v node >/dev/null 2>&1; then
    run "web pages: editor protocol, samples, packages, update protocol" node web/test_web.mjs
else
    echo "== skip web tests (no node)"
fi

[ $fail -eq 0 ] && echo "ALL HOST TESTS PASSED" || { echo "HOST TESTS FAILED"; exit 1; }
