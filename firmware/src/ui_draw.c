/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Melodee UI drawing: status bar (top), columns + gauges, graphs, focus readout,
 * footer (steps + engine / preset / page). */
static void draw_menu(void);
static uint32_t str_hash(uint32_t h, const char *s);
static uint16_t page_color(void);

/* Sample every UI frame, even away from SYSTEM. ON means the input is enabled,
 * not that a cable is connected; RX holds for 250 ms after incoming bytes. */
static uint32_t ui_trs_state;
static void ui_midi_status_tick(void)
{
#if MELODEE_UART
    static uint32_t last_bytes, last_rx_ms;
    static uint8_t active;
    uint32_t bytes = um.bytes, now = fm1_ms;
    if (bytes != last_bytes) {
        last_bytes = bytes;
        last_rx_ms = now;
        active = 1;
    }
    if (active && (uint32_t)(now - last_rx_ms) >= 250u)
        active = 0;
    ui_trs_state = active ? 2u : 1u;
#else
    ui_trs_state = 0;
#endif
}

/* --------------------------------------------------------- drawing --- */
static int is_eng_name(const char *s)                 /* one of the ENGINES[]->name strings */
{
    uint32_t e;
    for (e = 0; e < NENGINES; e++)
        if (s == ENGINES[e]->name)
            return 1;
    return 0;
}

/* at most 5 characters, and no wider than maxw */
static void fit(char *d, const char *src, const melodee_font_t *f, int32_t maxw)
{
    str_cpy(d, src, is_eng_name(src) ? 8 : 6);           /* engine names are kept whole */
    while (d[0] && text_w(f, d) > maxw)
        d[str_len(d) - 1u] = 0;
}

static int32_t batt_level(void)                         /* ADC ch3 thresholds */
{
    return song.batt_raw >= 591 ? 3 : song.batt_raw >= 561 ? 2 : song.batt_raw >= 531 ? 1 : 0;
}
/* charging (a USB host powers us; there is no charger status line): bars fill 1, 2, 3
 * every 0.6 s, so the header is redrawn twice a second at most; else the level */
static int32_t batt_shown(void)
{
    if (usb.config && !usb.suspended)
        return 1 + (int32_t)((fm1_ms / 600u) % 3u);
    return batt_level();
}

/* top bar: transport, BPM, octave | USB, battery, CPU; messages replace it */
static void draw_head(void)
{
    char b[16];
    uint32_t i;
    int32_t x;
    uint32_t rec = (song.rec >> song.sel) & 1u ? 2u : song.rec != 0u;   /* 2 the selected track armed, 1 another */
    uint32_t sig = (uint32_t)song.playing * 3u + rec * 5u + (uint32_t)(song.octave + 8) * 11u + song.sel * 13131u +
                   (ui.msg_t ? str_hash(7u, ui.msg) : 0u) + (uint32_t)song.g[G_BPM] * 101u + (ui.bpm_t != 0) * 31u +
                   (uint32_t)batt_shown() * 7777u + (usb.config && !usb.suspended) * 99991u;
    if (!ui.force && sig == ui.head_sig)
        return;
    ui.head_sig = sig;
    cv_begin(240, H_HEAD, C_BLACK);
    if (ui.msg_t) {
        cv_text(4, 1, &FONT_S, ui.msg, C_HI);
        cv_rect(0, 18, 240, 2, C_LINE);
        cv_rect(0, 18, 56, 2, page_color());
        cv_blit(0, Y_HEAD);
        return;
    }
    if (song.playing) {                               /* > play, square stop */
        for (i = 0; i < 5u; i++)
            cv_rect(4 + (int32_t)i * 2, 4 + (int32_t)i, 2, 10 - 2 * (int32_t)i, C_WHITE);
    } else {
        cv_rect(4, 5, 8, 8, page_color());
    }
    if (rec)                                          /* recording armed: white = this track, gray = another */
        cv_rect(18, 6, 6, 6, rec == 2u ? C_WHITE : C_GRAY);
    fmt_int(b, song.g[G_BPM]);
    x = 32;
    if (MELODEE_ICONS) {                              /* metronome, then the BPM */
        cv_icon(x, 2, ICON_TEMPO, C_GRAY);
        x += 14;
    }
    x = cv_text(x, 1, &FONT_S, b, ui.bpm_t ? C_WHITE : C_HI);   /* white while SELECT turns it */
    if (song.octave) {
        str_cpy(b, song.octave > 0 ? "+" : "", 4);
        fmt_int(b + str_len(b), song.octave);
        cv_text(x + 12, 1, &FONT_S, "OCT", C_GRAY);
        cv_text(x + 40, 1, &FONT_S, b, C_HI);
    }
    if (MELODEE_ICONS) {                              /* the selected track: tape + number */
        cv_icon(156, 2, ICON_TAPE, C_GRAY);
        b[0] = (char)('1' + song.sel);
        b[1] = 0;
        cv_text(170, 1, &FONT_S, b, C_HI);
    } else {
        b[0] = 'T';
        b[1] = (char)('1' + song.sel);
        b[2] = 0;
        cv_text(158, 1, &FONT_S, b, C_HI);
    }
    {   /* battery, 3 bars; USB when a host is there */
        int32_t lvl = batt_shown(), k;
        int32_t bx = 236 - 19;                          /* right edge (the CPU figure is in the console) */
        cv_rect(bx, 4, 17, 1, C_GRAY);
        cv_rect(bx, 12, 17, 1, C_GRAY);
        cv_rect(bx, 4, 1, 9, C_GRAY);
        cv_rect(bx + 16, 4, 1, 9, C_GRAY);
        cv_rect(bx + 17, 6, 2, 5, C_GRAY);
        for (k = 0; k < lvl; k++)
            cv_rect(bx + 2 + k * 5, 6, 3, 5, lvl == 1 && batt_level() <= 1 ? C_WHITE : C_HI);
        if (usb.config && !usb.suspended)
            cv_text(bx - 28, 1, &FONT_S, "USB", C_DIM);
    }
    cv_rect(0, 18, 240, 2, C_LINE);
    cv_rect(0, 18, 56, 2, page_color());
    cv_blit(0, Y_HEAD);
}
/* full redraw: the strips (head, columns, graph, foot) cover the rest, so only
 * the space between them is cleared, then the rules are drawn */
static void draw_frame(void)
{
    uint32_t i;
    lcd_fill(0, Y_FOOT + H_FOOT, 240, Y_GRAPH - Y_FOOT - H_FOOT, C_BLACK);
    lcd_fill(0, Y_GRAPH + H_GRAPH, 240, Y_LABEL - Y_GRAPH - H_GRAPH, C_BLACK);
    lcd_fill(0, Y_SEP_END, 240, Y_STEP - Y_SEP_END, C_BLACK);
    for (i = 0; i < 4u; i++) {
        lcd_fill(i * 60u + 4u, Y_LABEL - 3u, 52, 2, control_color(i));
        if (i)
            lcd_fill(i * 60u, Y_LABEL, 1, Y_SEP_END - Y_LABEL, C_LINE);
    }
    lcd_fill(0, Y_GRAPH - 2u, 240, 1, C_LINE);
    lcd_fill(0, Y_STEP - 2u, 240, 1, C_LINE);
}

/* one column: [icon] LABEL / value unit / gauge, redrawn only when it changed.
 * ratio: 0..1000 for the gauge, -1 = no gauge. icon: ICON_* (icons.c), ICON_AUTO = by label */
#define LABEL_X (MELODEE_ICONS ? ICON_CELL + ICON_GAP : 0)
static void draw_column(uint32_t c, const char *label, const char *val, const char *unit, uint16_t vc,
                        int32_t ratio, uint32_t icon)
{
    char l[8], v[8], u[8], key[32];
    int32_t x, gw = 52, fx;
    if (icon == ICON_AUTO)
        icon = icon_for_label(label);
    fit(l, label, &FONT_S, 54 - LABEL_X);
    fit(v, val, &FONT_S, is_eng_name(val) ? 56 : 40);   /* engine names whole */
    fit(u, unit, &FONT_S, 54 - text_w(&FONT_S, v) - 3);
    str_cpy(key, l, 8);                                 /* cache key: texts + colour + gauge */
    str_cpy(key + str_len(key), "|", 2);
    str_cpy(key + str_len(key), v, 8);
    str_cpy(key + str_len(key), "|", 2);
    str_cpy(key + str_len(key), u, 8);
    {
        uint32_t n = str_len(key);
        key[n] = (char)('A' + (vc == C_WHITE) + (vc == C_DIM) * 2);
        key[n + 1] = (char)(' ' + (ratio < 0 ? 0 : 1 + ratio / 20));
        key[n + 2] = (char)(icon == ICON_NONE ? '~' : '!' + icon % 90u);   /* same label, other icon */
        key[n + 3] = 0;
    }
    if (c == ui.hot_col) {
        str_cpy(ui.focus_l, l, 8);
        str_cpy(ui.focus_v, v, 8);
        str_cpy(ui.focus_u, u, 8);
    }
    if (!ui.force && str_eq(key, ui.col[c]))
        return;
    str_cpy(ui.col[c], key, sizeof ui.col[c]);
    cv_begin(55, Y_SEP_END - Y_LABEL, C_BLACK);         /* x 4..58: the rule at 59 stays */
    if (MELODEE_ICONS && icon != ICON_NONE && l[0])
        cv_icon(0, 1, icon, control_color(c));
    else if (l[0])
        cv_rect(0, 5, 5, 5, control_color(c));
    cv_text(l[0] ? LABEL_X : 0, 0, &FONT_S, l, C_GRAY);
    x = cv_text(0, Y_VALUE - Y_LABEL, &FONT_S, v, vc);
    cv_text(x + 3, Y_VALUE - Y_LABEL, &FONT_S, u, C_DIM);
    if (ratio >= 0) {                                   /* gauge: track, fill, 1 px end line */
        int32_t gy = Y_GAUGE - Y_LABEL;
        fx = ratio * gw / 1000;
        cv_rect(0, gy + 1, gw, 1, C_LINE);
        cv_rect(0, gy, fx, 3, control_color(c));
        cv_rect(fx, gy - 1, 2, 5, vc == C_WHITE ? C_WHITE : control_color(c));
    }
    cv_blit(c * 60u + 4u, Y_LABEL);
}

/* Matches voice.c: attack is linear, decay and release are exponential
 * (env += (target - env) * k each tick, ~99 % after the set time). Time
 * axis is the parameter value (the times themselves are exponential). */
static void graph_adsr(const track_t *t, uint16_t c)
{
    (void)c;
    int32_t a = 4 + t->p[P_ATK] * 50 / 127, d = 6 + t->p[P_DEC] * 50 / 127, r = 6 + t->p[P_REL] * 60 / 127;
    int32_t top = 8, bot = 90, sus = t->p[P_SUS] * 1000 / 127;          /* 0..1000 */
    int32_t x0 = 6, x1 = x0 + a, x3 = 232 - r, i, px, py;
    int32_t e = 32768;                                                  /* exp(-4.6 u), Q15 */
#define EGY(lvl) (bot - (lvl) * (bot - top) / 1000)
    cv_line(x0, bot, x1, top, control_color(0));                        /* attack */
    px = x1;
    py = top;
    for (i = 1; i <= d; i++) {                                          /* decay: exponential to SUS */
        int32_t lvl;
        e = (e * (32768 - 150733 / d)) >> 15;           /* k^d = exp(-4.6) */
        lvl = sus + ((1000 - sus) * e >> 15);
        cv_line(px, py, x1 + i, EGY(lvl), control_color(1));
        px = x1 + i;
        py = EGY(lvl);
    }
    cv_line(px, py, x3, EGY(sus), control_color(2));                     /* sustain */
    px = x3;
    py = EGY(sus);
    e = 32768;
    for (i = 1; i <= r; i++) {                                          /* release: exponential to 0 */
        e = (e * (32768 - 150733 / r)) >> 15;
        cv_line(px, py, x3 + i, EGY(sus * e >> 15), control_color(3));
        px = x3 + i;
        py = EGY(sus * e >> 15);
    }
    cv_line(0, bot + 1, 239, bot + 1, C_LINE);
#undef EGY
}

static void graph_lfo(const track_t *t, uint16_t c)
{
    int32_t x, py = 50;
    uint32_t ph = (uint32_t)t->p[P_LPHASE] << 25;
    for (x = 0; x < 240; x++) {                      /* (lfo_wave only reads the track) */
        int32_t y = 50 - lfo_wave((track_t *)t, ph + (uint32_t)x * (0xFFFFFFFFu / 120u)) * 38 / 32768;
        if (t->p[P_LWAVE] == 4)
            y = 50 - ((int32_t)((x / 20 * 2654435761u) >> 16) - 32768) * 38 / 32768;
        if (x)
            cv_line(x - 1, py, x, y, c);
        py = y;
    }
    cv_line(0, 50, 239, 50, C_LINE);
}

static void graph_steps(const track_t *t, uint16_t c)
{
    uint32_t row, i, bank = ui.bank * 16u;
    (void)t;
    (void)c;
    cv_text(3, 0, &FONT_S, "FOUR TRACKS", C_GRAY);
    for (row = 0; row < NTRK; row++) {
        const track_t *part = &trk[row];
        uint16_t col = control_color(row);
        int32_t y = 23 + (int32_t)row * 24;
        char label[3] = {'T', (char)('1' + row), 0};
        cv_text(2, y, &FONT_S, label, row == song.sel ? C_WHITE : col);
        if (row == song.sel)
            cv_rect(0, y + 19, 236, 1, col);
        for (i = 0; i < 16u; i++) {
            uint32_t index = bank + i;
            int32_t x = 29 + (int32_t)i * 13;
            if (i % 4u == 0u)
                cv_rect(x - 2, y - 2, 1, 18, C_LINE);
            if (index >= (uint32_t)part->p[P_SLEN])
                continue;
            if (step_on(&part->step[index]))
                cv_rect(x, y + 4, 9, 10, part->step[index].flags & SF_ACCENT ? C_WHITE : col);
            else if (part->step[index].time == ST_TIE)
                cv_rect(x, y + 10, 9, 2, C_DIM);
            else
                cv_rect(x + 3, y + 12, 3, 2, C_DIM);
            if ((song.playing && index == part->seq_idx) || (row == song.sel && index == ui.cursor))
                cv_rect(x, y + 16, 9, 2, C_WHITE);
        }
    }
}
/* STEP page: the cursor's 16-step bank as a little piano roll */
static void graph_roll(const track_t *t, uint16_t c)
{
    uint32_t i, j, len = (uint32_t)t->p[P_SLEN], base = ui.bank * 16u;
    int32_t lo = 127, hi = 0;
    for (i = 0; i < len; i++)
        if (step_on(&t->step[i]))
            for (j = 0; j < t->step[i].n; j++) {
                if (t->step[i].note[j] < lo)
                    lo = t->step[i].note[j];
                if (t->step[i].note[j] > hi)
                    hi = t->step[i].note[j];
            }
    if (lo > hi) {
        lo = 48;
        hi = 72;
    }
    if (hi - lo < 12)
        hi = lo + 12;
    for (i = 0; i < 16u; i++) {
        uint32_t si = base + i;
        const step_t *st = &t->step[si];
        const step_t *source = st;
        uint32_t tie = st->time == ST_TIE, start, next_tie;
        int32_t x = (int32_t)i * 15, yb = 86;
        if (si >= len)
            break;
        if (si == ui.cursor)
            cv_rect(x + 5, 92, 3, 3, C_WHITE);
        if (song.playing && si == t->seq_idx)
            cv_rect(x + 1, 97, 12, 1, C_WHITE);
        if (tie) {
            start = step_note_start(t, si);
            if (start < NSTEP)
                source = &t->step[start];
        }
        if (!step_on(source)) {
            cv_rect(x + 6, yb, 2, 1, C_DIM);
            continue;
        }
        next_tie = t->step[(si + 1u) % len].time == ST_TIE;
        for (j = 0; j < source->n; j++) {
            int32_t y = 80 - (source->note[j] - lo) * 74 / (hi - lo);
            cv_rect(x + (tie ? 0 : 2), y, (tie ? 12 : 10) + (next_tie ? 3 : 0), 1,
                    tie ? C_GRAY : (st->flags & SF_ACCENT) ? C_WHITE : c);
        }
        if (st->flags & SF_SLIDE) {
            int32_t y = 80 - (source->note[0] - lo) * 74 / (hi - lo);
            cv_line(x + 11, y, x + 17, y + 2, c);
        }
    }
}
static void graph_scale(const track_t *t, uint16_t c)
{
    static const uint8_t BLACK[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};
    uint32_t i, mask = scale_mask(t);
    for (i = 0; i < 12u; i++) {
        uint32_t deg = (i + 12u - (uint32_t)t->p[P_ROOT]) % 12u;
        int32_t x = 6 + (int32_t)i * 19;
        uint16_t col = (mask >> deg) & 1u ? (deg == 0u ? C_WHITE : c) : C_DIM;
        cv_rect(x, BLACK[i] ? 10 : 40, 16, 1, col);
        cv_rect(x + 7, BLACK[i] ? 10 : 40, 1, 40, col);
    }
}

static void mpc_pad_text(const track_t *t, char *b)
{
    uint32_t n = midi_map(t, 21), len;
    str_cpy(b, "H02: ", 24);
    fmt_int(b + str_len(b), mpc_degree(t));
    str_cpy(b + str_len(b), " -> ", 24 - str_len(b));
    len = str_len(b);
    if (n == KB_SILENT)
        str_cpy(b + len, "SILENT", 24 - len);
    else
        note_name(b + len, n);
}

static void graph_mpc(const track_t *t, uint16_t c)
{
    char b[24];
    mpc_pad_text(t, b);
    cv_text((240 - text_w(&FONT_S, b)) / 2, 24, &FONT_S, b, c);
    cv_text(28, 56, &FONT_S, "H01-H16: BANK H ONLY", C_DIM);
}

static void graph_fx(const track_t *t, uint16_t c)
{
    (void)c;
    uint32_t i;
    for (i = 0; i < 4u; i++) {
        int32_t h = t->p[P_DIST + i] * 80 / 127, x = (int32_t)i * 60 + 28;
        cv_rect(x, 10, 1, 80, C_LINE);
        cv_rect(x, 90 - h, 1, h, control_color(i));
        cv_rect(x - 3, 90 - h, 7, 1, control_color(i));
    }
}
/* SLICER page: the pattern's 16 steps, a 'x' step a full bar; a '.' step: GATE a bar as high as
 * it stays open (DEPTH), STUT hatched (it repeats the last 'x'); the step playing underlined.
 * Grey when the SLICER is OFF. */
static void graph_slicer(const track_t *t, uint16_t c)
{
    uint32_t i, pat = sl_pattern(t), mode = (uint32_t)t->p[P_SLCR], cur = sl[t - trk].idx;
    int32_t open = 70 - t->p[P_SLDEPTH] * 70 / 127;      /* px a closed GATE step keeps */
    uint16_t col = mode == SL_OFF ? C_DIM : c;
    for (i = 0; i < 16u; i++) {
        int32_t x = 4 + (int32_t)i * 14 + (int32_t)(i / 4u) * 2, y;
        if ((pat >> i) & 1u) {
            cv_rect(x, 10, 11, 70, col);
        } else if (mode == SL_STUT) {
            for (y = 10; y < 80; y += 4)
                cv_rect(x, y, 11, 1, col);
        } else {
            cv_rect(x, 79, 11, 1, C_LINE);
            if (open)
                cv_rect(x, 80 - open, 11, open, C_DIM);
        }
        if (mode != SL_OFF && i == cur)
            cv_rect(x, 85, 11, 3, C_WHITE);
    }
}

/* ------------------------------------------------------------- FM6 --- */
/* the algorithm as a DX7 draws it: carriers on the bottom row, each modulator over what it
 * modulates (one modulating several: over their middle); the selected operator filled, a
 * switched-off one dim, the feedback loop on its operator */
static uint32_t fm_mods[7];                          /* bit m: OP m modulates OP n (FM6_ALG routing) */
static int32_t fm_x[7], fm_next;                     /* column, in half columns */
static uint8_t fm_done[7];

static void fm_place(uint32_t op)
{
    uint32_t m;
    int32_t sum = 0, n = 0;
    fm_done[op] = 1;
    for (m = 1; m <= 6u; m++)
        if ((fm_mods[op] >> m) & 1u && !fm_done[m]) {
            fm_place(m);
            sum += fm_x[m];
            n++;
        }
    fm_x[op] = n ? sum / n : 2 * fm_next++;
}

static void graph_fmalg(uint16_t c)
{
    const int16_t *ed = fm6_ed[song.sel % NPART];
    uint32_t alg = (uint32_t)ed[FV_ALG] & 31u, bus[3] = {0, 0, 0}, car = 0, fb = 0, k, n, m;
    uint32_t depth[7] = {0};
    int32_t px[7], py[7];
    for (k = 0; k < 6u; k++) {                       /* the routing, as the engine runs it */
        uint32_t f = FM6_ALG[alg][k], op = 6u - k, dst = f & 3u, src = (f >> 4) & 3u;
        fm_mods[op] = src ? bus[src] : 0u;
        if (f & 0x40u)
            fb = op;
        if (!dst)
            car |= 1u << op;
        else
            bus[dst] = (f & 4u ? bus[dst] : 0u) | 1u << op;
    }
    for (n = 1; n <= 6u; n++)                        /* modulators sit a row over their highest target */
        for (m = n + 1u; m <= 6u; m++)
            if ((fm_mods[n] >> m) & 1u && depth[m] < depth[n] + 1u)
                depth[m] = depth[n] + 1u;
    fm_next = 0;
    for (n = 1; n <= 6u; n++)
        fm_done[n] = 0;
    for (n = 1; n <= 6u; n++)
        if ((car >> n) & 1u && !fm_done[n])
            fm_place(n);
    for (m = 1; m <= 6u; m++) {                      /* one modulating several: over their middle */
        int32_t sum = 0, cnt = 0;
        for (n = 1; n < m; n++)
            if ((fm_mods[n] >> m) & 1u) {
                sum += fm_x[n];
                cnt++;
            }
        if (cnt > 1)
            fm_x[m] = sum / cnt;
    }
    for (n = 1; n <= 6u; n++) {
        px[n] = 240 * (fm_x[n] + 1) / (2 * (fm_next ? fm_next : 1)) - 10;
        py[n] = 64 - (int32_t)depth[n] * 21;
    }
    cv_line(8, 90, 231, 90, C_LINE);                 /* the output */
    for (n = 1; n <= 6u; n++) {
        if ((car >> n) & 1u)
            cv_line(px[n] + 10, py[n] + 17, px[n] + 10, 90, C_LINE);
        for (m = 1; m <= 6u; m++)
            if ((fm_mods[n] >> m) & 1u)
                cv_line(px[m] + 10, py[m] + 17, px[n] + 10, py[n] - 1, C_GRAY);
    }
    if (fb && ed[FV_FB]) {                           /* the feedback loop */
        int32_t x = px[fb], y = py[fb];
        cv_line(x + 20, y + 8, x + 25, y + 8, C_GRAY);
        cv_line(x + 25, y + 8, x + 25, y - 4, C_GRAY);
        cv_line(x + 25, y - 4, x + 10, y - 4, C_GRAY);
        cv_line(x + 10, y - 4, x + 10, y - 1, C_GRAY);
    }
    for (n = 1; n <= 6u; n++) {
        char d[2] = {(char)('0' + n), 0};
        uint16_t col = ed[FV_ON + n - 1u] ? C_HI : C_DIM;
        if (n == fm6_opsel + 1u) {
            cv_rect(px[n], py[n], 20, 17, col == C_DIM ? C_GRAY : c);
            cv_text(px[n] + 6, py[n] + 1, &FONT_S, d, C_BLACK);
        } else {
            cv_rect(px[n], py[n], 20, 1, col);
            cv_rect(px[n], py[n] + 16, 20, 1, col);
            cv_rect(px[n], py[n], 1, 17, col);
            cv_rect(px[n] + 19, py[n], 1, 17, col);
            cv_text(px[n] + 6, py[n] + 1, &FONT_S, d, col);
        }
    }
}

/* a DX7 envelope: from L4 through L1, L2, L3 (held), back to L4; segments longer for lower rates */
static void graph_fmenv(const int16_t *r, const int16_t *l, int pitch, uint16_t c)
{
    (void)c;
    int32_t x = 6, i, top = 6, bot = 84, y0, y1;
#define FMY(v) (bot - (v) * (bot - top) / 99)
    y0 = FMY(l[3]);
    if (pitch)
        cv_line(0, FMY(50), 239, FMY(50), C_LINE);   /* the note's own pitch */
    for (i = 0; i < 4; i++) {
        int32_t w = 6 + (99 - r[i]) * 42 / 99, xe;
        if (i == 3) {                                /* held: L3, then the release */
            cv_line(x, y0, x + 26, y0, control_color(2));
            x += 26;
        }
        xe = x + w;
        y1 = FMY(l[i]);
        cv_line(x, y0, xe, y1, control_color((uint32_t)i));
        x = xe;
        y0 = y1;
    }
    cv_line(x, y0, 236, y0, C_DIM);
    cv_line(0, bot + 2, 239, bot + 2, C_LINE);
#undef FMY
}

static void graph_fmstore(uint16_t c)
{
    static int16_t ed[FM6_NP];
    char nm[12], b[32];
    fm6_unpack(ed, fm6_bank[fm6_slot % FM6_NUSER]);
    fm6_name(nm, ed);
    str_cpy(b, N_FM6V[FM6_NROM + fm6_slot % FM6_NUSER], sizeof b);
    str_cpy(b + str_len(b), ": ", 4);
    str_cpy(b + str_len(b), nm, 12);
    cv_text(4, 20, &FONT_S, "IN THE SLOT", C_GRAY);
    cv_text(4, 38, &FONT_S, b, c);
    cv_text(4, 62, &FONT_S, "STORE PUTS THIS VOICE THERE", C_DIM);
}
static uint32_t steps_hash(const track_t *t)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < NSTEP; i++) {
        const step_t *st = &t->step[i];
        h = (h ^ (st->note[0] + st->n * 128u + st->time * 1024u + st->flags * 4096u + st->note[1] * 65536u)) *
            16777619u;
    }
    return h;
}

static uint32_t str_hash(uint32_t h, const char *s)
{
    while (*s)
        h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

static uint32_t graph_signature(void)
{
    const page_t *pg = cur_page();
    const track_t *t = TSEL;
    uint32_t h = 2166136261u, i;
    if (ui.hot_t && settings.zoom)
        h = str_hash(str_hash(str_hash(h ^ 0x5555u, ui.focus_v), ui.focus_l), ui.focus_u);
    if (ui.home)
        return h ^ (ui.frame / 2u);                  /* scope: redraw every other frame */
    h ^= (uint32_t)pg->graph * 131u + TSEL->eng_req + song.sel * 7777u;
    if (pg->graph == GR_MPC)
        h = (h ^ (uint32_t)(song.octave + 3)) * 16777619u;
    for (i = 0; i < P_COUNT; i++)
        h = (h ^ (uint32_t)t->p[i]) * 16777619u;
    if (pg->scope == SC_ENGINE && pg->id[0] == P_E0)
        for (i = 0; i < 4u; i++) {
            h = (h ^ live_last[i]) * 16777619u;
            h = (h ^ live_held[i]) * 16777619u;
        }
    if (pg->scope == SC_GLOBAL && pg->graph == GR_NONE)
        for (i = 0; i < G_COUNT; i++)
            h = (h ^ (uint32_t)song.g[i]) * 16777619u;
    h ^= (uint32_t)TSEL->preset * 7u + (uint32_t)song.g[G_SLOT] * 13u + TSEL->user * 257u + up_gen * 7919u + ui.uslot * 104729u;
    if (fm6_shown() && (pg->scope == SC_FM6 || pg->scope == SC_FMOP || pg->scope == SC_ENGINE)) {
        const int16_t *ed = fm6_ed[song.sel % NPART];   /* FM6: the voice, the operator, the STORE slot */
        for (i = 0; i < FM6_NP; i++)
            h = (h ^ (uint32_t)ed[i]) * 16777619u;
        h ^= fm6_opsel * 2246822519u + fm6_slot * 3266489917u;
        if (pg->graph == GR_FMSTORE)
            for (i = 118; i < 128u; i++)
                h = (h ^ fm6_bank[fm6_slot % FM6_NUSER][i]) * 16777619u;
    }
    if (pg->graph == GR_SLCR && t->p[P_SLCR])        /* the SLICER's step playing */
        h ^= (sl[song.sel].idx + 1u) * 2654435761u;
    if (pg->graph == GR_SLOTS) {                     /* (a checksum over each slot) */
        for (i = 0; i < 4u; i++)
            h ^= (uint32_t)project_used(i) << (20u + i);
        h ^= settings.boot * 2654435761u;
    }
    if (pg->graph == GR_STEPS) {
        for (i = 0; i < NTRK; i++)
            h ^= steps_hash(&trk[i]) + trk[i].seq_idx * (i + 31u) + trk[i].pat * (i + 101u) +
                 trk[i].pat_q * (i + 211u) + (uint32_t)trk[i].p[P_SLEN] * (i + 313u);
        h ^= ui.bank * 7u + ui.cursor * 7919u + song.sel * 17u + song.playing * 65537u;
    } else if (pg->graph == GR_ROLL) {
        uint32_t ph = song.playing ? t->seq_idx : 0xFFFFu;
        if (ph / 16u != ui.bank)
            ph = 0xFFFFu;                            /* the roll shows the cursor's bank only */
        h ^= steps_hash(t) + ph * 31u + ui.cursor * 7919u;
    }
    return h;
}
/* preset browser: the global list (every engine), current one in white */
static void graph_browse(void)
{
    uint32_t total, cur = preset_pos(&total), e, k;
    int32_t row;
    if (!total)
        return;
    for (row = -3; row <= 3; row++) {
        int32_t y = 4 + (row + 3) * 17;
        char tag[4], nm[13];
        int sel = row == 0;
        e = preset_at((cur + total * 4u + (uint32_t)row) % total, &k);
        if (e == NENGINES) {                             /* user preset: "U07" and its name */
            up_slot_label(tag, k);
            up_name(k, nm);
        } else {
            str_cpy(tag, ENGINES[e]->name, sizeof tag);
            str_cpy(nm, ENGINES[e]->presets[k].name, sizeof nm);
        }
        if (sel)
            cv_rect(4, y + 6, 3, 3, C_WHITE);
        cv_text(14, y, &FONT_S, tag, sel ? C_GRAY : C_DIM);
        cv_text(54, y, &FONT_S, nm, sel ? C_WHITE : C_GRAY);
    }
}

/* user preset slots around the selected one: "U07  NAME" / EMPTY, the selected one in white */
static void graph_user(void)
{
    int32_t row, first = clamp((int32_t)ui.uslot - 3, 0, UP_SLOTS - 7);
    for (row = 0; row < 7; row++) {
        uint32_t k = (uint32_t)(first + row);
        int32_t y = 4 + row * 17;
        char tag[4], nm[13];
        int sel = k == ui.uslot, used = up_used(k);
        up_slot_label(tag, k);
        if (used)
            up_name(k, nm);
        else
            str_cpy(nm, "EMPTY", sizeof nm);
        if (sel)
            cv_rect(4, y + 6, 3, 3, C_WHITE);
        cv_text(14, y, &FONT_S, tag, sel ? C_WHITE : C_GRAY);
        cv_text(54, y, &FONT_S, nm, used ? (sel ? C_WHITE : C_HI) : C_DIM);
    }
}

/* project slots: used / empty, the selected one in white, BOOT by the one power-on loads */
static void graph_slots(void)
{
    uint32_t i;
    for (i = 0; i < 4u; i++) {
        int32_t y = 8 + (int32_t)i * 26;
        char b[4];
        int sel = (int32_t)i + 1 == song.g[G_SLOT];
        b[0] = (char)('1' + i);
        b[1] = 0;
        if (sel)
            cv_rect(4, y + 6, 3, 3, C_WHITE);
        cv_text(14, y, &FONT_S, b, sel ? C_WHITE : C_GRAY);
        cv_text(40, y, &FONT_S, project_used(i) ? "USED" : "EMPTY", project_used(i) ? (sel ? C_WHITE : C_HI) : C_DIM);
        if (i + 1u == settings.boot)
            cv_text(110, y, &FONT_S, "BOOT", control_color(1));
    }
}

/* TRACKS page: four channel strips (mixer style), one under each column: number +
 * REC / ARM / MUTE, the sound's short name, then a level fader with the output meter
 * (note activity), the pattern over its LEN (time running down; bar width = notes in
 * the step) and the play head. The selected track is drawn bright. Every part is
 * its own small canvas with its own signature: while the transport runs only the
 * head markers and the meters move. (No ZOOM focus here.) */
#define TS_HEAD_Y (Y_GRAPH + 2)
#define TS_NAME_Y (Y_GRAPH + 20)
#define TS_Y (Y_GRAPH + 38)
#define TS_H 84                                      /* body: rows Y_GRAPH + 38 .. + 121 */
static struct {
    uint32_t head[NTRK], name[NTRK], fader[NTRK], ov[NTRK], mark[NTRK];
    uint8_t meter[NTRK];
} ts;

static int32_t meter_px(int32_t a)                   /* |sample| (Q15) -> px: 6 dB = TS_H / 10 */
{
    int32_t lg = 0, v;
    if (a < 64)
        return 0;
    while ((a >> lg) > 1)
        lg++;
    v = lg * 8 + (((a << 3) >> lg) & 7);             /* 8 log2(a): 48 (-54 dB) .. 128 (+6 dB) */
    return clamp((v - 48) * (TS_H - 2) / 80, 0, TS_H - 2);
}

static uint32_t trk_level(uint32_t c)                /* LEVEL 0..127 (the drum track: GLO > DRUMS LEVEL) */
{
    return (uint32_t)(c == TRK_DRUM ? song.g[G_DRLVL] : trk[c].p[P_LEVEL]) & 127u;
}

static void trk_short_name(uint32_t c, char *b)      /* the track's sound, b holds 13 */
{
    const track_t *t = &trk[c];
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    if (c == TRK_DRUM)
        str_cpy(b, "DRUM", 13);
    else if (e == &ENG_FM6)
        fm6_name(b, fm6_ed[c % NPART]);              /* the voice playing */
    else if (user_of(t) < UP_SLOTS)
        up_name(user_of(t), b);
    else if (e->npresets)
        str_cpy(b, e->presets[t->preset % e->npresets].name, 13);
    else
        str_cpy(b, e->name, 13);
}

static void draw_tracks(void)
{
    uint32_t c;
    if (ui.force) {
        lcd_fill(0, Y_GRAPH, 240, H_GRAPH, C_BLACK);
        for (c = 0; c < NTRK; c++)
            ts.meter[c] = 0;
    }
    for (c = 0; c < NTRK; c++) {
        track_t *t = &trk[c];
        uint32_t x0 = c * 60u + 4u, sel = c == song.sel, lvl = trk_level(c), mute = !lvl || t->p[P_MUTE];
        uint32_t arm = (song.rec >> c) & 1u, st = arm ? (song.playing ? 1u : 2u) : mute ? 3u : 0u, sig;
        uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u, row = 0xFFFFu;
        int32_t pk, m;
        char b[16];
        if (c == TRK_DRUM) {
            pk = drums.peak;
            drums.peak = 0;
        } else {
            pk = t->peak;
            t->peak = 0;
        }
        /* head: number (white = selected), REC / ARM / MUTE */
        sig = 1u + sel + st * 2u;
        if (ui.force || sig != ts.head[c]) {
            ts.head[c] = sig;
            b[0] = (char)('1' + c);
            b[1] = 0;
            cv_begin(54, 16, C_BLACK);
            cv_text(0, 0, &FONT_S, b, sel ? C_WHITE : C_GRAY);
            if (sel)
                cv_rect(0, 15, 8, 1, C_WHITE);
            if (st == 1u || st == 2u) {
                cv_rect(14, 5, 6, 6, st == 1u ? C_WHITE : C_AMB);
                cv_text(24, 0, &FONT_S, st == 1u ? "REC" : "ARM", st == 1u ? C_WHITE : C_AMB);
            } else if (st) {
                cv_text(14, 0, &FONT_S, "MUTE", C_DIM);
            }
            cv_blit(x0, TS_HEAD_Y);
        }
        /* the sound: preset name (user preset, DRUM), cut to the column */
        trk_short_name(c, b);
        while (b[0] && text_w(&FONT_S, b) > 54)
            b[str_len(b) - 1u] = 0;
        sig = str_hash(2u + sel, b);
        if (ui.force || sig != ts.name[c]) {
            ts.name[c] = sig;
            cv_begin(54, 16, C_BLACK);
            cv_text(0, 0, &FONT_S, b, sel ? C_HI : C_DIM);
            cv_blit(x0, TS_NAME_Y);
        }
        /* fader (LEVEL) + meter of the output */
        m = mute ? 0 : meter_px(pk);
        if (m < ts.meter[c] - 3)
            m = ts.meter[c] - 3;                     /* falls ~20 dB/s */
        ts.meter[c] = (uint8_t)(m < 0 ? 0 : m);
        sig = 1u + lvl + ts.meter[c] * 128u + sel * 65536u +
              ((ui.home && ui.home_view <= 1u && ui.hot_t && ui.hot_col == c) ||
               (!ui.home && sel && ui.hot_t && ui.hot_col == 1u)) * 131072u +
              (t->p[P_MUTE] != 0) * 262144u;
        if (ui.force || sig != ts.fader[c]) {
            int32_t fy = (int32_t)(TS_H - 2u) - (int32_t)lvl * (TS_H - 4) / 127;
            ts.fader[c] = sig;
            cv_begin(12, TS_H, C_BLACK);
            cv_rect(3, 0, 1, TS_H, C_LINE);
            if (lvl) {
                cv_rect(2, fy, 3, TS_H - fy, C_DIM);
                cv_rect(0, fy - 1, 7, 2, (sig & 131072u) ? C_WHITE : control_color(c));
            } else {
                cv_rect(0, TS_H - 2, 7, 2, C_DIM);
            }
            if (ts.meter[c])
                cv_rect(9, TS_H - ts.meter[c], 3, ts.meter[c], control_color(c));
            cv_blit(x0, TS_Y);
        }
        /* the pattern: one band per step over LEN, a note step as wide as its notes */
        sig = steps_hash(t) * 31u + len * 7u + sel;
        if (ui.force || sig != ts.ov[c]) {
            uint32_t i;
            ts.ov[c] = sig;
            cv_begin(34, TS_H, C_BLACK);
            cv_rect(17, 0, 1, TS_H, C_LINE);
            for (i = 0; i < len; i++) {
                const step_t *s = &t->step[i];
                int32_t r0 = (int32_t)(i * TS_H / len), r1 = (int32_t)((i + 1u) * TS_H / len), h = r1 - r0;
                if (h > 2)
                    h--;                             /* a gap between steps when there is room */
                if (h < 1)
                    h = 1;
                if (step_on(s)) {
                    int32_t w = 2 + 6 * (int32_t)s->n;
                    cv_rect(17 - w / 2, r0, w, h, (s->flags & SF_ACCENT) && sel ? C_WHITE : control_color(c));
                } else if (s->time == ST_TIE) {
                    cv_rect(16, r0, 3, r1 - r0, C_DIM);
                }
            }
            cv_blit(x0 + 14u, TS_Y);
        }
        /* play head: a small arrow right of the pattern (white while recording) */
        if (song.playing)
            row = ((t->seq_idx % len) * 2u + 1u) * TS_H / (2u * len);
        sig = 1u + row + (st == 1u) * 65536u;
        if (ui.force || sig != ts.mark[c]) {
            int32_t i;
            ts.mark[c] = sig;
            cv_begin(5, TS_H, C_BLACK);
            if (row != 0xFFFFu)
                for (i = -2; i <= 2; i++) {
                    int32_t w = 5 - 2 * (i < 0 ? -i : i);
                    cv_rect(5 - w, (int32_t)row + i, w, 1, st == 1u ? C_WHITE : C_AMB);
                }
            cv_blit(x0 + 49u, TS_Y);
        }
    }
}

static void draw_pan_mixer(void)
{
    uint32_t row, sig = 0xA79243u;
    for (row = 0; row < NTRK; row++)
        sig = (sig ^ (uint32_t)(trk[row].p[P_PAN] + 64)) * 16777619u;
    sig ^= song.sel * 517u + (ui.hot_t ? (ui.hot_col + 1u) * 4099u : 0u);
    if (!ui.force && sig == ui.graph_sig)
        return;
    ui.graph_sig = sig;
    cv_begin(240, H_GRAPH, C_BLACK);
    cv_text(4, 0, &FONT_S, "STEREO FIELD", C_GRAY);
    cv_text(53, 0, &FONT_S, "L", C_DIM);
    cv_text(221, 0, &FONT_S, "R", C_DIM);
    for (row = 0; row < NTRK; row++) {
        int32_t y = 25 + (int32_t)row * 23;
        int32_t pan = trk[row].p[P_PAN];
        int32_t pos = 140 + pan * 86 / 64;
        char name[3] = {'T', (char)('1' + row), 0};
        uint16_t col = control_color(row);
        cv_text(4, y - 7, &FONT_S, name, row == song.sel ? C_WHITE : col);
        cv_line(54, y + 4, 226, y + 4, C_LINE);
        cv_rect(139, y - 3, 2, 16, C_DIM);
        cv_rect(pos - 4, y, 9, 9, ui.hot_t && ui.hot_col == row ? C_WHITE : col);
        if (row == song.sel)
            cv_rect(2, y + 15, 234, 1, col);
    }
    cv_blit(0, Y_GRAPH);
}

/* oscilloscope of the output, triggered on a rising zero crossing */
static void graph_scope(uint16_t c, int32_t top, int32_t height)
{
    static int16_t snap[SCOPE_N];
    uint32_t w = scope_w, i, trig = 0;
    int32_t mid = top + height / 2, amp = height / 2 - 6;
    int32_t py = mid, x, peak = 1500;
    for (i = 0; i < SCOPE_N; i++) {
        snap[i] = scope_buf[(w + i) & (SCOPE_N - 1u)];
        if (snap[i] > peak)
            peak = snap[i];
        else if (-snap[i] > peak)
            peak = -snap[i];
    }
    for (i = 1; i < SCOPE_N - 240u; i++)
        if (snap[i - 1] < 0 && snap[i] >= 0) {
            trig = i;
            break;
        }
    for (x = 20; x < 240; x += 40) {
        cv_rect(x, top + 4, 1, height - 8, C_LINE);
        cv_rect(x - 2, mid - 2, 5, 1, C_GRAY);
    }
    cv_line(0, top + height / 4, 239, top + height / 4, C_LINE);
    cv_line(0, mid, 239, mid, C_DIM);
    cv_line(0, top + height * 3 / 4, 239, top + height * 3 / 4, C_LINE);
    for (x = 0; x < 240; x++) {
        int32_t y = mid - snap[trig + (uint32_t)x] * amp / peak;   /* auto-scaled */
        if (x)
            cv_line(x - 1, py, x, y, c);
        py = y;
    }
}

/* Pages without a dedicated instrument drawing still show the four live
 * controls. The same slot colours appear above the graph, at the gauge, and
 * here; an empty slot stays visibly empty. */
static void graph_controls(const page_t *pg)
{
    uint32_t i;
    if (!(fm6_shown() && (pg->scope == SC_FM6 || pg->scope == SC_FMOP || pg->scope == SC_ENGINE)))
        cv_text(6, 1, &FONT_S, "TURN KNOBS 1-4", C_GRAY);
    for (i = 0; i < 4u; i++) {
        int16_t *vp;
        const param_desc_t *d = page_desc(pg, i, &vp);
        int32_t x = 15 + (int32_t)i * 60, y = 84;
        uint16_t col = control_color(i);
        char n[2] = {(char)('1' + i), 0};
        cv_rect(x + 16, 24, 1, 60, C_LINE);
        cv_rect(x + 1, 88, 32, 1, C_LINE);
        if (d && d->label && d->label[0] != '-') {
            int32_t ratio = RATIO(d, *vp);
            if (ratio < 0)
                ratio = 500;
            ratio = clamp(ratio, 0, 1000);
            y = 82 - ratio * 52 / 1000;
            cv_rect(x + 14, y, 5, 84 - y, col);
            cv_rect(x + 8, y - 2, 17, 3, ui.hot_t && ui.hot_col == i ? C_WHITE : col);
            cv_text(x + 12, 91, &FONT_S, n, col);
        } else {
            cv_rect(x + 13, 81, 7, 2, C_DIM);
            cv_text(x + 12, 91, &FONT_S, n, C_DIM);
        }
    }
}

/* Engine pages are instrument scenes. These are deliberately compact diagrams
 * driven by the current sound parameters, leaving the four exact values below. */
static void graph_engine(const track_t *t)
{
    uint32_t i, e = t->eng_req % NENGINES;
    int32_t x, y, py = 55;
    cv_line(4, 91, 235, 91, C_LINE);
    switch (e) {
    case 0: {                                           /* ANALOG: oscillator shape and detune */
        uint32_t w = (uint32_t)t->p[P_E0] % 5u;
        int32_t duty = 17;
        for (x = 5; x < 235; x += 2) {
            int32_t ph = x % 56, v;
            if (w == 0u)
                v = 78 - ph * 48 / 56;
            else if (w == 1u)
                v = ph < 28 ? 30 : 78;
            else if (w == 4u)
                v = ph < duty ? 30 : 78;
            else if (w == 3u)
                v = 54 - sine_i((uint32_t)ph * (0xFFFFFFFFu / 56u)) * 24 / 32768;
            else {
                v = 30 + (ph < 28 ? ph : 56 - ph) * 48 / 28;
            }
            if (x > 5)
                cv_line(x - 2, py, x, v, control_color(0));
            py = v;
        }
        cv_rect(8, 84, 55 + t->p[P_E1] * 90 / 127, 2, control_color(1));
        break;
    }
    case 1:                                            /* DIGITAL: four operators and their routing */
        for (i = 0; i < 4u; i++) {
            char n[2] = {(char)('1' + i), 0};
            x = 19 + (int32_t)i * 58;
            y = 34 + (int32_t)(i == 0 ? t->p[P_E0] * 4 : t->p[P_E0 + i]) % 30;
            cv_rect(x, y, 22, 1, control_color(i));
            cv_rect(x, y + 18, 22, 1, control_color(i));
            cv_rect(x, y, 1, 19, control_color(i));
            cv_rect(x + 21, y, 1, 19, control_color(i));
            cv_text(x + 7, y + 1, &FONT_S, n, C_HI);
        }
        break;
    case 2:                                            /* PHASE: two bent phase lines */
        for (x = 5; x < 235; x += 2) {
            int32_t ph = x % 114, warp = t->p[P_E2] * 42 / 127;
            y = 30 + (ph < 57 ? ph : 114 - ph) * (42 + warp) / 57;
            if (x > 5) {
                cv_line(x - 2, py, x, y, control_color(0));
                cv_line(x - 2, 108 - py, x, 108 - y, control_color(1));
            }
            py = y;
        }
        break;
    case 3: {                                          /* LOFI: stepped, quantized motion */
        int32_t levels = 2 + (127 - t->p[P_E3]) * 6 / 127;
        for (i = 0; i < 16u; i++) {
            x = 5 + (int32_t)i * 14;
            y = 30 + (int32_t)((i * (uint32_t)(t->p[P_E2] + 7)) % (uint32_t)levels) * 47 / levels;
            cv_rect(x, y, 11, 80 - y, control_color(i & 3u));
        }
        break;
    }
    case 4:                                            /* SAMPLE: loop reel and playback path */
        for (i = 0; i < 4u; i++) {
            x = 12 + (int32_t)i * 60;
            cv_rect(x, 35, 36, 1, control_color(i));
            cv_rect(x, 72, 36, 1, control_color(i));
            cv_rect(x, 35, 1, 38, control_color(i));
            cv_rect(x + 35, 35, 1, 38, control_color(i));
            cv_rect(x + 8, 44 + (int32_t)(i * 7u + t->p[P_E0]) % 18, 20, 3, control_color(i));
        }
        cv_line(4, 83, t->p[P_E3] ? 235 : 160, 83, control_color(3));
        break;
    case 5: {                                          /* VOICE: opening and formants */
        int32_t open = 7 + t->p[P_E0] * 28 / 127;
        cv_line(25, 54, 120, 54 - open, control_color(0));
        cv_line(120, 54 - open, 215, 54, control_color(1));
        cv_line(25, 54, 120, 54 + open, control_color(2));
        cv_line(120, 54 + open, 215, 54, control_color(3));
        for (i = 0; i < 3u; i++) {
            x = 66 + (int32_t)i * 52 + t->p[P_E1] / 16;
            cv_rect(x, 31, 2, 47, C_DIM);
        }
        break;
    }
    case 6:                                            /* TRIO: three oscillator lanes */
        for (i = 0; i < 3u; i++) {
            int32_t base = 35 + (int32_t)i * 24;
            for (x = 6; x < 235; x += 5) {
                y = base + ((x / (6 + (int32_t)i * 3) + t->p[P_E0 + i]) & 1 ? -6 : 6);
                cv_rect(x, y, 4, 2, control_color(i));
            }
        }
        break;
    case 7:                                            /* WHEEL: drawbar registration */
        for (i = 0; i < 9u; i++) {
            x = 16 + (int32_t)i * 25;
            y = 75 - drw_level(t->p, i) * 5;
            cv_rect(x + 8, 28, 1, 60, C_LINE);
            cv_rect(x + 2, y, 13, 4, control_color(i & 3u));
        }
        break;
    case 8: {                                          /* GRAIN: position, size and density */
        uint32_t count = 12u + (uint32_t)t->p[P_E3] / 3u;
        int32_t center = 32 + t->p[P_E1] * 172 / 127;
        int32_t spread = 8 + t->p[P_E2] * 44 / 127;
        cv_line(center, 25, center, 88, C_GRAY);
        for (i = 0; i < count; i++) {
            x = center + (int32_t)((i * 73u) % (uint32_t)(spread * 2 + 1)) - spread;
            y = 29 + (int32_t)((i * 37u) % 55u);
            cv_rect(x, y, 2 + (int32_t)(i & 1u), 2, control_color(i & 3u));
        }
        break;
    }
#if MELODEE_SLICE
    case 10:                                           /* SLICE: start slice on a 32-part strip */
        for (i = 0; i < 32u; i++) {
            x = 5 + (int32_t)i * 7;
            cv_rect(x, 40 + (int32_t)((i * 13u) % 26u), 5, 26, i == (uint32_t)t->p[P_E2] ? C_WHITE : control_color(i & 3u));
        }
        break;
#endif
    default:
        graph_controls(cur_page());
        break;
    }
}

static void graph_drums(const track_t *t)
{
    static const char *const NAME[4] = {"KICK", "SNARE", "HAT", "CYMB"};
    uint32_t i, j;
    for (i = 0; i < 4u; i++) {
        int32_t x = 4 + (int32_t)i * 60;
        int32_t decay = i == 0u ? t->p[P_E1] : i == 1u ? t->p[P_E3] :
                        i == 2u ? t->p[P_E6] : t->p[P_E7];
        cv_text(x + 4, 28, &FONT_S, NAME[i], control_color(i));
        cv_rect(x + 2, 94, 51, 1, C_LINE);
        for (j = 0; j < 10u; j++) {
            int32_t height = 41 - (int32_t)j * (23 - decay / 5) / 10;
            if (height < 3)
                height = 3;
            cv_rect(x + 5 + (int32_t)j * 5, 91 - height, 3, height, control_color(i));
        }
    }
}

static uint16_t page_color(void)
{
    static const uint8_t SLOT[FAM_COUNT] = {
        0, 0, 3, 1, 2, 0, 2, 1, 3, 1, 2
    };
    return control_color(ui.home ? 2u : SLOT[cur_page()->fam]);
}

/* HOME: the notes played last (keys, USB and TRS MIDI; after the scale).
 * A chord: its root large, the quality small, "/bass" when another note is lowest,
 * then the notes; anything else: the notes large. White while held. */
static const struct {
    uint16_t iv;                                     /* bit i: i semitones over the root */
    char q[8];
} CHORDS[] = {                                       /* simplest first: a chord off its bass takes the first that fits */
    {0x091, ""}, {0x089, "m"}, {0x049, "dim"}, {0x111, "aug"}, {0x0A1, "sus4"}, {0x085, "sus2"},
    {0x491, "7"}, {0x891, "maj7"}, {0x489, "m7"}, {0x449, "m7b5"}, {0x249, "dim7"}, {0x4A1, "7sus4"},
    {0x291, "6"}, {0x289, "m6"}, {0x095, "add9"}, {0x08D, "madd9"}, {0x0B1, "add11"}, {0x0A9, "madd11"},
    {0x889, "mM7"}, {0x511, "7#5"}, {0x451, "7b5"}, {0x911, "maj7#5"}, {0x849, "dimM7"},
    {0x495, "9"}, {0x895, "maj9"}, {0x48D, "m9"}, {0x4A5, "9sus4"}, {0x295, "6/9"}, {0x28D, "m6/9"},
    {0x493, "7b9"}, {0x499, "7#9"}, {0x4D1, "7#11"}, {0x591, "7b13"}, {0x8D1, "maj7#11"},
    /* 11ths and 13ths, also without the 5th or the 9th */
    {0x4B5, "11"}, {0x4AD, "m11"}, {0x4A9, "m11"}, {0x42D, "m11"},
    {0x695, "13"}, {0x691, "13"}, {0x615, "13"}, {0x611, "13"},
    {0x6AD, "m13"}, {0x68D, "m13"}, {0x689, "m13"}, {0x60D, "m13"},
    {0xA95, "maj13"}, {0xA91, "maj13"}, {0xA15, "maj13"},
    /* 7ths and 9ths without the 5th */
    {0x411, "7"}, {0x811, "maj7"}, {0x409, "m7"}, {0x415, "9"}, {0x815, "maj9"}, {0x40D, "m9"},
    {0x413, "7b9"}, {0x419, "7#9"}, {0x851, "maj7#11"},
};
#define NCHORDS (sizeof CHORDS / sizeof CHORDS[0])

/* the chord of pitch classes pcs, *root its root: on the bass when it makes one,
 * else the simplest on another root (shown "/bass") */
static const char *chord_of(uint32_t pcs, uint32_t bass, uint32_t *root)
{
    uint32_t i, k, best = NCHORDS;
    for (i = 0; i < 12u; i++) {
        uint32_t r = (bass + i) % 12u, iv = ((pcs >> r) | (pcs << (12u - r))) & 0xFFFu;
        if (!((pcs >> r) & 1u))
            continue;
        for (k = 0; k < best; k++)
            if (CHORDS[k].iv == iv) {
                best = k;
                *root = r;
                break;
            }
        if (!i && best < NCHORDS)
            break;                                   /* on the bass: no slash */
    }
    return best < NCHORDS ? CHORDS[best].q : 0;
}

/* the lowest of n notes (note[] holds the first 8) that fit in w px of font f, "C4 E4 G4",
 * " .." when some are left out; returns how many are shown */
static uint32_t notes_fit(char *b, const uint8_t *note, uint32_t n, const melodee_font_t *f, int32_t w)
{
    uint32_t m, i;
    for (m = n < 8u ? n : 8u; m > 1u; m--) {
        b[0] = 0;
        for (i = 0; i < m; i++) {
            if (i)
                str_cpy(b + str_len(b), " ", 2);
            note_name(b + str_len(b), note[i]);
        }
        if (m < n)
            str_cpy(b + str_len(b), " ..", 4);
        if (text_w(f, b) <= w)
            return m;
    }
    note_name(b, note[0]);
    return 1;
}

static int graph_notes(void)
{
    uint8_t note[8];
    char b[48];
    uint32_t i, n = 0, pcs = 0, root, bass;
    int held = (live_held[0] | live_held[1] | live_held[2] | live_held[3]) != 0;
    uint16_t c = held ? C_WHITE : C_HI;
    const char *q;
    for (i = 0; i < 128u; i++)
        if ((live_last[i >> 5] >> (i & 31u)) & 1u) {
            pcs |= 1u << (i % 12u);
            if (n < sizeof note)
                note[n] = (uint8_t)i;
            n++;
        }
    if (!n)
        return 0;                                    /* nothing played since power-on */
    bass = note[0] % 12u;
    q = chord_of(pcs, bass, &root);
    if (q) {
        int32_t x = cv_text(4, 0, &FONT_L, N_NOTE[root], c);
        x = cv_text(x + 1, 3, &FONT_S, q, c);
        if (root != bass) {
            x = cv_text(x + 2, 0, &FONT_L, "/", c);
            x = cv_text(x, 0, &FONT_L, N_NOTE[bass], c);
        }
        notes_fit(b, note, n, &FONT_S, 236 - (x + 10));
        cv_text(x + 10, 14, &FONT_S, b, held ? C_AMB : C_DIM);
    } else if (notes_fit(b, note, n, &FONT_L, 232) == n) {
        cv_text(4, 0, &FONT_L, b, c);
    } else {                                         /* too many for the large font */
        notes_fit(b, note, n, &FONT_S, 232);
        cv_text(4, 14, &FONT_S, b, c);
    }
    return 1;
}

static void draw_graph(void)
{
    const page_t *pg = cur_page();
    const track_t *t = TSEL;
    uint16_t c = page_color();
    uint32_t sig, top, drum_note = !ui.home && is_drum(t) && !page_for_drum(pg);
    if ((ui.home && (ui.home_view == 1u || ui.home_view == 2u)) || (!ui.home && pg->graph == GR_TRK)) {
        if (ui.home && ui.home_view == 2u)
            draw_pan_mixer();
        else
            draw_tracks();
        ui.graph_top = 1;                            /* the next graph draws its top rows again */
        return;
    }
    sig = graph_signature();
    if (!ui.force && sig == ui.graph_sig)
        return;
    ui.graph_sig = sig;
    cv_begin(240, H_GRAPH, C_BLACK);
    cv_oy = G_OY;
    if (ui.home) {
        if (ui.home_view == 0u) {
            int32_t scope_top = ui.hot_t && settings.zoom ? 52 : 32;
            graph_scope(c, scope_top, H_GRAPH - scope_top);
            if (!(ui.hot_t && settings.zoom) && !graph_notes())
                cv_text(4, 2, &FONT_S, "PLAY A NOTE", C_DIM);
        } else {
            graph_scope(c, 0, H_GRAPH);
        }
    } else if (drum_note) {                          /* a page the drum track has no use for */
        static const char *const L[2] = {"DRUM TRACK", "SEQ  TRACKS  GLO DRUMS"};
        cv_text((240 - text_w(&FONT_S, L[0])) / 2, 26, &FONT_S, L[0], C_HI);
        cv_text((240 - text_w(&FONT_S, L[1])) / 2, 52, &FONT_S, L[1], C_DIM);
    } else {
        /* These diagrams were designed with a separate text strip above them. */
        if (pg->graph == GR_ROLL || pg->graph == GR_FMALG || pg->graph == GR_FMEG ||
            pg->graph == GR_FMPEG || (pg->scope == SC_ENGINE && fm6_shown() && pg->id[0] == P_E0))
            cv_oy = 24;
        switch (pg->graph) {
        case GR_ADSR:
            graph_adsr(t, c);
            break;
        case GR_LFO:
            graph_lfo(t, c);
            break;
        case GR_STEPS:
            graph_steps(t, c);
            break;
        case GR_ROLL:
            graph_roll(t, c);
            break;
        case GR_SCALE:
            graph_scale(t, c);
            break;
        case GR_MPC:
            graph_mpc(t, c);
            break;
        case GR_FX:
            graph_fx(t, c);
            break;
        case GR_SLCR:
            graph_slicer(t, c);
            break;
        case GR_FMALG:
            graph_fmalg(c);
            break;
        case GR_FMEG: {
            const int16_t *op = &fm6_ed[song.sel % NPART][FM6_OPB(fm6_opsel + 1u)];
            graph_fmenv(op + FO_R1, op + FO_L1, 0, c);
            break;
        }
        case GR_FMPEG:
            graph_fmenv(&fm6_ed[song.sel % NPART][FV_PR], &fm6_ed[song.sel % NPART][FV_PL], 1, c);
            break;
        case GR_FMSTORE:
            graph_fmstore(c);
            break;
        case GR_BROWSE:
            cv_oy = 0;
            graph_browse();
            break;
        case GR_SLOTS:
            cv_oy = 0;
            graph_slots();
            break;
        case GR_USER:
            cv_oy = 0;
            graph_user();
            break;
        default:
            if (pg->scope == SC_ENGINE && fm6_shown() && pg->id[0] == P_E0)
                graph_fmalg(c);                      /* FM6 PATCH: the voice's algorithm */
            else if (pg->scope == SC_ENGINE && is_drum(t))
                graph_drums(t);
            else if (pg->scope == SC_ENGINE && !fm6_shown())
                graph_engine(t);
            else
                graph_controls(pg);
            break;
        }
    }
    top = !ui.home && !drum_note && (pg->graph == GR_BROWSE || pg->graph == GR_SLOTS || pg->graph == GR_USER);   /* these draw from the top */
    cv_oy = 0;
    if (!ui.home && pg->scope == SC_ENGINE && pg->id[0] == P_E0 &&
        !is_drum(t) && !fm6_shown() && !(ui.hot_t && settings.zoom)) {
        if (graph_notes())                            /* the focus readout takes its place */
            top = 1;
        else {
            cv_text(5, 2, &FONT_S, "PLAY A NOTE", C_GRAY);
            top = 1;
        }
    }
    if (!ui.home && fm6_shown() && (pg->scope == SC_FM6 || pg->scope == SC_FMOP || pg->scope == SC_ENGINE)) {
        char nm[24];                                 /* FM6: the voice name; on operator pages the operator */
        fm6_name(nm, fm6_ed[song.sel % NPART]);
        cv_text(4, 2, &FONT_S, nm, C_HI);
        if (pg->scope == SC_FMOP || pg->graph == GR_FMALG) {
            char o[8] = "OP1";
            o[2] = (char)('1' + fm6_opsel);
            cv_text(236 - text_w(&FONT_S, o), 2, &FONT_S, o, ACC);
        }
        top = 1;
    }
    if (!ui.home && pg->scope == SC_STEP) {
        uint32_t start = step_note_start(t, ui.cursor);
        char hint[32] = "PRESETS: PAT / HOLD: LEN";
        if (is_drum(t)) {
            str_cpy(hint, "DRUMS: ONE SHOT", sizeof hint);
        } else if (start < NSTEP) {
            str_cpy(hint, "HOLD + PRESETS: ", sizeof hint);
            fmt_int(hint + str_len(hint), (int32_t)step_note_length(t, start));
            str_cpy(hint + str_len(hint), " STP", 5);
        }
        cv_text(4, 2, &FONT_S, hint, C_HI);
        top = 1;
    }
    if (ui.hot_t && settings.zoom) {                 /* focus (menu ZOOM): the touched value, large and white */
        int32_t x;
        top = 1;
        cv_rect(0, 0, 150, 50, C_BLACK);
        cv_text(4, 0, &FONT_S, ui.focus_l, C_GRAY);
        x = cv_text(4, 16, &FONT_L, ui.focus_v, C_WHITE);
        cv_text(x + 4, 30, &FONT_S, ui.focus_u, C_DIM);
    }
    /* graphs keep out of the top G_OY rows: skip them unless something is (or was) there */
    cv_blit_from(0, Y_GRAPH, top || ui.graph_top || ui.force ? 0u : G_OY);
    ui.graph_top = (uint8_t)top;
}

static void draw_foot(void)
{
    char s[48], pn[16], en[10], ti[20];
    const track_t *t = TSEL;
    uint32_t sig;
    const page_t *pg = cur_page();
    const engine_t *e = ENGINES[TSEL->eng_req % NENGINES];
    const char *ename = is_drum(t) ? "DRUM" : e->name;
    pn[0] = 0;
    if (is_drum(t))
        str_cpy(pn, "ANALOG KIT", sizeof pn);
    else if (e == &ENG_FM6)
        fm6_name(pn, fm6_ed[song.sel % NPART]);       /* FM6: the voice playing */
    else if (user_of(t) < UP_SLOTS)
        up_name(user_of(t), pn);                       /* a user preset */
    else if (e->npresets)
        str_cpy(pn, e->presets[TSEL->preset % e->npresets].name, sizeof pn);
    if (ui.home) {
        str_cpy(ti, ui.home_view == 3u ? "MIX FX" : ui.home_view == 2u ? "MIX PAN"
                        : ui.home_view == 1u ? "MIX LEVEL" : "NOTES", sizeof ti);
    } else {                                           /* page title + number in its family: "ENV DEST 2/2" */
        uint32_t i, n = 0, k = 0;
        static const char *const DRUM_TITLE[2] = {"BD SD", "TUNE DCY"};
        const char *pt = pg->scope != SC_ENGINE ? 0 : is_drum(t) ? DRUM_TITLE[pg->id[0] != P_E0]
                                                : e->page_title[pg->id[0] != P_E0];   /* EDIT: the engine's */
        for (i = 0; i < NPAGES; i++)
            if (PAGES[i].fam == pg->fam && page_shown(&PAGES[i])) {
                n++;
                if (i == ui.page)
                    k = n;
            }
        if (pg->scope == SC_FMOP) {                  /* FM6 operator pages: "OP3 FREQ" */
            str_cpy(ti, "OP1 ", 10);
            ti[2] = (char)('1' + fm6_opsel);
            str_cpy(ti + 4, pg->title, 10);
        } else if (pg->fam == FAM_SEQ) {               /* LOOP pages: show the pattern, then the view */
            uint32_t j = 2;
            ti[0] = 'P';
            ti[1] = (char)('1' + t->pat);
            if (t->pat_q) {
                ti[j++] = '>';
                ti[j++] = (char)('0' + t->pat_q);
            }
            ti[j++] = ' ';
            str_cpy(ti + j, pg->graph == GR_STEPS ? "LOOP" : pg->title, 10);
        } else {
            str_cpy(ti, pt ? pt : pg->title, 10);
        }
        if (n > 1) {
            str_cpy(ti + str_len(ti), " ", 4);
            fmt_int(ti + str_len(ti), (int32_t)k);
            str_cpy(ti + str_len(ti), "/", 4);
            fmt_int(ti + str_len(ti), (int32_t)n);
        }
    }
    str_cpy(s, ename, sizeof s);
    s[str_len(s) + 1u] = 0;
    s[str_len(s)] = (char)('1' + song.sel);
    str_cpy(s + str_len(s), pn, 16);
    str_cpy(s + str_len(s), ti, sizeof ti);
    {   /* step markers: the playhead only when it is in the shown bank, the cursor only in SEQ */
        uint32_t ph = song.playing && t->seq_idx / 16u == ui.bank ? t->seq_idx : 0xFFu;
        sig = str_hash(0x9E3779B9u, s) + ph * 97u + (song.seq_mode ? ui.cursor : 0xFFu) * 3001u + steps_hash(t) +
              ui.bank * 7u + (uint32_t)t->p[P_SLEN] * 13u;
    }
    if (!ui.force && sig == ui.foot_sig)
        return;
    ui.foot_sig = sig;
    cv_begin(240, H_FOOT, C_BLACK);
    {
        char tn[3] = {'T', (char)('1' + song.sel), 0};
        const char *hint = ui.home ? (ui.home_view == 0u ? "HOME: MIXER" : ui.home_view == 1u ? "HOME: PAN"
                                   : ui.home_view == 2u ? "HOME: FX" : "HOME: NOTES")
                           : pg->scope == SC_STEP ? "PRESETS: PAT / LEN"
                           : pg->graph == GR_STEPS ? "PRESETS PATTERN"
                           : fm6_shown() && pg->scope == SC_FMOP ? "PRESETS OP"
                           : preset_pages() ? "PRESETS PAGE" : is_drum(t) ? "DRUM KIT" : "PRESETS SOUND";
        char pf[16];
        int32_t room = 234 - text_w(&FONT_S, hint) - 12;
        cv_rect(0, 0, 3, H_FOOT, page_color());
        cv_text(7, 0, &FONT_S, tn, page_color());
        fit(en, ename, &FONT_S, 91 - text_w(&FONT_S, tn));
        cv_text(29, 0, &FONT_S, en, C_HI);
        cv_text(236 - text_w(&FONT_S, ti), 0, &FONT_S, ti, C_WHITE);
        str_cpy(pf, pn, sizeof pf);
        while (pf[0] && text_w(&FONT_S, pf) > room)
            pf[str_len(pf) - 1u] = 0;
        cv_text(8, 14, &FONT_S, pf, C_AMB);
        cv_text(236 - text_w(&FONT_S, hint), 14, &FONT_S, hint, C_GRAY);
    }
    cv_blit(0, Y_FOOT);
    cv_begin(240, H_STEP, C_BLACK);
    {
        uint32_t i;
        for (i = 0; i < 16u; i++) {
            uint32_t si = ui.bank * 16u + i;
            int32_t sx = 4 + (int32_t)i * 14 + (int32_t)(i / 4u) * 4;
            if (si >= (uint32_t)t->p[P_SLEN])
                continue;
            cv_rect(sx, 1, 9, 3, step_on(&t->step[si]) ? page_color() : C_DIM);
            if ((song.playing && si == t->seq_idx) || (song.seq_mode && si == ui.cursor))
                cv_rect(sx, 5, 9, 2, C_WHITE);
        }
    }
    cv_blit(0, Y_STEP);
}
static void draw_columns(void)
{
    uint32_t c;
    char val[12];
    const char *unit;
    if (ui.home) {
        if (ui.home_view == 3u) {
            static const uint8_t MASTER[4] = {G_DTIME, G_DFDBK, G_RSIZE, G_DMIX};
            for (c = 0; c < 4u; c++) {
                const param_desc_t *d = &GP[MASTER[c]];
                int16_t v = song.g[MASTER[c]];
                param_format(d, v, val, &unit);
                draw_column(c, d->label, val, unit, VAL(c), RATIO(d, v), param_icon(d, v));
            }
            return;
        }
        if (ui.home_view <= 1u) {
            for (c = 0; c < 4u; c++) {
                char label[4] = {'T', (char)('1' + c), 0};
                uint32_t lvl = trk_level(c);
                fmt_int(val, (int32_t)lvl);
                draw_column(c, label, trk[c].p[P_MUTE] ? "MUTE" : val, "", VAL(c),
                            (int32_t)lvl * 1000 / 127, ICON_AUTO);
            }
            return;
        }
        for (c = 0; c < 4u; c++) {
            char label[4] = {'T', (char)('1' + c), 0};
            int16_t pan = trk[c].p[P_PAN];
            param_format(&TP[P_PAN], pan, val, &unit);
            draw_column(c, label, val, unit, VAL(c), RATIO(&TP[P_PAN], pan), ICON_AUTO);
        }
        return;
    }
    if (is_drum(TSEL) && !page_for_drum(cur_page())) {   /* "DRUM TRACK" (the graph says so) */
        for (c = 0; c < 4u; c++)
            draw_column(c, "", "", "", C_HI, -1, ICON_AUTO);
        return;
    }
    if (cur_page()->scope == SC_TRK) {                 /* TRACK LEVEL LEN PAN of the selected track */
        const track_t *t = TSEL;
        uint32_t lvl = trk_level(song.sel);
        fmt_int(val, (int32_t)song.sel + 1);
        draw_column(0, "TRACK", val, "/4", VAL(0u), (int32_t)song.sel * 1000 / (NTRK - 1), ICON_AUTO);
        if (!lvl || t->p[P_MUTE]) {                    /* (MUTE: a turn of KNOB 2 unmutes, tracks_edit) */
            str_cpy(val, "MUTE", 12);
            unit = "";
        } else if (is_drum(t)) {
            fmt_int(val, (int32_t)lvl);
            unit = "";
        } else {
            param_format(&TP[P_LEVEL], (int32_t)lvl, val, &unit);
        }
        draw_column(1, "LEVEL", val, unit, lvl && !t->p[P_MUTE] ? VAL(1u) : C_DIM, (int32_t)lvl * 1000 / 127, ICON_AUTO);
        param_format(&TP[P_SLEN], t->p[P_SLEN], val, &unit);
        draw_column(2, "LEN", val, unit, VAL(2u), RATIO(&TP[P_SLEN], t->p[P_SLEN]), ICON_AUTO);
        param_format(&TP[P_PAN], t->p[P_PAN], val, &unit);
        draw_column(3, "PAN", val, unit, VAL(3u), RATIO(&TP[P_PAN], t->p[P_PAN]), param_icon(&TP[P_PAN], t->p[P_PAN]));
        return;
    }
    if (cur_page()->graph == GR_BROWSE) {
        uint32_t total, cur = preset_pos(&total);
        char u[8];
        fmt_int(val, (int32_t)cur + 1);
        str_cpy(u, "/", 8);
        fmt_int(u + 1, (int32_t)total);
        draw_column(0, "No.", val, u, VAL(0u), -1, ICON_NONE);
        draw_column(1, "ENG", ENGINES[TSEL->eng_req]->name, "", VAL(1u), -1, engine_icon(ENGINES[TSEL->eng_req]->name));
        draw_column(2, "", "", "", C_HI, -1, ICON_AUTO);
        draw_column(3, "", "", "", C_HI, -1, ICON_AUTO);
        return;
    }
    if (cur_page()->graph == GR_FMSTORE && fm6_shown()) {   /* FM6: user slot, then three GO buttons */
        draw_column(0, "SLOT", N_FM6V[FM6_NROM + fm6_slot % FM6_NUSER], "", VAL(0u),
                    (int32_t)fm6_slot * 1000 / (int32_t)(FM6_NUSER - 1u), ICON_AUTO);
        draw_column(1, "STORE", "--", "", C_HI, -1, ICON_AUTO);
        draw_column(2, "SEND", "--", "", C_HI, -1, ICON_AUTO);
        draw_column(3, "INIT", "--", "", C_HI, -1, ICON_AUTO);
        return;
    }
    if (cur_page()->graph == GR_USER) {                  /* SLOT, then three GO buttons */
        int used = up_used(ui.uslot);
        up_slot_label(val, ui.uslot);
        draw_column(0, "SLOT", val, "", VAL(0u), (int32_t)ui.uslot * 1000 / (int32_t)(UP_SLOTS - 1u), ICON_AUTO);
        draw_column(1, "LOAD", "--", "", used ? C_HI : C_DIM, -1, ICON_AUTO);
        draw_column(2, "ERASE", "--", "", used ? C_HI : C_DIM, -1, ICON_AUTO);
        draw_column(3, "SAVE", "--", "", C_HI, -1, ICON_AUTO);
        return;
    }
    if (cur_page()->scope == SC_STEP) {
        static const char *const TIME_N[3] = {"NOTE", "TIE", "REST"};
        const step_t *st = &TSEL->step[ui.cursor];
        char u[8];
        if (st->n) {
            note_name(val, st->note[0]);
            u[0] = 0;
            if (st->n > 1) {
                str_cpy(u, "+", 8);
                fmt_int(u + 1, st->n - 1);
            }
        } else {
            str_cpy(val, "--", 12);
            u[0] = 0;
        }
        {
            static const char *const FLAG_N[4] = {"-", "ACC", "SLD", "A+S"};
            char sn[8], sl[8];
            fmt_int(sn, (int32_t)ui.cursor + 1);
            str_cpy(sl, "/", 8);
            fmt_int(sl + 1, TSEL->p[P_SLEN]);
            draw_column(0, "STEP", sn, sl, VAL(0u), -1, ICON_AUTO);
            draw_column(1, "NOTE", val, u, step_on(st) ? VAL(1u) : C_DIM, -1, ICON_AUTO);
            draw_column(2, "TIME", TIME_N[st->time % 3u], "", VAL(2u), -1, ICON_AUTO);
            draw_column(3, "FLAG", FLAG_N[(st->flags & SF_ACCENT ? 1u : 0u) | (st->flags & SF_SLIDE ? 2u : 0u)], "",
                        VAL(3u), -1, ICON_AUTO);
        }
        return;
    }
    for (c = 0; c < 4u; c++) {
        int16_t *vp;
        const param_desc_t *d = page_desc(cur_page(), c, &vp);
        if (!d || !d->label || d->label[0] == '-') {
            draw_column(c, "", "", "", C_HI, -1, ICON_AUTO);
            continue;
        }
        if (cur_page()->id[c] == G_MIDI && cur_page()->scope == SC_GLOBAL) {
            if (ui.midi_view)
                draw_column(c, "MIDI", "TRS", ui_trs_state == 2u ? "RX" : ui_trs_state ? "ON" : "OFF",
                            ui_trs_state == 2u ? C_WHITE : VAL(c), -1, ICON_AUTO);
            else
                draw_column(c, "MIDI", "USB", !usb.up ? "OFF" : usb.config ? "ON" : "--", VAL(c), -1, ICON_AUTO);
            continue;
        }
#if MELODEE_USB_AUDIO
        if ((cur_page()->id[c] == G_USBOUT || cur_page()->id[c] == G_USBIN) && cur_page()->scope == SC_GLOBAL) {
            uint32_t out = cur_page()->id[c] == G_USBOUT;   /* the wanted state; the host follows (main.c) */
            draw_column(c, "AUDIO", out ? "OUT" : "IN", ua_off_want & (out ? UA_OFF_OUT : UA_OFF_IN) ? "OFF" : "ON",
                        VAL(c), -1, ICON_AUTO);
            continue;
        }
#endif
        if (cur_page()->id[c] == G_BOOT && cur_page()->scope == SC_GLOBAL) {
            char b[2] = {(char)('0' + settings.boot), 0};
            draw_column(c, "BOOT", settings.boot ? b : "OFF", "", VAL(c), -1, ICON_AUTO);
            continue;
        }
        if (cur_page()->id[c] == G_INFO && cur_page()->scope == SC_GLOBAL) {
            fmt_int(val, (int32_t)(song.cpu_q8 * 100u / 256u));
            unit = "%";
        } else {
            param_format(d, *vp, val, &unit);
        }
        draw_column(c, d->label, val, unit, VAL(c), d->fmt == F_ENUM && d->max < 2 ? -1 : RATIO(d, *vp),
                    param_icon(d, *vp));
    }
}

static void draw_confirm(void)
{
    char detail[24] = "TRACK 1 / ALL PATTERNS";
    detail[6] = (char)('1' + ui.confirm_trk);
    cv_begin(240, 124, C_BLACK);
    cv_rect(0, 0, 240, 3, control_color(1));
    cv_text(8, 15, &FONT_S, "CONFIRM ACTION", C_GRAY);
    cv_text(8, 40, &FONT_L, "CLEAR?", C_WHITE);
    cv_text(8, 83, &FONT_S, ui.confirm == 2 ? detail : "CURRENT PATTERN", C_HI);
    cv_rect(8, 113, 224, 1, C_LINE);
    cv_blit(0, 0);
    cv_begin(240, 116, C_BLACK);
    cv_text(8, 35, &FONT_S, "OCT-  CANCEL", C_GRAY);
    cv_text(8, 65, &FONT_S, "OCT+  CLEAR", control_color(1));
    cv_blit(0, 124);
}


static void ui_draw(void)
{
    ui_midi_status_tick();
    ui.frame++;
    if (ui.menu) {
        draw_menu();
        ui.force = 0;
        return;
    }
    if (ui.confirm) {                                   /* clear-the-sequence dialog */
        if (ui.force) {
            draw_confirm();
            ui.force = 0;
        }
        return;
    }
    cursor_fix();
    seq_record_follow();
    if (ui.force)
        draw_frame();
    melodee_dbg.stage = 3;
    draw_head();
    melodee_dbg.stage = 4;
    draw_columns();
    melodee_dbg.stage = 5;
    draw_graph();
    if (ui.msg_t)
        ui.msg_t--;
    if (ui.bpm_t)
        ui.bpm_t--;
    if (ui.arm_t && !--ui.arm_t)
        ui.arm = 0;
    if (ui.hot_t)
        ui.hot_t--;
    melodee_dbg.stage = 6;
    draw_foot();
    ui.force = 0;
}
