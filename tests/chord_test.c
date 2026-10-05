/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Panel chord performance through the real panel, note router, editor and persistence. */
#define MELODEE_UI_PREVIEW 1
#include "seq_edit_test.c"

static int pitch_on(uint32_t track, uint32_t pitch)   /* a gated voice of the track plays it */
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (trk[track].v[i].active && trk[track].v[i].gate && trk[track].v[i].stage != 4u &&
            trk[track].v[i].note == pitch)
            return 1;
    return 0;
}
static void panel_notes(uint32_t keys)
{
    key_frame(keys, 0);
    events_block(0);
}
static void voicing_test(void)
{
    uint8_t got[4];
    uint32_t mode, shape, root, inv, spread, n, i;
    reset(16);
    scale_setting_set(TSEL, P_SCALE, 1);
    TSEL->p[P_CHMODE] = 1;
    assert(chord_notes(TSEL, 60, got) == 3 && !memcmp(got, (uint8_t[]){60,64,67}, 3));
    TSEL->p[P_CHMODE] = 2;
    assert(chord_notes(TSEL, 62, got) == 4 && !memcmp(got, (uint8_t[]){62,65,69,72}, 4));
    TSEL->p[P_CHMODE] = 3; TSEL->p[P_CHTYPE] = 7;
    assert(chord_notes(TSEL, 60, got) == 4 && !memcmp(got, (uint8_t[]){60,64,67,71}, 4));
    TSEL->p[P_CHTYPE] = 0; TSEL->p[P_CHINV] = 1;
    assert(chord_notes(TSEL, 60, got) == 3 && !memcmp(got, (uint8_t[]){64,67,72}, 3));
    TSEL->p[P_CHSPREAD] = 1;
    assert(chord_notes(TSEL, 60, got) == 3 && !memcmp(got, (uint8_t[]){64,72,79}, 3));
    TSEL->p[P_CHSPREAD] = 2;
    assert(chord_notes(TSEL, 60, got) == 3 && !memcmp(got, (uint8_t[]){64,79,96}, 3));
    for (mode = 0; mode < 4; mode++) for (shape = 0; shape < 10; shape++)
        for (root = 0; root < 128; root++) for (inv = 0; inv < 4; inv++) for (spread = 0; spread < 3; spread++) {
            TSEL->p[P_CHMODE] = mode; TSEL->p[P_CHTYPE] = shape;
            TSEL->p[P_CHINV] = inv; TSEL->p[P_CHSPREAD] = spread;
            n = chord_notes(TSEL, root, got);
            assert(n <= 4);
            for (i = 0; i < n; i++) assert(got[i] < 128 && (!i || got[i] > got[i-1]));
            if (!mode) assert(n == 1 && got[0] == root);
        }
    assert(chord_notes(TSEL, KB_SILENT, got) == 0);
    track_select(TRK_DRUM);
    TSEL->p[P_CHMODE] = 3;
    assert(chord_notes(TSEL, 36, got) == 1 && got[0] == 36);
    puts("chords: scale triads/sevenths, fixed shapes, inversions, spreads, all MIDI bounds, drums bypass");
}
static void chord_lifecycle_test(void)
{
    uint32_t before, i;
    reset(16); go_home();
    fm1_in.notes = kb_prev = 0;
    mi_w = mi_r = mo_w = mo_r = 0;
    transport_req = panic_req = 0; usb.config = 1;
    TSEL->p[P_CHMODE] = 3; TSEL->p[P_VOICE] = V_POLY;
    panel_notes(1u << 7);
    assert(pitch_on(0,60) && pitch_on(0,64) && pitch_on(0,67) && mo_w == 3);
    panel_notes((1u << 7) | (1u << 14));
    assert(live_refs[0][67] == 2 && pitch_on(0,71) && pitch_on(0,74) && mo_w == 5);
    before = mo_w;
    panel_notes(1u << 14);
    assert(!pitch_on(0,60) && !pitch_on(0,64) && pitch_on(0,67) && mo_w == before + 2);
    for (i = before; i < mo_w; i++) assert(((midi_out_q[i % MOQ] >> 16) & 127u) != 67);
    trk[0].p[P_CHMODE] = 0; trk[0].p[P_CHINV] = 2; trk[0].p[P_ROOT] = 4;
    track_select(1);
    panel_notes(0);
    assert(!pitch_on(0,67) && !pitch_on(0,71) && !pitch_on(0,74) && mo_w == 10);
    for (i = 0; i < 128; i++) assert(live_refs[0][i] == 0);
    track_select(0); TSEL->p[P_ROOT] = 0; TSEL->p[P_CHMODE] = 3; TSEL->p[P_CHINV] = 0;
    midi_frame(0x90, 60, 100, 0);
    assert(pitch_on(0,60) && !pitch_on(0,64) && !pitch_on(0,67));
    midi_frame(0x80, 60, 0, 0);
    panel_notes(1u << 7);
    before = mo_w;
    panic_req = 1;
    events_block(0);
    assert(mo_w == before + 3 && !kb_chord_n[7] && !pitch_on(0,60));
    panel_notes(0);
    assert(mo_w == before + 3);
    puts("chords: overlapping ownership, MIDI out, settings/track changes, incoming MIDI and panic releases");
}
static void chord_midi_burst_test(void)
{
    uint8_t expected[128] = {0}, voicing[4];
    uint32_t on[128] = {0}, off[128] = {0}, k, i, n, total = 0, start;
    reset(16); go_home();
    usb.config = 1; mo_r = mo_w = MOQ - 7; start = mo_w;
    fm1_in.notes = kb_prev = 0;
    TSEL->p[P_CHMODE] = 3; TSEL->p[P_CHTYPE] = 7;
    TSEL->p[P_CHSPREAD] = 2; TSEL->p[P_VOICE] = V_POLY;
    for (k = 0; k < 27; k++) {
        n = chord_notes(TSEL, kb_map(TSEL, k), voicing);
        for (i = 0; i < n; i++) expected[voicing[i]] = 1;
    }
    for (i = 0; i < 128; i++) total += expected[i];
    assert(total > 64); /* This dense voicing exceeded the original output ring. */
    panel_notes((1u << 27) - 1u);
    panel_notes(0);     /* Both edges arrive before the host drains, across ring wrap. */
    assert(mo_w - start == 2 * total);
    for (i = start; i < mo_w; i++) {
        uint32_t packet = midi_out_q[i % MOQ], note = (packet >> 16) & 127u;
        if (((packet >> 8) & 0xF0u) == 0x90u) on[note]++;
        else if (((packet >> 8) & 0xF0u) == 0x80u) off[note]++;
        else assert(0);
    }
    for (i = 0; i < 128; i++) assert(on[i] == expected[i] && off[i] == expected[i]);
    puts("chords: dense 27-key MIDI burst, shared-pitch releases and output ring wrap");
}
static void chord_editor_persistence_test(void)
{
    int16_t shape;
    reset(16);
    page_go(page_named("CHORD"));
    TSEL->p[P_VOICE] = V_MONO;
    edit_param(0, 3);
    assert(TSEL->p[P_CHMODE] == 3 && TSEL->p[P_VOICE] == V_POLY);
    TSEL->p[P_CHMODE] = 2; shape = TSEL->p[P_CHTYPE];
    knob_frame(1, 1);                       /* scale modes: the scale picks the quality */
    assert(TSEL->p[P_CHTYPE] == shape && !strcmp(ui.msg, "QUALITY FOLLOWS SCALE"));
    TSEL->p[P_CHMODE] = 3; knob_frame(1, 1);
    assert(TSEL->p[P_CHTYPE] > shape);
    ui.force = 1; ui_draw();
    TSEL->p[P_CHTYPE] = 8; TSEL->p[P_CHINV] = 1; TSEL->p[P_CHSPREAD] = 1;
    apply_preset(4);
    assert(TSEL->p[P_CHMODE] == 3 && TSEL->p[P_CHTYPE] == 8 && TSEL->p[P_VOICE] == V_POLY);
    page_go(page_named("STEP"));
    key_frame(1u << 7, 0);
    assert(TSEL->step[0].n == 4 && !memcmp(TSEL->step[0].note, (uint8_t[]){63,70,79,84}, 4));
    key_frame(0, 0);
    project_save(0);
    TSEL->p[P_CHMODE] = 0; TSEL->p[P_CHTYPE] = 0;
    project_load(0);
    assert(TSEL->p[P_CHMODE] == 3 && TSEL->p[P_CHTYPE] == 8 && TSEL->step[0].n == 4);
    template_save(); TSEL->p[P_CHMODE] = 0; template_load();
    assert(TSEL->p[P_CHMODE] == 3 && seq_is_empty(TSEL));
    {
        tmpl_v1_t old;
        uint32_t t, i;
        memset(&old, 0, sizeof old); old.magic = TMPL_MAGIC_V1; old.size = sizeof old; old.sel = 2; old.fm6_has = 4;
        old.fm6[2][8] = 91; old.fm6_on[2] = 63; old.fm6_fn[2][3] = 12;
        for (t = 0; t < NTRK; t++) for (i = 0; i < PROJ_NP_V5; i++) old.t[t].p[i] = (int16_t)(t * 100 + i);
        assert(template_import(&old, sizeof old) && template_used() && tmpl.sel == 2 && tmpl.fm6[2][8] == 91 && tmpl.fm6_fn[2][3] == 12);
        for (t = 0; t < NTRK; t++) {
            assert(tmpl.t[t].p[P_CHMODE] == 0 && tmpl.t[t].p[P_CHINV] == 0 && tmpl.t[t].p[P_E0] == t * 100 + 50);
            assert(!memcmp(tmpl.t[t].p, old.t[t].p, 50 * sizeof(int16_t)));
        }
        old.size--; assert(!template_import(&old, sizeof old));
    }
    puts("chords: poly activation, scale-led quality, preset retention, STEP voicing, project/template roundtrip and TMP1 migration");
}
int main(void)
{
    voicing_test(); chord_lifecycle_test(); chord_midi_burst_test(); chord_editor_persistence_test();
    puts("CHORD TEST PASSED");
    return 0;
}
