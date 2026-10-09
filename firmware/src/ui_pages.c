/* SPDX-License-Identifier: GPL-3.0-only */
/* The pages the proposal did not draw, in the redesign's language (docs/design: mock/r5_pages, ref/env, lfo, ..):
 * no footer; the header names the page and shows where it is in its family (dots); the sound being edited at the
 * foot; the four knobs as the page needs them, the knob just turned lifted in the track's colour; each page's own
 * picture.
 * CURVE (ENV, LFO, LFO 2): the curve is the page (a panel of 160 rows, drawn in two slices: a canvas holds 124 x 240),
 * a value on each of its segments; the knobs as a slim strip under it.
 * Each part remembers what it drew. draw_column draws the knobs in the style col_style asks (pv_column). Included by
 * ui_draw.c */
enum { PV_NONE, PV_CURVE };
#define PV_PANEL_X 4
#define PV_PANEL_W 232
#define PV_SLICE 128                                    /* rows of a panel slice (232 x 128 fits CV_MAX) */
#define PV_CURVE_Y 22                                   /* CURVE: the panel, rows 22 .. 181 */
#define PV_CURVE_H 160
#define PV_STRIP_Y 188                                  /* .. the knobs, 54 x 30, rows 188 .. 217 */
#define PV_STRIP_H 30
#define PV_SOUND_Y 220                                  /* the sound: rows 220 .. 239 */

static struct { uint32_t panel, sound; int32_t slice0; } pv;
static struct { int16_t x0, y0, x1, y1; } pv_box[6];   /* the labels on a panel (pv_text) */
static uint32_t pv_nbox;

static uint32_t pv_kind(void)
{
    const page_t *pg = cur_page();
    if (ui.home || act_cols() || grid_on())
        return PV_NONE;
    if ((pg->graph == GR_ADSR && pg->scope == SC_TRACK) || pg->graph == GR_LFO)
        return PV_CURVE;
    return PV_NONE;
}

/* ---------------------------------------------------------- knobs --- */
/* CS_STRIP: a 54 x 30 cell, its label (8 px, the track's colour) over its value (11 px) and unit; the knob just turned:
 * tinted, outlined */
static void pv_column(uint32_t c, const char *label, const char *val, const char *unit, uint16_t vc, int32_t ratio,
                      int hot)
{
    uint16_t bg = hot ? ux_mix(T_SURF, T_THEME, 22) : T_SURF, lc = vc == T_DIM ? T_DIM : T_THEME;
    int32_t x;
    (void)ratio;
    cv_begin(CARD_W, PV_STRIP_H, T_BG);
    if (hot) {
        cv_rrect(0, 0, CARD_W, PV_STRIP_H, 6, T_THEME, T_BG);
        cv_rrect(1, 1, CARD_W - 2, PV_STRIP_H - 2, 5, bg, T_THEME);
    } else {
        cv_rrect(0, 0, CARD_W, PV_STRIP_H, 6, bg, T_BG);
    }
    if (label[0])
        cv_text_fit(6, 2, &AF_X, label, lc, bg, CARD_W - 8);
    if (val[0]) {
        int32_t uw = unit[0] ? text_w(&AF_X, unit) + 2 : 0;
        x = cv_free_text(6, 14, &AF_S, val, vc, bg, CARD_W - 8 - uw);
        if (unit[0] && x + uw <= CARD_W - 2)
            cv_text_on(x + 2, 15, &AF_X, unit, T_MID, bg);
    }
    cv_blit((uint32_t)CARD_X(c), PV_STRIP_Y);
}

/* ---------------------------------------------------------- parts --- */
/* the sound being edited: its engine (the track's colour), its name */
static void pv_sound(void)
{
    char nm[20];
    const char *en = ENGINES[eng_idx(TSEL->eng_req)]->name;
    uint32_t sig;
    int32_t x;
    sound_name(TSEL, nm);
    sig = str_hash(str_hash(song.sel * 7u + ux.gen * 977u + ux.pal * 31u, en), nm);
    if (!ui.force && sig == pv.sound)
        return;
    pv.sound = sig;
    cv_begin(240, 240 - PV_SOUND_Y, T_BG);
    x = cv_text_on(8, 3, &AF_X, en, T_THEME, T_BG);
    cv_free_text(x + 6, 1, &AF_S, nm, T_SEC, T_BG, 232 - x - 6);
    cv_blit(0, PV_SOUND_Y);
}
/* a panel h rows tall at y, drawn by fn in slices (fn draws in panel coordinates; pv.slice0: the slice's first row) */
static void pv_panel(int32_t y, int32_t h, void (*fn)(int32_t h))
{
    int32_t top;
    for (top = 0; top < h; top += PV_SLICE) {
        int32_t sh = h - top < PV_SLICE ? h - top : PV_SLICE;
        cv_begin(PV_PANEL_W, (uint32_t)sh, T_BG);
        cv_oy = -top;
        pv.slice0 = top;
        cv_rrect(0, 0, PV_PANEL_W, h, 6, T_PANEL, T_BG);
        cv_bg = T_PANEL;
        pv_nbox = 0;
        fn(h);
        cv_oy = 0;
        cv_blit(PV_PANEL_X, (uint32_t)(y + top));
    }
}
/* a label on a panel: below the labels already on it that it would cover (pv.nbox: a panel's), moved up off a slice's
 * seam, drawn in the slice that holds it */
static void pv_text(int32_t x, int32_t y, const aafont_t *f, const char *s, uint16_t c, int align)
{
    int32_t w = text_w(f, s), seam;
    uint32_t i;
    if (align == 1) x -= w / 2;
    else if (align == 2) x -= w;
    for (i = 0; i < pv_nbox; i++)
        if (x < pv_box[i].x1 && x + w > pv_box[i].x0 && y < pv_box[i].y1 && y + f->h > pv_box[i].y0)
            y = pv_box[i].y1 + 1;
    seam = (y + f->h) / PV_SLICE * PV_SLICE;
    if (seam > y && seam < y + f->h)
        y = seam - f->h;
    if (pv_nbox < NELEM(pv_box))
        pv_box[pv_nbox++] = (typeof(pv_box[0])){(int16_t)x, (int16_t)y, (int16_t)(x + w), (int16_t)(y + f->h)};
    if (y >= pv.slice0 && y + f->h <= pv.slice0 + PV_SLICE)
        cv_text_on(x, y, f, s, c, T_PANEL);
}
/* knob c's value as the page formats it, and its unit */
static void pv_value(uint32_t c, char *val, const char **unit)
{
    int16_t *vp;
    const param_desc_t *d = page_desc(cur_page(), c, &vp);
    val[0] = 0;
    *unit = "";
    if (d && vp)
        param_format(d, *vp, val, unit);
}
static int32_t pv_hot(void) { return ui.hot_t ? (int32_t)(ui.hot_col & 3u) : -1; }

/* ---------------------------------------------------------- CURVE --- */
/* ENV: attack (linear), decay (exponential to the sustain), the sustain held, the release (exponential); the area
 * under it tinted; a handle at each segment's end; each segment's value on it; the segment turning in text */
static void pv_env(int32_t h)
{
    const page_t *pg = cur_page();
    int32_t va = TSEL->p[pg->id[0]], vd = TSEL->p[pg->id[1]], vs = TSEL->p[pg->id[2]], vr = TSEL->p[pg->id[3]];
    int32_t top = 18, bot = h - 22, x0 = 12, x1 = x0 + 10 + va * 40 / 127, x2 = x1 + 16 + vd * 52 / 127;
    int32_t x4 = PV_PANEL_W - 14, x3 = x4 - 24 - vr * 48 / 127, sus = vs * 1000 / 127, hot = pv_hot(), x, py = 0;
    int32_t e = 32768, kd = 150733 / (x2 - x1), kr = 150733 / (x4 - x3);
    uint16_t c = T_THEME, fill = ux_mix(T_PANEL, T_THEME, 14);
    char v[12], b[20];
    const char *unit;
#define PVY(l) (bot * 16 - (l) * (bot - top) * 16 / 1000)
    for (x = x0; x <= x4; x++) {                        /* the curve in 1/16 px */
        int32_t l, y, seg = x < x1 ? 0 : x < x2 ? 1 : x < x3 ? 2 : 3;
        if (seg == 0) l = (x - x0) * 1000 / (x1 - x0 ? x1 - x0 : 1);
        else if (seg == 1) { e = x == x1 ? 32768 : (e * (32768 - kd)) >> 15; l = sus + ((1000 - sus) * e >> 15); }
        else if (seg == 2) l = sus;
        else { e = x == x3 ? 32768 : (e * (32768 - kr)) >> 15; l = sus * e >> 15; }
        y = PVY(l);
        cv_rect(x, y / 16 + 1, 1, bot - y / 16, fill);
        cv_vspan_aa(x, x > x0 ? py : y, y, seg == hot ? T_TEXT : c);
        py = y;
    }
    {   /* the handles: the attack's peak, the decay's end, the sustain's end, the release's end */
        const int32_t hx[4] = {x1, x2, x3, x4}, hy[4] = {top, PVY(sus) / 16, PVY(sus) / 16, bot};
        uint32_t i;
        for (i = 0; i < 4u; i++) {
            int32_t d = (int32_t)i == hot ? 11 : 9;
            cv_disc(hx[i], hy[i], d + 4, T_PANEL, 0);
            cv_disc(hx[i], hy[i], d, (int32_t)i == hot ? T_TEXT : c, 0);
        }
        /* each value on its segment: the attack's by the peak, the decay's on its slope, the sustain's over it, the
         * release's under its end; the one turning larger, in text */
        for (i = 0; i < 4u; i++) {
            const aafont_t *f = (int32_t)i == hot ? &AF_M : &AF_X;
            uint16_t tc = (int32_t)i == hot ? T_TEXT : T_MID;
            int32_t ys = hy[2];
            pv_value(i, v, &unit);
            str_cpy(b, v, sizeof b);
            if (unit[0]) { str_cpy(b + str_len(b), " ", 2); str_cpy(b + str_len(b), unit, 6); }
            if (i == 0u) pv_text(x1 + 8, top - 13 < 2 ? 2 : top - 13, f, b, tc, 0);
            else if (i == 1u) pv_text(x1 + (x2 - x1) / 2 + 8, top + (ys - top) / 2 - 6, f, b, tc, 0);
            else if (i == 2u) pv_text((x2 + x3) / 2, ys - 6 - f->h, f, b, tc, 1);
            else pv_text(x4 - 8, bot + 4 > h - f->h - 2 ? h - f->h - 2 : bot + 4, f, b, tc, 2);
        }
    }
#undef PVY
}
/* LFO: two cycles from its phase, its reach dashed (the fade in, then the depth), the zero line; the rate at the top,
 * where it goes (the strongest of LFO DEST's) beside it */
static void pv_lfo(int32_t h)
{
    static const uint8_t DST[4] = {P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP};
    static const char *const DST_NAME[4] = {"Pitch", "Filter", "Shape", "Level"};
    const track_t *t = TSEL;
    int32_t uni = t->p[P_LPOL] != 0, cy = uni ? h - 26 : h / 2, amp = uni ? h - 66 : 48, x, py = 0;
    int32_t fw = t->p[P_LFADE] * 120 / 127, x0 = 10, w = PV_PANEL_W - 20, best = 0, bi = -1, hot = pv_hot();
    uint32_t ph = (uint32_t)t->p[P_LPHASE] << 25, i;
    char v[12], b[20];
    const char *unit;
    cv_rect(x0, cy, w, 1, T_LINE);
    {                                                   /* its reach, dashed: the fade in, then the depth */
        uint16_t gc = hot == 3 ? T_TEXT : ux_mix(T_PANEL, T_THEME, 35);
        for (x = 0; x < w; x += 5) {
            int32_t lv = x < fw ? x * amp / fw : amp;
            cv_rect(x0 + x, cy - lv, 2, 1, gc);
        }
    }
    for (x = 0; x < w; x++) {                           /* two cycles (lfo_wave only reads the track) */
        int32_t wv = lfo_wave((track_t *)t, ph + (uint32_t)x * (0xFFFFFFFFu / ((uint32_t)w / 2u))), y, f = fw && x < fw ? x * 1024 / fw : 1024;
        if (t->p[P_LWAVE] == 4)
            wv = (int32_t)((x / 20 * 2654435761u) >> 16) - 32768;
        y = uni ? cy * 16 - (wv + 32768) * amp / 4096 * f / 1024 : cy * 16 - wv * amp / 2048 * f / 1024;
        cv_vspan_aa(x0 + x, x ? py : y, y, T_THEME);
        py = y;
    }
    pv_value(0, v, &unit);                              /* the rate */
    str_cpy(b, v, sizeof b);
    if (unit[0]) { str_cpy(b + str_len(b), " ", 2); str_cpy(b + str_len(b), unit, 6); }
    pv_text(10, 8, &AF_S, b, T_TEXT, 0);
    for (i = 0; i < 4u; i++) {                          /* where it goes: the strongest destination */
        int32_t a = t->p[DST[i]] < 0 ? -t->p[DST[i]] : t->p[DST[i]];
        if (a > best) best = a, bi = (int32_t)i;
    }
    if (bi >= 0 && pv.slice0 == 0) {
        int32_t cw = text_w(&AF_X, DST_NAME[bi]) + 26;
        uint16_t cb = ux_mix(T_PANEL, T_THEME, 20);
        cv_rrect(PV_PANEL_W - 8 - cw, 8, cw, 16, 4, cb, T_PANEL);
        cv_icon_mid(PV_PANEL_W - 8 - cw + 5, 16, 12, ICON_X_RIGHT, T_THEME, cb);
        cv_text_on(PV_PANEL_W - 8 - cw + 19, 10, &AF_X, DST_NAME[bi], T_THEME, cb);
    }
}
static void pv_curve(int32_t h)
{
    if (cur_page()->graph == GR_ADSR)
        pv_env(h);
    else
        pv_lfo(h);
}
static uint32_t pv_curve_sig(void)
{
    const page_t *pg = cur_page();
    uint32_t h = 2166136261u, c;
    for (c = 0; c < 4u; c++)
        h = (h ^ (uint32_t)(pg->id[c] < P_COUNT ? TSEL->p[pg->id[c]] : 0)) * 16777619u;
    h = (h ^ (uint32_t)(TSEL->p[P_LD_PIT] + 131 * TSEL->p[P_LD_FLT] + 17161 * TSEL->p[P_LD_SHP])) * 16777619u;
    h = (h ^ (uint32_t)(TSEL->p[P_LD_AMP] + 256 * TSEL->p[P_LPOL] + 65536 * TSEL->p[P_LWAVE])) * 16777619u;
    return h ^ (uint32_t)(pv_hot() + 1) * 40503u ^ ux.gen * 977u ^ ux.pal * 31u ^ ui.page * 7919u ^ song.sel * 13u;
}

/* ----------------------------------------------------------- draw --- */
static void pv_draw(void)
{
    uint32_t kind = pv_kind(), sig;
    if (ui.force) {                                     /* the BG the parts do not cover */
        uint32_t i;
        lcd_fill(0, H_HEAD, 240, PV_CURVE_Y - H_HEAD, T_BG);
        lcd_fill(0, PV_CURVE_Y, PV_PANEL_X, PV_CURVE_H, T_BG);
        lcd_fill(PV_PANEL_X + PV_PANEL_W, PV_CURVE_Y, 240u - PV_PANEL_X - PV_PANEL_W, PV_CURVE_H, T_BG);
        lcd_fill(0, PV_CURVE_Y + PV_CURVE_H, 240, PV_STRIP_Y - PV_CURVE_Y - PV_CURVE_H, T_BG);
        lcd_fill(0, PV_STRIP_Y, (uint32_t)CARD_X(0), PV_STRIP_H, T_BG);
        for (i = 0; i < 4u; i++)
            lcd_fill((uint32_t)(CARD_X(i) + CARD_W), PV_STRIP_Y, i < 3u ? (uint32_t)(CARD_X(i + 1u) - CARD_X(i) - CARD_W) :
                     240u - (uint32_t)(CARD_X(i) + CARD_W), PV_STRIP_H, T_BG);
        lcd_fill(0, PV_STRIP_Y + PV_STRIP_H, 240, PV_SOUND_Y - PV_STRIP_Y - PV_STRIP_H, T_BG);
    }
    draw_head();
    col_style = CS_STRIP;
    draw_columns();
    col_style = CS_CARD;
    pv_sound();
    (void)kind;
    sig = pv_curve_sig();
    if (ui.force || sig != pv.panel) {                  /* (the busiest last) */
        pv.panel = sig;
        pv_panel(PV_CURVE_Y, PV_CURVE_H, pv_curve);
    }
}
