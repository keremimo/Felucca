/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of project formats 1..6: old parameters, patterns, engine selection and
 * FM6 voices survive migration; an older pattern becomes pattern 1 of NPAT. New MPC
 * degree defaults to 1. Damaged projects are refused.
 * Run by tests/run_tests.sh (needs build/gen from one firmware build). */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i)       /* ui.c TRK_DEF: ANALOG, DIGITAL, LOFI */
{
    static const uint8_t E[NPART] = {0, 1, 3};
    return i < NPART ? E[i] : 0u;
}
#include "../firmware/src/project.c"

static int check(const char *what, int ok)
{
    printf("%-60s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

/* the value parameter k (old id) of track t had in the old project */
static int16_t oldv(uint32_t t, uint32_t k) { return (int16_t)(t * 100u + k * 3u + 1u); }

static const uint8_t OLD_ENG[NTRK] = {7, 0, 6, 8};   /* WHEEL, ANALOG, TRIO; the drum track: 8 (none) */
static void fill_v2_track(proj_trk_v2_t *d, uint32_t t)
{
    uint32_t k;
    for (k = 0; k < PROJ_NP_V2; k++)
        d->p[k] = oldv(t, k);
    d->engine = OLD_ENG[t];
    d->preset = (uint8_t)(t + 5u);
    for (k = 0; k < NSTEP; k++) {
        step_t *s = &d->step[k];
        s->note[0] = (uint8_t)(36u + (k + t) % 40u);
        s->n = (uint8_t)(k % 3u);
        s->time = (uint8_t)(k % 3u);
        s->flags = (uint8_t)(k & 3u);
        s->vel = (uint8_t)(64u + t);
    }
}

/* an older track's pattern is pattern 1, playing, with the track's LEN etc.; the others empty */
static int pats_ok(const proj_trk_t *n, const step_t *step)
{
    uint32_t k, i;
    int ok = n->pat == 0 && !memcmp(n->pt[0].step, step, sizeof n->pt[0].step);
    for (i = 0; i < 4u; i++)
        ok &= n->pt[0].set[i] == n->p[P_SLEN + i];
    for (k = 1; k < NPAT; k++) {
        ok &= n->pt[k].set[0] == 0;
        for (i = 0; i < NSTEP; i++)
            ok &= n->pt[k].step[i].time == ST_REST && !n->pt[k].step[i].n;
    }
    return ok;
}

/* track t of the converted project has the old values where they belong */
static int track_ok(const proj_trk_t *n, const proj_trk_v2_t *o, uint32_t t)
{
    uint32_t k;
    int ok = (t == TRK_DRUM ? n->engine == 0 && n->preset == 0 : n->engine == o->engine && n->preset == o->preset) &&
             pats_ok(n, o->step);
    for (k = 0; k <= P_DETUNE; k++)
        ok &= n->p[k] == oldv(t, k);
    ok &= n->p[P_SLCR] == 0 && n->p[P_SLPAT] == TP[P_SLPAT].def && n->p[P_SLRATE] == TP[P_SLRATE].def &&
          n->p[P_SLDEPTH] == TP[P_SLDEPTH].def && n->p[P_MPCDEG] == 1;
    for (k = 0; k < 8u; k++)
        ok &= n->p[P_E0 + k] == oldv(t, 45u + k);
    return ok;
}

static int track_v4_ok(const proj_trk_t *n, const proj_trk_v4_t *o)
{
    uint32_t k;
    int ok = n->engine == o->engine && n->preset == o->preset && pats_ok(n, o->step) && n->p[P_MPCDEG] == 1;
    for (k = 0; k < 49u; k++) ok &= n->p[k] == o->p[k];
    for (k = 0; k < 8u; k++) ok &= n->p[P_E0 + k] == o->p[49u + k];
    return ok;
}

int main(void)
{
    static project_v2_t v2;
    static project_v1_t v1;
    static project_t q, q2;
    static project_v3_t v3;
    static project_v4_t v4;
    static project_v5_t v5;
    static project_old_t buf;
    uint32_t i, t;
    int bad = 0, ok;

    bad += check("layout: MPC degree follows SLICER, before engine parameters",
                 P_SLCR == P_DETUNE + 1 && P_SLDEPTH + 1 == P_MPCDEG && P_MPCDEG + 1 == P_CHMODE && P_CHSPREAD + 1 == P_E0 &&
                 P_E0 == 54 && P_COUNT == 62u);
    bad += check("format 7 fits its flash object (storage.c ST_PROJ_SPAN 5)", sizeof(project_t) <= 5u * 4096u - 256u);

    /* format 2, as written before the SLICER */
    memset(&v2, 0, sizeof v2);
    v2.magic = PROJ_MAGIC_V2;
    v2.size = sizeof v2;
    for (i = 0; i < PROJ_NG_V2; i++)
        v2.g[i] = (int16_t)(500 + i);
    v2.sel = 2;
    for (t = 0; t < NTRK; t++)
        fill_v2_track(&v2.t[t], t);
    v2.sum = proj_hash(&v2, sizeof v2 - 4u);
    bad += check("FUN2 image is 2552 bytes (as stored)", sizeof v2 == 2552u);
    memcpy(&buf, &v2, sizeof v2);
    ok = proj_import(&q, &buf, (int)sizeof v2);
    bad += check("FUN2 -> FUN7: converted, valid slot, no FM6 voices",
                 ok && proj_ok(&q) && q.magic == PROJ_MAGIC && !q.fm6_has);
    ok = q.sel == 2;
    for (i = 0; i < G_COUNT; i++)
        ok &= q.g[i] == (int16_t)(500 + i);
    bad += check("FUN2 -> FUN7: globals and selected track", ok);
    ok = 1;
    for (t = 0; t < NTRK; t++)
        ok &= track_ok(&q.t[t], &v2.t[t], t);
    bad += check("FUN2 -> FUN7: parameters mapped, SLICER OFF, degree 1", ok);

    bad += check("FUN2 -> FUN7: engine bytes kept (WHEEL 7, ANALOG 0, TRIO 6), drum 0",
                 q.t[0].engine == 7 && q.t[1].engine == 0 && q.t[2].engine == 6 && q.t[3].engine == 0 &&
                 str_eq(ENGINES[7]->name, "WHEEL") && str_eq(ENGINES[6]->name, "TRIO") && NENGINES > 8);

    /* format 5, as written before patterns: MPC degree, engines 8 / 9 and an FM6 voice */
    q.t[0].p[P_MPCDEG] = 5;
    q.t[1].engine = 8;
    q.t[2].engine = 9;
    for (i = 0; i < 128u; i++)
        q.fm6[2][i] = (uint8_t)(i * 7u & 0x7Fu);
    q.fm6_on[2] = 0x2D;
    q.fm6_has = 4;
    memset(&v5, 0, sizeof v5);
    v5.magic = PROJ_MAGIC_V5;
    v5.size = sizeof v5;
    memcpy(v5.g, q.g, sizeof v5.g);
    v5.sel = 3;
    for (t = 0; t < NTRK; t++) {
        memcpy(v5.t[t].p, q.t[t].p, sizeof v5.t[t].p);
        v5.t[t].engine = q.t[t].engine;
        v5.t[t].preset = q.t[t].preset;
        memcpy(v5.t[t].step, v2.t[t].step, sizeof v5.t[t].step);
    }
    memcpy(v5.fm6, q.fm6, sizeof v5.fm6);
    memcpy(v5.fm6_on, q.fm6_on, sizeof v5.fm6_on);
    v5.fm6_has = q.fm6_has;
    v5.sum = proj_hash(&v5, sizeof v5 - 4u);
    bad += check("FUN5 image is 2980 bytes (as stored)", sizeof v5 == 2980u);
    memcpy(&buf, &v5, sizeof v5);
    ok = proj_import(&q2, &buf, (int)sizeof v5) && proj_ok(&q2) && q2.sel == 3 && !memcmp(q2.g, v5.g, sizeof v5.g) &&
         q2.t[1].engine == 8 && q2.fm6_has == 4 && q2.fm6[2][9] == 63 && q2.fm6_on[2] == 0x2D &&
         q2.t[0].p[P_MPCDEG] == 5 && str_eq(ENGINES[9]->name, "FM6");
    for (t = 0; t < NTRK; t++)
        ok &= !memcmp(q2.t[t].p, v5.t[t].p, 50u * sizeof(int16_t)) && !memcmp(q2.t[t].p + P_E0, v5.t[t].p + 50u, 8u * sizeof(int16_t)) && q2.t[t].p[P_CHMODE] == 0 && pats_ok(&q2.t[t], v5.t[t].step);
    bad += check("FUN5 -> FUN7: degree 5, engine 8, FM6 voice; its pattern is pattern 1", ok);
    bad += check("FUN5 wrong length refused", !proj_import(&q2, &buf, (int)sizeof v5 - 2));

    /* format 6: a pattern edited, another playing; the checksum covers them */
    q2.t[1].pat = 5;
    q2.t[1].pt[5].step[7].n = 2;
    q2.t[1].pt[5].set[0] = 32;
    q2.sum = proj_sum(&q2);
    ok = proj_ok(&q2);
    q2.t[1].pt[6].step[0].vel ^= 1u;
    bad += check("FUN6: patterns under the checksum", ok && !proj_ok(&q2));

    /* Actual FUN7 layout, before chord parameters existed, and its FUN6 prefix. */
    {
        static project_v7_t old;
        memset(&old, 0, sizeof old);
        old.magic = PROJ_MAGIC_V7; old.size = sizeof old;
        memcpy(old.g, q2.g, sizeof old.g); old.sel = q2.sel;
        for (t = 0; t < NTRK; t++) {
            memcpy(old.t[t].p, q2.t[t].p, 50u * sizeof(int16_t));
            memcpy(old.t[t].p + 50, q2.t[t].p + P_E0, 8u * sizeof(int16_t));
            old.t[t].engine = q2.t[t].engine; old.t[t].preset = q2.t[t].preset; old.t[t].pat = q2.t[t].pat;
            memcpy(old.t[t].pt, q2.t[t].pt, sizeof old.t[t].pt);
        }
        memcpy(old.fm6, q2.fm6, sizeof old.fm6);
        memcpy(old.fm6_on, q2.fm6_on, sizeof old.fm6_on); old.fm6_has = q2.fm6_has;
        for (t = 0; t < NPART; t++) for (i = 0; i < 16; i++) old.fm6_fn[t][i] = (int8_t)(t + i);
        old.sum = proj_hash(&old, sizeof old - 4u);
        memcpy(&q2, &old, sizeof old);
        ok = proj_from_v7(&q2, sizeof old) && proj_ok(&q2) && q2.fm6_has == 4 && q2.fm6[2][9] == 63 &&
             q2.t[1].pat == 5 && q2.t[1].pt[5].step[7].n == 2 && !memcmp(q2.fm6_fn, old.fm6_fn, sizeof old.fm6_fn);
        for (t = 0; t < NTRK; t++) {
            ok &= !memcmp(q2.t[t].p, old.t[t].p, 50u * sizeof(int16_t)) &&
                  !memcmp(q2.t[t].p + P_E0, old.t[t].p + 50, 8u * sizeof(int16_t)) &&
                  !memcmp(q2.t[t].pt, old.t[t].pt, sizeof old.t[t].pt);
            for (i = P_CHMODE; i <= P_CHSPREAD; i++) ok &= q2.t[t].p[i] == TP[i].def;
        }
        bad += check("FUN7 -> FUN8: every pattern, sound, function and new default preserved", ok);
        old.magic = PROJ_MAGIC_V6; old.size = PROJ_V6_SIZE;
        { uint32_t sum = proj_hash(&old, PROJ_V6_SIZE - 4u); memcpy((uint8_t *)&old + PROJ_V6_SIZE - 4u, &sum, 4); }
        memcpy(&q2, &old, PROJ_V6_SIZE);
        ok = proj_from_v6(&q2, PROJ_V6_SIZE) && proj_ok(&q2) && q2.fm6_has == 4 && q2.fm6[2][9] == 63 &&
             q2.t[1].pat == 5 && q2.t[1].pt[5].step[7].n == 2 && q2.fm6_fn[0][0] < 0 && q2.fm6_fn[2][15] < 0;
        bad += check("FUN6 -> FUN8: in place, voices and patterns kept, functions default", ok);
        memcpy(&q2, &old, PROJ_V6_SIZE); ((uint8_t *)&q2)[100] ^= 1;
        bad += check("FUN6 with a bad sum refused", !proj_from_v6(&q2, PROJ_V6_SIZE));
        old.magic = PROJ_MAGIC_V7; old.size = sizeof old; old.sum = proj_hash(&old, sizeof old - 4u);
        memcpy(&q2, &old, sizeof old); ((uint8_t *)&q2)[100] ^= 1;
        bad += check("FUN7 with a bad sum refused", !proj_from_v7(&q2, sizeof old));
    }
    {
        project_t f = q2;
        f.fm6_fn[1][0] = 0;
        f.magic = PROJ_MAGIC;
        f.size = sizeof f;
        f.sum = proj_sum(&f);
        bad += check("FUN7: the FM6 functions under the checksum", proj_ok(&f) && (f.fm6_fn[1][1] ^= 1, !proj_ok(&f)));
    }

    /* format 3, as written before FM6: the same tracks, no voices */
    memset(&v3, 0, sizeof v3);
    v3.magic = PROJ_MAGIC_V3;
    v3.size = sizeof v3;
    memcpy(v3.g, q.g, sizeof v3.g);
    v3.sel = 1;
    for (t = 0; t < NTRK; t++) {
        for (i = 0; i < PROJ_NP_V4; i++) v3.t[t].p[i] = oldv(t, i);
        v3.t[t].engine = q.t[t].engine;
        v3.t[t].preset = q.t[t].preset;
        memcpy(v3.t[t].step, q.t[t].pt[0].step, sizeof v3.t[t].step);
    }
    v3.sum = proj_hash(&v3, sizeof v3 - 4u);
    memcpy(&buf, &v3, sizeof v3);
    bad += check("FUN3 image is 2584 bytes (as stored)", sizeof v3 == 2584u);
    ok = proj_import(&q2, &buf, (int)sizeof v3) && proj_ok(&q2) &&
         !memcmp(q2.g, q.g, sizeof q.g) && q2.sel == 1 && !q2.fm6_has;
    for (t = 0; t < NTRK; t++) ok &= track_v4_ok(&q2.t[t], &v3.t[t]);
    bad += check("FUN3 -> FUN7: remapped tracks, globals, degree 1; no FM6 voices", ok);

    memset(&v4, 0, sizeof v4);
    v4.magic = PROJ_MAGIC_V4;
    v4.size = sizeof v4;
    v4.sel = 2;
    memcpy(v4.g, v3.g, sizeof v4.g);
    memcpy(v4.t, v3.t, sizeof v4.t);
    memcpy(v4.fm6, q.fm6, sizeof v4.fm6);
    memcpy(v4.fm6_on, q.fm6_on, sizeof v4.fm6_on);
    v4.fm6_has = q.fm6_has;
    v4.sum = proj_hash(&v4, sizeof v4 - 4u);
    memcpy(&buf, &v4, sizeof v4);
    ok = proj_import(&q2, &buf, (int)sizeof v4) && proj_ok(&q2) && q2.sel == 2 &&
         !memcmp(q2.g, v4.g, sizeof v4.g) && !memcmp(q2.fm6, v4.fm6, sizeof v4.fm6) &&
         !memcmp(q2.fm6_on, v4.fm6_on, sizeof v4.fm6_on) && q2.fm6_has == v4.fm6_has;
    for (t = 0; t < NTRK; t++) ok &= track_v4_ok(&q2.t[t], &v4.t[t]);
    bad += check("FUN4 -> FUN7: remapped tracks, degree 1, complete FM6 voices", ok);
    bad += check("FUN4 wrong length refused", !proj_import(&q2, &buf, (int)sizeof v4 - 2));
    v4.fm6[2][3] ^= 1u;
    memcpy(&buf, &v4, sizeof v4);
    bad += check("FUN4 corrupt voice refused", !proj_import(&q2, &buf, (int)sizeof v4));
    v3.t[0].p[0]++;
    memcpy(&buf, &v3, sizeof v3);
    bad += check("FUN3 with a bad checksum: refused", !proj_import(&q2, &buf, (int)sizeof v3));

    /* damaged / wrong size */
    v2.t[1].p[3]++;
    memcpy(&buf, &v2, sizeof v2);
    bad += check("FUN2 with a bad checksum: refused", !proj_import(&q2, &buf, (int)sizeof v2));
    v2.t[1].p[3]--;
    memcpy(&buf, &v2, sizeof v2);
    bad += check("FUN2 with a wrong length: refused", !proj_import(&q2, &buf, (int)sizeof v2 - 2));
    memcpy(&buf, &v5, sizeof v5);
    buf.v5.magic = PROJ_MAGIC_V2;
    bad += check("FUN5 size with a FUN2 magic: refused", !proj_import(&q2, &buf, (int)sizeof v5));

    /* format 1: one instrument -> track 1, the others their defaults */
    memset(&v1, 0, sizeof v1);
    v1.magic = PROJ_MAGIC_V1;
    v1.size = sizeof v1;
    for (i = 0; i < PROJ_NG_V2; i++)
        v1.g[i] = (int16_t)(700 + i);
    fill_v2_track(&v1.t, 0);
    v1.sum = proj_hash(&v1, sizeof v1 - 4u);
    memcpy(&buf, &v1, sizeof v1);
    ok = proj_import(&q, &buf, (int)sizeof v1) && proj_ok(&q) && track_ok(&q.t[0], &v1.t, 0) && q.g[5] == 705;
    for (t = 1; t < NTRK; t++)
        ok &= q.t[t].preset == 0xFF && q.t[t].p[P_SLCR] == 0 && q.t[t].p[P_LEVEL] == TP[P_LEVEL].def &&
              q.t[t].p[P_E0] == ENGINES[trk_def_engine(t)]->edit[0].def && q.t[t].pt[0].step[0].time == ST_REST &&
              q.t[t].pat == 0 && q.t[t].pt[0].set[0] == q.t[t].p[P_SLEN];
    bad += check("FUN1 -> FUN7: track 1 mapped, tracks 2..4 defaults", ok);

    printf("%s\n", bad ? "PROJECT FORMAT TEST FAILED" : "project format test passed");
    return bad != 0;
}
