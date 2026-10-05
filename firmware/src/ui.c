/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Melodee user interface. Four columns map to KNOB 1..4. Rendering is lazy:
 * every element remembers what it last drew and is redrawn only on change. */
#ifndef MELODEE_VERSION
#define MELODEE_VERSION "0.5 BETA"   /* build.py --beta X.Y passes its own */
#endif
static void project_save(uint32_t slot);
static void panel_setup(void);
static void project_load(uint32_t slot);
static int project_used(uint32_t slot);
static void project_quick_save(void);
static void template_save(void);             /* the template (SLOT TMPL): project.c */
static void template_load(void);
static int template_used(void);
static int up_used(uint32_t k);              /* user presets: upreset.c */
static int up_load(uint32_t k);
static uint32_t up_count(void);
static uint32_t up_nth(uint32_t n);
static uint32_t up_rank(uint32_t slot);
static void up_name(uint32_t k, char *b);
static void up_slot_label(char *b, uint32_t k);
static void up_ui(uint32_t op, uint32_t k);
static void fm6_store(uint32_t k);           /* FM6 STORE page: fm6_store.c */
static void fm6_send(void);
static void fm6_init_voice(void);
static uint32_t user_of(const track_t *t)    /* user preset slot its sound came from, UP_SLOTS = none */
{
    return t->user && up_used(t->user - 1u) ? t->user - 1u : UP_SLOTS;
}
static uint32_t up_gen;                      /* bumped on every user bank change (redraws) */
static uint8_t sync_reload;                  /* engine / preset / project / user preset loaded: editor RELOAD push */

#define ACC C_HI                   /* amber everywhere; white is the only accent */
#define VAL(c) ((c) == ui.hot_col && ui.hot_t ? C_WHITE : C_HI)
#define RATIO(d, v) ((d)->max > (d)->min ? ((int32_t)(v) - (d)->min) * 1000 / ((d)->max - (d)->min) : -1)
/* Layout: mode and sound above the scene, four controls below it. */
#define Y_HEAD 0
#define H_HEAD 20
#define Y_FOOT 20
#define H_FOOT 30
#define Y_GRAPH 54
#define H_GRAPH 124
#define G_OY 0                        /* the scene uses the full center area */
#define Y_LABEL 183
#define Y_VALUE 199
#define Y_GAUGE 222
#define Y_SEP_END 230
#define Y_STEP 233
#define H_STEP 7

static struct {
    uint8_t home;
    uint8_t home_view;           /* HOME: 0 notes, 1 levels, 2 pan, 3 master effects */
    uint8_t page;                /* index into PAGES */
    uint8_t fam_last[FAM_COUNT]; /* last page used per family */
    uint8_t bank;                /* SEQ: 16-step bank (follows the cursor) */
    uint8_t cursor;              /* SEQ: step being edited (STEP page KNOB 1 moves it) */
    uint8_t entry_open;          /* SEQ: keys held since the first press of this entry */
    uint8_t step_env;            /* ENV pressed on STEP: tap opens ENV, hold + SELECT resizes */
    uint8_t step_env_used;       /* SELECT used: releasing ENV stays on STEP */
    uint8_t step_scl;            /* SCALE pressed on STEP: tap opens scales, hold + SELECT moves */
    uint8_t step_scl_used;       /* SELECT used: releasing SCALE stays on STEP */
    uint8_t step_oct, step_oct_used; /* STEP: OCT taps on release, held with FX undo/redo */
    uint8_t hot_col, hot_t;      /* column whose knob was just turned (drawn white) */
    uint8_t menu;                /* 0 off, 1 list, 2 about (HOME held) */
    uint8_t menu_sel;
    uint8_t midi_view;           /* SYSTEM knob 1: USB (0) / TRS (1) status view; both inputs stay active */
    uint32_t menu_sig, home_t0;  /* HOME press time (btn_hold) */
    uint8_t force;               /* full redraw pending */
    uint8_t msg_t;               /* transient message frames */
    uint8_t arm, arm_t;          /* destructive action armed: param id, frames left to confirm */
    uint32_t rec_t0;             /* REC press time (btn_hold) */
    uint32_t save_t0;            /* SAVE press time (btn_hold): a tap opens SAVE, a hold saves the project */
    uint8_t confirm;             /* 1 = "clear the sequence?" (REC held on SEQ / ARP), 2 = "clear track n?" (TRACKS) */
    uint8_t confirm_trk;         /* the track the dialog clears */
    uint8_t uslot;               /* SAVE > USER: the selected user preset slot */
    uint8_t edit_hold;           /* EDIT held: 1 it opened the family, 2 pressed on an EDIT page (a tap: next page) */
    uint8_t edit_used;           /* a key was used while EDIT was held (its release is no tap) */
    uint32_t edit_t0;            /* EDIT press time */
    uint8_t seq_hold;            /* SEQ held: 1 it opened the family, 2 pressed on a SEQ page (a tap: next page) */
    uint8_t seq_used;            /* a pattern key was used while SEQ was held (its release is no tap) */
    uint8_t pat_src;             /* SEQ + keys: the pattern key held + 1 (let go: picked; another key: copied there) */
    uint8_t pat_did;             /* it was copied: its release picks nothing */
    uint32_t seq_t0;             /* SEQ press time */
    char msg[24];
    uint32_t enc_t[NE];
    /* drawn-state cache */
    char col[4][32];
    char focus_l[8], focus_v[8], focus_u[8];   /* the touched column, shown large */
    uint32_t graph_sig, head_sig, foot_sig, frame;
    uint8_t graph_top;           /* the graph strip's top G_OY rows hold something */
} ui;
static uint16_t step_midi_held; /* MIDI notes held for the selected STEP entry */

static const page_t *cur_page(void) { return &PAGES[ui.page]; }

static uint32_t page_first(uint32_t fam)
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].fam == fam)
            return i;
    return 0;
}

/* transient message in the top bar: a + b */
static void ui_say(const char *a, const char *b)
{
    uint32_t n;
    str_cpy(ui.msg, a, sizeof ui.msg);
    n = str_len(ui.msg);
    str_cpy(ui.msg + n, b, sizeof ui.msg - n);
    ui.msg_t = 40;
}

static void ui_message(const char *s) { ui_say(s, ""); }

static void page_entered(void)
{
    const page_t *pg = cur_page();
    song.seq_mode = !ui.home && pg->fam == FAM_SEQ;
    ui.entry_open = 0;
    step_midi_held = 0;
    step_midi_r = step_midi_w;
    ui.hot_t = 0;                                /* the white value / focus box was the old page's */
    ui.force = 1;
    if (!ui.home && pg->graph == GR_FMSTORE && fm6_shown())
        fm6_slot = (uint8_t)fm6_store_slot(song.sel, fm6_slot);   /* the voice's own slot, or a free one */
}

static void page_go(uint32_t i)                  /* open page i (of its family) */
{
    ui.page = (uint8_t)i;
    ui.fam_last[PAGES[i].fam] = ui.page;
    ui.home = 0;
    page_entered();
}

#include "seq_edit.c"

/* SEQ cursor: wraps inside the pattern length, the bank follows, a step entry ends */
static void cursor_set(int32_t c)
{
    int32_t len = TSEL->p[P_SLEN] > 0 ? TSEL->p[P_SLEN] : 1;
    ui.cursor = (uint8_t)((c % len + len) % len);
    ui.bank = (uint8_t)(ui.cursor / 16u);
    ui.entry_open = 0;
    step_midi_held = 0;
}

static void cursor_fix(void)                           /* LEN got shorter: onto the last step */
{
    if (ui.cursor >= (uint32_t)TSEL->p[P_SLEN])
        cursor_set(TSEL->p[P_SLEN] - 1);
}

#include "seq_undo.c"

static int seq_record_follow(void)
{
    uint32_t idx;
    if (!song.seq_mode || !song.playing || !(song.rec & (1u << song.sel)))
        return 0;
    idx = TSEL->seq_pos == 0x7FFFFFFFu ? 0u : TSEL->seq_idx;
    if (ui.cursor != idx || ui.bank != idx / 16u || ui.entry_open || step_midi_held)
        cursor_set((int32_t)idx);
    return 1;
}

static void note_name(char *b, uint32_t n)
{
    str_cpy(b, N_NOTE[n % 12u], 4);
    fmt_int(b + str_len(b), (int32_t)(n / 12u) - 1);
}

static void open_family(uint32_t fam)
{
    if (fam == FAM_SEQ && (ui.home || cur_page()->fam != FAM_SEQ)) {
        uint32_t i = page_first(FAM_SEQ);
        while (i < NPAGES && PAGES[i].fam == FAM_SEQ && PAGES[i].graph != GR_STEPS)
            i++;
        page_go(i < NPAGES && PAGES[i].fam == FAM_SEQ ? i : page_first(FAM_SEQ));
        return;
    }
    if (!ui.home && cur_page()->fam == fam) {          /* same module button: its next control surface */
        uint32_t i = ui.page;
        do {
            if (++i >= NPAGES || PAGES[i].fam != fam)
                i = page_first(fam);
        } while (!page_shown(&PAGES[i]) && i != ui.page);
        page_go(i);
    } else
        page_go(page_first(fam));                      /* entering a module starts at its main control */
}

/* PRESETS navigates utility pages; in the musical workspaces it picks sounds or patterns. */
static int preset_pages(void)
{
    const page_t *pg = cur_page();
    return !ui.home && (pg->fam == FAM_GLO || pg->fam == FAM_SAVE) && pg->graph != GR_BROWSE;
}

static void page_scroll(int32_t direction)
{
    uint32_t fam = cur_page()->fam, i = ui.page;
    do {
        if (direction > 0)
            i = i + 1u < NPAGES && PAGES[i + 1u].fam == fam ? i + 1u : page_first(fam);
        else if (i > 0u && PAGES[i - 1u].fam == fam)
            i--;
        else {
            i = page_first(fam);
            while (i + 1u < NPAGES && PAGES[i + 1u].fam == fam)
                i++;
        }
    } while (!page_shown(&PAGES[i]) && i != ui.page);
    if (i != ui.page)
        page_go(i);
}

/* the page went away (another track or engine: the FM6 pages): to its family's first page */
static void page_fix(void)
{
    if (!ui.home && !page_shown(cur_page())) {
        ui.page = (uint8_t)page_first(cur_page()->fam);
        page_entered();
    }
}

static void go_home(void)
{
    ui.home = 1;
    ui.home_view = 0;
    ui.entry_open = 0;
    step_midi_held = 0;
    step_midi_r = step_midi_w;
    ui.hot_t = 0;
    song.seq_mode = 0;
    ui.force = 1;
}

static void home_view_step(int32_t direction)      /* notes, levels, pan, master effects (a HOME tap, SELECT) */
{
    ui.home_view = (uint8_t)((ui.home_view + (direction > 0 ? 1u : 3u)) % 4u);
    ui.hot_t = 0;
    ui.force = 1;
}

static void home_tap(void)
{
    if (ui.home)
        home_view_step(1);
    else
        go_home();
}

/* ------------------------------------------------------- track setup --- */
/* the parts' sounds at power-on (engine, preset): bass, pad, lead */
static const uint8_t TRK_DEF[NPART][2] = {{0, 4}, {1, 5}, {3, 0}};   /* ANALOG ACID, DIGITAL PAD, LOFI PULSE LD */
static uint32_t trk_def_engine(uint32_t i) { return i < NPART ? TRK_DEF[i][0] : 0u; }

static int seq_is_empty(const track_t *t)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        if (t->step[i].n)
            return 0;
    return 1;
}

/* A sequence that came from a user preset and was not touched since is replaced by the next
 * user preset's pattern; one the user recorded, edited or loaded from a project is kept.
 * (Factory presets bring no pattern: they never touch the steps.) */
static uint32_t pat_sig[NTRK];               /* seq_sig() right after a user preset's pattern was loaded */
static uint32_t seq_sig(const track_t *t)    /* FNV-1a over the steps and LEN */
{
    const uint8_t *b = (const uint8_t *)t->step;
    uint32_t i, h = 2166136261u ^ (uint32_t)(uint16_t)t->p[P_SLEN];
    for (i = 0; i < sizeof t->step; i++)
        h = (h ^ b[i]) * 16777619u;
    return h;
}
static int seq_replaceable(const track_t *t) { return seq_is_empty(t) || seq_sig(t) == pat_sig[trk_index(t)]; }

/* a user preset's 16 steps: absolute notes, 0 = rest; flags 1 = accent, 2 = slide, 4 = tie (holds the previous note) */
static void load_pat16(track_t *t, const uint8_t *note, const uint8_t *flags)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++) {
        step_t *s = &t->step[i];
        uint8_t n = i < 16u ? note[i] : 0, fl = i < 16u ? flags[i] : 0;
        s->note[0] = n;
        s->n = n ? 1 : 0;
        s->time = (fl & 4u) ? ST_TIE : n ? ST_NOTE : ST_REST;
        s->flags = n ? (fl & (SF_ACCENT | SF_SLIDE)) : 0;
        s->vel = n ? 96 : 0;
    }
    t->p[P_SLEN] = 16;
    pat_sig[trk_index(t)] = seq_sig(t);
}

static void track_defaults_steps(track_t *t)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        step_clear(&t->step[i]);
}

static int is_scale_setting(uint32_t id)
{
    return id == P_SCALE || id == P_QUANT || id == P_MPCDEG;
}

/* Scale, quantization and MPC degree belong to the song's three synth parts.
 * Drums keep their own slots and never participate in these settings. */
static void scale_setting_set(track_t *t, uint32_t id, int16_t value)
{
    uint32_t k;
    if (is_drum(t)) {
        t->p[id] = value;
        t->p[P_MPCDEG] = (int16_t)mpc_degree(t);
        return;
    }
    for (k = 0; k < NPART; k++) {
        trk[k].p[id] = value;
        trk[k].p[P_MPCDEG] = (int16_t)mpc_degree(&trk[k]);
    }
}

/* what loading a sound (factory or user preset) leaves alone: the mix (LEVEL, PAN, MUTE:
 * the TRACKS faders), scale/quantization and pattern parameters (LEN, DIV, SWING, GATE).
 * The SLICER is part of the sound: a factory preset turns it OFF (its defaults), a user preset brings its own */
static int param_kept(uint32_t i)
{
    return i == P_LEVEL || i == P_PAN || i == P_MUTE || is_scale_setting(i) ||
           (i >= P_SLEN && i <= P_SGATE) || (i >= P_CHMODE && i <= P_CHSPREAD);
}

/* preset pi of the engine the track asked for: the whole sound (not the pattern parameters) */
static void apply_preset_to(track_t *t, uint32_t pi)
{
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    uint32_t i;
    if (is_drum(t))
        return;
    panic_req |= (uint8_t)(1u << trk_index(t));       /* MONO/POLY may change: release what sounds */
    t->user = 0;
    if (t == TSEL)
        sync_reload = 1;
    if (!e->npresets)
        return;
    pi %= e->npresets;
    t->preset = (uint8_t)pi;
    if (e == &ENG_FM6)
        fm6_cur[trk_index(t)] = 0;                   /* its VOICE afresh: edits of the buffer go */
    for (i = 0; i < P_E0; i++)                        /* the rest of the sound to its defaults: a preset */
        if (!param_kept(i))
            t->p[i] = TP[i].def;                     /* sounds the same after any edit (not the pattern, not the mix) */
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = (int16_t)e->presets[pi].e[i];
    t->p[P_ATK] = e->presets[pi].env[0];
    t->p[P_DEC] = e->presets[pi].env[1];
    t->p[P_SUS] = e->presets[pi].env[2];
    t->p[P_REL] = e->presets[pi].env[3];
    t->p[P_ED_FLT] = e->presets[pi].fenv;
    t->p[P_VOICE] = !t->p[P_CHMODE] && e->presets[pi].mono ? V_LEGATO : V_POLY;   /* mono presets keep the legato feel */
    {   /* the rest of the patch: sends, arpeggiator */
        static const uint8_t FX_DEF[4] = {0, 24, 28, 36};
        const preset_t *pr = &e->presets[pi];
        for (i = 0; i < 4u; i++) {
            t->p[P_DIST + i] = (int16_t)(pr->fx[i] ? pr->fx[i] - 1 : FX_DEF[i]);
            t->p[P_AMODE + i] = (int16_t)(pr->arp[i] ? pr->arp[i] - 1 : TP[P_AMODE + i].def);
        }
    }
}

/* the engine's defaults and its first preset. With the audio IRQ off: the ISR sees the old engine with
 * its values or the new one with its own (voice.c engine_block), never one with the other's */
static void set_engine_of(track_t *t, uint32_t ei)
{
    const engine_t *e = ENGINES[ei % NENGINES];
    uint32_t i;
    if (is_drum(t))
        return;
    fm1_irq_off();
    t->eng_req = (uint8_t)(ei % NENGINES);
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = e->edit[i].def;
    apply_preset_to(t, 0);
    fm1_irq_on();
}

static void apply_preset(uint32_t pi) { apply_preset_to(TSEL, pi); }
static void set_engine(uint32_t ei) { set_engine_of(TSEL, ei); }

static void track_defaults(track_t *t)
{
    uint32_t i;
    for (i = 0; i < P_E0; i++)
        t->p[i] = TP[i].def;
    track_defaults_steps(t);
}

/* switch engine (its defaults + first preset) and say so */
static void select_engine(uint32_t e)
{
    if (is_drum(TSEL))
        return;
    set_engine(e);
    ui_say("ENGINE ", ENGINES[TSEL->eng_req]->name);
    ui.force = 1;
}

/* the presets of every engine, then the used user presets, as one list (the PRESETS knob and the PRESETS page browse it) */
static uint32_t preset_pos(uint32_t *total)          /* list index of the selected track's preset */
{
    uint32_t n = 0, cur = 0, e;
    for (e = 0; e < NENGINES; e++) {
        if (e == TSEL->eng_req)
            cur = n + TSEL->preset % (ENGINES[e]->npresets ? ENGINES[e]->npresets : 1u);
        n += ENGINES[e]->npresets;
    }
    if (user_of(TSEL) < UP_SLOTS)
        cur = n + up_rank(user_of(TSEL));
    *total = n + up_count();
    return cur;
}

/* list index n (< total) -> engine, *k its preset; NENGINES = user preset, *k its slot */
static uint32_t preset_at(uint32_t n, uint32_t *k)
{
    uint32_t e;
    for (e = 0; e < NENGINES && n >= ENGINES[e]->npresets; e++)
        n -= ENGINES[e]->npresets;
    *k = e < NENGINES ? n : up_nth(n);
    return e;
}

static void preset_go(uint32_t n)                    /* load list index n into the selected track */
{
    uint32_t k, e = preset_at(n, &k);
    if (is_drum(TSEL))
        return;                                      /* one GM kit: nothing to browse */
    if (e == NENGINES) {
        up_load(k);
        return;
    }
    if (e != TSEL->eng_req)
        select_engine(e);
    apply_preset(k);
    ui.force = 1;
}

/* select track i (KNOB 1 on TRACKS, the editor): its sound, pages and pattern from now on */
static void track_select(uint32_t i)
{
    if (i >= NTRK || i == song.sel)
        return;
    step_history_clear();
    song.sel = (uint8_t)i;
    ui.entry_open = 0;
    step_midi_held = 0;
    step_midi_r = step_midi_w;
    ui.cursor = 0;
    ui.bank = 0;
    sync_reload = 1;
    ui.force = 1;
}
