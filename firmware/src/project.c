/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Projects: four slots in flash (storage.c OBJ_PROJECT0.., ST_PROJ_SPAN sectors a copy).
 * One project is in RAM at a time (proj_buf); proj_have says which slots hold one. Without
 * the flash (RAM only) proj_buf keeps the last save.
 *
 * Formats: 7 ("FUN7", written) adds the FM6 parts' DX7 function settings (pitch bend,
 * portamento, controllers) to format 6; a format 6 project ("FUN6", read in place) gives them
 * their defaults. Format 6 holds NPAT patterns per track, each with its LEN / DIV /
 * SWING / GATE, and the one playing; the rest as format 5. Older formats are read from
 * their one-sector objects (OBJ_LEGACY0..) and converted: their pattern becomes pattern 1,
 * the others start empty. Format 5 ("FUN5") adds MPC pad degree to format 4. Format 4 ("FUN4")
 * adds the FM6 parts' voices (DX7 packed, with their operator switches) to format 3.
 * Formats 3/4 hold 57 parameters per track (including SLICER); new parameters take
 * their defaults and engine parameters stay mapped to P_E0..P_E7. Format 3 ("FUN3")
 * has no stored FM6 voices (FM6 parts load their VOICE); 2 ("FUN2") and 1
 * ("FUN1") are read and converted: they hold PROJ_NP_V2 parameters per track,
 * mapped by count as user presets are (the first PROJ_NP_V2 - 8 are P_LEVEL.. in order, the last 8
 * P_E0..P_E7; the parameters added since take their defaults, so the SLICER is OFF). Their engine
 * bytes are kept: formats 1 and 2 had engines 0..7 (ANALOG .. WHEEL), and the engines added since
 * (GRAIN 8, FM6 9; SLICE, when built, after them) were appended, no index moved; the drum track's byte (it has no engine) becomes 0.
 *
 * Built on the host too (tests/project_test.c, -DPROJ_HOST): the part above the #ifndef
 * PROJ_HOST needs core.h, params.c (TP), the engines and trk_def_engine (ui.c). */
#define PROJ_MAGIC 0x46554E37u                 /* "FUN7": format 6 + the FM6 parts' function settings */
#define PROJ_MAGIC_V6 0x46554E36u              /* "FUN6": format 5 + NPAT patterns per track; read only */
#define PROJ_MAGIC_V5 0x46554E35u              /* "FUN5": format 4 + MPC pad degree; read only */
#define PROJ_MAGIC_V4 0x46554E34u              /* "FUN4": format 3 + FM6 voices; read only */
#define PROJ_MAGIC_V3 0x46554E33u              /* "FUN3": four tracks, 57 parameters each; read only */
#define PROJ_MAGIC_V2 0x46554E32u              /* "FUN2": four tracks, PROJ_NP_V2 parameters; read only */
#define PROJ_MAGIC_V1 0x46554E31u              /* "FUN1": one instrument; loads into track 1 */
#define PROJ_NP_V2 53u                         /* P_COUNT of formats 1 and 2 (P_E0 was 45) */
#define PROJ_NG_V2 27u                         /* G_COUNT of formats 1 .. 5 */
#define PROJ_NP_V4 57u                         /* P_COUNT of formats 3 and 4 (P_E0 was 49) */
#define PROJ_NP_V5 58u                         /* P_COUNT of format 5 (P_E0 was 50) */
typedef struct {                               /* one track; the drum track ignores engine / preset */
    int16_t p[P_COUNT];                        /* (P_SLEN..P_SGATE: those of pattern pat) */
    uint8_t engine, preset;
    uint8_t pat, rsv;                          /* the pattern playing */
    pattern_t pt[NPAT];
} proj_trk_t;
typedef struct {
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel, rsv[3];                       /* the selected track */
    proj_trk_t t[NTRK];
    uint8_t fm6[NPART][128];                   /* the FM6 parts' voices, DX7 packed (fm6_has: which) */
    uint8_t fm6_on[NPART], fm6_has;            /* their operator switches (bit n - 1: OP n); bit k: part k */
    int8_t fm6_fn[NPART][16];                  /* the parts' FM6 functions (FN_PBUP..), [0] < 0: defaults */
    uint32_t sum;
} project_t;
typedef struct {                               /* a track of format 5, read only */
    int16_t p[PROJ_NP_V5];
    uint8_t engine, preset;
    step_t step[NSTEP];
} proj_trk_v5_t;
typedef struct {                               /* format 5 (until patterns), read only */
    uint32_t magic, size;
    int16_t g[PROJ_NG_V2];
    uint8_t sel, rsv[3];
    proj_trk_v5_t t[NTRK];
    uint8_t fm6[NPART][128];
    uint8_t fm6_on[NPART], fm6_has;
    uint32_t sum;
} project_v5_t;
typedef struct {                               /* a track of formats 3 and 4, read only */
    int16_t p[PROJ_NP_V4];
    uint8_t engine, preset;
    step_t step[NSTEP];
} proj_trk_v4_t;
typedef struct {                               /* format 4 (until MPC degree), read only */
    uint32_t magic, size;
    int16_t g[PROJ_NG_V2];
    uint8_t sel, rsv[3];
    proj_trk_v4_t t[NTRK];
    uint8_t fm6[NPART][128];
    uint8_t fm6_on[NPART], fm6_has;
    uint32_t sum;
} project_v4_t;
typedef struct {                               /* format 3 (until FM6), read only */
    uint32_t magic, size;
    int16_t g[PROJ_NG_V2];
    uint8_t sel, rsv[3];
    proj_trk_v4_t t[NTRK];
    uint32_t sum;
} project_v3_t;
typedef struct {                               /* a track of formats 1 and 2, read only */
    int16_t p[PROJ_NP_V2];
    uint8_t engine, preset;
    step_t step[NSTEP];
} proj_trk_v2_t;
typedef struct {                               /* format 2 (until 0.9), read only */
    uint32_t magic, size;
    int16_t g[PROJ_NG_V2];
    uint8_t sel, rsv[3];
    proj_trk_v2_t t[NTRK];
    uint32_t sum;
} project_v2_t;
typedef struct {                               /* format 1 (until 0.5 beta), read only */
    uint32_t magic, size;
    int16_t g[PROJ_NG_V2];
    proj_trk_v2_t t;
    uint32_t sum;
} project_v1_t;
typedef union {                                /* any of the one-sector formats, as stored */
    project_v5_t v5;
    project_v4_t v4;
    project_v3_t v3;
    project_v2_t v2;
    project_v1_t v1;
} project_old_t;
_Static_assert(sizeof(project_v5_t) == 2980u && sizeof(project_v4_t) == 2972u && sizeof(project_v3_t) == 2584u &&
               sizeof(project_v2_t) == 2552u && sizeof(project_v1_t) == 688u,
               "formats 1..5 as they were stored");

static uint32_t proj_hash(const void *p, uint32_t n)   /* FNV-1a over n bytes */
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t i, s = 0x811C9DC5u;
    for (i = 0; i < n; i++)
        s = (s ^ b[i]) * 16777619u;
    return s;
}
static uint32_t proj_sum(const project_t *p) { return proj_hash(p, sizeof *p - 4u); }

/* format 6, in place: the same up to the FM6 switches, then its sum where format 7 has the functions */
#define PROJ_V6_SIZE (sizeof(project_t) - sizeof(((project_t *)0)->fm6_fn))
static int proj_from_v6(project_t *q, int n)
{
    uint32_t sum;
    if (n != (int)PROJ_V6_SIZE || q->magic != PROJ_MAGIC_V6 || q->size != PROJ_V6_SIZE)
        return 0;
    memcpy(&sum, (const uint8_t *)q + PROJ_V6_SIZE - 4u, 4);
    if (sum != proj_hash(q, PROJ_V6_SIZE - 4u))
        return 0;
    memset(q->fm6_fn, 0xFF, sizeof q->fm6_fn);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    q->sum = proj_sum(q);
    return 1;
}
static int proj_ok(const project_t *q) { return q->magic == PROJ_MAGIC && q->size == sizeof *q && q->sum == proj_sum(q); }

/* the globals of formats 1 .. 5 (G_* unchanged since; any added later: their defaults) */
static void proj_g_from_v2(int16_t *g, const int16_t *g2)
{
    uint32_t i;
    for (i = 0; i < G_COUNT; i++)
        g[i] = i < PROJ_NG_V2 ? g2[i] : GP[i].def;
}

/* np parameters of an older track -> today's, mapped by count (see the top) */
static void proj_p_from(int16_t *p, const int16_t *s, uint32_t np)
{
    uint32_t k, nc = np - 8u;
    for (k = 0; k < P_E0; k++)
        p[k] = k < nc ? s[k] : TP[k].def;
    for (k = 0; k < 8u; k++)
        p[P_E0 + k] = s[nc + k];
}

/* the one pattern of an older track (0: none) -> pattern 1, playing; the others empty */
static void proj_pats_from(proj_trk_t *d, const step_t *step)
{
    uint32_t k, i;
    for (k = 0; k < NPAT; k++) {
        for (i = 0; i < NSTEP; i++)
            d->pt[k].step[i].time = ST_REST;
        d->pt[k].set[0] = 0;
    }
    if (step)
        memcpy(d->pt[0].step, step, sizeof d->pt[0].step);
    for (i = 0; i < 4u; i++)
        d->pt[0].set[i] = d->p[P_SLEN + i];
    d->pat = 0;
}

/* a track of formats 1 and 2 -> today's; drum: the drum track */
static void proj_trk_from_v2(proj_trk_t *d, const proj_trk_v2_t *s, int drum)
{
    proj_p_from(d->p, s->p, PROJ_NP_V2);
    d->engine = drum ? 0u : s->engine;          /* (indices 0..7 as they were) */
    d->preset = drum ? 0u : s->preset;
    proj_pats_from(d, s->step);
}

/* a format 2 project (n bytes in *v2) -> slot q in the current format */
static int proj_from_v2(project_t *q, const project_v2_t *v2, int n)
{
    uint32_t i;
    if (n != (int)sizeof *v2 || v2->magic != PROJ_MAGIC_V2 || v2->size != sizeof *v2 ||
        v2->sum != proj_hash(v2, sizeof *v2 - 4u))
        return 0;
    memset(q, 0, sizeof *q);
    memset(q->fm6_fn, 0xFF, sizeof q->fm6_fn);         /* FM6 functions: the defaults */
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    proj_g_from_v2(q->g, v2->g);
    q->sel = v2->sel;
    for (i = 0; i < NTRK; i++)
        proj_trk_from_v2(&q->t[i], &v2->t[i], i == TRK_DRUM);
    q->sum = proj_sum(q);
    return 1;
}

/* a format 1 project (n bytes in *v1) -> slot q: the instrument becomes track 1,
 * tracks 2..4 start empty (their sounds as at power-on) */
static int proj_from_v1(project_t *q, const project_v1_t *v1, int n)
{
    uint32_t i;
    if (n != (int)sizeof *v1 || v1->magic != PROJ_MAGIC_V1 || v1->size != sizeof *v1 ||
        v1->sum != proj_hash(v1, sizeof *v1 - 4u))
        return 0;
    memset(q, 0, sizeof *q);
    memset(q->fm6_fn, 0xFF, sizeof q->fm6_fn);         /* FM6 functions: the defaults */
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    proj_g_from_v2(q->g, v1->g);
    proj_trk_from_v2(&q->t[0], &v1->t, 0);
    for (i = 1; i < NTRK; i++) {               /* the other tracks: their defaults, no steps */
        uint32_t k;
        for (k = 0; k < P_COUNT; k++)
            q->t[i].p[k] = k >= P_E0 ? ENGINES[trk_def_engine(i)]->edit[k - P_E0].def : TP[k].def;
        q->t[i].engine = (uint8_t)trk_def_engine(i);
        q->t[i].preset = 0xFF;                 /* 0xFF: its default preset (project_load) */
        proj_pats_from(&q->t[i], 0);
    }
    q->sum = proj_sum(q);
    return 1;
}

static void proj_trk_from_v4(proj_trk_t *d, const proj_trk_v4_t *s)
{
    proj_p_from(d->p, s->p, PROJ_NP_V4);
    d->engine = s->engine;
    d->preset = s->preset;
    proj_pats_from(d, s->step);
}

/* format 4 -> current: retain FM6 voices, add default MPC degree */
static int proj_from_v4(project_t *q, const project_v4_t *v4, int n)
{
    uint32_t i;
    if (n != (int)sizeof *v4 || v4->magic != PROJ_MAGIC_V4 || v4->size != sizeof *v4 ||
        v4->sum != proj_hash(v4, sizeof *v4 - 4u))
        return 0;
    memset(q, 0, sizeof *q);
    memset(q->fm6_fn, 0xFF, sizeof q->fm6_fn);         /* FM6 functions: the defaults */
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    proj_g_from_v2(q->g, v4->g);
    q->sel = v4->sel;
    for (i = 0; i < NTRK; i++)
        proj_trk_from_v4(&q->t[i], &v4->t[i]);
    memcpy(q->fm6, v4->fm6, sizeof q->fm6);
    memcpy(q->fm6_on, v4->fm6_on, sizeof q->fm6_on);
    q->fm6_has = v4->fm6_has;
    q->sum = proj_sum(q);
    return 1;
}

/* format 3 -> current: no FM6 voices (the parts load their VOICE) */
static int proj_from_v3(project_t *q, const project_v3_t *v3, int n)
{
    uint32_t i;
    if (n != (int)sizeof *v3 || v3->magic != PROJ_MAGIC_V3 || v3->size != sizeof *v3 ||
        v3->sum != proj_hash(v3, sizeof *v3 - 4u))
        return 0;
    memset(q, 0, sizeof *q);
    memset(q->fm6_fn, 0xFF, sizeof q->fm6_fn);         /* FM6 functions: the defaults */
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    proj_g_from_v2(q->g, v3->g);
    q->sel = v3->sel;
    for (i = 0; i < NTRK; i++)
        proj_trk_from_v4(&q->t[i], &v3->t[i]);
    q->sum = proj_sum(q);
    return 1;
}

/* format 5 -> current: its pattern becomes pattern 1 */
static int proj_from_v5(project_t *q, const project_v5_t *v5, int n)
{
    uint32_t i;
    if (n != (int)sizeof *v5 || v5->magic != PROJ_MAGIC_V5 || v5->size != sizeof *v5 ||
        v5->sum != proj_hash(v5, sizeof *v5 - 4u))
        return 0;
    memset(q, 0, sizeof *q);
    memset(q->fm6_fn, 0xFF, sizeof q->fm6_fn);         /* FM6 functions: the defaults */
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    proj_g_from_v2(q->g, v5->g);
    q->sel = v5->sel;
    for (i = 0; i < NTRK; i++) {
        proj_p_from(q->t[i].p, v5->t[i].p, PROJ_NP_V5);
        q->t[i].engine = v5->t[i].engine;
        q->t[i].preset = v5->t[i].preset;
        proj_pats_from(&q->t[i], v5->t[i].step);
    }
    memcpy(q->fm6, v5->fm6, sizeof q->fm6);
    memcpy(q->fm6_on, v5->fm6_on, sizeof q->fm6_on);
    q->fm6_has = v5->fm6_has;
    q->sum = proj_sum(q);
    return 1;
}

/* n bytes of a one-sector project (formats 1..5) -> q in the current format; 0 = not a project */
static int proj_import(project_t *q, const project_old_t *b, int n)
{
    return proj_from_v5(q, &b->v5, n) || proj_from_v4(q, &b->v4, n) || proj_from_v3(q, &b->v3, n) ||
           proj_from_v2(q, &b->v2, n) || proj_from_v1(q, &b->v1, n);
}

#ifndef PROJ_HOST
static project_t proj_buf __attribute__((section(".pool")));   /* the project being saved or loaded */
static uint8_t proj_have;                      /* bit per slot: it holds a project (project_used) */
static uint8_t proj_ram;                       /* RAM only: the slot + 1 whose save proj_buf holds */

/* slot -> proj_buf (an older format converted): 1, or 0 = none */
static int proj_fetch(uint32_t slot)
{
#if FELUCCA_FLASH
    static project_old_t old;
    int n;
    if (flash_ok) {
        n = st_load(OBJ_PROJECT0 + (slot & 3u), &proj_buf, sizeof proj_buf);
        if ((n == (int)sizeof proj_buf && proj_ok(&proj_buf)) || proj_from_v6(&proj_buf, n))
            return 1;
        n = st_load(OBJ_LEGACY0 + (slot & 3u), &old, sizeof old);
        return proj_import(&proj_buf, &old, n);
    }
#endif
    return proj_ram == (slot & 3u) + 1u && proj_ok(&proj_buf);
}

static void project_save(uint32_t slot)
{
    project_t *p = &proj_buf;
    uint32_t i, k;
    memset(p, 0, sizeof *p);
    p->magic = PROJ_MAGIC;
    p->size = sizeof *p;
    for (i = 0; i < G_COUNT; i++)
        p->g[i] = song.g[i];
    p->sel = song.sel;
    fm1_irq_off();                                      /* no pattern switch halfway through */
    for (i = 0; i < NTRK; i++) {
        proj_trk_t *d = &p->t[i];
        memcpy(d->p, trk[i].p, sizeof trk[i].p);
        d->engine = trk[i].eng_req;
        d->preset = trk[i].preset;
        d->pat = trk[i].pat;
        memcpy(d->pt, pat_bank[i], sizeof d->pt);
        memcpy(d->pt[d->pat].step, trk[i].step, sizeof trk[i].step);   /* the one playing: from the track */
        for (k = 0; k < 4u; k++)
            d->pt[d->pat].set[k] = trk[i].p[P_SLEN + k];
    }
    fm1_irq_on();
    for (i = 0; i < NPART; i++) {                       /* the FM6 parts' voices, edits and all */
        if (ENGINES[trk[i].eng_req % NENGINES] == &ENG_FM6) {
            fm6_pack(p->fm6[i], fm6_ed[i]);
            for (k = 0; k < 6u; k++)
                p->fm6_on[i] |= (uint8_t)(fm6_ed[i][FV_ON + k] ? 1u << k : 0u);
            p->fm6_has |= (uint8_t)(1u << i);
        }
        memset(p->fm6_fn[i], 0xFF, sizeof p->fm6_fn[i]);
        if (fm6_fnok[i])
            for (k = 0; k < FM6_NFN; k++)
                p->fm6_fn[i][k] = (int8_t)fm6_ed[i][FN_PBUP + k];
    }
    p->sum = proj_sum(p);
#if FELUCCA_FLASH
    if (flash_ok) {
        if (st_save(OBJ_PROJECT0 + (slot & 3u), p, sizeof *p)) {
            ui_message("SAVE ERROR");
            return;
        }
        proj_have |= (uint8_t)(1u << (slot & 3u));
        ui_message("SAVED");
        return;
    }
#endif
    proj_ram = (uint8_t)((slot & 3u) + 1u);
    proj_have = (uint8_t)(1u << (slot & 3u));
    ui_message("SAVED (RAM)");
}

static void step_sane(step_t *st)                       /* a loaded step back inside the step model */
{
    uint32_t j;
    if (st->n > 4u)
        st->n = 4;
    if (st->time > ST_REST)
        st->time = ST_REST;
    for (j = 0; j < 4u; j++)
        st->note[j] &= 127u;
}

static void project_load(uint32_t slot)
{
    project_t *p = &proj_buf;
    uint32_t i, k, j;
    if (!proj_fetch(slot)) {
        ui_message("EMPTY SLOT");
        return;
    }
    transport_req = 2;
    panic_req = (1u << NTRK) - 1u;
    fm1_irq_off();                                      /* the audio ISR must not see half a project */
    for (i = 0; i < G_COUNT; i++)
        if (i != G_SLOT && i != G_LOAD && i != G_SAVE)
            song.g[i] = (int16_t)clamp(p->g[i], GP[i].min, GP[i].max);
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        const proj_trk_t *s = &p->t[k];
        uint32_t e = k < NPART ? s->engine % NENGINES : 0u;
        t->eng_req = (uint8_t)e;
        t->user = 0;                                    /* (no user preset slot is saved) */
        for (i = 0; i < P_COUNT; i++) {                 /* every value back inside its range */
            const param_desc_t *d = i < P_E0 || i > P_E7 ? &TP[i] : k == TRK_DRUM ? &DR_EDIT[i - P_E0]
                                                                                 : &ENGINES[e]->edit[i - P_E0];
            t->p[i] = (int16_t)clamp(s->p[i], d->min, d->max);
        }
        t->preset = (uint8_t)(ENGINES[e]->npresets ? (s->preset == 0xFFu ? 0u : s->preset) % ENGINES[e]->npresets : 0u);
        if (k < NPART) {                                /* FM6: its saved voice, else its VOICE afresh */
            fm6_fn_reset(fm6_ed[k]);                    /* its functions (older formats: the defaults) */
            if (p->fm6_fn[k][0] >= 0)
                for (i = 0; i < FM6_NFN; i++)
                    fm6_set(fm6_ed[k], FN_PBUP + i, p->fm6_fn[k][i]);
            fm6_fnok[k] = 1;
            fm6_cur[k] = 0;
            if (ENGINES[e] == &ENG_FM6 && ((p->fm6_has >> k) & 1u)) {
                fm6_unpack(fm6_ed[k], p->fm6[k]);
                for (i = 0; i < 6u; i++)
                    fm6_ed[k][FV_ON + i] = (int16_t)((p->fm6_on[k] >> i) & 1u);
                fm6_cur[k] = (int16_t)(t->p[P_E0] + 1);
            }
        }
        for (j = 0; j < NPAT; j++) {                    /* the patterns, each back inside the step model */
            pattern_t *b = &pat_bank[k][j];
            memcpy(b, &s->pt[j], sizeof *b);
            for (i = 0; i < NSTEP; i++)
                step_sane(&b->step[i]);
            if (b->set[0])
                for (i = 0; i < 4u; i++)
                    b->set[i] = (int16_t)clamp(b->set[i], TP[P_SLEN + i].min, TP[P_SLEN + i].max);
        }
        t->pat = (uint8_t)(s->pat % NPAT);
        t->pat_q = 0;
        memcpy(t->step, pat_bank[k][t->pat].step, sizeof t->step);
    }
    song.sel = (uint8_t)(p->sel < NTRK ? p->sel : 0u);
    /* Older projects can hold different values per part. The selected synth part
     * supplies the song setting; a selected drum track leaves part 1 in charge. */
    k = song.sel < NPART ? song.sel : 0u;
    scale_setting_set(&trk[k], P_SCALE, trk[k].p[P_SCALE]);
    scale_setting_set(&trk[k], P_QUANT, trk[k].p[P_QUANT]);
    scale_setting_set(&trk[k], P_MPCDEG, trk[k].p[P_MPCDEG]);
    fm1_irq_on();
    for (k = 0; k < NPART; k++)                         /* a format 1 project: the default sounds of tracks 2, 3 */
        if (p->t[k].preset == 0xFFu) {
            apply_preset_to(&trk[k], TRK_DEF[k][1]);
            track_defaults_steps(&trk[k]);
        }
    sync_reload = 1;
    ui.force = 1;
    ui_message("LOADED");
}

/* settings + learned panel table: one flash object. The flash copy wins at
 * boot (the .noinit copies are garbage after a power-off). New fields go at
 * the end: a shorter PER2 record still loads (the rest stays 0), and older
 * firmware reads the prefix it knows. */
typedef struct {
    uint32_t magic, palette, lowcut, zoom;
    panel_t panel;
    uint32_t usb_off;
} persist_t;
#define PERSIST_MAGIC 0x50455232u                  /* "PER2" */
#if FELUCCA_FLASH
static persist_t persist_saved;
#endif

static void persist_boot(void)                    /* before settings_init / panel_init */
{
#if FELUCCA_FLASH
    persist_t p;
    uint32_t f = irq_save();
    flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;       /* the expected 1 MiB part, else stay RAM-only */
    irq_restore(f);
    if (!flash_ok)
        return;
    fl_plain_window_init();                        /* flash above 0x93000 reads as plaintext through XIP
                                                    * (user sample sets are played from there) */
    {
        uint32_t k;
        for (k = 0; k < SMP_USER_SLOTS; k++)
            smp_user_scan(k);
    }
    {
        int n;
        memset(&p, 0, sizeof p);
        n = st_load(OBJ_SETTINGS, &p, sizeof p);
        if ((n == (int)sizeof p || n == (int)(sizeof p - sizeof p.usb_off)) && p.magic == PERSIST_MAGIC) {
            settings.magic = SETTINGS_MAGIC;
            settings.palette = p.palette;
            settings.lowcut = p.lowcut;
            settings.zoom = p.zoom;
            settings.usb_off = p.usb_off;
            if (p.panel.magic == PANEL_MAGIC)
                panel = p.panel;
            persist_saved = p;
        } else if (n == (int)(8u + sizeof(panel_t)) && p.magic == 0x50455231u) {   /* "PER1": palette, panel */
            const uint32_t *w = (const uint32_t *)&p;
            panel_t old;
            memcpy(&old, w + 2, sizeof old);
            settings.magic = SETTINGS_MAGIC;
            settings.palette = w[1];
            settings.lowcut = 0;
            settings.zoom = 0;
            settings.usb_off = 0;
            if (old.magic == PANEL_MAGIC)
                panel = old;
        }
    }
    {   /* projects: which slots hold one, so the slot list is right after power-on */
        uint32_t i;
        for (i = 0; i < 4u; i++)
            if (proj_fetch(i))
                proj_have |= (uint8_t)(1u << i);
    }
    up_boot();                                     /* user presets */
#endif
}

static int project_used(uint32_t slot) { return (proj_have >> (slot & 3u)) & 1u; }

static void settings_save(void)
{
#if FELUCCA_FLASH
    persist_t p;
    if (!flash_ok)
        return;
    memset(&p, 0, sizeof p);
    p.magic = PERSIST_MAGIC;
    p.palette = settings.palette;
    p.lowcut = settings.lowcut;
    p.zoom = settings.zoom;
    p.panel = panel;
    p.usb_off = settings.usb_off;
    if (!memcmp(&p, &persist_saved, sizeof p))
        return;                                    /* unchanged: no erase cycle */
    if (st_save(OBJ_SETTINGS, &p, sizeof p) == 0)
        persist_saved = p;
#endif
}

#if FELUCCA_FLASH
_Static_assert(sizeof(project_t) <= ST_PROJ_SPAN * ST_SECTOR - ST_PAYLOAD_OFF, "project does not fit its flash object");
#endif
#endif /* PROJ_HOST */
