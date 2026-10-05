/* SPDX-License-Identifier: GPL-3.0-only */
/* Real panel/UI/renderer and playback, with host substitutes for the LCD,
 * physical input and persistent storage. Optional argv[1]: screen as PPM. */
#include <assert.h>
#define main hostsim_main
#include "hostsim.c"
#undef main

static uint16_t screen[240 * 240];
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j;
    assert(x + w <= 240 && y + h <= 240);
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            screen[(y + j) * 240 + x + i] = (uint16_t)((p[j * w + i] >> 8) | (p[j * w + i] << 8));
}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    assert(x + w <= 240 && y + h <= 240);
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            screen[j * 240 + i] = c;
}
#include "../firmware/src/gfx.c"
#define FM1_NCOL 11
#define FM1_TICKS_PER_US 1u
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {      /* as fm1_input.h: where the LEDs are */
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};
static uint8_t fm1_led[FM1_NCOL], fm1_led_dim[2][FM1_NCOL], fm1_led_dim_mask[2];
static uint32_t button_edges, note_edges;
static int32_t encoders[7];
static void fm1_led_key(uint32_t id, int on) { (void)id; (void)on; }
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static void fm1_wdt_feed(void) { fm1_ms++; }
static uint32_t fm1_ticks(void) { return fm1_ms * 1000u; }
static uint32_t fm1_input_edges(uint32_t which) { uint32_t n = button_edges; (void)which; button_edges = 0; return n; }
static uint32_t fm1_input_note_edges(void) { uint32_t n = note_edges; note_edges = 0; return n; }
static int32_t fm1_enc_take(uint32_t e) { int32_t n = encoders[e]; encoders[e] = 0; return n; }
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
#include "../firmware/src/icons.c"
#define SCOPE_N 512u
static int16_t scope_buf[SCOPE_N];
static uint32_t scope_w;
static struct { uint32_t stage; } melodee_dbg;
#include "../firmware/src/ui_draw.c"
#include "../firmware/src/ui_menu.c"
#include "../firmware/src/ui_input.c"
#include "../firmware/src/upreset.c"
#include "../firmware/src/project.c"

static void fm6_store(uint32_t k) { (void)k; assert(0); }
static void fm6_send(void) { assert(0); }
static void fm6_init_voice(void) { assert(0); }

static void reset(uint32_t len)
{
    uint32_t i;
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    memset(&ui, 0, sizeof ui);
    step_history_clear();
    memset(encoders, 0, sizeof encoders);
    memset(midi_notes, 0, sizeof midi_notes);
    memset(midi_ch, 0, sizeof midi_ch);
    memset(midi_owners, 0, sizeof midi_owners);
    memset(live_refs, 0, sizeof live_refs);
    memset(kb_chord_n, 0, sizeof kb_chord_n);
    step_midi_w = step_midi_r = step_midi_overflow = 0;
    mi_w = mi_r = 0;
    host_tracks_init();
    for (i = 0; i < NTRK; i++) {
        track_defaults_steps(&trk[i]);
        pat_clear_bank(&trk[i]);
    }
    panel = PANEL_DEFAULT;
    settings_init();
    TSEL->p[P_SLEN] = (int16_t)len;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].scope == SC_STEP)
            ui.page = (uint8_t)i;
    assert(cur_page()->scope == SC_STEP);
    page_entered();
    fm1_in.notes = fm1_in.buttons = button_edges = note_edges = 0;
    last_note = 60;
}

static void key_frame(uint32_t keys, int32_t length_delta)
{
    int note_action = keys || ui.entry_open || step_midi_w != step_midi_r;
    note_edges = keys & ~fm1_in.notes;
    fm1_in.notes = keys;
    encoders[panel.enc[EN_SELECT]] = note_action ? length_delta : 0;
    encoders[panel.enc[EN_K1]] = note_action ? 0 : length_delta;
    fm1_ms += 20;
    ui_input();
}

static void midi_frame(uint32_t status, uint32_t pitch, uint32_t vel, int32_t step_delta)
{
    midi_in_q[mi_w++ % MQ] = (status >> 4) | status << 8 | pitch << 16 | vel << 24;
    events_block(0);
    key_frame(fm1_in.notes, step_delta);
}

static void note(uint32_t index)
{
    step_t *s = &TSEL->step[index];
    s->time = ST_NOTE;
    s->n = 3;
    s->note[0] = 60; s->note[1] = 64; s->note[2] = 67;
    s->flags = SF_ACCENT;
    s->vel = 110;
}

static void lengths_test(void)
{
    uint32_t len, at, n, i;
    for (len = 1; len <= NSTEP; len++)
        for (at = 0; at < len; at++) {
            reset(len);
            note(at);
            step_t original = TSEL->step[at];
            for (n = 1; n <= len; n++) {
                assert(step_note_resize(TSEL, at, (int32_t)n) == n);
                assert(step_note_length(TSEL, at) == n);
                for (i = 0; i < n; i++) assert(step_note_start(TSEL, (at + i) % len) == at);
                assert(!memcmp(&original, &TSEL->step[at], sizeof original));
            }
            assert(step_note_resize(TSEL, at, -99) == 1);
            for (i = 0; i < len; i++)
                if (i != at) assert(TSEL->step[i].time == ST_REST && !TSEL->step[i].n && !TSEL->step[i].flags);
        }
    reset(16);
    note(14); note(3);
    step_t neighbor = TSEL->step[3];
    assert(step_note_resize(TSEL, 14, 16) == 5);
    assert(!memcmp(&neighbor, &TSEL->step[3], sizeof neighbor));
    assert(step_note_resize(TSEL, 14, 2) == 2);
    assert(TSEL->step[0].time == ST_REST && TSEL->step[2].time == ST_REST);
    assert(!memcmp(&neighbor, &TSEL->step[3], sizeof neighbor));
    reset(16);
    for (i = 0; i < 16; i++) TSEL->step[i].time = ST_TIE;
    assert(step_note_start(TSEL, 0) == NSTEP);
    assert(step_note_resize(TSEL, 0, 5) == 0);
    reset(16);
    note(0);
    TSEL->step[4].time = ST_TIE;
    assert(step_note_resize(TSEL, 0, 16) == 3);
    assert(TSEL->step[3].time == ST_REST && TSEL->step[4].time == ST_TIE);
    puts("lengths: every pattern length/onset, loop wrap, chords, shrink, neighboring notes and orphan ties ok");
}

static void gestures_test(void)
{
    reset(32);
    cursor_set(14);
    /* Enter a chord and turn SELECT on its first frame. */
    key_frame((1u << 7) | (1u << 11) | (1u << 14), 3);
    assert(ui.entry_open && ui.cursor == 14 && step_note_length(TSEL, 14) == 4);
    assert(TSEL->step[14].n == 3 && TSEL->preset == 0);
    key_frame((1u << 7) | (1u << 11) | (1u << 14), -2);
    assert(ui.cursor == 14 && step_note_length(TSEL, 14) == 2 && TSEL->step[16].time == ST_REST);
    key_frame((1u << 7) | (1u << 11) | (1u << 14), 2);
    assert(step_note_length(TSEL, 14) == 4);
    /* Last detent and key release in the same frame: include it before advance. */
    key_frame(0, 1);
    assert(!ui.entry_open && ui.cursor == 19 && ui.bank == 1);
    key_frame(0, -2);                       /* released: STEP moves the cursor */
    assert(ui.cursor == 17 && step_note_length(TSEL, 14) == 5);
    cursor_set(14);
    note(18);
    key_frame(1u << 7, 20);                 /* an existing neighbor stops extension */
    assert(step_note_length(TSEL, 14) == 4 && TSEL->step[18].n == 3);
    assert(!strcmp(ui.msg, "NEXT NOTE"));
    key_frame(0, 0);
    cursor_set(20);
    key_frame(0, 3);
    assert(ui.cursor == 23 && TSEL->step[20].time == ST_REST);
    encoders[panel.enc[EN_K1]] = -9;         /* STEP moves the cursor when released */
    ui_input();
    assert(ui.cursor == 14);
    encoders[panel.enc[EN_K3]] = 1;          /* TIME still edits NOTE/TIE/REST */
    ui_input();
    assert(TSEL->step[14].time == ST_TIE);
    encoders[panel.enc[EN_K3]] = -1;
    ui_input();
    assert(TSEL->step[14].time == ST_NOTE);
    encoders[panel.enc[EN_PRESET]] = 1;
    ui_input();
    assert(TSEL->pat_q == 2 && TSEL->preset == 0); /* LOOP's PRESETS selects patterns */
    reset(16);
    cursor_set(14);
    key_frame(1u << 7, 3);
    key_frame(0, 0);
    assert(ui.cursor == 2);                /* entry advance wraps over the loop */
    reset(16);
    track_select(TRK_DRUM);
    key_frame(1u << 7, 3);
    assert(step_note_length(TSEL, 0) == 1 && !strcmp(ui.msg, "DRUMS: ONE SHOT"));
    key_frame(0, 0);
    assert(ui.cursor == 1);
    reset(16);
    TSEL->p[P_QUANT] = 2;                  /* WHITE */
    key_frame(1u << 8, 0);                 /* silent scale key cannot start an entry */
    key_frame(0, 0);
    assert(ui.cursor == 0 && !TSEL->step[0].n);
    puts("gestures: held chord, release/detent ordering, SELECT length, STEP cursor, TIME, patterns, drums and scale mode ok");
}

static void midi_entry_test(void)
{
    reset(32);
    TSEL->p[P_VOICE] = V_POLY;
    TSEL->p[P_QUANT] = Q_WHITE;
    TSEL->p[P_SCALE] = 1;                    /* major: MIDI C4/E4/G4 */
    cursor_set(14);
    midi_frame(0x91, 60, 100, 0);           /* channel 2 belongs to another track */
    assert(ui.cursor == 14 && !ui.entry_open);
    midi_frame(0x90, 61, 100, 0);           /* WHITE: black key is silent */
    assert(!ui.entry_open);
    midi_frame(0x90, 60, 100, 2);
    midi_frame(0x90, 64, 100, 1);
    midi_frame(0x90, 67, 100, 0);
    assert(ui.entry_open && step_midi_held == 3 && step_note_length(TSEL, 14) == 4);
    assert(TSEL->step[14].n == 3 && TSEL->step[14].note[0] == 60 &&
           TSEL->step[14].note[1] == 64 && TSEL->step[14].note[2] == 67);
    midi_frame(0x80, 60, 0, 0);
    midi_frame(0x90, 64, 0, 0);            /* note-on velocity zero is note-off */
    assert(ui.cursor == 14 && step_midi_held == 1);
    midi_frame(0x80, 67, 0, 1);           /* final detent applies before advancing */
    assert(ui.cursor == 19 && !step_midi_held && !ui.entry_open);
    assert(step_note_length(TSEL, 14) == 5);
    midi_frame(0x80, 61, 0, 0);
    midi_frame(0x80, 60, 0, 0);           /* duplicate/stray note-offs do nothing */
    assert(ui.cursor == 19);
    midi_frame(0x90, 72, 100, 0);
    key_frame(1u << 7, 1);                /* mixed panel and MIDI keys share an entry */
    assert(ui.entry_open && step_note_length(TSEL, 19) == 2);
    midi_frame(0x80, 72, 0, 0);
    assert(ui.cursor == 19);
    key_frame(0, 0);
    assert(ui.cursor == 21);
    midi_frame(0x90, 74, 100, 0);
    assert(ui.entry_open && step_midi_held == 1);
    midi_silence_track(0);                  /* preset/panic clears every held MIDI key */
    key_frame(0, 0);
    assert(ui.cursor == 22 && !step_midi_held);
    reset(16);
    scale_setting_set(TSEL, P_QUANT, Q_MPC);
    scale_setting_set(TSEL, P_SCALE, 1);
    TSEL->p[P_ROOT] = 0;
    midi_frame(0x90, 19, 100, 0);
    midi_frame(0x90, 36, 100, 0);
    midi_frame(0x90, 60, 100, 0);
    assert(!ui.entry_open && !step_midi_held && !TSEL->step[0].n && ui.cursor == 0);
    midi_frame(0x90, 21, 100, 2);          /* H02 is ROOT; other banks cannot join the entry */
    midi_frame(0x90, 64, 100, 0);
    assert(ui.entry_open && step_midi_held == 1 && TSEL->step[0].n == 1);
    assert(TSEL->step[0].note[0] == 60 && step_note_length(TSEL, 0) == 3);
    midi_frame(0x80, 64, 0, 0);
    assert(ui.entry_open && ui.cursor == 0);
    midi_frame(0x80, 21, 0, 0);
    assert(!ui.entry_open && !step_midi_held && ui.cursor == 3);
    puts("MIDI STEP entry: routing, WHITE scale, chords, release, final detent and mixed input ok");
}

static void live_record_follow_test(void)
{
    uint32_t period, hits;
    reset(32);
    TSEL->p[P_VOICE] = V_POLY;
    TSEL->p[P_QUANT] = Q_MPC;              /* MPC Sample pads H01-H16 send notes 20-35 */
    song.playing = 1;
    song.rec = 1u << song.sel;
    TSEL->seq_idx = 18;
    period = div_samples((uint32_t)TSEL->p[P_SDIV]);
    TSEL->seq_pos = step_samples(TSEL, period, 18) * 3u / 4u;
    midi_frame(0x90, 23, 100, 0);
    assert(ui.cursor == 18 && ui.bank == 1);
    assert(TSEL->step[18].n == 1 && TSEL->step[18].note[0] == midi_map(TSEL, 23));
    assert(!TSEL->step[19].n && !step_midi_held && !ui.entry_open);
    midi_frame(0x80, 23, 0, 0);
    assert(ui.cursor == 18 && !TSEL->step[19].n);
    TSEL->seq_idx = 19;
    TSEL->seq_pos = 1;
    key_frame(0, 0);
    assert(ui.cursor == 19 && ui.bank == 1);
    midi_frame(0x90, 30, 100, 0);
    midi_frame(0x80, 30, 0, 0);
    assert(TSEL->step[19].n == 1 && TSEL->step[19].note[0] == midi_map(TSEL, 30) && ui.cursor == 19);

    reset(32);
    song.playing = 1;                        /* unarmed STEP entry still uses its manual cursor */
    TSEL->seq_idx = 18;
    cursor_set(5);
    midi_frame(0x90, 60, 100, 0);
    midi_frame(0x80, 60, 0, 0);
    assert(TSEL->step[5].n == 1 && !TSEL->step[18].n && ui.cursor == 6);

    reset(16);
    track_select(TRK_DRUM);
    song.rec = 1u << TRK_DRUM;
    transport_req = 1;
    hits = drums.age;
    midi_frame(0x99, 39, 100, 0);          /* Start and the first note share an audio block */
    assert(song.playing && TSEL->seq_idx == 0 && TSEL->step[0].note[0] == 39);
    assert(drums.age == hits + 1u);
    puts("armed playback: MIDI records on the playing step, selection follows across banks, unarmed entry stays manual");
}

static void preset_scale_settings_test(void)
{
    uint32_t i, scl_page = NPAGES;
    project_t *saved;
    reset(16);
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].graph == GR_SCALE)
            scl_page = i;
    assert(scl_page < NPAGES);
    ui.page = (uint8_t)scl_page;
    ui.home = 0;
    scale_setting_set(TDRUM, P_SCALE, 9);
    scale_setting_set(TDRUM, P_QUANT, Q_ALL);
    edit_param(1, 2);                         /* panel SCL */
    edit_param(2, 2);                         /* panel QNT -> WHITE */
    for (i = 0; i < NPART; i++)
        assert(trk[i].p[P_SCALE] == 2 && trk[i].p[P_QUANT] == Q_WHITE);
    assert(TDRUM->p[P_SCALE] == 9 && TDRUM->p[P_QUANT] == Q_ALL);

    trk[0].p[P_ROOT] = 5;                    /* ROOT remains part-specific */
    trk[1].p[P_ROOT] = 7;
    track_select(1);
    apply_preset(1);
    set_engine(2);
    apply_preset(0);
    assert(trk[1].p[P_SCALE] == 2 && trk[1].p[P_QUANT] == Q_WHITE);
    assert(trk[0].p[P_ROOT] == 5 && trk[1].p[P_ROOT] == TP[P_ROOT].def);

    assert(up_store(0, "SCALE TEST") == 3); /* RAM-only user preset */
    track_select(2);
    scale_setting_set(TSEL, P_SCALE, 3);
    scale_setting_set(TSEL, P_QUANT, Q_ALL);
    track_select(1);
    assert(up_load(0) == 0);                 /* stored 2/WHITE cannot replace 3/ALL */
    apply_preset(0);
    for (i = 0; i < NPART; i++)
        assert(trk[i].p[P_SCALE] == 3 && trk[i].p[P_QUANT] == Q_ALL);
    assert(TDRUM->p[P_SCALE] == 9 && TDRUM->p[P_QUANT] == Q_ALL);

    project_save(0);
    scale_setting_set(TSEL, P_SCALE, 1);
    scale_setting_set(TSEL, P_QUANT, Q_OFF);
    project_load(0);
    for (i = 0; i < NPART; i++)
        assert(trk[i].p[P_SCALE] == 3 && trk[i].p[P_QUANT] == Q_ALL);
    assert(TDRUM->p[P_SCALE] == 9 && TDRUM->p[P_QUANT] == Q_ALL);

    saved = &proj_buf;                       /* (RAM only: the last save) older values per track */
    saved->sel = 2;
    saved->t[0].p[P_SCALE] = 1;
    saved->t[0].p[P_QUANT] = Q_SNAP;
    saved->t[2].p[P_SCALE] = 4;
    saved->t[2].p[P_QUANT] = Q_WHITE;
    saved->sum = proj_sum(saved);
    project_load(0);                        /* selected synth part supplies the shared value */
    for (i = 0; i < NPART; i++)
        assert(trk[i].p[P_SCALE] == 4 && trk[i].p[P_QUANT] == Q_WHITE);
    assert(TDRUM->p[P_SCALE] == 9 && TDRUM->p[P_QUANT] == Q_ALL);

    saved->sel = TRK_DRUM;
    saved->sum = proj_sum(saved);
    project_load(0);                        /* if drums were selected, part 1 supplies it */
    for (i = 0; i < NPART; i++)
        assert(trk[i].p[P_SCALE] == 1 && trk[i].p[P_QUANT] == Q_SNAP);
    assert(TDRUM->p[P_SCALE] == 9 && TDRUM->p[P_QUANT] == Q_ALL);
    track_select(0);
    scale_setting_set(TSEL, P_QUANT, Q_MPC);
    project_save(1);
    scale_setting_set(TSEL, P_QUANT, Q_OFF);
    project_load(1);
    for (i = 0; i < NPART; i++)
        assert(trk[i].p[P_QUANT] == Q_MPC);
    puts("scale settings: panel, all synth tracks, factory/user presets, projects and drum independence ok");
}

static void mpc_page_test(void)
{
    uint32_t mode, i, sig;
    char b[24];
    reset(16);
    transport_req = panic_req = 0;
    for (mode = Q_OFF; mode < Q_MPC; mode++) {
        scale_setting_set(TSEL, P_QUANT, (int16_t)mode);
        page_go(page_first(FAM_SCL));
        open_family(FAM_SCL);
        assert(cur_page()->graph == GR_CHORD);
        open_family(FAM_SCL);              /* MPC hidden: back to SCL */
        assert(cur_page()->graph == GR_SCALE);
    }
    scale_setting_set(TSEL, P_SCALE, 1);
    scale_setting_set(TSEL, P_QUANT, Q_MPC);
    page_scroll(1);
    assert(cur_page()->graph == GR_CHORD);
    page_scroll(1);
    assert(cur_page()->graph == GR_MPC && page_shown(cur_page()));
    assert(TSEL->p[P_MPCDEG] == 1);
    encoders[panel.enc[EN_K1]] = 2;
    ui_input();
    for (i = 0; i < NPART; i++) assert(trk[i].p[P_MPCDEG] == 3);
    assert(TDRUM->p[P_MPCDEG] == 1);
    assert(midi_map(TSEL, 20) == 62 && midi_map(TSEL, 21) == 64 && midi_map(TSEL, 22) == 65);
    assert(kb_map(TSEL, 7) == 60);          /* degree shifts MPC pads, not panel keys */
    mpc_pad_text(TSEL, b);
    assert(!strcmp(b, "H02: 3 -> E4"));
    ui_draw();
    sig = graph_signature();
    song.octave = 1;
    assert(graph_signature() != sig);
    mpc_pad_text(TSEL, b);
    assert(!strcmp(b, "H02: 3 -> E5"));
    ui_draw();
    song.octave = 0;
    edit_param(0, 99);
    assert(TSEL->p[P_MPCDEG] == 7);
    scale_setting_set(TSEL, P_SCALE, 5);    /* pentatonic clamps degree 7 to 5 */
    for (i = 0; i < NPART; i++) assert(trk[i].p[P_MPCDEG] == 5);
    edit_param(0, 1);
    assert(TSEL->p[P_MPCDEG] == 5);
    edit_param(0, -99);
    assert(TSEL->p[P_MPCDEG] == 1);
    scale_setting_set(TSEL, P_SCALE, 0);
    edit_param(0, 99);
    assert(TSEL->p[P_MPCDEG] == 12);
    TSEL->p[P_ROOT] = 11;
    TSEL->p[P_TRANS] = 24;
    song.octave = 3;
    mpc_pad_text(TSEL, b);
    assert(!strcmp(b, "H02: 12 -> SILENT") && text_w(&FONT_S, b) <= 232);
    ui_draw();
    TSEL->p[P_ROOT] = TSEL->p[P_TRANS] = song.octave = 0;

    scale_setting_set(TSEL, P_SCALE, 1);
    scale_setting_set(TSEL, P_MPCDEG, 3);
    assert(up_store(1, "MPC DEGREE") == 3);
    scale_setting_set(TSEL, P_MPCDEG, 5);
    apply_preset(0);
    set_engine(1);
    assert(up_load(1) == 0 && TSEL->p[P_MPCDEG] == 5);
    project_save(2);
    scale_setting_set(TSEL, P_MPCDEG, 1);
    project_load(2);
    for (i = 0; i < NPART; i++) assert(trk[i].p[P_MPCDEG] == 5);
    transport_req = panic_req = 0;
    track_defaults_steps(TSEL);
    TSEL->p[P_ROOT] = 0;
    TSEL->p[P_TRANS] = 0;
    for (i = page_first(FAM_SEQ); PAGES[i].scope != SC_STEP; i++)
        ;
    ui.page = (uint8_t)i;
    page_entered();
    midi_frame(0x90, 21, 100, 1);
    assert(TSEL->step[0].n == 1 && TSEL->step[0].note[0] == 67);
    midi_frame(0x80, 21, 0, 0);
    assert(ui.cursor == 2 && !step_midi_held);
    track_defaults_steps(TSEL);
    song.playing = song.rec = 1;
    TSEL->seq_idx = TSEL->seq_pos = 0;
    midi_frame(0x90, 21, 100, 0);
    assert(TSEL->step[0].n == 1 && TSEL->step[0].note[0] == 67);
    midi_frame(0x80, 21, 0, 0);
    song.playing = song.rec = 0;

    open_family(FAM_SCL);                  /* entering a module starts at its main page */
    assert(cur_page()->graph == GR_SCALE);
    open_family(FAM_SCL);
    assert(cur_page()->graph == GR_CHORD);
    open_family(FAM_SCL);
    assert(cur_page()->graph == GR_MPC);
    scale_setting_set(TSEL, P_QUANT, Q_WHITE);
    ui_input();                            /* mode changed externally: hidden page falls back */
    assert(cur_page()->graph == GR_SCALE);
    scale_setting_set(TSEL, P_QUANT, Q_MPC);
    page_scroll(1);
    assert(cur_page()->graph == GR_CHORD);
    page_scroll(1);
    assert(cur_page()->graph == GR_MPC);
    track_select(TRK_DRUM);
    ui_input();
    assert(cur_page()->graph == GR_SCALE);
    puts("MPC page: conditional navigation, DEG knob/range, note preview, shared persistence, preset retention and sequencer input ok");
}

static void playback_test(void)
{
    uint32_t i, j, age[NVOICE];
    reset(16);
    note(0);
    TSEL->p[P_VOICE] = V_POLY;
    assert(step_note_resize(TSEL, 0, 4) == 4);
    TSEL->seq_idx = 0;
    seq_step(TSEL, &TSEL->step[0], 1000, 0);
    assert(TSEL->seq_n == 3 && TSEL->seq_hold);
    for (j = 0; j < NVOICE; j++) age[j] = TSEL->v[j].age;
    for (i = 1; i < 4; i++) {
        TSEL->seq_idx = (uint16_t)i;
        seq_step(TSEL, &TSEL->step[i], 1000, 0);
        assert(TSEL->seq_n == 3);
        for (j = 0; j < NVOICE; j++) assert(TSEL->v[j].age == age[j]);
    }
    TSEL->seq_idx = 4;
    seq_step(TSEL, &TSEL->step[4], 1000, 0);
    assert(TSEL->seq_n == 0);
    puts("playback: generated ties sustain the chord without retriggering and the following rest releases it");
}

static int last_has(uint32_t n) { return (live_last[n >> 5] >> (n & 31u)) & 1u; }
static int held_any(void) { return (live_held[0] | live_held[1] | live_held[2] | live_held[3]) != 0; }
static int area_has(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint16_t c)
{
    uint32_t x, y;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            if (screen[y * 240 + x] == c)
                return 1;
    return 0;
}
static const char *chord_name(const uint8_t *n, uint32_t cnt, char *b)
{
    uint32_t i, pcs = 0, root;
    const char *q;
    for (i = 0; i < cnt; i++) pcs |= 1u << (n[i] % 12u);
    q = chord_of(pcs, n[0] % 12u, &root);
    if (!q) return "-";
    str_cpy(b, N_NOTE[root], 8);
    str_cpy(b + str_len(b), q, 8);
    if (root != n[0] % 12u) {
        str_cpy(b + str_len(b), "/", 2);
        str_cpy(b + str_len(b), N_NOTE[n[0] % 12u], 4);
    }
    return b;
}

static void write_ppm(const char *path)
{
    uint32_t i;
    FILE *f = fopen(path, "wb");
    assert(f);
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = screen[i];
        uint8_t rgb[3] = {(uint8_t)((c >> 11) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static void home_notes_test(const char *path)
{
    static const struct { uint8_t n[5], cnt; const char *name; } CASES[] = {
        {{60, 64, 67}, 3, "C"}, {{64, 67, 72}, 3, "C/E"}, {{57, 60, 64}, 3, "Am"}, {{60, 63, 67, 70}, 4, "Cm7"},
        {{60, 64, 67, 69}, 4, "C6"}, {{57, 60, 64, 67}, 4, "Am7"}, {{59, 62, 65}, 3, "Bdim"}, {{48, 60, 64, 67, 71}, 5, "Cmaj7"},
        {{60, 62, 67}, 3, "Csus2"}, {{55, 59, 62, 65}, 4, "G7"}, {{60, 67}, 2, "-"}, {{60, 61, 62}, 3, "-"},
        {{60, 62, 64, 67, 69}, 5, "C6/9"}, {{48, 58, 62, 64, 69}, 5, "C13"}, {{52, 56, 62, 67}, 4, "E7#9"},
        {{60, 64, 66, 71}, 4, "Cmaj7#11"}, {{57, 60, 62, 64, 67}, 5, "Am11"}, {{55, 58, 61, 64}, 4, "Gdim7"},
        {{60, 62, 65, 67, 70}, 5, "C9sus4"}, {{48, 58, 62, 65}, 4, "A#add9/C"}, {{60, 63, 66, 71}, 4, "CdimM7"},
        {{49, 53, 56, 59, 62}, 5, "C#7b9"}, {{64, 67, 72, 74}, 4, "Cadd9/E"}, {{60, 64, 69, 71, 74}, 5, "Cmaj13"},
        {{50, 60, 63, 67, 70}, 5, "Cm9/D"}, {{55, 60, 65}, 3, "Csus4/G"}, {{50, 55, 60}, 3, "Gsus4/D"},   /* off the bass: the simplest */
    };
    char b[16];
    uint32_t i, k, r;
    for (i = 0; i < sizeof CASES / sizeof CASES[0]; i++)
        assert(!strcmp(chord_name(CASES[i].n, CASES[i].cnt, b), CASES[i].name));
    for (i = 0; i < sizeof CHORDS / sizeof CHORDS[0]; i++) {   /* every shape on every root, root position */
        assert(CHORDS[i].iv & 1u);
        for (k = 0; k < i; k++) assert(CHORDS[k].iv != CHORDS[i].iv);
        for (r = 0; r < 12u; r++) {
            uint32_t pcs = ((CHORDS[i].iv << r) | (CHORDS[i].iv >> (12u - r))) & 0xFFFu, root = 99;
            assert(chord_of(pcs, r, &root) == CHORDS[i].q && root == r);
        }
    }

    reset(16);
    memset(live_held, 0, sizeof live_held);
    memset(live_last, 0, sizeof live_last);
    transport_req = panic_req = 0;
    TSEL->p[P_VOICE] = V_POLY;
    go_home();
    ui_draw();
    assert(!area_has(4, Y_GRAPH, 236, Y_GRAPH + 30, C_WHITE));   /* nothing played yet: no readout */

    /* MIDI translated by the scale: WHITE minor turns C E G into C Eb G */
    scale_setting_set(TSEL, P_SCALE, 2);
    scale_setting_set(TSEL, P_QUANT, Q_WHITE);
    midi_frame(0x90, 60, 100, 0);
    midi_frame(0x90, 64, 100, 0);
    midi_frame(0x90, 67, 100, 0);
    assert(last_has(60) && last_has(63) && last_has(67) && !last_has(64) && held_any());
    ui.force = 1;
    ui_draw();
    assert(area_has(4, Y_GRAPH, 40, Y_GRAPH + 30, C_WHITE));      /* held: white */
    if (path)
        write_ppm(path);
    midi_frame(0x80, 60, 0, 0);
    midi_frame(0x80, 64, 0, 0);
    midi_frame(0x80, 67, 0, 0);
    assert(!held_any() && last_has(60) && last_has(63) && last_has(67));   /* released: kept */
    ui.frame += 2;
    ui_draw();
    assert(!area_has(4, Y_GRAPH, 40, Y_GRAPH + 30, C_WHITE) && area_has(4, Y_GRAPH, 40, Y_GRAPH + 30, C_HI));

    /* a tap faster than a frame still shows; the drum channel leaves it alone */
    midi_frame(0x90, 62, 100, 0);
    midi_frame(0x80, 62, 0, 0);
    assert(last_has(62) && !last_has(60) && !held_any());
    midi_frame(0x99, 36, 100, 0);
    midi_frame(0x89, 36, 0, 0);
    assert(last_has(62) && !last_has(36));

    /* legato: the notes held at the last note-on, not every note since the first */
    scale_setting_set(TSEL, P_QUANT, Q_OFF);
    midi_frame(0x90, 60, 100, 0);
    midi_frame(0x90, 62, 100, 0);
    midi_frame(0x80, 60, 0, 0);
    midi_frame(0x90, 64, 100, 0);
    assert(last_has(62) && last_has(64) && !last_has(60));
    midi_silence_track(0);                  /* panic: nothing held, the last notes stay */
    assert(!held_any() && last_has(64));

    /* the panel keys, through the octave */
    song.octave = 1;
    fm1_in.notes = 1u << 7;
    events_block(0);
    assert(last_has(72) && held_any());
    fm1_in.notes = 0;
    events_block(0);
    assert(!held_any() && last_has(72));
    song.octave = 0;
    puts("HOME notes: chord names, MIDI + keys after the scale, kept after release, taps, drums, panic");
}

static uint32_t page_named(const char *title)
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (!strcmp(PAGES[i].title, title))
            return i;
    assert(0);
    return 0;
}

static void edit_frame(int down, uint32_t keys)       /* one frame: EDIT held or not, these keys down */
{
    uint32_t bit = 1u << panel.btn[B_EDIT];
    if (down && !(fm1_in.buttons & bit))
        button_edges |= bit;
    fm1_in.buttons = down ? fm1_in.buttons | bit : fm1_in.buttons & ~bit;
    key_frame(keys, 0);
}

static void fm6_page_nav_test(void)
{
    static int16_t ed[FM6_NP];
    uint32_t e, k, fm6 = NENGINES, analog = 0;
    reset(16);
    for (e = 0; e < NENGINES; e++)
        if (ENGINES[e] == &ENG_FM6)
            fm6 = e;
    assert(fm6 < NENGINES);
    TSEL->eng_req = (uint8_t)fm6;
    TSEL->p[P_E0] = 0;
    fm6_load(song.sel, 0);
    for (k = 0; k < FM6_NUSER; k++) {               /* the bank as fm6_bank_init leaves it */
        fm6_from_rom(ed, &FM6_INIT);
        fm6_pack(fm6_bank[k], ed);
    }

    /* EDIT from another page opens the family on press; its release adds nothing */
    go_home();
    edit_frame(1, 0);
    assert(ui.page == page_named("EDIT 1"));
    edit_frame(0, 0);
    assert(ui.page == page_named("EDIT 1"));
    /* a tap on an EDIT page: the next page on release; FM6's EDIT 2 (ENGINE), then STORE */
    edit_frame(1, 0);
    assert(ui.page == page_named("EDIT 1"));
    edit_frame(0, 0);
    assert(ui.page == page_named("EDIT 2"));
    edit_frame(1, 0);
    edit_frame(0, 0);
    assert(ui.page == page_named("STORE"));
    assert(fm6_slot == 0);                          /* a factory voice: the first INIT VOICE slot */

    /* EDIT held: the black keys jump, a pair alternates, the white keys pick the operator */
    edit_frame(1, 0);
    edit_frame(1, 1u << 13);                        /* F#4: EG RATE / EG LVL */
    assert(ui.page == page_named("EG RATE"));
    edit_frame(1, 0);
    edit_frame(1, 1u << 13);
    assert(ui.page == page_named("EG LVL"));
    edit_frame(1, 0);
    edit_frame(1, 1u << 13);
    assert(ui.page == page_named("EG RATE"));
    edit_frame(1, 1u << 13 | 1u << 4);              /* A3: OP3 */
    assert(fm6_opsel == 2);
    edit_frame(1, 1u << 5);                         /* A#3: ALGO */
    assert(ui.page == page_named("ALGO"));
    edit_frame(1, 1u << 25);                        /* F#5: the DX7 functions, FM BEND / FM PORTA */
    assert(ui.page == page_named("FM BEND"));
    edit_frame(1, 0);
    edit_frame(1, 1u << 25);
    assert(ui.page == page_named("FM PORTA"));
    edit_frame(1, 0);
    edit_frame(1, 1u << 5);                         /* A#3: ALGO */
    assert(ui.page == page_named("ALGO"));
    /* the keys stay silent while EDIT is held; the release after a jump is no tap */
    kb_prev = 0;
    fm1_in.notes = 1u << 7;
    events_block(0);
    assert(kb_note[7] == KB_SILENT && !held_any());
    fm1_in.notes = 0;
    events_block(0);
    edit_frame(0, 0);
    assert(ui.page == page_named("ALGO"));
    fm1_in.notes = 1u << 7;                         /* EDIT let go: the keys play again */
    events_block(0);
    assert(kb_note[7] == 60 && held_any());
    fm1_in.notes = 0;
    events_block(0);
    assert(!held_any());

    /* STORE offers the user slot the part plays while the buffer is that voice, else a free one */
    fm6_from_rom(ed, &FM6_ROM[3]);
    fm6_pack(fm6_bank[0], ed);
    TSEL->p[P_E0] = (int16_t)FM6_NROM;
    fm6_load(song.sel, FM6_NROM);
    fm6_slot = 9;
    edit_frame(1, 0);
    edit_frame(1, 1u << 3);                         /* G#3: STORE */
    assert(ui.page == page_named("STORE") && fm6_slot == 0);
    fm6_from_rom(fm6_ed[song.sel], &FM6_ROM[5]);    /* a voice from SysEx over U01 */
    edit_frame(1, 1u << 5);
    edit_frame(1, 1u << 3);
    assert(ui.page == page_named("STORE") && fm6_slot == 1);
    edit_frame(0, 0);

    /* another engine: only its pages light up, the FM6 keys do nothing */
    TSEL->eng_req = (uint8_t)analog;
    page_fix();
    edit_frame(1, 0);
    edit_frame(1, 1u << 5);                         /* ALGO: FM6 only */
    assert(cur_page()->fam == FAM_EDIT && ui.page != page_named("ALGO"));
    edit_frame(1, 1u << 22);                        /* D#5: VOICE / VOICE 2 */
    assert(ui.page == page_named("VOICE"));
    edit_frame(1, 1u << 1);                         /* F#3: EDIT 1 / EDIT 2 */
    assert(ui.page == page_named("EDIT 1"));
    edit_frame(1, 0);
    edit_frame(1, 1u << 1);
    assert(ui.page == page_named("EDIT 2"));
    edit_frame(0, 0);
    TSEL->p[P_E0] = 0;
    puts("FM6 pages: EDIT + black keys jump, white keys pick the operator, silent keys, STORE slot");
}

static void render_test(const char *path)
{
    uint32_t i, j;
    reset(32);
    note(14);
    TSEL->step[14].note[2] = 72;             /* highest note must stay below the hint strip */
    step_note_resize(TSEL, 14, 6);
    cursor_set(17);
    ui_draw();
    /* The second bank starts mid-chord: all three notes continue visibly. */
    for (j = 0; j < 3; j++) {
        int y = Y_GRAPH + 24 + 80 - (TSEL->step[14].note[j] - 60) * 74 / 12;
        assert(screen[y * 240] == C_GRAY && screen[y * 240 + 14] == C_GRAY);
    }
    assert(text_w(&FONT_S, "HOLD + SELECT: 64 STP") <= 232);
    assert(text_w(&FONT_S, "ENV + SELECT: 64 STP") <= 232);
    assert(text_w(&FONT_S, "SCALE + SELECT: MOVE") <= 232);
    if (path) {
        FILE *f = fopen(path, "wb");
        assert(f);
        fprintf(f, "P6\n240 240\n255\n");
        for (i = 0; i < 240u * 240u; i++) {
            uint16_t c = screen[i];
            uint8_t rgb[3] = {(uint8_t)((c >> 11) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31)};
            fwrite(rgb, 1, 3, f);
        }
        fclose(f);
    }
    puts("display: sustained chords cross bank boundaries and the length hint fits");
}

static void seq_frame(int down, uint32_t keys)        /* one frame: SEQ held or not, these keys down */
{
    uint32_t bit = 1u << panel.btn[B_SEQ];
    if (down && !(fm1_in.buttons & bit))
        button_edges |= bit;
    fm1_in.buttons = down ? fm1_in.buttons | bit : fm1_in.buttons & ~bit;
    key_frame(keys, 0);
}

static void pat_pick(uint32_t key)                    /* SEQ held, key k (0 = F3) tapped, SEQ let go */
{
    seq_frame(1, 0);
    seq_frame(1, 1u << key);
    seq_frame(1, 0);
    seq_frame(0, 0);
}

static void patterns_test(void)
{
    track_t *t;
    uint32_t n, last = 0;
    reset(16);
    t = TSEL;
    note(0);
    assert(ui.page == page_named("STEP") && t->pat == 0 && pat_used(t, 0) && !pat_used(t, 1));

    /* SEQ held on a SEQ page: a white key picks its pattern on release; stopped, it comes at once
     * (a never played one keeps the track's LEN); the keys stay silent, the page stays */
    seq_frame(1, 0);
    seq_frame(1, 1u << 2);                          /* G3: pattern 2 */
    assert(t->pat_q == 0 && !t->step[1].n);         /* (no step entry) */
    kb_prev = 0;
    events_block(0);
    assert(kb_note[2] == KB_SILENT && !held_any());
    seq_frame(1, 0);
    assert(t->pat_q == 2);
    events_block(0);
    assert(t->pat == 1 && !t->pat_q && !t->step[0].n && t->p[P_SLEN] == 16);
    seq_frame(0, 0);
    assert(ui.page == page_named("STEP"));          /* a key was used: no page turn */
    t->p[P_SLEN] = 8;                               /* LEN etc. go with the pattern */
    note(3);
    pat_pick(0);
    events_block(0);
    assert(t->pat == 0 && t->p[P_SLEN] == 16 && t->step[0].n == 3 && !t->step[3].n);
    assert(pat_bank[song.sel][1].set[0] == 8 && pat_bank[song.sel][1].step[3].n == 3);

    /* hold one, press another: copied there, nothing picked */
    seq_frame(1, 0);
    seq_frame(1, 1u << 0);
    seq_frame(1, 1u << 0 | 1u << 4);                /* A3: pattern 3 */
    seq_frame(1, 0);
    seq_frame(0, 0);
    assert(t->pat == 0 && !t->pat_q && pat_used(t, 2) && pat_bank[song.sel][2].set[0] == 16 &&
           !memcmp(pat_bank[song.sel][2].step, t->step, sizeof t->step));

    /* playing: the switch waits for the end of the track's loop */
    transport_req = 1;
    events_block(0);
    pat_pick(2);                                    /* pattern 2 */
    assert(t->pat_q == 2 && t->pat == 0);
    for (n = 0; n < 2000u && t->pat == 0; n++) {
        last = t->seq_idx;
        events_block(256);
    }
    assert(t->pat == 1 && !t->pat_q && last == 15 && t->seq_idx == 0 && t->p[P_SLEN] == 8 && t->step[3].n == 3);
    pat_pick(0);                                    /* F3: pattern 1 queued */
    assert(t->pat_q == 1);
    pat_pick(2);                                    /* G3: the one playing, the queue is dropped */
    assert(!t->pat_q && t->pat == 1);
    transport_req = 2;
    events_block(0);

    /* a tap of SEQ on a SEQ page still turns the page: STEP, TEMPO, LOOP */
    seq_frame(1, 0);
    seq_frame(0, 0);
    assert(ui.page == page_named("TEMPO"));
    seq_frame(1, 0);
    seq_frame(0, 0);
    assert(ui.page == page_named("PATTERN"));

    /* projects keep every pattern, its LEN and the one playing */
    project_save(3);
    track_defaults_steps(t);
    pat_clear_bank(t);
    t->p[P_SLEN] = 16;
    project_load(3);
    assert(t->pat == 1 && t->p[P_SLEN] == 8 && t->step[3].n == 3 && !t->step[0].n);
    assert(pat_used(t, 0) && pat_used(t, 2) && !pat_used(t, 3) && pat_bank[song.sel][0].set[0] == 16 &&
           pat_bank[song.sel][0].step[0].n == 3);
    transport_req = panic_req = 0;
    puts("patterns: SEQ + keys pick (at once stopped, at the loop end playing), copy, LEN per pattern, projects");
}

static void workspace_test(void)
{
    uint32_t rec_bit;
    reset(16);
    go_home();
    assert(ui.home && !ui.home_view);
    trk[0].p[P_LEVEL] = 40;
    encoders[panel.enc[EN_K1]] = 1;
    ui_input();
    assert(trk[0].p[P_LEVEL] > 40);
    home_tap();
    assert(ui.home && ui.home_view == 1u);
    home_tap();
    assert(ui.home && ui.home_view == 2u);
    trk[0].p[P_PAN] = 0;
    encoders[panel.enc[EN_K1]] = 1;
    ui_input();
    assert(trk[0].p[P_PAN] > 0);
    home_tap();
    assert(ui.home && ui.home_view == 3u);
    song.g[G_DFDBK] = 40;
    encoders[panel.enc[EN_K2]] = 1;
    ui_input();
    assert(song.g[G_DFDBK] > 40);
    rec_bit = 1u << panel.btn[B_REC];
    fm1_in.buttons |= rec_bit;
    ui_input();
    fm1_ms += 20;
    fm1_in.buttons &= ~rec_bit;
    ui_input();
    assert((song.rec & 1u) && transport_req == 1u && ui.home_view);
    ui_draw();
    open_family(FAM_ENV);
    assert(cur_page()->graph == GR_ADSR && !preset_pages());
    encoders[panel.enc[EN_PRESET]] = 1;
    ui_input();
    assert(TSEL->preset == 1 && cur_page()->graph == GR_ADSR);
    open_family(FAM_ENV);
    assert(str_eq(cur_page()->title, "ENV DEST"));
    open_family(FAM_ENV);
    assert(cur_page()->graph == GR_ADSR);
    open_family(FAM_SEQ);
    assert(cur_page()->graph == GR_STEPS);
    encoders[panel.enc[EN_PRESET]] = 1;
    ui_input();
    assert(TSEL->pat_q == 2);
    encoders[panel.enc[EN_PRESET]] = -1;
    ui_input();
    assert(!TSEL->pat_q);
    open_family(FAM_SEQ);
    assert(cur_page()->scope == SC_STEP && !preset_pages());
    open_family(FAM_SAVE);
    assert(cur_page()->graph == GR_BROWSE);
    open_family(FAM_SAVE);
    assert(cur_page()->graph == GR_USER);
    open_family(FAM_SAVE);
    assert(cur_page()->graph == GR_SLOTS);
    home_tap();
    assert(ui.home && !ui.home_view);
    puts("workspaces: NOTES/MIX LEVEL/PAN/FX, direct recording, instrument sounds, LOOP patterns and STEP");
}

/* SELECT: the open workspace's pages both ways (HOME: its views), on STEP the cursor; it no longer
 * sets the tempo. SEQ > TEMPO holds the project's BPM and SWG, and a project brings them back */
static void select_turn(int32_t s)
{
    fm1_ms += 100;                                  /* (slow turns: one detent each) */
    encoders[panel.enc[EN_SELECT]] = s;
    ui_input();
}

static void select_knob_test(void)
{
    int16_t bpm;
    reset(16);
    go_home();
    bpm = song.g[G_BPM];
    select_turn(1);
    assert(ui.home && ui.home_view == 1u && song.g[G_BPM] == bpm);
    select_turn(-1);
    select_turn(-1);
    assert(ui.home && ui.home_view == 3u);
    open_family(FAM_ENV);
    select_turn(1);
    assert(str_eq(cur_page()->title, "ENV DEST"));
    select_turn(-1);
    assert(cur_page()->graph == GR_ADSR);
    select_turn(-1);                                /* back past the first page: the last */
    assert(str_eq(cur_page()->title, "ENV DEST"));
    open_family(FAM_GLO);
    assert(cur_page()->id[0] == G_CLOCK && cur_page()->id[1] == G_TUNE);   /* GLOBAL: the studio's */

    open_family(FAM_SEQ);                           /* LOOP; to its left, TEMPO */
    assert(cur_page()->graph == GR_STEPS);
    select_turn(-1);
    assert(ui.page == page_named("TEMPO") && song.seq_mode);
    fm1_ms += 100;
    encoders[panel.enc[EN_K1]] = 1;
    encoders[panel.enc[EN_K2]] = 1;
    ui_input();
    assert(song.g[G_BPM] == bpm + 1 && song.g[G_SWING] == 1);
    encoders[panel.enc[EN_PRESET]] = 1;             /* PRESETS still queues the next pattern */
    ui_input();
    assert(TSEL->pat_q == 2);
    TSEL->pat_q = 0;
    select_turn(-1);
    assert(ui.page == page_named("STEP"));
    cursor_set(0);
    select_turn(1);                                 /* STEP: the cursor, not the page */
    assert(ui.page == page_named("STEP") && ui.cursor == 1);
    select_turn(-1);
    select_turn(-1);
    assert(ui.cursor == 15);                        /* wraps inside LEN */
    encoders[panel.enc[EN_SELECT]] = 1;             /* turned fast: three steps a detent */
    ui_input();
    assert(ui.cursor == 2);

    project_save(1);
    song.g[G_BPM] = 90;
    song.g[G_SWING] = 0;
    project_load(1);
    assert(song.g[G_BPM] == bpm + 1 && song.g[G_SWING] == 1);
    transport_req = panic_req = 0;
    puts("SELECT: pages both ways, HOME views, the STEP cursor; SEQ > TEMPO: the project's BPM and SWG");
}

static void performance_gestures_test(void)
{
    uint32_t rec = 1u << panel.btn[B_REC], play = 1u << panel.btn[B_PLAY];
    uint32_t fx = 1u << panel.btn[B_FX];
    reset(16);
    go_home();
    button_edges |= rec | play;
    fm1_in.buttons |= rec | play;
    ui_input();
    assert((song.rec & 1u) && transport_req == 1u);
    fm1_in.buttons &= ~(rec | play);
    ui_input();
    assert(song.rec & 1u);                         /* releasing REC does not disarm it */

    reset(16);
    go_home();
    edit_frame(1, 1u << SOUND_KEY[1]);
    assert(TSEL->preset == 1 && cur_page()->fam == FAM_EDIT);
    edit_frame(0, 0);

    reset(16);
    go_home();
    open_family(FAM_SEQ);
    open_family(FAM_SEQ);
    note(0);
    cursor_set(0);
    button_edges |= fx;
    fm1_in.buttons |= fx;
    ui_input();
    assert(!TSEL->step[0].n && ui.cursor == 1 && cur_page()->scope == SC_STEP);
    fm1_in.buttons &= ~fx;
    ui_input();
    puts("performance: REC+PLAY punch-in, EDIT sound keys, FX step clear");
}

static void step_delete_test(void)
{
    static const struct { uint32_t len, start, duration, offset; } cases[] = {
        {16, 0, 4, 0},                           /* chord and ties, next note adjacent */
        {64, 15, 5, 0},                          /* crosses a display bank */
        {16, 14, 4, 0},                          /* crosses the pattern boundary */
        {16, 14, 6, 2},                          /* delete within a tie chain */
        {16, 4, 16, 0},                          /* one note fills the whole loop */
        {1, 0, 1, 0},                            /* single-step pattern */
    };
    uint32_t c, i, fx = 1u << panel.btn[B_FX];
    step_t before[NSTEP];
    for (c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        uint32_t len = cases[c].len, start = cases[c].start;
        uint32_t duration = cases[c].duration, offset = cases[c].offset;
        uint32_t at = (start + offset) % len;
        reset(len);
        note(start);
        assert(step_note_resize(TSEL, start, (int32_t)duration) == duration);
        if (duration < len)
            note((start + duration) % len);
        memcpy(before, TSEL->step, sizeof before);
        cursor_set((int32_t)at);
        button_edges |= fx;
        fm1_in.buttons |= fx;
        ui_input();
        assert(ui.cursor == (start + 1u) % len && cur_page()->scope == SC_STEP);
        assert(!strcmp(ui.msg, "NOTE CLEARED"));
        for (i = 0; i < NSTEP; i++) {
            uint32_t distance = (i + len - start) % len;
            step_t *st = &TSEL->step[i];
            if (i < len && distance < duration)
                assert(st->time == ST_REST && !st->n && !st->flags && !st->vel);
            else
                assert(!memcmp(st, &before[i], sizeof *st));
        }
        fm1_in.buttons &= ~fx;
        ui_input();
    }

    reset(16);
    note(0);
    assert(step_note_resize(TSEL, 0, 3) == 3);
    note(7);
    assert(step_note_resize(TSEL, 7, 3) == 3);
    memcpy(before, TSEL->step, sizeof before);
    cursor_set(0);
    button_edges |= fx;
    fm1_in.buttons |= fx;
    ui_input();
    for (i = 0; i < 3; i++)
        assert(TSEL->step[i].time == ST_REST && !TSEL->step[i].n);
    for (i = 3; i < NSTEP; i++)
        assert(!memcmp(&TSEL->step[i], &before[i], sizeof before[i]));
    fm1_in.buttons &= ~fx;
    ui_input();
    puts("STEP delete: whole note from onset or tie, bank/loop wrap, full loop, neighboring notes/rests preserved");
}

static void step_moves_test(void)
{
    uint32_t len, n, at, i;
    int32_t dir;
    step_t saved[NSTEP];
    for (len = 1; len <= NSTEP; len++)
        for (n = 1; n <= len; n++)
            for (dir = -1; dir <= 1; dir += 2) {
                reset(len);
                at = len - 1u;
                note(at);
                assert(step_note_resize(TSEL, at, (int32_t)n) == n);
                for (i = 0; i < n; i++) {
                    TSEL->step[(at + i) % len].flags |= SF_SLIDE;
                    saved[i] = TSEL->step[(at + i) % len];
                }
                for (i = 0; i < len; i++) {
                    at = step_note_move(TSEL, at, dir);
                    assert(step_note_length(TSEL, at) == n);
                    for (uint32_t j = 0; j < n; j++)
                        assert(!memcmp(&saved[j], &TSEL->step[(at + j) % len], sizeof saved[j]));
                    for (uint32_t j = n; j < len; j++)
                        assert(TSEL->step[(at + j) % len].time == ST_REST);
                }
                assert(at == len - 1u);
            }
    reset(16);
    note(2); note(8);
    step_note_resize(TSEL, 2, 3);
    step_note_resize(TSEL, 8, 2);
    memcpy(saved, TSEL->step, sizeof saved);
    assert(step_note_move(TSEL, 2, 12) == 5);     /* fast turn stops at the next note */
    assert(!memcmp(&saved[8], &TSEL->step[8], sizeof saved[8]) && TSEL->step[9].time == ST_TIE);
    assert(step_note_move(TSEL, 5, -12) == 10);  /* backward wraps and stops behind its tail */
    reset(16);
    note(0);
    TSEL->step[4].time = ST_TIE;
    assert(step_note_move(TSEL, 0, 16) == 2);    /* leave a rest before an orphan tie */
    assert(TSEL->step[4].time == ST_TIE);
    assert(step_note_move(TSEL, NSTEP, 1) == NSTEP);
    puts("STEP move: every length, both directions, wrap, full-loop notes, chords/flags/velocity, collisions and orphan ties");
}

static void step_modifier_frame(uint32_t label, int down, int32_t delta)
{
    uint32_t bit = 1u << panel.btn[label];
    if (down && !(fm1_in.buttons & bit))
        button_edges |= bit;
    if (down)
        fm1_in.buttons |= bit;
    else
        fm1_in.buttons &= ~bit;
    encoders[panel.enc[EN_SELECT]] = delta;
    fm1_ms += 100;
    ui_input();
}

static void step_modifiers_test(void)
{
    reset(32);
    note(14);
    step_note_resize(TSEL, 14, 4);
    cursor_set(16);                             /* resize from a tie, not just the onset */
    step_modifier_frame(B_ENV, 1, 2);
    assert(cur_page()->scope == SC_STEP && ui.cursor == 14 && step_note_length(TSEL, 14) == 6);
    step_modifier_frame(B_ENV, 1, -3);
    assert(step_note_length(TSEL, 14) == 3);
    step_modifier_frame(B_ENV, 0, 0);
    assert(cur_page()->scope == SC_STEP);
    cursor_set(15);
    step_modifier_frame(B_SCL, 1, 3);
    assert(ui.cursor == 17 && ui.bank == 1 && step_note_length(TSEL, 17) == 3 && !TSEL->pat_q);
    step_modifier_frame(B_SCL, 0, 0);
    assert(cur_page()->scope == SC_STEP);        /* release must not cycle pages */
    step_modifier_frame(B_SCL, 1, 0);
    step_modifier_frame(B_SCL, 0, -1);          /* final detent on release still moves */
    assert(cur_page()->scope == SC_STEP && ui.cursor == 16 && step_note_length(TSEL, 16) == 3);
    step_modifier_frame(B_ENV, 1, 0);
    step_modifier_frame(B_ENV, 0, -1);          /* likewise for resizing */
    assert(cur_page()->scope == SC_STEP && step_note_length(TSEL, 16) == 2);
    step_modifier_frame(B_SCL, 1, 1);
    step_modifier_frame(B_SCL, 0, 0);
    step_modifier_frame(B_ENV, 1, 1);
    step_modifier_frame(B_ENV, 0, 0);
    note(20);
    step_modifier_frame(B_SCL, 1, 1);
    assert(ui.cursor == 17 && !strcmp(ui.msg, "NO ROOM"));
    step_modifier_frame(B_SCL, 0, 0);
    cursor_set(4);
    step_modifier_frame(B_SCL, 1, 1);
    assert(ui.cursor == 4 && !strcmp(ui.msg, "SELECT A NOTE"));
    step_modifier_frame(B_SCL, 0, 0);
    step_modifier_frame(B_ENV, 1, 1);
    assert(ui.cursor == 4 && !strcmp(ui.msg, "SELECT A NOTE"));
    step_modifier_frame(B_ENV, 0, 0);
    assert(cur_page()->scope == SC_STEP);
    step_modifier_frame(B_ENV, 1, 0);            /* unused ENV still opens the instrument envelope */
    step_modifier_frame(B_ENV, 0, 0);
    assert(cur_fam() == FAM_ENV);
    reset(16);
    step_modifier_frame(B_SCL, 1, 0);
    assert(cur_page()->scope == SC_STEP);
    step_modifier_frame(B_SCL, 0, 0);
    assert(cur_fam() == FAM_SCL);
    reset(16);
    track_select(TRK_DRUM);
    note(0);
    step_modifier_frame(B_ENV, 1, 2);
    assert(step_note_length(TSEL, 0) == 1 && !strcmp(ui.msg, "DRUMS: ONE SHOT"));
    step_modifier_frame(B_ENV, 0, 0);
    step_modifier_frame(B_SCL, 1, -1);
    assert(ui.cursor == 15 && TSEL->step[15].n == 3 && !TSEL->step[0].n);
    step_modifier_frame(B_SCL, 0, 0);
    reset(16);
    key_frame(1u << 7, 2);
    encoders[panel.enc[EN_PRESET]] = 1;
    ui_input();
    assert(ui.entry_open && ui.cursor == 0 && step_note_length(TSEL, 0) == 3 && TSEL->pat_q == 2);
    key_frame(0, 0);
    reset(16);
    key_frame(1u << 7, 15);                     /* full-loop release keeps the cursor but changes its hint */
    uint32_t held_sig = graph_signature();
    key_frame(0, 0);
    assert(ui.cursor == 0 && !ui.entry_open && graph_signature() != held_sig);
    puts("STEP modifiers: ENV resize and tap, SCALE move without page/pattern switch, ties, banks, empty steps, drums and PRESETS while held");
}

static void knob_frame(uint32_t k, int32_t steps)  /* one frame with knob k (0..3) turned */
{
    encoders[panel.enc[EN_K1 + k]] = steps;
    fm1_ms += 20;
    ui_input();
}

static void history_shortcut(int redo, int together)
{
    uint32_t label = redo ? B_OCTUP : B_OCTDN;
    uint32_t oct = 1u << panel.btn[label], fx = 1u << panel.btn[B_FX];
    if (together) {
        button_edges |= oct | fx;
        fm1_in.buttons |= oct | fx;
        fm1_ms += 20;
        ui_input();
        fm1_in.buttons &= ~(oct | fx);
        fm1_ms += 20;
        ui_input();
    } else {
        uint32_t cursor = ui.cursor;
        step_modifier_frame(label, 1, 0);
        assert(ui.cursor == cursor);                   /* the modifier must not navigate */
        step_modifier_frame(B_FX, 1, 0);
        step_modifier_frame(B_FX, 0, 0);
        step_modifier_frame(label, 0, 0);
    }
    assert(cur_page()->scope == SC_STEP && !ui.step_oct && !ui.step_oct_used);
}

static void step_history_test(void)
{
    step_t states[5][NSTEP], before[NSTEP];
    uint32_t i, cursor;
    reset(32);
    memcpy(states[0], TSEL->step, sizeof states[0]);
    key_frame((1u << 7) | (1u << 11) | (1u << 14), 2);
    key_frame((1u << 7) | (1u << 11) | (1u << 14), 1);
    key_frame(0, 0);
    memcpy(states[1], TSEL->step, sizeof states[1]);
    assert(step_history.count == 2 && step_note_length(TSEL, 0) == 4);
    cursor_set(0);
    step_modifier_frame(B_SCL, 1, 2);
    step_modifier_frame(B_SCL, 1, 1);
    step_modifier_frame(B_SCL, 0, 0);
    memcpy(states[2], TSEL->step, sizeof states[2]);
    assert(step_history.count == 3 && step_note_length(TSEL, 3) == 4);
    cursor_set(4);
    step_modifier_frame(B_ENV, 1, 2);
    step_modifier_frame(B_ENV, 1, -1);
    step_modifier_frame(B_ENV, 0, 0);
    memcpy(states[3], TSEL->step, sizeof states[3]);
    assert(step_history.count == 4 && step_note_length(TSEL, 3) == 5);
    cursor_set(5);                                     /* delete from a chord's tie tail */
    step_modifier_frame(B_FX, 1, 0);
    step_modifier_frame(B_FX, 0, 0);
    memcpy(states[4], TSEL->step, sizeof states[4]);
    assert(step_history.count == 5);
    for (i = 4; i > 0; i--) {
        history_shortcut(0, i & 1u);
        assert(!memcmp(TSEL->step, states[i - 1u], sizeof states[0]) && !strcmp(ui.msg, "UNDO"));
    }
    cursor = ui.cursor;
    history_shortcut(0, 0);
    assert(ui.cursor == cursor && !memcmp(TSEL->step, states[0], sizeof states[0]) && !strcmp(ui.msg, "NOTHING TO UNDO"));
    for (i = 1; i <= 4; i++) {
        history_shortcut(1, i & 1u);
        assert(!memcmp(TSEL->step, states[i], sizeof states[0]) && !strcmp(ui.msg, "REDO"));
    }
    history_shortcut(1, 0);
    assert(!strcmp(ui.msg, "NOTHING TO REDO"));
    history_shortcut(0, 0);
    cursor_set(25);
    step_modifier_frame(B_ENV, 1, 1);                    /* blocked edits keep redo */
    step_modifier_frame(B_ENV, 0, 0);
    step_modifier_frame(B_FX, 1, 0);                     /* clearing an empty step is a no-op too */
    step_modifier_frame(B_FX, 0, 0);
    history_shortcut(1, 0);
    assert(!memcmp(TSEL->step, states[4], sizeof states[0]));
    history_shortcut(0, 0);
    cursor_set(3);
    knob_frame(1, 1);                                  /* NOTE transpose branches the history */
    memcpy(before, TSEL->step, sizeof before);
    history_shortcut(1, 0);
    assert(!memcmp(TSEL->step, before, sizeof before) && !strcmp(ui.msg, "NOTHING TO REDO"));
    history_shortcut(0, 0);
    assert(!memcmp(TSEL->step, states[3], sizeof states[0]));

    reset(16);
    note(0);
    ui_input();
    for (i = 0; i < STEP_HISTORY + 4u; i++)
        knob_frame(1, 1);
    assert(step_history.count == STEP_HISTORY + 1u);
    for (i = 0; i < STEP_HISTORY; i++)
        history_shortcut(0, 0);
    assert(TSEL->step[0].note[0] == 64);
    memcpy(before, TSEL->step, sizeof before);
    history_shortcut(0, 0);
    assert(!memcmp(TSEL->step, before, sizeof before) && !strcmp(ui.msg, "NOTHING TO UNDO"));
    for (i = 0; i < STEP_HISTORY; i++)
        history_shortcut(1, 0);
    assert(TSEL->step[0].note[0] == 72);

    reset(16);
    key_frame(1u << 7, 4);                              /* undo before releasing a held note */
    history_shortcut(0, 0);
    assert(!TSEL->step[0].n && !ui.entry_open);
    key_frame(0, 0);
    history_shortcut(1, 0);
    assert(step_note_length(TSEL, 0) == 5);
    reset(16);
    key_frame(1u << 7, 4);
    step_modifier_frame(B_FX, 1, 0);                   /* deleting while keys are held is its own edit */
    step_modifier_frame(B_FX, 0, 0);
    assert(!TSEL->step[0].n && step_history.count == 3);
    history_shortcut(0, 0);
    assert(step_note_length(TSEL, 0) == 5);
    key_frame(0, 0);
    history_shortcut(0, 0);
    assert(!TSEL->step[0].n);
    reset(16);
    midi_frame(0x90, 60, 100, 2);
    midi_frame(0x90, 64, 100, 1);
    midi_frame(0x80, 60, 0, 0);
    midi_frame(0x80, 64, 0, 1);
    memcpy(before, TSEL->step, sizeof before);
    assert(step_history.count == 2);
    history_shortcut(0, 0);
    assert(!TSEL->step[0].n);
    history_shortcut(1, 0);
    assert(!memcmp(TSEL->step, before, sizeof before));

    reset(16);
    cursor_set(0);
    step_modifier_frame(B_OCTDN, 1, 0);
    assert(ui.cursor == 0);
    step_modifier_frame(B_OCTDN, 0, 0);
    assert(ui.cursor == 15);
    step_modifier_frame(B_OCTUP, 1, 0);
    step_modifier_frame(B_OCTUP, 0, 0);
    assert(ui.cursor == 0);
    key_frame(1u << 7, 1);
    key_frame(0, 0);
    memcpy(before, TSEL->step, sizeof before);
    uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]) | (1u << panel.btn[B_FX]);
    button_edges |= both;
    fm1_in.buttons |= both;
    ui_input();
    fm1_in.buttons &= ~both;
    ui_input();
    assert(!memcmp(TSEL->step, before, sizeof before) && !strcmp(ui.msg, "USE ONE OCT BUTTON"));
    uint8_t old = panel.btn[B_FX];                     /* shortcuts follow calibration, not matrix order */
    panel.btn[B_FX] = panel.btn[B_OCTDN];
    panel.btn[B_OCTDN] = old;
    history_shortcut(0, 1);
    assert(!TSEL->step[0].n);
    history_shortcut(1, 0);
    assert(!memcmp(TSEL->step, before, sizeof before));
    reset(16);
    cursor_set(14);
    note(14);
    step_note_resize(TSEL, 14, 4);
    memcpy(before, TSEL->step, sizeof before);
    ui_input();
    knob_frame(3, -1);                                 /* FLAG then TIME, preserving wrapped chord metadata */
    memcpy(states[1], TSEL->step, sizeof states[1]);
    knob_frame(2, 1);
    memcpy(states[2], TSEL->step, sizeof states[2]);
    history_shortcut(0, 0);
    assert(!memcmp(TSEL->step, states[1], sizeof before));
    history_shortcut(0, 0);
    assert(!memcmp(TSEL->step, before, sizeof before));
    history_shortcut(1, 0);
    history_shortcut(1, 0);
    assert(!memcmp(TSEL->step, states[2], sizeof before));
    puts("STEP undo/redo: grouped entry/move/resize, whole-note delete, transpose, eight edits, branching, no-ops, MIDI, held input, chords and calibrated shortcuts");
}

static void step_history_context_test(void)
{
    step_t before[NSTEP];
    reset(16);
    key_frame(1u << 7, 1);
    key_frame(0, 0);
    track_select(1);
    history_shortcut(0, 0);
    assert(!strcmp(ui.msg, "NOTHING TO UNDO") && !TSEL->step[0].n && trk[0].step[0].n);
    track_select(0);
    history_shortcut(0, 0);
    assert(!strcmp(ui.msg, "NOTHING TO UNDO") && TSEL->step[0].n);
    cursor_set(0);
    knob_frame(1, 1);
    pat_switch(TSEL, 1);
    history_shortcut(0, 0);
    assert(!strcmp(ui.msg, "NOTHING TO UNDO") && !TSEL->step[0].n);
    pat_switch(TSEL, 0);
    ui_input();
    knob_frame(1, 1);
    TSEL->p[P_SLEN] = 8;
    memcpy(before, TSEL->step, sizeof before);
    history_shortcut(0, 0);
    assert(!strcmp(ui.msg, "NOTHING TO UNDO") && !memcmp(TSEL->step, before, sizeof before));
    knob_frame(1, 1);
    note(7);                                           /* editor/external edit invalidates stale history */
    memcpy(before, TSEL->step, sizeof before);
    history_shortcut(0, 0);
    assert(!strcmp(ui.msg, "NOTHING TO UNDO") && !memcmp(TSEL->step, before, sizeof before));
    knob_frame(1, 1);
    song.playing = song.rec = 1;
    history_shortcut(0, 0);
    assert(!strcmp(ui.msg, "NOTHING TO UNDO"));
    song.playing = song.rec = 0;
    ui_input();
    knob_frame(1, 1);
    project_save(0);
    project_load(0);                                    /* even loading identical steps resets history */
    memcpy(before, TSEL->step, sizeof before);
    history_shortcut(0, 0);
    assert(!strcmp(ui.msg, "NOTHING TO UNDO") && !memcmp(TSEL->step, before, sizeof before));
    cursor_set(0);
    knob_frame(1, 1);
    open_family(FAM_SAVE);
    step_history_sync();
    track_defaults_steps(TSEL);                         /* a tools/library change does not enter STEP history */
    step_history_end();
    assert(!step_history.valid);
    page_go(page_named("STEP"));
    history_shortcut(0, 0);
    assert(!strcmp(ui.msg, "NOTHING TO UNDO") && !TSEL->step[0].n);
    puts("STEP undo context: tracks, patterns, length changes, external edits, armed playback and identical project loads stay isolated");
}

static void scope_fixture(uint32_t period)
{
    uint32_t i;
    for (i = 0; i < SCOPE_N; i++)
        scope_buf[i] = (int16_t)(12000 * sin(6.283185307179586 * i / period));
    scope_w = 0;
}

static void home_render_navigation_test(void)
{
    static uint16_t expected[240 * H_GRAPH];
    uint32_t engine, page, view;
    reset(16);
    settings.zoom = 0;
    memset(live_last, 0, sizeof live_last);
    memset(live_held, 0, sizeof live_held);
    live_last[60u >> 5] = 1u << (60u & 31u);
    live_last[64u >> 5] = (1u << (64u & 31u)) | (1u << (67u & 31u));
    memcpy(live_held, live_last, sizeof live_held);
    scope_fixture(47);
    for (engine = 0; engine <= NENGINES; engine++) {
        song.sel = engine == NENGINES ? TRK_DRUM : 0;
        if (engine < NENGINES)
            TSEL->eng_req = (uint8_t)engine;
        go_home();
        ui_draw();
        assert(area_has(4, Y_GRAPH, 40, Y_GRAPH + 30, C_WHITE));
        assert(area_has(0, Y_GRAPH + 36, 240, Y_GRAPH + 64, page_color()));
        memcpy(expected, screen + Y_GRAPH * 240, sizeof expected);
        for (page = 0; page < NPAGES; page++) {
            if (!page_shown(&PAGES[page]))
                continue;
            page_go(page);
            ui_draw();
            home_tap();
            ui_draw();
            assert(!memcmp(expected, screen + Y_GRAPH * 240, sizeof expected));
        }
        for (view = 0; view < 4; view++) {
            home_tap();
            ui_draw();
        }
        assert(!memcmp(expected, screen + Y_GRAPH * 240, sizeof expected));
    }
    /* Audio changes must redraw without a control event or full-screen refresh. */
    scope_fixture(71);
    ui.frame += 2;
    ui_draw();
    assert(!memcmp(expected, screen + Y_GRAPH * 240, 240 * 32 * sizeof(uint16_t)));
    assert(memcmp(expected + 240 * 32, screen + (Y_GRAPH + 32) * 240, 240 * (H_GRAPH - 32) * sizeof(uint16_t)));
    settings.zoom = 1;
    ui.hot_t = 4;
    ui.frame += 2;
    ui_draw();
    assert(area_has(0, Y_GRAPH + 58, 240, Y_GRAPH + 82, page_color()));
    ui.hot_t = 0;
    memset(live_held, 0, sizeof live_held);
    memset(scope_buf, 0, sizeof scope_buf);
    ui.frame += 2;
    ui_draw();
    assert(area_has(4, Y_GRAPH, 40, Y_GRAPH + 30, C_HI));
    assert(!area_has(0, Y_GRAPH + 36, 240, Y_GRAPH + 64, page_color()));
    puts("HOME display: notes and live waveform together, all engines/FM6, return from every page, zoom, release and silence");
}

static void loop_redraw_test(void)
{
    static uint16_t previous[240 * H_GRAPH];
    reset(16);
    go_home();
    open_family(FAM_SEQ);                           /* LOOP */
    trk[1].seq_idx = 3;
    song.playing = 1;
    ui_draw();
    memcpy(previous, screen + Y_GRAPH * 240, sizeof previous);
    song.playing = 0;
    ui_draw();
    assert(memcmp(previous, screen + Y_GRAPH * 240, sizeof previous));
    memcpy(previous, screen + Y_GRAPH * 240, sizeof previous);
    trk[1].p[P_SLEN] = 4;
    ui_draw();
    assert(memcmp(previous, screen + Y_GRAPH * 240, sizeof previous));
    puts("LOOP display: stopping clears playheads; other tracks' length changes redraw");
}

static void playing_key_lights_test(void)
{
    track_t *t;
    uint32_t k;
    reset(16);
    t = TSEL;
    t->p[P_SCALE] = 1;                         /* C major */
    t->p[P_ROOT] = 0;
    t->p[P_QUANT] = Q_OFF;
    assert(play_key_led(t, 0) && !play_key_led(t, 1));  /* F, F# */
    assert(play_key_led(t, 7) && !play_key_led(t, 8));  /* C, C# */
    t->p[P_ROOT] = 2;                          /* D major moves the guide */
    assert(!play_key_led(t, 0) && play_key_led(t, 1));
    t->p[P_TRANS] = 7;
    song.octave = 2;
    assert(!play_key_led(t, 0) && play_key_led(t, 1)); /* output transposition keeps layout */
    t->p[P_SCALE] = 0;
    for (k = 0; k < 27u; k++)
        assert(play_key_led(t, k));            /* chromatic */
    t->p[P_SCALE] = 1;
    t->p[P_QUANT] = Q_SNAP;
    for (k = 0; k < 27u; k++)
        assert(play_key_led(t, k));            /* every key snaps to a pitch */
    t->p[P_QUANT] = Q_WHITE;
    assert(play_key_led(t, 7) && !play_key_led(t, 8) && play_key_led(t, 9));
    t->p[P_QUANT] = Q_MPC;
    assert(play_key_led(t, 7) && !play_key_led(t, 8) && play_key_led(t, 9));
    t->p[P_QUANT] = Q_ALL;
    assert(play_key_led(t, 7) && play_key_led(t, 8) && play_key_led(t, 9));
    assert(play_key_led(TDRUM, 8));            /* drum keys remain available */
    reset(16);                                 /* sounding keys are bright, the layout dim */
    t = TSEL;
    t->p[P_SCALE] = 1;
    t->p[P_QUANT] = Q_WHITE;
    assert(play_key_led(t, 7) == KL_DIM && play_key_led(t, 8) == KL_OFF);
    fm1_in.notes = (1u << 7) | (1u << 8);      /* C4 and the silent C#4 held */
    events_block(0);
    assert(play_key_led(t, 7) == KL_ON && play_key_led(t, 8) == KL_OFF && play_key_led(t, 9) == KL_DIM);
    song.octave = 1;                           /* a held key stays lit at its new octave */
    assert(play_key_led(t, 7) == KL_ON);
    song.octave = 0;
    fm1_in.notes = 0;
    events_block(0);
    assert(play_key_led(t, 7) == KL_DIM);
    midi_frame(0x90, 62, 100, 0);              /* MIDI D4: second degree, the D4 key */
    assert(play_key_led(t, 9) == KL_ON && play_key_led(t, 7) == KL_DIM);
    midi_frame(0x91, 60, 100, 0);              /* another track's channel */
    assert(play_key_led(t, 7) == KL_DIM);
    midi_frame(0x80, 62, 0, 0);
    assert(play_key_led(t, 9) == KL_DIM);
    t->p[P_QUANT] = Q_OFF;                     /* a key outside the scale still lights when played */
    midi_frame(0x90, 61, 100, 0);
    assert(play_key_led(t, 8) == KL_ON);
    midi_frame(0x80, 61, 0, 0);
    assert(play_key_led(t, 8) == KL_OFF);
    settings.keys = KEYS_LOW;
    ui_leds();
    assert(fm1_led_dim_mask[0] == 7u);
    settings.keys = KEYS_FULL;
    ui_leds();
    assert(fm1_led_dim_mask[0] == 0u);
    puts("playing key lights: scale, root, layouts, transposition, drums; bright when played or from MIDI");
}

static int led_lit(const uint8_t *pic, uint32_t id)   /* id's LED in fm1_led or a dim plane */
{
    uint8_t q = led_pos[id];
    return q != 0xFF && ((pic[q >> 3] >> (q & 7u)) & 1u);
}

static void panel_lights_test(void)
{
    uint32_t b, k;
    reset(16);
    settings.keys = KEYS_MID;
    TSEL->p[P_SCALE] = 1;
    TSEL->p[P_QUANT] = Q_WHITE;
    page_go(page_named("ENV"));
    ui_leds();
    assert(fm1_led_dim_mask[0] == KEYS_DIM_MASK[KEYS_MID] && fm1_led_dim_mask[1] == BTN_DIM_MASK);
    for (b = 0; b < NB; b++)                   /* every button glows; ENV and its workspace EDIT bright */
        assert(led_lit(fm1_led_dim[1], panel.btn[b]) &&
               led_lit(fm1_led, panel.btn[b]) == (b == B_ENV || b == B_EDIT));
    assert(led_lit(fm1_led_dim[0], 14u + 7) && !led_lit(fm1_led, 14u + 7));   /* C4: the layout, dim */
    fm1_in.buttons = 1u << panel.btn[B_SAVE];  /* a held button is bright */
    song.octave = 1;
    ui_leds();
    assert(led_lit(fm1_led, panel.btn[B_SAVE]) && led_lit(fm1_led, panel.btn[B_OCTUP]) &&
           !led_lit(fm1_led, panel.btn[B_OCTDN]));
    fm1_in.buttons = 0;
    song.octave = 0;

    page_go(page_named("LIGHTS"));             /* GLO > LIGHTS: KEYS OFF darkens every LED */
    edit_param(0, -1);
    assert(settings.keys == (KEYS_MID | KEYS_DARK) && set_t);
    fm1_in.notes = 1u << 7;                    /* but a played one lights */
    events_block(0);
    ui_leds();
    for (k = 0; k < 27u; k++)
        assert(led_lit(fm1_led, 14u + k) == (k == 7u) && led_lit(fm1_led_dim[0], 14u + k) == (k == 7u));
    fm1_in.buttons = 1u << panel.btn[B_SAVE];  /* no idle glow; engaged (GLO) and held (SAVE) still light */
    ui_leds();
    for (b = 0; b < NB; b++)
        assert(!led_lit(fm1_led_dim[1], panel.btn[b]) && led_lit(fm1_led, panel.btn[b]) == (b == B_GLO || b == B_SAVE));
    fm1_in.buttons = 0;
    fm1_in.notes = 0;
    events_block(0);
    midi_frame(0x90, 62, 100, 0);              /* and one from MIDI in (D4) */
    ui_leds();
    assert(led_lit(fm1_led, 14u + 9) && !led_lit(fm1_led, 14u + 7));
    midi_frame(0x80, 62, 0, 0);
    page_go(page_named("EDIT 1"));             /* EDIT + keys: the shortcuts glow (the buttons' level) */
    edit_frame(1, 0);
    assert(nav_held());
    ui_leds();
    for (k = 0; k < 27u; k++)
        assert(!led_lit(fm1_led, 14u + k) && !led_lit(fm1_led_dim[0], 14u + k));
    assert(TSEL->preset != 1u && led_lit(fm1_led_dim[1], 14u + SOUND_KEY[1]) && !led_lit(fm1_led_dim[1], 14u + 26));
    edit_frame(1, 1u << 26);                   /* a key pressed there lights (G5: no shortcut) */
    ui_leds();
    for (k = 0; k < 27u; k++)
        assert(led_lit(fm1_led, 14u + k) == (k == 26u));
    edit_frame(1, 0);
    settings.keys = KEYS_MID;                  /* (they show with the keys on) */
    ui_leds();
    assert(TSEL->preset != 1u && led_lit(fm1_led, 14u + SOUND_KEY[1]));   /* (steady: not the current sound) */
    settings.keys = KEYS_MID | KEYS_DARK;
    edit_frame(0, 0);
    page_go(page_named("LIGHTS"));
    ui.force = 1;
    ui_draw();
    assert(!strncmp(ui.col[0], "KEYS|OFF|", 9));
    fm1_ms += 1600;                            /* to flash once the knob rests */
    ui_input();
    assert(!set_t);
    edit_param(0, -1);                         /* already off: nothing to save */
    assert(!set_t);
    edit_param(0, 1);                          /* ON: the level as it was */
    assert(settings.keys == KEYS_MID && set_t);
    ui_leds();
    assert(led_lit(fm1_led_dim[0], 14u + 7));
    settings.keys = KEYS_LOW | KEYS_DARK;      /* a flash copy with the flag keeps it */
    settings_init();
    assert(settings.keys == (KEYS_LOW | KEYS_DARK));
    ui.menu = 1;                               /* Settings > KEYS turned: the keys are back on */
    ui.menu_sel = MI_KEYS;
    encoders[panel.enc[EN_K1]] = 1;
    menu_input(0);
    assert(settings.keys == KEYS_MID);
    ui.menu = 0;
    puts("panel lights: buttons dim / bright, GLO > LIGHTS KEYS OFF, Settings > KEYS level kept");
}

/* GLO > GLOBAL and DRUMS kept on the device: any change (knob, editor, a project) goes to flash once
 * it rests and the transport is stopped; power-on restores them (glo_restore), a BOOT project or the
 * template then brings its own. BPM and SWG (SEQ > TEMPO) are the project's: never kept */
static void glo_kept_test(void)
{
    uint32_t i;
    reset(16);
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    ui_input();
    fm1_ms += 1600;
    ui_input();
    assert(!set_t);
    song.g[G_TUNE] = 7;
    song.playing = 1;
    ui_input();
    assert(settings.glo[3] == 7 && set_t);
    fm1_ms += 1600;
    ui_input();
    assert(set_t);                             /* playing: an erase would silence it */
    song.playing = 0;
    ui_input();
    assert(!set_t);
    fm1_ms = (fm1_ms + 1u) & ~1u;              /* an even ms: set_t (| 1) is past it, still no save yet */
    song.g[G_CLOCK] = 1;
    ui_input();
    assert(settings.glo[2] == 1 && set_t);
    fm1_ms += 1600;
    ui_input();
    assert(!set_t);
    song.g[G_BPM] = 133;                       /* the project's tempo and swing: nothing to save */
    song.g[G_SWING] = 30;
    ui_input();
    assert(!set_t);
    for (i = 0; i < G_COUNT; i++)              /* power-on: the defaults, then the values kept */
        song.g[i] = GP[i].def;
    settings.glo[0] = 133;                     /* a record from when BPM and SWG were kept */
    settings.glo[1] = 30;
    settings.glo[4] = 500;                     /* (a .noinit copy out of range) */
    glo_restore();
    assert(song.g[G_TUNE] == 7 && song.g[G_CLOCK] == 1 && song.g[G_BPM] == GP[G_BPM].def &&
           song.g[G_SWING] == GP[G_SWING].def && song.g[G_DRCH] == GP[G_DRCH].max &&
           settings.glo[4] == GP[G_DRCH].max);
    puts("GLO values: kept once they rest while stopped, BPM and SWG not (the project's); restored at power-on");
}

#ifndef MELODEE_UI_PREVIEW
/* SAVE > PROJECT BOOT: KNOB 2 picks OFF / 1..4 (the column shows it) and is kept once it rests;
 * power-on loads that project and points SLOT at it, a failed boot or an empty slot leaves the
 * default sounds. Factory presets never touch the steps. */
static void boot_project_test(void)
{
    track_t *t;
    uint32_t i;
    reset(16);
    t = TSEL;
    settings.boot = 0;
    ui.page = (uint8_t)page_named("PROJECT");
    page_entered();
    for (i = 0; i < 6u; i++) {                      /* right: 1, 2, 3, 4, then it stays */
        encoders[panel.enc[EN_K1 + 1u]] = 1;
        fm1_ms += 20;
        ui_input();
    }
    assert(settings.boot == 4 && set_t && ui.hot_col == 1u);
    ui.force = 1;
    ui_draw();
    assert(!strcmp(ui.focus_l, "BOOT") && !strcmp(ui.focus_v, "4"));
    for (i = 0; i < 9u; i++) {                      /* left: down to OFF */
        encoders[panel.enc[EN_K1 + 1u]] = -1;
        fm1_ms += 20;
        ui_input();
    }
    ui.force = 1;
    ui_draw();
    assert(settings.boot == 0 && !strcmp(ui.focus_v, "OFF"));
    fm1_ms += 1600;                                 /* the knob rests: settings_save, once */
    ui_input();
    assert(!set_t);

    note(0);                                        /* project 3 holds a note; it boots */
    project_save(2);
    assert(project_used(2));
    settings.boot = 3;
    track_defaults_steps(t);                        /* power-on: the default state, SLOT 1 */
    song.g[G_SLOT] = 1;
    bootguard.failed = 1;                           /* the start before crashed: not loaded */
    project_boot();
    assert(song.g[G_SLOT] == 3 && !t->step[0].n && !strcmp(ui.msg, "BOOT PROJECT SKIPPED"));
    bootguard.failed = 0;
    project_boot();
    assert(song.g[G_SLOT] == 3 && t->step[0].n && !strcmp(ui.msg, "LOADED P3"));
    settings.boot = 4;                              /* an empty slot: nothing loads */
    track_defaults_steps(t);
    project_boot();
    assert(song.g[G_SLOT] == 4 && !t->step[0].n);
    settings.boot = 0;

    set_engine(0);
    for (i = 0; i < ENG_ANALOG.npresets; i++) {      /* (ACID, SAW LEAD .. had patterns once) */
        apply_preset(i);
        assert(seq_is_empty(t));
    }
    puts("boot project: BOOT knob, power-on load, failed boot skips it; factory presets leave the steps");
}

/* SAVE > PROJECT, SLOT TMPL: SAVE (two detents) keeps the sounds, the mix and the globals, no pattern;
 * LOAD, or power-on with BOOT OFF or an empty BOOT slot, starts a new project from it, SLOT on a free
 * project slot. The RAM project (proj_buf) is not touched. */
static void template_test(void)
{
    track_t *t;
    uint32_t i, k;
    reset(16);
    t = TSEL;
    memset(&tmpl, 0, sizeof tmpl);
    settings.boot = 0;
    ui.page = (uint8_t)page_named("PROJECT");
    page_entered();
    for (i = 0; i < 6u; i++)                        /* SLOT past 4: the template */
        knob_frame(0, 1);
    ui.force = 1;
    ui_draw();
    assert(song.g[G_SLOT] == PROJ_TMPL && !strcmp(ui.focus_l, "SLOT") && !strcmp(ui.focus_v, "TMPL"));
    knob_frame(1, -1);                              /* BOOT OFF, no template yet */
    ui.force = 1;
    ui_draw();
    assert(!strcmp(ui.focus_v, "OFF"));

    set_engine(1);                                  /* a sound, a mix, a tempo, and patterns */
    apply_preset(2);
    t->p[P_LEVEL] = 77;
    song.g[G_BPM] = 133;
    t->p[P_SLEN] = 8;
    note(0);
    pat_bank[song.sel][3].step[5].n = 1;
    pat_bank[song.sel][3].set[0] = 12;
    knob_frame(3, 1);
    assert(!template_used() && !strcmp(ui.msg, "AGAIN: SAVE AS TEMPLATE"));
    knob_frame(3, 1);
    assert(template_used() && !strcmp(ui.msg, "TEMPLATE (RAM)"));
    assert(tmpl.t[song.sel].engine == 1 && tmpl.t[song.sel].preset == 2 && tmpl.t[song.sel].p[P_LEVEL] == 77 &&
           tmpl.g[G_BPM] == 133 && tmpl.t[song.sel].p[P_SLEN] == TP[P_SLEN].def);
    ui.force = 1;
    knob_frame(1, -1);                              /* BOOT OFF now reads TMPL */
    ui_draw();
    assert(!strcmp(ui.focus_v, "TMPL"));

    set_engine(0);                                  /* something else, then LOAD the template */
    song.g[G_BPM] = 90;
    note(3);
    knob_frame(2, 1);
    assert(!strcmp(ui.msg, "AGAIN: LOAD TEMPLATE"));
    knob_frame(2, 1);
    assert(!strcmp(ui.msg, "TEMPLATE LOADED"));
    assert(t->eng_req == 1 && t->preset == 2 && t->p[P_LEVEL] == 77 && song.g[G_BPM] == 133 &&
           t->p[P_SLEN] == TP[P_SLEN].def && t->pat == 0);
    for (k = 0; k < NTRK; k++) {                    /* not one step anywhere */
        assert(seq_is_empty(&trk[k]));
        for (i = 0; i < NPAT; i++)
            assert(!pat_used(&trk[k], i) && !pat_bank[k][i].set[0]);
    }
    assert(song.g[G_SLOT] != PROJ_TMPL && !project_used((uint32_t)song.g[G_SLOT] - 1u));   /* a free slot */

    note(0);                                        /* project 2 (RAM: proj_buf) survives the template */
    project_save(1);
    template_save();
    template_load();
    assert(seq_is_empty(t) && song.g[G_SLOT] == 1);
    project_load(1);
    assert(t->step[0].n && !strcmp(ui.msg, "LOADED P2"));

    set_engine(0);                                  /* power-on with BOOT OFF: the template */
    project_boot();
    assert(t->eng_req == 1 && seq_is_empty(t) && song.g[G_SLOT] == 1 && !strcmp(ui.msg, "TEMPLATE LOADED"));
    set_engine(0);
    bootguard.failed = 1;                           /* after a failed start: the default sounds */
    project_boot();
    assert(t->eng_req == 0 && !strcmp(ui.msg, "TEMPLATE SKIPPED"));
    bootguard.failed = 0;
    settings.boot = 4;                              /* BOOT on an empty slot: the template, SLOT there */
    project_boot();
    assert(t->eng_req == 1 && song.g[G_SLOT] == 4);
    settings.boot = 2;                              /* BOOT on a project: that project */
    project_boot();
    assert(t->step[0].n && song.g[G_SLOT] == 2 && !strcmp(ui.msg, "LOADED P2"));
    settings.boot = 0;
    memset(&tmpl, 0, sizeof tmpl);
    puts("template: SLOT TMPL saves without patterns, LOAD and power-on start from it, SLOT on a free slot");
}

static void save_hold(uint32_t frames_ms)       /* SAVE pressed, held frames_ms, let go */
{
    uint32_t sv = 1u << panel.btn[B_SAVE];
    button_edges |= sv;
    fm1_in.buttons |= sv;
    fm1_ms += 20;
    ui_input();
    fm1_ms += frames_ms;
    ui_input();
    fm1_in.buttons &= ~sv;
    fm1_ms += 20;
    ui_input();
}

/* SAVE: a tap opens the library on release; held 0.7 s on any page it saves the current project (the
 * last loaded, saved or booted) back to its slot at once, "SAVED P3"; SLOT browsing does not move it;
 * a new project (none yet, the template) opens PROJECT on a free slot instead; nothing in the menu */
static void quick_save_test(void)
{
    track_t *t;
    uint32_t pg;
    reset(16);
    t = TSEL;
    memset(&tmpl, 0, sizeof tmpl);
    proj_cur = proj_ram = proj_have = 0;
    open_family(FAM_ENV);
    pg = ui.page;
    save_hold(0);                                   /* a tap */
    assert(cur_page()->fam == FAM_SAVE && proj_cur == 0);

    open_family(FAM_ENV);                           /* held, a new project: PROJECT, a free slot */
    save_hold(720);
    assert(cur_page()->graph == GR_SLOTS && song.g[G_SLOT] == 1 && !proj_have &&
           !strcmp(ui.msg, "NEW PROJECT: PICK SLOT"));   /* (the release was no tap: still PROJECT) */

    note(0);
    project_save(2);                                /* SAVE on the PROJECT page: P3 is current */
    assert(proj_cur == 3 && !strcmp(ui.msg, "SAVED P3 (RAM)"));
    open_family(FAM_ENV);
    pg = ui.page;
    song.g[G_SLOT] = 1;                             /* SLOT looked elsewhere */
    note(5);
    ui.msg[0] = 0;
    save_hold(720);
    assert(ui.page == pg && proj_ram == 3 && proj_cur == 3 && !strcmp(ui.msg, "SAVED P3 (RAM)"));
    track_defaults_steps(t);
    project_load(2);
    assert(t->step[5].n && song.g[G_SLOT] == 3 && !strcmp(ui.msg, "LOADED P3"));
    save_hold(300);                                 /* too short: a tap */
    assert(cur_page()->fam == FAM_SAVE && !strcmp(ui.msg, "LOADED P3"));

    ui.menu = 1;                                    /* in the menu: no save, no tap */
    pg = ui.page;
    ui.msg[0] = 0;
    save_hold(720);
    assert(!ui.msg[0] && ui.page == pg);
    ui.menu = 0;

    template_save();                                /* from the template: a new project again */
    template_load();
    assert(proj_cur == 0);
    open_family(FAM_ENV);
    save_hold(720);
    assert(cur_page()->graph == GR_SLOTS && !strcmp(ui.msg, "NEW PROJECT: PICK SLOT"));
    memset(&tmpl, 0, sizeof tmpl);
    puts("quick save: SAVE held saves the current project, a tap opens SAVE, a new project picks a slot");
}

int main(int argc, char **argv)
{
    lengths_test();
    gestures_test();
    preset_scale_settings_test();
    midi_entry_test();
    live_record_follow_test();
    mpc_page_test();
    playback_test();
    home_notes_test(argc > 2 ? argv[2] : NULL);
    fm6_page_nav_test();
    patterns_test();
    workspace_test();
    select_knob_test();
    performance_gestures_test();
    step_delete_test();
    step_moves_test();
    step_modifiers_test();
    step_history_test();
    step_history_context_test();
    home_render_navigation_test();
    loop_redraw_test();
    playing_key_lights_test();
    boot_project_test();
    template_test();
    quick_save_test();
    panel_lights_test();
    glo_kept_test();
    render_test(argc > 1 ? argv[1] : NULL);
    return 0;
}
#endif
