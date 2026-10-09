/* SPDX-License-Identifier: GPL-3.0-only */
/* NEW SONG (SAVE > PROJECT, KNOB 1 past TMPL: NEW, then LOAD's OCT+): a new project from the template (none saved:
 * the power-on sounds), every pattern empty, in two screens. KEY: KNOB 1 ROOT, 2 SCALE, 3 TEMPO. ROLES: KNOB k track
 * k's role: KEEP (the template's sound) or DRUMS BASS CHORDS LEAD PAD: the first sound of that category, unless the
 * template's sound already is one. OCT+ next / create, OCT- back / cancel. Stopped only: the base loads like a project */
static void template_key(uint32_t *root, uint32_t *scale, int32_t *bpm);   /* project.c */
static int32_t accel(uint32_t role, int32_t s, int32_t range);            /* ui_input.c */
static void project_new_blank(void);
static int project_dirty(void);
enum { NR_KEEP, NR_DRUMS, NR_BASS, NR_CHORDS, NR_LEAD, NR_PAD, NR_N };
static const char *const NR_NAME[NR_N] = {"KEEP", "DRUMS", "BASS", "CHORDS", "LEAD", "PAD"};
static const uint8_t NR_CAT[NR_N] = {CAT_NONE, CAT_DRUM, CAT_BASS, CAT_KEYS, CAT_LEAD, CAT_PAD};
static struct {
    uint8_t on;                                     /* 0 closed, 1 KEY, 2 ROLES */
    uint8_t root, scale, role[NTRK];
    int16_t bpm;
} nw;
static int new_on(void) { return nw.on != 0; }
static void new_close(void) { if (nw.on) { nw.on = 0; ui.force = 1; } }
static void new_open(void)
{
    uint32_t r = (uint32_t)TSEL->p[P_ROOT], s = (uint32_t)TSEL->p[P_SCALE];
    int32_t b = song.g[G_BPM];
    template_key(&r, &s, &b);                       /* (the template's, when there is one) */
    memset(&nw, 0, sizeof nw);
    nw.root = (uint8_t)r;
    nw.scale = (uint8_t)s;
    nw.bpm = (int16_t)b;
    nw.on = 1;
    ui.force = 1;
}

/* the first sound of role r's category (its LIST order; DRUMS: the DRUM engine's first kit): its source, *k;
 * USER_NONE: none */
static uint32_t new_role_sound(uint32_t r, uint32_t *k)
{
    uint32_t i, total, src;
    if (r == NR_DRUMS) {
        *k = 0;
        return ENGI_DRUM;
    }
    for (i = 0; i < NELEM(CAT_ORDER) && CAT_ORDER[i] != NR_CAT[r]; i++)
        ;
    if (i == NELEM(CAT_ORDER))
        return USER_NONE;
    list_scan(LM_CAT + i, 0xFFFFFFFFu, 0, 0, &src, k, &total);
    return total ? src : USER_NONE;
}
/* the base loaded: each role's sound (selected track k meanwhile: the loads are the selected track's), the key, tempo */
static void new_create(void)
{
    uint32_t k, sel;
    if (transport_busy()) { ui_message("STOP FIRST"); return; }
    if (template_used()) template_load();
    else project_new_blank();
    sel = song.sel;
    undo_depth++;                                   /* (no undo levels: a new project) */
    for (k = 0; k < NTRK; k++) {
        uint32_t src, kk, cs, ck;
        track_t *t = &trk[k];
        if (nw.role[k] == NR_KEEP)
            continue;
        song.sel = (uint8_t)k;
        cur_entry(&cs, &ck);
        if (entry_cat(cs, ck) == NR_CAT[nw.role[k]])
            continue;                               /* the template's sound is one already */
        if ((src = new_role_sound(nw.role[k], &kk)) == USER_NONE)
            continue;
        if (src == USER_NATIVE_P5 || src == USER_NATIVE_FM || src == USER_NATIVE_CZ)
            native_load(src_engine(src, kk), kk, k);
        else if (src == USER_GENERAL)
            up_load(kk);
        else {
            if (src != t->eng_req) set_engine_of(t, src);
            apply_preset_to(t, kk);
        }
    }
    undo_depth--;
    song.sel = (uint8_t)sel;
    for (k = 0; k < NTRK; k++)
        trk[k].p[P_ROOT] = (int16_t)nw.root;
    TSEL->p[P_SCALE] = (int16_t)nw.scale;
    scale_share(TSEL);
    song.g[G_BPM] = nw.bpm;
    undo_clear();
    nw.on = 0;
    go_home();
    sync_reload = 1;
    ui_message("NEW SONG");
}

/* OCT+ next / create, OCT- back / cancel; KNOB 1..4 the screen's; HOME tapped: cancel */
static void new_input(uint32_t oct, int home_tap)
{
    uint32_t k;
    for (k = 0; k < 4u; k++) {
        int32_t s = panel_enc(EN_K1 + k);
        if (!s) continue;
        ui.hot_col = (uint8_t)k;
        ui.hot_t = 40;
        if (nw.on == 1) {
            if (k == 0u) nw.root = (uint8_t)clamp((int32_t)nw.root + (s > 0 ? 1 : -1), TP[P_ROOT].min, TP[P_ROOT].max);
            else if (k == 1u) nw.scale = (uint8_t)clamp((int32_t)nw.scale + (s > 0 ? 1 : -1), 0, SCALE_TOTAL - 1);
            else if (k == 2u) nw.bpm = (int16_t)clamp(nw.bpm + accel(EN_K1 + k, s, GP[G_BPM].max - GP[G_BPM].min), GP[G_BPM].min, GP[G_BPM].max);
        } else {
            nw.role[k] = (uint8_t)clamp((int32_t)nw.role[k] + (s > 0 ? 1 : -1), 0, NR_N - 1);
        }
    }
    if (home_tap)
        new_close();
    else if (oct & 2u) {
        if (nw.on == 1) { nw.on = 2; ui.force = 1; }
        else new_create();
    } else if (oct & 1u) {
        if (nw.on == 2) { nw.on = 1; ui.force = 1; }
        else new_close();
    }
}

static void new_draw(void)
{
    static uint32_t sig;
    uint32_t k, s = nw.on * 7u + nw.root * 131u + nw.scale * 1031u + (uint32_t)nw.bpm * 40503u + ux.gen * 977u +
                    (ui.hot_t ? ui.hot_col + 1u : 0u) * 7u;
    char v[12];
    const char *unit;
    for (k = 0; k < NTRK; k++)
        s = s * 33u + nw.role[k];
    if (ui.force)
        draw_frame(0);
    draw_head();
    if (!ui.force && s == sig) {
        if (ui.hot_t) ui.hot_t--;
        return;
    }
    sig = s;
    if (nw.on == 1) {                               /* KEY: the cards, the key and tempo large */
        param_format(&TP[P_ROOT], nw.root, v, &unit);
        draw_column(0, "ROOT", v, unit, VAL(0u), -1, ICON_AUTO);
        draw_column(1, "SCALE", N_SCALE[nw.scale % SCALE_TOTAL], "", VAL(1u), -1, ICON_NONE);
        fmt_int(v, nw.bpm);
        draw_column(2, "TEMPO", v, "BPM", VAL(2u), RATIO(&GP[G_BPM], nw.bpm), ICON_AUTO);
        draw_column(3, "", "", "", T_THEME, -1, ICON_NONE);
    } else {                                        /* ROLES: a track each */
        for (k = 0; k < NTRK; k++) {
            char l[8] = "TRACK 1";
            l[6] = (char)('1' + k);
            draw_column(k, l, NR_NAME[nw.role[k]], "", nw.role[k] ? VAL(k) : T_DIM, -1, ICON_NONE);
        }
    }
    cv_begin(240, H_GRAPH, T_BG);
    cv_rrect(3, 0, 234, H_GRAPH, 5, T_SURF, T_BG);
    cv_bg = T_SURF;
    cv_text_c(120, 6, &AF_S, nw.on == 1 ? "NEW SONG  1/2" : "NEW SONG  2/2", T_MID, T_SURF);
    if (nw.on == 1) {
        char key[24];
        param_format(&TP[P_ROOT], nw.root, v, &unit);
        str_cpy(key, v, sizeof key);
        cv_text_c(120, 30, &AF_L, key, T_THEME, T_SURF);
        cv_text_c(120, 66, &AF_M, N_SCALE[nw.scale % SCALE_TOTAL], T_TEXT, T_SURF);
        fmt_int(key, nw.bpm);
        str_cpy(key + str_len(key), " BPM", 8);
        cv_text_c(120, 92, &AF_M, key, T_MID, T_SURF);
    } else {
        for (k = 0; k < NTRK; k++) {                /* the track, its role, the sound it gets */
            int32_t y = 26 + (int32_t)k * 23;
            uint32_t kk, src;
            char nm[16], tag[6];
            char n[2] = {(char)('1' + k), 0};
            cv_rrect(10, y, 14, 14, 4, T_TRK(k), T_SURF);
            cv_text_c(17, y, &AF_S, n, T_INK, T_TRK(k));
            cv_text_on(32, y, &AF_S, NR_NAME[nw.role[k]], nw.role[k] ? T_TEXT : T_DIM, T_SURF);
            if (!nw.role[k] || (src = new_role_sound(nw.role[k], &kk)) == USER_NONE)
                str_cpy(nm, template_used() ? "TEMPLATE" : "--", sizeof nm);
            else
                entry_label(src, kk, tag, nm);
            cv_free_text(96, y, &AF_S, nm, nw.role[k] ? T_THEME : T_DIM, T_SURF, 130);
        }
    }
    cv_blit(0, Y_GRAPH);
    cv_begin(240, H_FOOT, T_BG);
    {
        khint_t kh[2] = {{KC_OCTDN, nw.on == 1 ? "CANCEL" : "BACK"}, {KC_OCTUP, nw.on == 1 ? "NEXT" : "CREATE"}};
        cv_key_row(8, 232, 9, kh, 2, 3u, T_BG);
    }
    cv_blit(0, Y_FOOT);
    ui.force = 0;
    if (ui.hot_t) ui.hot_t--;
}
