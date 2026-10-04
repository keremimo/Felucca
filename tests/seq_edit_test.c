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
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {{0}};
static uint8_t fm1_led[FM1_NCOL];
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
static struct { uint32_t stage; } felucca_dbg;
#include "../firmware/src/ui_draw.c"
#include "../firmware/src/ui_menu.c"
#include "../firmware/src/ui_input.c"

static void project_save(uint32_t slot) { (void)slot; assert(0); }
static void project_load(uint32_t slot) { (void)slot; assert(0); }
static int project_used(uint32_t slot) { (void)slot; return 0; }
static void settings_save(void) {}
static int up_used(uint32_t k) { (void)k; return 0; }
static int up_load(uint32_t k) { (void)k; assert(0); return 0; }
static uint32_t up_count(void) { return 0; }
static uint32_t up_nth(uint32_t n) { return n; }
static uint32_t up_rank(uint32_t n) { return n; }
static void up_name(uint32_t k, char *b) { (void)k; b[0] = 0; }
static void up_slot_label(char *b, uint32_t k) { fmt_int(b, (int32_t)k + 1); }
static void up_ui(uint32_t op, uint32_t k) { (void)op; (void)k; assert(0); }
static void fm6_store(uint32_t k) { (void)k; assert(0); }
static void fm6_send(void) { assert(0); }
static void fm6_init_voice(void) { assert(0); }

static void reset(uint32_t len)
{
    uint32_t i;
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    memset(&ui, 0, sizeof ui);
    memset(encoders, 0, sizeof encoders);
    memset(midi_notes, 0, sizeof midi_notes);
    memset(midi_ch, 0, sizeof midi_ch);
    memset(midi_owners, 0, sizeof midi_owners);
    memset(live_refs, 0, sizeof live_refs);
    step_midi_w = step_midi_r = step_midi_overflow = 0;
    mi_w = mi_r = 0;
    host_tracks_init();
    for (i = 0; i < NTRK; i++) track_defaults_steps(&trk[i]);
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
    note_edges = keys & ~fm1_in.notes;
    fm1_in.notes = keys;
    encoders[panel.enc[EN_K1]] = length_delta;
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
    /* Enter a chord and turn STEP on its first frame. */
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
    assert(TSEL->preset == 1);              /* PRESETS browses on STEP */
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
    puts("gestures: held chord, release/detent ordering, STEP cursor/length, TIME, preset browsing, drums and scale mode ok");
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
    puts("MIDI STEP entry: routing, WHITE scale, chords, release, final detent and mixed input ok");
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

static void render_test(const char *path)
{
    uint32_t i, j;
    reset(32);
    note(14);
    step_note_resize(TSEL, 14, 6);
    cursor_set(17);
    ui_draw();
    /* The second bank starts mid-chord: all three notes continue visibly. */
    for (j = 0; j < 3; j++) {
        int y = Y_GRAPH + G_OY + 80 - (TSEL->step[14].note[j] - 60) * 74 / 12;
        assert(screen[y * 240] == C_GRAY && screen[y * 240 + 14] == C_GRAY);
    }
    assert(text_w(&FONT_S, "HOLD + STEP: 64 STP") <= 232);
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

int main(int argc, char **argv)
{
    lengths_test();
    gestures_test();
    midi_entry_test();
    playback_test();
    render_test(argc > 1 ? argv[1] : NULL);
    return 0;
}
