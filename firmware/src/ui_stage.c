/* SPDX-License-Identifier: GPL-3.0-only */
/* Melodee's Stage (HOME) and the PATTERNS grid (SEQ > PATTERNS), drawn to the redesign's mockups (docs/design).
 * Stage, top to bottom: the header (its chip: the engine); the selected track's own four knobs (its engine's,
 * stage_page); the panel: the notes it played and their chord in front of the live waveform, smooth, dimmed while
 * they are held (a knob turning: what it shapes and its value; a DRUM track: its lanes, lit as they hit); a lane per
 * track: a stripe and its number in its colour, the sound, its pattern (the next one dashed beside it while it waits
 * for the bar), the steps of the bar playing, its level. No footer. Each part remembers what it drew: the panel follows
 * the scope (every other frame), a lane redraws when its sound, pattern, steps or level change, the playhead alone
 * its bar. Every other frame the lanes, then the panel last: the SPI (12 MHz) sends its 39 KB while the next frame is
 * worked out (a blit right after it would wait for it, lcd_window).
 * PATTERNS: the four tracks x their eight patterns; KNOB k queues track k's (ui_input.c edit_param). */
#define ST_PANEL_H 82                                   /* the panel: Y_GRAPH .. +82 (y 70 .. 152) */
#define ST_LANE_Y (Y_GRAPH + ST_PANEL_H + 4)            /* lane k: ST_LANE_Y + ST_LANE_P k, ST_LANE_H tall */
#define ST_LANE_H 19
#define ST_LANE_P 21
#define ST_LANE_X 4
#define ST_LANE_W 232
#define ST_STEP_X 124                                   /* a lane's 16 steps: 4 x 8, 5.6 apart */
#define ST_STEP_SX(i) (ST_STEP_X + (int32_t)(i) * 56 / 10)

static struct {
    uint32_t lane[NTRK], ph[NTRK];                      /* each lane's drawn state (its playhead apart) */
    uint8_t hit_t[NLANE];                               /* frames a DRUM lane's name stays lit */
    uint8_t hit_trk;                                    /* .. of this track */
} stage;

/* ------------------------------------------------------- the knobs --- */
/* a knob's label: the panel's, plain words for CZ-1's (its panel says R1, L1 ..) */
static const char *stage_label(uint32_t k, const param_desc_t *d)
{
    static const char *const CZ_L[4] = {"WAVE", "DCW", "DETUNE", "VIBRATO"};
    return stage_pg.scope == SC_CZ1 ? CZ_L[k & 3u] : d->label;
}
/* a knob's value as Stage shows it: the Prophet's 0..127 panel values in % (the mockup's CUTOFF 62 %) */
static void stage_value(const page_t *sp, const param_desc_t *d, int32_t v, char *val, const char **unit)
{
    if (sp->scope == SC_P5 && d->fmt == F_INT && d->min == 0 && d->max == 127) {
        fmt_int(val, (v * 100 + 63) / 127);
        *unit = "%";
        return;
    }
    param_format(d, v, val, unit);
}
/* the four cards (ui_draw.c draw_columns on HOME): a value without effect on the sound as it is drawn dim */
static void stage_columns(void)
{
    const page_t *sp = stage_page();
    uint32_t c;
    for (c = 0; c < 4u; c++) {
        int16_t *vp;
        const char *unit;
        char val[12];
        const param_desc_t *d = page_desc(sp, c, &vp);
        int dim = sp->scope == SC_CZ1 && !cz_ed_active(cz_patch[song.sel % NTRK].raw, sp->id[c]);
        if (!d || !vp || !d->label || d->label[0] == '-') {
            draw_column(c, "", "", "", T_THEME, -1, ICON_NONE);
            continue;
        }
        stage_value(sp, d, *vp, val, &unit);
        draw_column(c, stage_label(c, d), val, unit, dim ? T_DIM : VAL(c), d->fmt == F_ENUM && d->max < 2 ? -1 : RATIO(d, *vp),
                    ICON_NONE);
    }
}

/* ------------------------------------------------------- the panel --- */
/* the output's waveform over the panel's width, smooth: triggered on a rising zero crossing, auto-scaled, each sample
 * the mean of three, 2 px anti-aliased (gfx.c cv_vspan_aa); rows top .. top + h */
static void stage_wave(uint16_t c, int32_t top, int32_t h)
{
    static int16_t snap[SCOPE_N];
    uint32_t w = scope_w, i, trig = 1;
    int32_t peak = 1500, x, py = 0, cy = (top + h / 2) * 16 - 16;
    for (i = 0; i < SCOPE_N; i++) {
        snap[i] = scope_buf[(w + i) & (SCOPE_N - 1u)];
        if (snap[i] > peak) peak = snap[i];
        else if (-snap[i] > peak) peak = -snap[i];
    }
    for (i = 2; i < SCOPE_N - 240u; i++)
        if (snap[i - 1] < 0 && snap[i] >= 0) { trig = i; break; }
    for (x = 0; x < 224; x++) {
        uint32_t k = trig + (uint32_t)x;
        int32_t v = ((int32_t)snap[k - 1u] + 2 * snap[k] + snap[k + 1u]) / 4;
        int32_t y = cy - v * (h / 2 - 8) * 16 / peak;
        cv_vspan_aa(8 + x, x ? py : y, y, c);
        py = y;
    }
}

/* the notes the selected track played (live_last: its last voicing, held or let go), the chord they make, in front of
 * what is on the canvas (cv_over): the root 30 px at baseline 30, the quality 15 px, the notes 11 px at baseline 50.
 * Held: the chord in text, the notes in the track's colour; let go: both faded into the panel. 0 none, 1 let go, 2 held */
static int stage_notes(int32_t x1)
{
    uint32_t bits[4], i, n = 0, pcs = 0, root = 0, bass, held = 0, k = song.sel % NTRK;
    uint8_t note[8];
    char b[48];
    const char *q;
    uint16_t c, cn;
    fm1_irq_off();                                      /* one coherent ISR snapshot, nothing drawn meanwhile */
    for (i = 0; i < 4u; i++) {
        bits[i] = live_last[k][i];
        held |= live_held[k][i];
    }
    fm1_irq_on();
    for (i = 0; i < 128u; i++)
        if ((bits[i >> 5] >> (i & 31u)) & 1u) {
            pcs |= 1u << (i % 12u);
            if (n < sizeof note) note[n] = (uint8_t)i;
            n++;
        }
    if (!n) return 0;
    c = held ? T_TEXT : ux_mix(T_PANEL, T_THEME, 75);
    cn = held ? T_THEME : ux_mix(T_PANEL, T_THEME, 55);
    bass = note[0] % 12u;
    q = micro_active(TSEL) ? 0 : chord_of(pcs, bass, &root);
    if (q) {
        int32_t x = cv_text(10, 1, &AF_L, N_NOTE[root], c);
        x = cv_text(x + 1, 15, &AF_M, q, c);
        if (root != bass) {
            x = cv_text(x + 3, 1, &AF_L, "/", c);
            cv_text(x, 1, &AF_L, N_NOTE[bass], c);
        }
        notes_fit(b, note, n, &AF_S, x1 - 10);
        cv_text(10, 39, &AF_S, b, cn);
    } else {
        const aafont_t *f = &AF_L;
        if (notes_fit(b, note, n, f, x1 - 10) != n) {
            f = &AF_S;
            notes_fit(b, note, n, f, x1 - 10);
        }
        cv_text(10, f == &AF_L ? 1 : 15, f, b, c);
    }
    return (int)(1u + (held != 0u));
}
/* HOME's notes alone (tests/ui_test.c): 1 = there were notes */
static int graph_notes(void) { return stage_notes(220) != 0; }

/* a DRUM track: its eight lanes in a row at the panel's foot, a lane's name lit while it hits (drum_flash: the ISR's) */
static void stage_hits(void)
{
    uint32_t l;
    for (l = 0; l < NLANE; l++) {
        int32_t x = 7 + (int32_t)l * 28;
        int lit = stage.hit_t[l] != 0u;
        cv_over = 0;
        cv_rrect(x, 62, 26, 14, 4, lit ? T_THEME : T_SURF, T_PANEL);
        if (!lit) {                                     /* (an unlit lane: outlined in line) */
            cv_rrect(x, 62, 26, 14, 4, T_LINE, T_PANEL);
            cv_rrect(x + 1, 63, 24, 12, 3, T_SURF, T_LINE);
        }
        cv_text_c(x + 13, 63, &AF_X, drum_lane_abbr(TSEL, l), lit ? T_INK : T_DIM, lit ? T_THEME : T_SURF);
    }
}

/* While one of Stage's knobs turns (ui.hot_t), the panel shows what it shapes behind the notes: a filter's response
 * (cutoff, resonance; the Prophet's envelope amount too), an envelope (its attack .. release); 0 = nothing to show */
enum { SP_NONE, SP_FILTER, SP_ENV };
static uint32_t stage_picture(int32_t *v)
{
    const page_t *sp = stage_page();
    uint32_t hot = ui.hot_col & 3u, id = sp->id[hot], k;
    if (!ui.hot_t)
        return SP_NONE;
    if (sp->scope == SC_P5) {
        const uint8_t *r = p5_patch_of(TSEL)->raw;
        if (id == P5_CUTOFF || id == P5_RESONANCE || id == P5_ENV_FILTER) {
            v[0] = r[P5_CUTOFF];
            v[1] = r[P5_RESONANCE];
            return SP_FILTER;
        }
        if (id == P5_RELEASE_AMP) {
            v[0] = r[P5_ATTACK_AMP]; v[1] = r[P5_DECAY_AMP]; v[2] = r[P5_SUSTAIN_AMP]; v[3] = r[P5_RELEASE_AMP];
            return SP_ENV;
        }
        return SP_NONE;
    }
    if (sp->scope != SC_ENGINE)
        return SP_NONE;
    if (id >= P_ATK && id <= P_REL) {
        for (k = 0; k < 4u; k++)
            v[k] = TSEL->p[P_ATK + k];
        return SP_ENV;
    }
    {   /* an engine's filter: its F_CUTOFF knob and the RES beside it */
        int32_t cut = -1, res = 0;
        for (k = 0; k < 4u; k++) {
            const param_desc_t *d = track_desc(TSEL, sp->id[k]);
            if (d->fmt == F_CUTOFF) cut = (int32_t)k;
            else if (str_eq(d->label, "RES")) res = TSEL->p[sp->id[k]];
        }
        if (cut < 0 || (hot != (uint32_t)cut && !str_eq(track_desc(TSEL, id)->label, "RES")))
            return SP_NONE;
        v[0] = TSEL->p[sp->id[cut]];
        v[1] = res;
        return SP_FILTER;
    }
}
/* a low-pass response: flat, the resonance's peak at the cutoff (a dotted line), then the slope; cut, res 0..127 */
static void stage_filter(int32_t cut, int32_t res, uint16_t c)
{
    int32_t x, fc = 10 + clamp(cut, 0, 127) * 200 / 127, peak = clamp(res, 0, 127) * 22 / 127;
    int32_t top = 4, bot = ST_PANEL_H - 6, y0 = 22 * 16, py = 0;
    for (x = top; x < bot; x += 5)
        cv_rect(fc, x, 1, 2, ux_mix(T_PANEL, c, 60));
    for (x = 0; x < 220; x++) {
        int32_t d = 8 + x - fc, y = y0 - peak * 16 * 64 / (64 + d * d) + (d > 0 ? d * 16 : 0);
        y = clamp(y, top * 16, bot * 16);
        cv_vspan_aa(8 + x, x ? py : y, y, c);
        py = y;
    }
}

static void stage_panel_hits(void)                      /* every frame: the DRUM lanes hit since the last one */
{
    uint8_t f[NTRK];
    uint32_t k, sel = song.sel % NTRK, l;
    fm1_irq_off();
    for (k = 0; k < NTRK; k++) {
        f[k] = drum_flash[k];
        drum_flash[k] = 0;
    }
    fm1_irq_on();
    if (stage.hit_trk != sel + 1u) {
        stage.hit_trk = (uint8_t)(sel + 1u);
        memset(stage.hit_t, 0, sizeof stage.hit_t);
    }
    for (l = 0; l < NLANE; l++)
        stage.hit_t[l] = (f[sel] >> l) & 1u ? 10u : stage.hit_t[l] ? stage.hit_t[l] - 1u : 0u;
}
static void stage_panel(void)
{
    uint32_t sel = song.sel % NTRK, drum = (uint32_t)drum_track(TSEL), pic;
    int32_t pv[4];
    int held = 0;
    cv_begin(240, ST_PANEL_H, T_BG);
    cv_rrect(4, 0, 232, ST_PANEL_H, 6, T_PANEL, T_BG);
    cv_bg = T_PANEL;
    if (!drum) {
        uint32_t i;
        fm1_irq_off();
        for (i = 0; i < 4u; i++) held |= live_held[sel][i] != 0u;
        fm1_irq_on();
    }
    pic = drum ? SP_NONE : stage_picture(pv);
    /* behind: the waveform (dimmed while notes are held, or under a drum's lanes), or what the knob turning shapes */
    if (pic == SP_FILTER)
        stage_filter(pv[0], pv[1], ux_mix(T_PANEL, T_THEME, 55));
    else if (pic == SP_ENV)
        graph_adsr_v(pv[0], pv[1], pv[2], pv[3], 8, ST_PANEL_H - 10, ux_mix(T_PANEL, T_THEME, 55));
    else
        stage_wave(held || drum ? ux_mix(T_PANEL, T_THEME, 40) : T_THEME, 0, drum ? 60 : ST_PANEL_H);
    cv_over = 1;                                        /* in front: blended over it */
    if (drum) {
        stage_hits();
    } else {
        stage_notes(pic ? 170 : 226);
        if (pic) {                                      /* the value turning, at the top right */
            int16_t *vp;
            const char *unit;
            char val[12];
            const page_t *sp = stage_page();
            const param_desc_t *d = page_desc(sp, ui.hot_col & 3u, &vp);
            if (d && vp) {
                stage_value(sp, d, *vp, val, &unit);
                str_cpy(val + str_len(val), unit, 4);
                cv_text_r(228, 6, &AF_M, val, T_THEME, T_PANEL);
            }
        }
    }
    cv_over = 0;
    cv_blit(0, Y_GRAPH);
}

/* ------------------------------------------------------- the lanes --- */
/* lane k's 16 steps (the page playing): on in the track's colour (ties fainter), the playhead in text, off in line;
 * drawn at x0 of the canvas, on bg */
static void stage_steps(const track_t *t, uint32_t page, uint32_t ph, uint16_t tc, int32_t x0, int32_t y0, uint16_t bg)
{
    uint32_t i, len = (uint32_t)t->p[P_SLEN];
    for (i = 0; i < 16u; i++) {
        uint32_t si = page * 16u + i;
        const step_t *st = &seq_steps(t)[si % NSTEP];
        uint16_t c;
        if (si >= len)
            break;
        c = si == ph ? T_TEXT : step_on(st) ? ux_mix(bg, tc, 80) : st->time == ST_TIE ? ux_mix(bg, tc, 40) : T_LINE;
        cv_rrect(x0 + (int32_t)i * 56 / 10, y0, 4, 8, 1, c, bg);
    }
}
/* lane k: the stripe, the number, the sound, the pattern chip (selected: filled, else outlined; the next one dashed
 * beside it), the bar, the level. The playhead alone moving redraws the bar alone */
static void stage_lane(uint32_t k)
{
    const track_t *t = &trk[k];
    uint32_t sel = k == song.sel, mute = t->p[P_MUTE] != 0u, arm = (song.rec >> k) & 1u, len = (uint32_t)t->p[P_SLEN];
    uint32_t page = song.playing && t->seq_idx < len ? t->seq_idx / 16u : 0u, ph = song.playing ? t->seq_idx : 0xFFFFu;
    uint32_t next = t->pattern_next < NPAT ? t->pattern_next + 1u : 0u, lvl = (uint32_t)t->p[P_LEVEL] & 127u, sig;
    int pending = sel && browse_pending();
    uint16_t tc = mute ? T_DIM : T_TRK(k), bg = sel ? T_LIFT : T_QUIET;
    int32_t y = ST_LANE_Y + ST_LANE_P * (int32_t)k, x;
    char b[16], chip[4] = {'P', (char)('1' + t->pattern % NPAT), 0, 0};
    if (pending) {                                      /* browsing: the sound the list shows, not yet loaded */
        uint32_t kk, s = browse_shown(&kk);
        char tag[6];
        entry_label(s, kk, tag, b);
        if (!b[0])                                      /* (an unnamed slot: its tag, "F012") */
            str_cpy(b, tag, sizeof b);
    } else {
        trk_short_name(k, b);
    }
    sig = str_hash(1u + sel * 2u + mute * 4u + arm * 8u + (uint32_t)pending * 16u, b) + next * 131u + lvl * 40503u +
          t->pattern * 7919u + steps_hash(t) + page * 613u + len * 13u + ux.gen * 977u + ux.pal * 31u;
    if (!ui.force && sig == stage.lane[k]) {
        if (ph != stage.ph[k]) {                        /* the playhead moved: the bar only */
            stage.ph[k] = ph;
            cv_begin(92, 10, bg);
            stage_steps(t, page, ph, tc, 1, 1, bg);
            cv_blit(ST_LANE_X + ST_STEP_X - 1, (uint32_t)(y + 4));
        }
        return;
    }
    stage.lane[k] = sig;
    stage.ph[k] = ph;
    cv_begin(ST_LANE_W, ST_LANE_H, T_BG);
    if (sel) {                                          /* the selected track: lifted, outlined in its colour */
        cv_rrect(0, 0, ST_LANE_W, ST_LANE_H, 5, T_TRK(k), T_BG);
        cv_rrect(1, 1, ST_LANE_W - 2, ST_LANE_H - 2, 4, bg, T_TRK(k));
    } else {
        cv_rrect(0, 0, ST_LANE_W, ST_LANE_H, 5, bg, T_BG);
    }
    cv_rect(0, sel ? 1 : 0, 3, ST_LANE_H - (sel ? 2 : 0), tc);   /* the stripe */
    {
        char n[2] = {(char)('1' + k), 0};
        cv_text_on(9, 3, &AF_S, n, tc, bg);
    }
    x = 21;
    if (arm) {                                          /* armed: a REC dot before the name */
        cv_circle(x + 3, 9, 6, T_REC, bg);
        x += 9;
    }
    cv_free_text(x, 3, &AF_S, b, pending ? T_ACCENT : mute ? T_DIM : sel ? T_TEXT : T_SEC, bg, 96 - x);
    if (sel) {                                          /* the pattern: filled (selected), else outlined */
        cv_rrect(100, 4, 18, 12, 3, tc, bg);
        cv_text_c(109, 5, &AF_X, chip, T_INK, tc);
    } else {
        cv_rrect(100, 4, 18, 12, 3, tc, bg);
        cv_rrect(101, 5, 16, 10, 2, bg, tc);
        cv_text_c(109, 5, &AF_X, chip, tc, bg);
    }
    if (next) {                                         /* the next one, waiting for the bar: dashed */
        char n2[3] = {'P', (char)('0' + next), 0};
        cv_dashed(102 + 18, 4, 18, 12, 3, tc);
        (void)n2;
    }
    stage_steps(t, page, ph, tc, ST_STEP_X, 5, bg);
    cv_rect(218, 14, 10, 2, T_LINE);                    /* the level: a 3 px bar over a line */
    cv_rrect(218, 14 - (int32_t)(lvl * 11u / 127u), 3, (int32_t)(lvl * 11u / 127u) + 2, 1, tc, bg);
    cv_blit(ST_LANE_X, (uint32_t)y);
}

/* the BG around the panel and between the lanes (a full redraw: the canvases cover the rest) */
static void stage_frame(void)
{
    uint32_t k;
    lcd_fill(0, Y_GRAPH + ST_PANEL_H, 240, ST_LANE_Y - Y_GRAPH - ST_PANEL_H, T_BG);
    for (k = 0; k < NTRK; k++) {
        uint32_t y = ST_LANE_Y + ST_LANE_P * k;
        lcd_fill(0, y, ST_LANE_X, ST_LANE_H, T_BG);
        lcd_fill(ST_LANE_X + ST_LANE_W, y, 240u - ST_LANE_X - ST_LANE_W, ST_LANE_H, T_BG);
        lcd_fill(0, y + ST_LANE_H, 240, k + 1u < NTRK ? ST_LANE_P - ST_LANE_H : 240u - y - ST_LANE_H, T_BG);
    }
}

/* every other frame (the scope's): the lanes, then the panel last (its SPI transfer overlaps the next frame) */
static void stage_draw(void)
{
    uint32_t k;
    stage_panel_hits();
    if (!ui.force && (ui.frame & 1u))
        return;
    for (k = 0; k < NTRK; k++)
        stage_lane(k);
    stage_panel();
}

/* ------------------------------------------------------ PATTERNS --- */
/* pattern b of track k holds something: a note, a hit, a recorded note's step */
static int pattern_used(uint32_t k, uint32_t b)
{
    const step_t *s = b == trk[k].pattern ? trk[k].step : pattern_at(k, b)->step;
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        if (step_on(&s[i]) || (s[i].flags & SF_RECORDED))
            return 1;
    return 0;
}
static uint32_t patgrid_sig(void)
{
    uint32_t h = 2166136261u, k, b, i;
    for (k = 0; k < NTRK; k++) {
        const track_t *t = &trk[k];
        uint32_t len = (uint32_t)t->p[P_SLEN];
        h = (h ^ (t->pattern + 16u * t->pattern_next + 4096u * (uint32_t)t->p[P_MUTE])) * 16777619u;
        h = (h ^ (song.playing && len ? t->seq_idx * 8u / len : 99u)) * 16777619u;
        for (b = 0; b < NPAT; b++)
            h = (h ^ (uint32_t)pattern_used(k, b)) * 16777619u;
    }
    h = (h ^ (chain_config.count + 32u * jam.n + 1024u * (chain.running ? chain.row + 1u : 0u) + 65536u * ui.song_row)) * 16777619u;
    for (i = 0; i < CHAIN_ROWS; i++)
        h = (h ^ (chain_config.row[i].repeat + 32u * chain_patterns[i][0] + 256u * chain_patterns[i][1] +
                  2048u * chain_patterns[i][2] + 16384u * chain_patterns[i][3])) * 16777619u;
    return h ^ (fm1_ms / 250u & 1u) * 0x9E37u ^ song.sel * 131u;
}
/* the song under the grid (SEQ held opens PATTERNS): three of its rows around the one playing (else the one SONG
 * picked): the row's number, each track's pattern in its colour, the repeats; the row playing filled. No song yet:
 * the rows the jam logged, under JAM */
static void patgrid_song(int32_t y0)
{
    uint32_t n = chain_config.count, jamrows = !n && jam.n, cur = chain.running ? chain.row : ui.song_row, i, k, first;
    if (jamrows) n = jam.n;
    cv_text_on(9, y0, &AF_S, jamrows ? "JAM" : "SONG", T_DIM, T_SURF);
    if (!n) {
        cv_text_on(48, y0, &AF_S, "--", T_DIM, T_SURF);
        return;
    }
    if (cur >= n) cur = n - 1u;
    first = cur > 1u ? cur - 1u : 0u;
    if (n > 3u && first > n - 3u) first = n - 3u;
    for (i = first; i < first + 3u && i < n; i++) {
        int32_t y = y0 + (int32_t)(i - first) * 14;
        int sel = !jamrows && chain.running && i == chain.row;
        uint16_t bg = sel ? T_THEME : T_SURF;
        char b[6];
        if (sel)
            cv_rrect(44, y, 150, 14, 4, T_THEME, T_SURF);
        fmt_int(b, (int32_t)i + 1);
        cv_text_r(64, y, &AF_S, b, sel ? T_INK : T_MID, bg);
        for (k = 0; k < NTRK; k++) {
            char d[2] = {(char)('1' + (jamrows ? jam.pat[i][k] : chain_patterns[i][k]) % NPAT), 0};
            cv_text_c(84 + (int32_t)k * 20, y, &AF_S, d, sel ? T_INK : trk[k].p[P_MUTE] ? T_DIM : T_TRK(k), bg);
        }
        b[0] = 'x';
        fmt_int(b + 1, jamrows ? jam.rep[i] : chain_config.row[i].repeat);
        cv_text_on(162, y, &AF_S, b, sel ? T_INK : T_MID, bg);
    }
}
/* the grid: a row per track (its number; selected: filled), a cell per pattern: playing / selected in the track's
 * colour (playing: how far through it at its foot), waiting for the bar: outlined, blinking; holding notes: raised;
 * empty: a faint outline. The song under it */
static void graph_patgrid(void)
{
    uint32_t k, b, blink = (fm1_ms / 250u) & 1u;
    for (b = 0; b < NPAT; b++) {
        char n[2] = {(char)('1' + b), 0};
        cv_text_c(45 + (int32_t)b * 25, -1, &AF_S, n, T_DIM, T_SURF);
    }
    for (k = 0; k < NTRK; k++) {
        const track_t *t = &trk[k];
        int32_t y = 13 + (int32_t)k * 16;
        uint16_t tc = t->p[P_MUTE] ? T_DIM : T_TRK(k);
        char n[2] = {(char)('1' + k), 0};
        if (k == song.sel) {
            cv_rrect(10, y, 14, 14, 4, tc, T_SURF);
            cv_text_c(17, y, &AF_S, n, T_INK, tc);
        } else {
            cv_text_c(17, y, &AF_S, n, tc, T_SURF);
        }
        for (b = 0; b < NPAT; b++) {
            int32_t x = 34 + (int32_t)b * 25;
            uint32_t len = (uint32_t)t->p[P_SLEN];
            if (b == t->pattern) {
                cv_rrect(x, y, 22, 14, 4, tc, T_SURF);
                if (song.playing && len) {
                    int32_t w = (int32_t)((t->seq_idx % len + 1u) * 16u / len);
                    cv_rrect(x + 3, y + 10, 16, 2, 1, ux_mix(tc, T_INK, 50), tc);
                    cv_rrect(x + 3, y + 10, w < 2 ? 2 : w, 2, 1, T_INK, ux_mix(tc, T_INK, 50));
                }
            } else if (b == t->pattern_next) {
                uint16_t oc = blink ? T_DIM : tc;
                cv_rrect(x, y, 22, 14, 4, oc, T_SURF);
                cv_rrect(x + 2, y + 2, 18, 10, 3, pattern_used(k, b) ? T_RAISE : T_SURF, oc);
            } else if (pattern_used(k, b)) {
                cv_rrect(x, y, 22, 14, 4, T_RAISE, T_SURF);
            } else {
                cv_rrect(x, y, 22, 14, 4, T_LINE, T_SURF);
                cv_rrect(x + 1, y + 1, 20, 12, 3, T_SURF, T_LINE);
            }
        }
    }
    patgrid_song(80);
}
/* the PATTERNS cards: KNOB k = track k's pattern (the one waiting, else the one playing) */
static void patgrid_columns(void)
{
    uint32_t c;
    for (c = 0; c < NTRK; c++) {
        const track_t *t = &trk[c];
        char l[8] = "TRACK 1", v[4] = {0, 0, 0, 0};
        uint32_t p = t->pattern_next < NPAT ? t->pattern_next : t->pattern;
        l[6] = (char)('1' + c);
        v[0] = (char)('1' + p % NPAT);
        draw_column(c, l, v, t->pattern_next < NPAT ? "NEXT" : "/8", VAL(c), (int32_t)(p % NPAT) * 1000 / (NPAT - 1u),
                    ICON_X_PATTERN);
    }
}
/* KNOB k: track k's next pattern (stopped: at once; playing: at the end of its bar; a song plays: refused) */
static void patgrid_edit(uint32_t k, int32_t steps)
{
    track_t *t = &trk[k % NTRK];
    uint32_t from = t->pattern_next < NPAT ? t->pattern_next : t->pattern;
    if (pattern_request(t, (uint32_t)clamp((int32_t)from + (steps > 0 ? 1 : -1), 0, NPAT - 1u)))
        ui_message("STOP SONG TO SWITCH");
    ui.force = 1;
}
