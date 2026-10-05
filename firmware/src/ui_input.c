/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Melodee UI input: LEDs, knobs and buttons, SEQ step entry, panel setup. */
/* ----------------------------------------------------------- LEDs --- */
/* The LED picture is built off-line and copied one byte per column: clearing
 * and relighting would let the 10 kHz scan catch the dark gap and flicker. */
static uint8_t led_pos[41];                        /* (col << 3) | row bit, 0xFF = none */

static void led_pos_init(void)
{
    uint32_t id, p, r;
    for (id = 0; id < 41u; id++) {
        led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (FM1_KEYMAP[r][p] == (int8_t)id)
                    led_pos[id] = (uint8_t)((p << 3) | r);
    }
}

static void led_put(uint8_t *nl, uint32_t id, int on)
{
    uint8_t q = led_pos[id];
    if (q != 0xFF && on)
        nl[q >> 3] |= (uint8_t)(1u << (q & 7u));
}

static const uint8_t FAM_BTN[FAM_COUNT] = {B_HOME, B_ENV, B_LFO, B_FX, B_SCL, B_EDIT, B_GLO, B_SAVE,
                                           B_ARP, B_SEQ, B_REC};   /* button of each page family */

static uint32_t cur_fam(void) { return ui.home ? FAM_HOME : cur_page()->fam; }

/* ---------------------------------------------------- EDIT + keys --- */
/* EDIT held: black keys jump to deeper sound controls. White keys recall the
 * first eight sounds of the active engine (FM6 keeps its six operator keys). */
#define NAV_N 11u
static const uint8_t NAV_BLACK[NAV_N] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};   /* F#3 .. F#5 */
static const uint8_t NAV_WHITE[6] = {0, 2, 4, 6, 7, 9};                              /* F3 .. D4: OP1..OP6 */
static const uint8_t SOUND_KEY[8] = {0, 2, 4, 6, 7, 9, 11, 12};                       /* first eight sounds */
static const char *const NAV_PAGE[NAV_N][2] = {
    {"EDIT 1", "EDIT 2"}, {"STORE", 0}, {"ALGO", 0}, {"FREQ", 0}, {"OUT", 0}, {"EG RATE", "EG LVL"},
    {"SCALE", "CURVE"}, {"PITCH EG", "PITCH LV"}, {"FM LFO", "FM LFO 2"}, {"VOICE", "VOICE 2"},
    {"FM BEND", "FM PORTA"}};

static uint8_t nav_page(uint32_t g, uint32_t j)   /* page index of NAV_PAGE[g][j], 0xFF = none */
{
    static uint8_t pg[NAV_N][2], ready;
    uint32_t a, b, i;
    if (!ready) {
        for (a = 0; a < NAV_N; a++)
            for (b = 0; b < 2u; b++) {
                pg[a][b] = 0xFF;
                for (i = 0; NAV_PAGE[a][b] && i < NPAGES; i++)
                    if (PAGES[i].fam == FAM_EDIT && str_eq(PAGES[i].title, NAV_PAGE[a][b]))
                        pg[a][b] = (uint8_t)i;
            }
        ready = 1;
    }
    return pg[g][j] != 0xFF && page_shown(&PAGES[pg[g][j]]) ? pg[g][j] : 0xFF;
}

static int nav_held(void) { return kb_nav_btn && (fm1_in.buttons & kb_nav_btn); }

static void nav_keys(uint32_t pressed)
{
    uint32_t k, g;
    for (k = 0; k < 27u; k++) {
        if (!((pressed >> k) & 1u))
            continue;
        ui.edit_used = 1;
        for (g = 0; g < NAV_N; g++) {
            uint8_t a = nav_page(g, 0), b = nav_page(g, 1);
            if (NAV_BLACK[g] != k)
                continue;
            if (ui.page == a && b != 0xFF)
                page_go(b);
            else if (a != 0xFF || b != 0xFF)
                page_go(a != 0xFF ? a : b);
        }
        if (fm6_shown()) {
            for (g = 0; g < 6u; g++)
                if (NAV_WHITE[g] == k) {
                    fm6_opsel = (uint8_t)g;
                    ui.force = 1;
                }
        } else if (!is_drum(TSEL)) {
            const engine_t *e = ENGINES[TSEL->eng_req % NENGINES];
            for (g = 0; g < 8u && g < e->npresets; g++)
                if (SOUND_KEY[g] == k) {
                    apply_preset(g);
                    ui.force = 1;
                }
        }
    }
}

static void nav_leds(uint8_t *nl)
{
    uint32_t g, blink = (fm1_ticks() / (150000u * FM1_TICKS_PER_US)) & 1u;
    for (g = 0; g < NAV_N; g++) {
        uint8_t a = nav_page(g, 0), b = nav_page(g, 1);
        led_put(nl, 14u + NAV_BLACK[g], (a != 0xFF || b != 0xFF) && (blink || (ui.page != a && ui.page != b)));
    }
    for (g = 0; g < 6u && fm6_shown(); g++)
        led_put(nl, 14u + NAV_WHITE[g], blink || fm6_opsel != g);
    if (!fm6_shown() && !is_drum(TSEL)) {
        const engine_t *e = ENGINES[TSEL->eng_req % NENGINES];
        for (g = 0; g < 8u && g < e->npresets; g++)
            led_put(nl, 14u + SOUND_KEY[g], g == TSEL->preset ? (int)blink : 1);
    }
}

/* ------------------------------------------------------ SEQ + keys --- */
/* SEQ held on a SEQ page: the white keys F3..F4 are the selected track's patterns 1..8. A tap
 * picks one (seq.c: when the track's loop ends, at once while stopped); holding one and pressing
 * another copies the held one there. Lit = it holds notes, blinking = playing, fast = queued. */
static const uint8_t PAT_KEY[NPAT] = {0, 2, 4, 6, 7, 9, 11, 12};   /* F3 .. F4 */

static void pat_copy(track_t *t, uint32_t a, uint32_t b)   /* pattern a (steps, LEN etc.) onto b */
{
    pattern_t *pb = pat_bank[trk_index(t)];
    int16_t set[4];
    uint32_t i;
    fm1_irq_off();                                      /* (the ISR switches patterns) */
    for (i = 0; i < 4u; i++)
        set[i] = a == t->pat ? t->p[P_SLEN + i] : pb[a].set[i];
    memcpy(pat_steps(t, b), pat_steps(t, a), sizeof t->step);
    for (i = 0; i < 4u; i++)
        if (b != t->pat)
            pb[b].set[i] = set[i];
        else if (set[0])                                /* (a never played: b keeps its own) */
            t->p[P_SLEN + i] = set[i];
    fm1_irq_on();
}

static void pat_keys(uint32_t pressed)
{
    uint32_t k;
    for (k = 0; k < NPAT; k++) {
        if (!((pressed >> PAT_KEY[k]) & 1u))
            continue;
        ui.seq_used = 1;
        if (ui.pat_src && ((fm1_in.notes >> PAT_KEY[ui.pat_src - 1u]) & 1u)) {   /* one held: copy it here */
            if (k + 1u != ui.pat_src) {
                char b[16] = "1 > 1 COPIED";
                b[0] = (char)('0' + ui.pat_src);
                b[4] = (char)('1' + k);
                pat_copy(TSEL, ui.pat_src - 1u, k);
                ui_say("PATTERN ", b);
                ui.force = 1;
            }
            ui.pat_did = 1;
        } else {
            ui.pat_src = (uint8_t)(k + 1u);
            ui.pat_did = 0;
        }
    }
}

static void pat_release(void)                           /* the picked key let go (SEQ may be up): pick it */
{
    track_t *t = TSEL;
    uint32_t k = ui.pat_src - 1u;
    char b[8] = "1";
    if ((fm1_in.notes >> PAT_KEY[k]) & 1u)
        return;
    ui.pat_src = 0;
    if (ui.pat_did)
        return;
    t->pat_q = (uint8_t)(k == t->pat ? 0u : k + 1u);   /* the one playing: a queued switch is dropped */
    b[0] = (char)('1' + k);
    if (t->pat_q && song.playing)
        str_cpy(b + 1, " NEXT", 7);                     /* (seq.c: at the end of the track's loop) */
    ui_say("PATTERN ", b);
    ui.force = 1;
}

static void pat_leds(uint8_t *nl)
{
    track_t *t = TSEL;
    uint32_t k, tk = fm1_ticks() / (75000u * FM1_TICKS_PER_US);
    for (k = 0; k < NPAT; k++)
        led_put(nl, 14u + PAT_KEY[k], k + 1u == t->pat_q ? (int)(tk & 1u)
                                      : k == t->pat     ? (int)((tk >> 1) & 1u)
                                                        : pat_used(t, k));
}

/* The playing layout follows the selected track. OFF leaves pitches chromatic,
 * so mark the keys in ROOT/SCL without changing what they sound. TRN and the
 * octave buttons shift both the note and the scale by the same interval.
 * A key is bright while it is held or its pitch sounds from MIDI in (live_refs),
 * the rest of the layout dim (Settings > KEYS); GLO > LIGHTS KEYS OFF: none lit. */
enum { KL_OFF, KL_DIM, KL_ON };
static uint32_t play_key_led(const track_t *t, uint32_t k)
{
    uint32_t n = kb_map(t, k), ti = trk_index(t);
    int32_t degree;
    if ((((fm1_in.notes >> k) & 1u) && kb_trk[k] == ti && kb_note[k] != KB_SILENT) ||
        (n != KB_SILENT && live_refs[ti][n]))
        return KL_ON;
    if (is_drum(t) || is_gm_sample(t) || is_slice(t))
        return KL_DIM;
    if (t->p[P_QUANT] == Q_OFF) {
        degree = (53 + (int32_t)k - t->p[P_ROOT] + 120) % 12;
        return (scale_mask(t) >> (uint32_t)degree) & 1u ? KL_DIM : KL_OFF;
    }
    return n != KB_SILENT ? KL_DIM : KL_OFF;
}

static const uint8_t KEYS_DIM_MASK[KEYS_N] = {0, 7, 3, 1, 0};   /* frames lit: -, 1/8, 1/4, 1/2, all */
#define BTN_DIM_MASK 3u                                           /* idle buttons: 1/4 of the frames */

/* Every button glows dim and is bright while held or engaged: its page family,
 * PLAY (blinking), REC, a shifted octave. The keys show the playing layout
 * (play_key_led) or, EDIT / SEQ held, their shortcuts. */
static void ui_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0}, nd[FM1_NCOL] = {0}, nb[FM1_NCOL] = {0};
    uint32_t k, c, b;
    uint32_t fam = cur_fam(), lvl = settings.keys & ~KEYS_DARK;
    static uint8_t ready;
    if (!ready) {
        led_pos_init();
        ready = 1;
    }
    for (b = 0; b < NB; b++) {
        led_put(nb, panel.btn[b], 1);
        led_put(nl, panel.btn[b], (int)((fm1_in.buttons >> panel.btn[b]) & 1u));
    }
    led_put(nl, panel.btn[FAM_BTN[fam]], 1);
    if (fam == FAM_EDIT || fam == FAM_ENV || fam == FAM_LFO || fam == FAM_FX || fam == FAM_SCL || fam == FAM_ARP)
        led_put(nl, panel.btn[B_EDIT], 1);            /* instrument workspace remains visible */
    led_put(nl, panel.btn[B_PLAY], song.playing && ((song.tick / 64u) & 1u) == 0u);   /* blinks: intended */
    led_put(nl, panel.btn[B_REC], song.rec != 0u);
    led_put(nl, panel.btn[B_OCTDN], song.octave < 0);
    led_put(nl, panel.btn[B_OCTUP], song.octave > 0);
    if (nav_held() && cur_fam() == FAM_SEQ)            /* SEQ + keys: the patterns */
        pat_leds(nl);
    else if (nav_held())                                /* EDIT + keys: the key map */
        nav_leds(nl);
    else if (!(settings.keys & KEYS_DARK))
        for (k = 0; k < 27u; k++) {
            uint32_t lv = play_key_led(TSEL, k);
            led_put(nl, 14u + k, lv == KL_ON);
            led_put(nd, 14u + k, lv == KL_DIM && lvl != KEYS_OFF);
        }
    fm1_led_dim_mask[0] = KEYS_DIM_MASK[lvl];
    fm1_led_dim_mask[1] = BTN_DIM_MASK;
    for (c = 0; c < FM1_NCOL; c++) {
        fm1_led_dim[0][c] = (uint8_t)(nd[c] | nl[c]);   /* first: a key going dim <-> bright never goes dark */
        fm1_led_dim[1][c] = nb[c];
        fm1_led[c] = nl[c];
    }
}

/* ---------------------------------------------------------- input --- */
static int32_t accel(uint32_t role, int32_t s, int32_t range)
{
    uint32_t now = fm1_ticks(), dt = now - ui.enc_t[role];
    ui.enc_t[role] = now;
    if (range > 40 && dt < 60u * 1000u * FM1_TICKS_PER_US)
        return s * (range > 150 ? 6 : 3);
    return s;
}

/* TRACKS page: KNOB 1 TRACK, 2 LEVEL (0 = mute; the drum track: GLO > DRUMS LEVEL),
 * 3 LEN of its pattern, 4 PAN. A track muted with MUTE (VOICE 2, the editor): the first
 * turn of KNOB 2 unmutes it (the drum track has no VOICE 2 page to do that) */
static void tracks_edit(uint32_t slot, int32_t steps)
{
    track_t *t = TSEL;
    int16_t *vp;
    const param_desc_t *d;
    switch (slot) {
    case 0:
        track_select((uint32_t)clamp((int32_t)song.sel + (steps > 0 ? 1 : -1), 0, NTRK - 1));
        return;
    case 1:
        if (t->p[P_MUTE]) {
            t->p[P_MUTE] = 0;
            return;
        }
        vp = is_drum(t) ? &song.g[G_DRLVL] : &t->p[P_LEVEL];
        d = is_drum(t) ? &GP[G_DRLVL] : &TP[P_LEVEL];
        break;
    case 2:
        vp = &t->p[P_SLEN];
        d = &TP[P_SLEN];
        break;
    default:
        vp = &t->p[P_PAN];
        d = &TP[P_PAN];
        break;
    }
    *vp = (int16_t)clamp(*vp + accel(EN_K1 + slot, steps, d->max - d->min), d->min, d->max);
}

/* REC tap on TRACKS: arm / disarm live recording on the selected track; arming while
 * stopped starts the transport too */
static void tracks_rec_tap(void)
{
    uint8_t bit = (uint8_t)(1u << song.sel);
    song.rec ^= bit;
    if ((song.rec & bit) && !song.playing)
        transport_req = 1;
}

static void step_length_edit(int32_t steps);

static void step_edit(uint32_t slot, int32_t steps)
{
    step_t *st = &TSEL->step[ui.cursor];
    uint32_t i;
    switch (slot) {
    case 0:                                               /* STEP cursor; PRESETS shapes a held note */
        if (!ui.entry_open)
            cursor_set(ui.cursor + steps);
        break;
    case 1:                                               /* NOTE: transpose the step */
        if (!st->n) {
            st->note[0] = last_note;
            st->n = 1;
            st->time = ST_NOTE;
            break;
        }
        for (i = 0; i < st->n; i++)
            st->note[i] = (uint8_t)clamp(st->note[i] + steps, 1, 127);
        st->time = ST_NOTE;
        last_note = st->note[0];
        break;
    case 2:
        st->time = (uint8_t)clamp((int32_t)st->time + (steps > 0 ? 1 : -1), ST_NOTE, ST_REST);
        break;
    default: {                                            /* FLAG: - / ACC / SLD / A+S */
        uint32_t f = (st->flags & SF_ACCENT ? 1u : 0u) | (st->flags & SF_SLIDE ? 2u : 0u);
        f = (uint32_t)clamp((int32_t)f + (steps > 0 ? 1 : -1), 0, 3);
        st->flags = (uint8_t)((st->flags & ~(SF_ACCENT | SF_SLIDE)) | (f & 1u ? SF_ACCENT : 0u) | (f & 2u ? SF_SLIDE : 0u));
        break;
    }
    }
}

/* PRESETS while entering a note resizes it from its onset. */
static void step_length_edit(int32_t steps)
{
    track_t *t = TSEL;
    uint32_t start = step_note_start(t, ui.cursor), n;
    int32_t wanted;
    if (is_drum(t)) {
        ui_message("DRUMS: ONE SHOT");
        return;
    }
    if (start == NSTEP) {
        ui_message("SELECT A NOTE");
        return;
    }
    wanted = clamp((int32_t)step_note_length(t, start) + steps, 1, (int32_t)step_pattern_len(t));
    n = step_note_resize(t, start, wanted);
    ui.cursor = (uint8_t)start;
    ui.bank = (uint8_t)(start / 16u);
    ui.hot_t = 0;                                      /* show the length hint, even with ZOOM on */
    if (n < (uint32_t)wanted)
        ui_message("NEXT NOTE");
}

/* SAVE > PROJECT BOOT, GLO > LIGHTS: kept in flash once the knob rests (set_save), not on every detent */
static uint32_t set_t;                                    /* fm1_ms of the last change | 1, 0 = saved */
static void set_save(void)
{
    if (set_t && fm1_ms - set_t > 1500u) {
        set_t = 0;
        settings_save();                                  /* (nothing to write if it is back where it was) */
    }
}

static void edit_param(uint32_t slot, int32_t steps)
{
    int16_t *vp;
    const page_t *pg = cur_page();
    const param_desc_t *d;
    uint32_t id = pg->id[slot];
    int32_t v;
    if (is_drum(TSEL) && !page_for_drum(pg))
        return;                                           /* "DRUM TRACK": nothing to edit here */
    if (pg->scope == SC_GLOBAL && id == G_MIDI) {
        if (steps)
            ui.midi_view = steps > 0 ? 1u : 0u;
        return;                                           /* display selection, not an input filter */
    }
    if (pg->scope == SC_GLOBAL && id == G_BOOT) {        /* OFF, 1..4: the project power-on loads */
        if (steps) {
            settings.boot = (uint32_t)clamp((int32_t)settings.boot + (steps > 0 ? 1 : -1), 0, 4);
            set_t = fm1_ms | 1u;
        }
        return;
    }
    if (pg->scope == SC_GLOBAL && id == G_LIGHTS) {
        uint32_t keys = steps > 0 ? settings.keys & ~KEYS_DARK : settings.keys | KEYS_DARK;
        if (steps && keys != settings.keys) {             /* right = ON, left = OFF */
            settings.keys = keys;
            set_t = fm1_ms | 1u;
        }
        return;
    }
#if MELODEE_USB_AUDIO
    if (pg->scope == SC_GLOBAL && (id == G_USBOUT || id == G_USBIN)) {
        if (steps)                                        /* right = ON, left = OFF; the host follows */
            ua_off_set(id == G_USBOUT ? UA_OFF_OUT : UA_OFF_IN, steps > 0, fm1_ms);   /* (main.c) */
        return;
    }
#endif
    if (pg->scope == SC_STEP) {
        step_edit(slot, steps);
        return;
    }
    if (pg->scope == SC_TRK) {
        tracks_edit(slot, steps);
        return;
    }
    if (pg->graph == GR_BROWSE) {                         /* KNOB 1: one preset, KNOB 2: the next / previous engine */
        if (slot == 0u && !is_drum(TSEL)) {
            uint32_t total, cur = preset_pos(&total);
            if (total)
                preset_go((cur + (steps > 0 ? 1u : total - 1u)) % total);
        } else if (slot == 1u && !is_drum(TSEL)) {
            select_engine((TSEL->eng_req + (steps > 0 ? 1u : NENGINES - 1u)) % NENGINES);
        }
        return;
    }
    if (pg->graph == GR_FMSTORE) {                        /* FM6: KNOB 1 user slot; STORE / SEND / INIT: GO buttons */
        static const char *const FM_GO[3] = {"STORE", "SEND", "INIT"};
        if (!fm6_shown())
            return;
        if (slot == 0u) {
            fm6_slot = (uint8_t)clamp((int32_t)fm6_slot + steps, 0, FM6_NUSER - 1);
            ui.arm = 0;
            return;
        }
        if (steps <= 0)
            return;
        if (ui.arm != 0xE8u + slot) {                     /* one detent arms, a second one within ~1.5 s acts */
            ui.arm = (uint8_t)(0xE8u + slot);
            ui.arm_t = 90;
            ui_say("AGAIN: ", FM_GO[slot - 1u]);
            return;
        }
        ui.arm = 0;
        if (slot == 1u)
            fm6_store(fm6_slot);
        else if (slot == 2u)
            fm6_send();
        else
            fm6_init_voice();
        ui.force = 1;
        return;
    }
    if (pg->graph == GR_USER) {                           /* KNOB 1 slot; LOAD / ERASE / SAVE: GO buttons */
        static const char *const UP_GO[3] = {"LOAD", "ERASE", "SAVE"};
        if (slot == 0u) {
            ui.uslot = (uint8_t)clamp((int32_t)ui.uslot + steps, 0, UP_SLOTS - 1);
            ui.arm = 0;
            return;
        }
        if (steps <= 0)
            return;
        if (ui.arm != 0xE0u + slot) {                     /* one detent arms, a second one within ~1.5 s acts */
            ui.arm = (uint8_t)(0xE0u + slot);
            ui.arm_t = 90;
            ui_say("AGAIN: ", UP_GO[slot - 1u]);
            return;
        }
        ui.arm = 0;
        up_ui(slot - 1u, ui.uslot);
        return;
    }
    d = page_desc(pg, slot, &vp);
    if (!d || !vp || d->max == d->min)
        return;
    v = clamp(*vp + accel(EN_K1 + slot, steps, d->max - d->min), d->min, d->max);
    *vp = (int16_t)v;
    if (pg->scope == SC_TRACK && is_scale_setting(id))
        scale_setting_set(TSEL, id, (int16_t)v);
    if (!v)
        return;
    if (pg->scope == SC_GLOBAL && (id == G_LOAD || id == G_SAVE || id == G_CLRSEQ || id == G_INITSND) &&
        ui.arm != id) {                                   /* one detent arms, a second one within ~1.5 s acts */
        *vp = 0;
        ui.arm = (uint8_t)id;
        ui.arm_t = 90;
        ui_say("AGAIN: ", (id == G_LOAD || id == G_SAVE) && song.g[G_SLOT] == PROJ_TMPL
                              ? (id == G_LOAD ? "LOAD TEMPLATE" : "SAVE AS TEMPLATE") : d->label);
        return;
    }
    ui.arm = 0;
    if (pg->scope != SC_GLOBAL)
        return;
    switch (id) {                                         /* GO buttons: act, then back to 0 */
    case G_LOAD:
        *vp = 0;
        if (song.g[G_SLOT] == PROJ_TMPL)
            template_load();                              /* a new project: the template's sounds */
        else
            project_load((uint32_t)song.g[G_SLOT] - 1u);
        break;
    case G_SAVE:
        *vp = 0;
        if (song.g[G_SLOT] == PROJ_TMPL)
            template_save();                              /* save as template: everything but the patterns */
        else
            project_save((uint32_t)song.g[G_SLOT] - 1u);
        break;
    case G_CLRSEQ:
        *vp = 0;
        track_defaults_steps(TSEL);
        ui_message("PATTERN CLEARED");
        break;
    case G_INITSND:
        *vp = 0;
        set_engine(TSEL->eng_req);                              /* engine defaults + its first preset */
        ui_message("SOUND INIT");
        ui.force = 1;
        break;
    default:
        break;
    }
}

/* SEQ step entry from either the panel keys or an incoming MIDI note. */
static void seq_entry_note(uint32_t note)
{
    track_t *t = TSEL;
    step_t *st = &t->step[ui.cursor];
    uint32_t i;
    if (!ui.entry_open) {
        ui.entry_open = 1;
        st->n = 0;
        st->time = ST_NOTE;
    }
    if (t->p[P_VOICE] != V_POLY) {
        st->note[0] = (uint8_t)note;
        st->n = 1;
    } else {
        for (i = 0; i < st->n && st->note[i] != note; i++)
            ;
        if (i == st->n && st->n < 4u)
            st->note[st->n++] = (uint8_t)note;
    }
    last_note = (uint8_t)note;
}

/* The audio ISR owns MIDI routing; the UI takes its mapped note edges here. */
static void seq_midi_events(int accept)
{
    if (step_midi_overflow) {
        step_midi_r = step_midi_w;
        step_midi_overflow = 0;
        step_midi_held = 0;
        ui.entry_open = 0;
        if (accept)
            ui_message("MIDI INPUT BUSY");
        return;
    }
    while (step_midi_r != step_midi_w) {
        uint32_t edge = step_midi_q[step_midi_r++ % STEP_MIDI_Q];
        if (!accept || ((edge >> 8) & 3u) != song.sel)
            continue;
        if (edge & (1u << 11)) {
            step_midi_held = 0;
        } else if (edge & (1u << 10)) {
            if (step_midi_held < 0xFFFFu)
                step_midi_held++;
            seq_entry_note(edge & 127u);
        } else if (step_midi_held) {
            step_midi_held--;
        }
    }
}

/* The keys pressed together (POLY: up to 4, MONO: the last one) become the
 * cursor step; releasing all keys moves past its ties. */
static void seq_entry(uint32_t pressed)
{
    uint32_t k;
    for (k = 0; k < 27u; k++) {
        uint32_t note;
        if (!((pressed >> k) & 1u))
            continue;
        note = kb_map(TSEL, k);
        if (note == KB_SILENT)
            continue;
        seq_entry_note(note);
    }
}

/* HOME / REC: tap on release, hold 0.7 s fires once. t0 = press time | 1,
 * bit 1 = fired (or swallowed: then the release is no tap either) */
enum { BT_NONE, BT_TAP, BT_HOLD };
static uint32_t btn_hold(uint32_t *t0, uint32_t label, uint32_t now, int hold_ok)
{
    uint32_t tap;
    if ((fm1_in.buttons >> panel.btn[label]) & 1u) {
        if (!*t0)
            *t0 = (now | 1u) & ~2u;
        else if (hold_ok && !(*t0 & 2u) && now - (*t0 & ~3u) > 700u * 1000u * FM1_TICKS_PER_US) {
            *t0 |= 2u;
            return BT_HOLD;
        }
        return BT_NONE;
    }
    tap = *t0 && !(*t0 & 2u);
    *t0 = 0;
    return tap ? BT_TAP : BT_NONE;
}

static void ui_input(void)
{
    uint32_t pressed = fm1_input_edges(0), notes = fm1_input_note_edges(), now = fm1_ticks(), id, b, k, fam = cur_fam();
    uint32_t home = btn_hold(&ui.home_t0, B_HOME, now, 1);
    uint32_t rec = btn_hold(&ui.rec_t0, B_REC, now, !ui.menu);
    uint32_t save = btn_hold(&ui.save_t0, B_SAVE, now, !ui.menu && !ui.confirm);
    int32_t s;
    set_save();
    if (home == BT_HOLD) {                              /* HOME held: open the menu, or leave it */
        if (ui.menu) {
            menu_close();
        } else {
            ui.menu = 1;
            ui.menu_sel = 0;
            ui.confirm = 0;                             /* (a clear dialog is cancelled) */
            ui.force = 1;
            song.seq_mode = 0;
        }
    }
    if (ui.menu || ui.confirm)
        kb_nav_btn = ui.edit_hold = ui.seq_hold = ui.pat_src = 0;   /* (no EDIT / SEQ + keys there) */
    if (ui.menu) {                                      /* HOME / REC taps do nothing here */
        seq_midi_events(0);
        step_midi_held = ui.entry_open = 0;
        if (ui.rec_t0)
            ui.rec_t0 |= 2u;                            /* a REC press in the menu is no tap later */
        if (ui.save_t0)
            ui.save_t0 |= 2u;                           /* (nor a SAVE press) */
        if (!ui.home_t0)
            menu_input(pressed);
        return;
    }
    if (rec == BT_HOLD) {
        if (fam != FAM_TRK) {
            open_family(FAM_TRK);                      /* long REC: track setup and pattern mixer */
        } else {
            ui.confirm = 2;                            /* TRACKS: clear all patterns of this track */
            ui.confirm_trk = song.sel;
            ui.force = 1;
        }
    } else if (rec == BT_TAP && !ui.confirm)
        tracks_rec_tap();                               /* arm and roll from any musical workspace */
    if (ui.confirm) {                                   /* OCT- cancels, OCT+ clears; nothing else reacts */
        seq_midi_events(0);
        step_midi_held = ui.entry_open = 0;
        if ((pressed >> panel.btn[B_OCTUP]) & 1u) {
            track_t *t = &trk[ui.confirm_trk % NTRK];
            track_defaults_steps(t);                    /* SEQ / ARP: the pattern playing */
            if (ui.confirm == 2) {                      /* TRACKS: every pattern of the track */
                fm1_irq_off();
                pat_clear_bank(t);
                fm1_irq_on();
            }
            t->nheld = 0;                               /* and the latched arp chord */
            t->arp_phys = 0;
            ui.force = 1;
            if (ui.confirm == 2) {
                char b[12] = "1 CLEARED";
                b[0] = (char)('1' + ui.confirm_trk);
                ui_say("TRACK ", b);
            } else {
                ui_message("SEQUENCE CLEARED");
            }
            ui.confirm = 0;
        } else if ((pressed >> panel.btn[B_OCTDN]) & 1u) {
            ui.confirm = 0;
            ui.force = 1;
        }
        enc_drop();
        return;
    }
    if (home == BT_TAP)                                 /* HOME: notes/scope, levels, pan, master effects */
        home_tap();
    if (save == BT_HOLD)                                /* SAVE held, on any page: the project back to its slot */
        project_quick_save();
    else if (save == BT_TAP)                            /* tapped: the library (on release, as HOME) */
        open_family(FAM_SAVE);
    cursor_fix();                                       /* LEN may have changed (knob, editor, load) */
    page_fix();                                         /* the track or its engine changed: FM6 pages */
    seq_record_follow();
    if (ui.edit_hold && !((fm1_in.buttons >> panel.btn[B_EDIT]) & 1u)) {   /* EDIT let go */
        if ((ui.edit_hold & 3u) == 2u && !ui.edit_used && now - ui.edit_t0 < 500u * 1000u * FM1_TICKS_PER_US)
            open_family(FAM_EDIT);                      /* a tap on an EDIT page: the next page */
        ui.edit_hold = 0;
    } else if (ui.edit_hold && !(ui.edit_hold & 4u) && !ui.edit_used && now - ui.edit_t0 > 400u * 1000u * FM1_TICKS_PER_US) {
        ui_message(fm6_shown() ? "BLACK: PAGE  WHITE: OP" : "WHITE: SOUND BLACK: PAGE");
        ui.edit_hold |= 4u;
    }
    if (ui.seq_hold && !((fm1_in.buttons >> panel.btn[B_SEQ]) & 1u)) {   /* SEQ let go */
        if ((ui.seq_hold & 3u) == 2u && !ui.seq_used && now - ui.seq_t0 < 500u * 1000u * FM1_TICKS_PER_US)
            open_family(FAM_SEQ);                       /* a tap on a SEQ page: the next page */
        ui.seq_hold = 0;
    } else if (ui.seq_hold && !(ui.seq_hold & 4u) && !ui.seq_used && now - ui.seq_t0 > 400u * 1000u * FM1_TICKS_PER_US) {
        ui_message("WHITE KEYS: PATTERNS");             /* held: what the keys do */
        ui.seq_hold |= 4u;
    }
    for (id = 0; id < 14u; id++) {
        if (!((pressed >> id) & 1u))
            continue;
        b = panel_btn_of(id);
        switch (b) {
        case B_PLAY:
            if ((fm1_in.buttons >> panel.btn[B_REC]) & 1u) {
                song.rec |= (uint8_t)(1u << song.sel);  /* REC + PLAY: punch in / start */
                if (!song.playing)
                    transport_req = 1;
                ui.rec_t0 |= 2u;                       /* REC release is not another tap */
            } else
                transport_req = song.playing ? 2 : 1;
            break;
        case B_REC:                                     /* tap / hold: above */
        case B_SAVE:
            break;
        case B_OCTDN:
        case B_OCTUP: {
            if (cur_fam() == FAM_SEQ) {
                cursor_set((int32_t)ui.cursor + (b == B_OCTDN ? -1 : 1));
                ui.force = 1;
                break;
            }
            uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
            if ((fm1_in.buttons & both) == both)
                song.octave = 0;
            else
                song.octave += b == B_OCTDN ? (song.octave > -3 ? -1 : 0) : (song.octave < 3 ? 1 : 0);
            break;
        }
        case B_SEQ:
            ui.seq_t0 = now;                            /* held: SEQ + keys (pat_keys) */
            ui.seq_used = 0;
            if (!ui.home && cur_page()->fam == FAM_SEQ) {   /* on a SEQ page the next one comes on release */
                ui.seq_hold = 2;
                break;
            }
            ui.seq_hold = 1;
            open_family(FAM_SEQ);
            break;
        case B_EDIT:
            ui.edit_t0 = now;                           /* held: EDIT + keys (nav_keys) */
            ui.edit_used = 0;
            if (!ui.home && cur_page()->fam == FAM_EDIT) {   /* on an EDIT page the next one comes on release */
                ui.edit_hold = 2;
                break;
            }
            ui.edit_hold = 1;
            /* fall through */
        case B_FX:
            if (b == B_FX && cur_fam() == FAM_SEQ && cur_page()->scope == SC_STEP) {
                step_clear(&TSEL->step[ui.cursor]);
                cursor_set(ui.cursor + 1);
                ui_message("STEP CLEARED");
                break;
            }
            /* fall through */
        default: {                                      /* page family buttons (HOME, REC: above) */
            uint32_t f;
            for (f = FAM_HOME + 1u; f < FAM_COUNT; f++)
                if (FAM_BTN[f] == b)
                    open_family(f);
            break;
        }
        }
    }
    kb_nav_btn = ui.home                         ? 0u
                 : cur_page()->fam == FAM_EDIT ? 1u << panel.btn[B_EDIT]
                 : cur_page()->fam == FAM_SEQ  ? 1u << panel.btn[B_SEQ]
                                               : 0u;
    if (nav_held()) {                                   /* EDIT + keys: pages and operators; SEQ + keys: */
        if (cur_fam() == FAM_SEQ)                       /* patterns; no notes */
            pat_keys(notes);
        else
            nav_keys(notes);
        notes = 0;
    }
    if (ui.pat_src)
        pat_release();
    if (song.seq_mode && cur_page()->scope == SC_STEP && !seq_record_follow()) {
        seq_entry(notes);
        seq_midi_events(1);
    } else {
        seq_midi_events(0);
    }

    if ((s = panel_enc(EN_PRESET)) != 0) {
        if (!ui.home && (cur_page()->scope == SC_FMOP || cur_page()->graph == GR_FMALG) && fm6_shown()) {
            fm6_opsel = (uint8_t)clamp((int32_t)fm6_opsel + (s > 0 ? 1 : -1), 0, 5);   /* FM6: PRESETS picks the operator */
            ui.force = 1;
        } else if (ui.home) {
            track_select((uint32_t)clamp((int32_t)song.sel + (s > 0 ? 1 : -1), 0, NTRK - 1));
        } else if (cur_fam() == FAM_SEQ) {
            if (cur_page()->scope == SC_STEP && ui.entry_open)
                step_length_edit(s);
            else {
                uint32_t target = (uint32_t)(((int32_t)(TSEL->pat_q ? TSEL->pat_q - 1u : TSEL->pat) +
                                              (s > 0 ? 1 : (int32_t)NPAT - 1)) % (int32_t)NPAT);
                TSEL->pat_q = (uint8_t)(target == TSEL->pat ? 0u : target + 1u);   /* back to the current pattern cancels */
                ui.force = 1;
            }
        } else if (cur_page()->graph == GR_BROWSE || cur_fam() == FAM_TRK ||
                   cur_fam() == FAM_EDIT || cur_fam() == FAM_ENV || cur_fam() == FAM_LFO ||
                   cur_fam() == FAM_FX || cur_fam() == FAM_SCL || cur_fam() == FAM_ARP) {
            /* In the instrument workspace PRESETS loads sounds directly; TRACKS
             * and the library browser keep the same browsing action. */
            uint32_t total, cur = preset_pos(&total);
            if (total)
                preset_go((cur + (s > 0 ? 1u : total - 1u)) % total);   /* past the factory ones: user presets */
        } else if (preset_pages()) {
            page_scroll(s);
        }
    }
    if ((s = panel_enc(EN_ALGO)) != 0)             /* ALGORITHM: the selected track, on every page */
        track_select((uint32_t)clamp((int32_t)song.sel + (s > 0 ? 1 : -1), 0, NTRK - 1));
    if ((s = panel_enc(EN_SELECT)) != 0) {          /* SELECT knob = global tempo */
        song.g[G_BPM] = (int16_t)clamp(song.g[G_BPM] + accel(EN_SELECT, s, 200), GP[G_BPM].min, GP[G_BPM].max);
        ui.bpm_t = 40;                              /* the header's BPM lights up; no message over the header */
    }
    for (k = 0; k < 4u; k++) {
        const page_t *pg = cur_page();
        int16_t *hv;
        if ((s = panel_enc(EN_K1 + k)) == 0)
            continue;
        if (ui.home || pg->scope == SC_STEP || pg->scope == SC_TRK || page_desc(pg, k, &hv) ||
            ((pg->graph == GR_USER || pg->graph == GR_FMSTORE) && k == 0u)) {     /* (not an empty column, nor "DRUM TRACK") */
            ui.hot_col = (uint8_t)k;
            ui.hot_t = 40;
        }
        if (ui.home) {
            if (ui.home_view == 3u) {
                static const uint8_t MASTER[4] = {G_DTIME, G_DFDBK, G_RSIZE, G_DMIX};
                const param_desc_t *d = &GP[MASTER[k]];
                int16_t *vp = &song.g[MASTER[k]];
                *vp = (int16_t)clamp(*vp + accel(EN_K1 + k, s, d->max - d->min), d->min, d->max);
            } else if (ui.home_view == 2u) {
                int16_t *vp = &trk[k].p[P_PAN];
                *vp = (int16_t)clamp(*vp + accel(EN_K1 + k, s, 127), TP[P_PAN].min, TP[P_PAN].max);
            } else {
                int16_t *vp = k == TRK_DRUM ? &song.g[G_DRLVL] : &trk[k].p[P_LEVEL];
                if (k != TRK_DRUM && trk[k].p[P_MUTE])
                    trk[k].p[P_MUTE] = 0;
                *vp = (int16_t)clamp(*vp + accel(EN_K1 + k, s, 127), 0, 127);
            }
        } else {
            edit_param(k, s);
        }
    }
    /* Apply the last length detent before advancing if the keys lift in the
     * same UI frame. Drums keep their original one-step entry behavior. */
    if (song.seq_mode && cur_page()->scope == SC_STEP && !seq_record_follow() &&
        ui.entry_open && !fm1_in.notes && !step_midi_held) {
        uint32_t n = is_drum(TSEL) ? 1u : step_note_length(TSEL, ui.cursor);
        cursor_set(ui.cursor + (n ? n : 1u));
    }
}

/* ---------------------------------------------------- panel setup --- */
/* 30 s without input: give up and keep the old table (a stuck key cannot hang the boot) */
#define SETUP_IDLE_MS 30000u
static void panel_setup(void)
{
    uint32_t i, used = 0, t0 = fm1_ms;
    const panel_t old = panel;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 10, 240, &FONT_S, "HARDWARE CALIBRATION", C_WHITE, 1);
    draw_text_box(0, 30, 240, &FONT_S, "TEACH EACH BUTTON AND KNOB", C_GRAY, 1);
    while (fm1_in.buttons) {                             /* wait for OCT-/OCT+ release */
        fm1_wdt_feed();
        if (fm1_ms - t0 > SETUP_IDLE_MS)
            goto timeout;
    }
    fm1_input_edges(0);
    for (i = 0; i < NB; i++) {
        uint32_t p = 0, id;
        draw_text_box(0, 80, 240, &FONT_S, "PRESS", C_GRAY, 1);
        draw_text_box(0, 100, 240, &FONT_L, B_NAME[i], C_WHITE, 1);
        t0 = fm1_ms;
        while (!(p & ~used)) {
            fm1_wdt_feed();
            p |= fm1_input_edges(0);
            if (fm1_ms - t0 > SETUP_IDLE_MS)
                goto timeout;
        }
        for (id = 0; id < 14u; id++)
            if (((p & ~used) >> id) & 1u)
                break;
        panel.btn[i] = (uint8_t)id;
        used |= 1u << id;
    }
    used = 0;
    for (i = 0; i < NE; i++) {
        uint32_t e;
        int32_t st = 0;
        draw_text_box(0, 80, 240, &FONT_S, "TURN RIGHT", C_GRAY, 1);
        draw_text_box(0, 100, 240, &FONT_L, E_NAME[i], C_WHITE, 1);
        for (e = 0; e < 7u; e++)
            fm1_enc_take(e);
        t0 = fm1_ms;
        for (;;) {
            fm1_wdt_feed();
            if (fm1_ms - t0 > SETUP_IDLE_MS)
                goto timeout;
            for (e = 0; e < 7u; e++)
                if (!((used >> e) & 1u) && (st = fm1_enc_take(e)) != 0)
                    break;
            if (e < 7u)
                break;
        }
        panel.enc[i] = (uint8_t)e;
        panel.dir[i] = (int8_t)(st > 0 ? 1 : -1);
        used |= 1u << e;
        fm1_delay_ms(300);
        fm1_enc_take(e);
    }
    panel.magic = PANEL_MAGIC;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.force = 1;
    return;
timeout:
    panel = old;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.force = 1;
    ui_message("SETUP CANCELLED");
}
