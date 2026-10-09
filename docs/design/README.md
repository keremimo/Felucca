# Melodee redesign: references and parity

The redesign is built from the proposal's mockups, screen by screen, and checked against them.

- `mock/`: the proposal's mockups as they were drawn (HTML + SVG).
- `ref/<screen>.svg`, `ref/<screen>.png`: one mockup screen each, at the device's 240 x 240 pixels in the bundled
  Rubik. `tools/mock_refs.py` regenerates the PNGs (headless Chrome).
- `tests/ui_render.c` draws the same states on the firmware's own code (`ref_*` scenes, with the device's sounds);
  `tools/ui_mockcmp.py build/ui_new build/ui_mock` puts each beside its mockup (mockup | device | difference) and scores
  it: colour (8 x 8 blocks within 32 of the mockup's), layout (F1 of the edges, 2 px apart allowed), parity (their mean).
  `tests/run_tests.sh` writes the report to `build/ui_mock/report.txt`. A phase is done when its screens reach the
  target below and the side-by-side images are approved.

| Screens | Mockup | Baseline (0.13 + phases 1-5) | Target | Built |
|---|---|---|---|---|
| stage_held, stage_cutoff, stage_drum | `stage_columns` | 57-62 | 85 | R2: 90-93 |
| browser | `concept_screens` (2) | 48 | 80 | R3: 93.5 |
| patterns | `concept_screens` (3) | 48 | 85 | R4: 96.7 |
| song | `concept_screens` (4) | 38 | 80 | R4: 94.7 |
| env, lfo | `r5_pages` | (new) | 80 | R5: 83.4, 92.2 |
| edit_osc, fx, dly | `r5_pages` | (new) | 80 | R5: 93.0, 95.2, 84.0 |
| mixer | `r5_pages` | (new) | 80 | R5: 95.1 |
| notes | `r5_pages` | (new) | 80 | R5: 66.1 (the view centres the notes a row apart) |
| settings, dialog | `r5_pages` | (new) | 80 | R5: 94.5, 88.1 |

Texts differ on purpose (the device's own sound names): the targets leave room for them.

## Tokens (from the mockups' code)

Colours: background `#17141F`, panel `#1D1926`, surface `#221E2C`, raised `#262033` (the selected lane), quiet lane
`#1A1722`, line `#2E2939`, text `#F3EEF8`, mid `#9A93A8`, dim `#5A5468`, secondary names `#C9C2D6`. Tracks: coral
`#FF7A5C`, amber `#FFC145`, mint `#46D9B0`, lilac `#9D8CFF`. On a filled track colour: background-coloured text.

Type (Rubik): labels 8.5-9 px / 500, capitals, in the track's colour; names 10 px (selected: 500); header 11 px / 500;
values 15 px / 500 with 9 px units in mid; the chord root 30 px / 500, its quality 17 px / 500, the notes 11 px / 500.

Stage (`stage_note_overlay`), all positions in device pixels:

- Header, 18 px: play glyph at x 8; BPM 11 px / 500 at x 20; four track dots (r 2.5, x 54 + 8 k, y 11: the selected
  white, the others dim); at the right a context chip 82 x 14 r 4 at x 150, filled in the track's colour, its text
  9 px / 500 in the background colour (the engine: PROPHET-5, 909 KIT ..).
- Cards: 54 x 44 at x 4 + 58 k, y 22, r 6, surface; label 8.5 px at baseline 12, value 15 px at baseline 30, unit after
  it, gauge 42 x 3 r 1.5 at y 36 (line, filled in the track's colour). The knob turning: a fill of 22 % track colour
  over the surface, outlined in the track's colour.
- Panel: x 4, y 70, 232 x 82, r 6, panel colour. The waveform 2 px, smooth, in the track's colour (dimmed to 35 % while
  notes are held); the chord in front at x 14 baseline 100, the notes at baseline 120. A knob turning: what it shapes
  (the filter's curve, its cutoff dashed) in place of the waveform, its value at the top right, 13 px.
- Lanes: y 156 + 21 k, 232 x 19, r 5; the selected one raised and outlined in its colour, the others quiet; a 3 px
  stripe in the track's colour at the left; the number 10 px / 500 in the track's colour at x 13; the name 10 px at
  x 25 (selected: text, 500; else secondary); the pattern chip 18 x 12 r 3 at x 104 (selected: filled, else outlined),
  8 px / 500; 16 steps 4 x 8 r 1 from x 128, 5.6 apart (on: the track's colour, the playhead text, off: line); the
  level at x 222: a 3 px bar over a 10 x 2 line.
- Drum track: the lanes BD SD CP CH OH RS as chips 32 x 14 at y 132 (hitting: filled), the waveform dimmed above.
