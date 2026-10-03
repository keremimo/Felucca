/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the project formats (firmware/src/project.c, -DPROJ_HOST part): a format 2 ("FUN2",
 * 53 parameters per track) and a format 1 ("FUN1") project, built byte for byte as the firmware
 * before the SLICER stored them, convert to format 4 ("FUN4"): every old value at its parameter, the
 * SLICER parameters at their defaults (OFF), steps, globals, selection, the engine bytes (0..7 kept:
 * the engines added since were appended; the drum track's byte 0), no FM6 voices; a format 3
 * ("FUN3", before FM6) converts as is; damaged ones are refused.
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

/* track t of the converted project has the old values where they belong */
static int track_ok(const proj_trk_t *n, const proj_trk_v2_t *o, uint32_t t)
{
    uint32_t k;
    int ok = (t == TRK_DRUM ? n->engine == 0 && n->preset == 0 : n->engine == o->engine && n->preset == o->preset) &&
             !memcmp(n->step, o->step, sizeof n->step);
    for (k = 0; k <= P_DETUNE; k++)
        ok &= n->p[k] == oldv(t, k);
    ok &= n->p[P_SLCR] == 0 && n->p[P_SLPAT] == TP[P_SLPAT].def && n->p[P_SLRATE] == TP[P_SLRATE].def &&
          n->p[P_SLDEPTH] == TP[P_SLDEPTH].def;
    for (k = 0; k < 8u; k++)
        ok &= n->p[P_E0 + k] == oldv(t, 45u + k);
    return ok;
}

int main(void)
{
    static project_v2_t v2;
    static project_v1_t v1;
    static project_t q, q2;
    static project_v3_t v3;
    static union {
        project_t v4;
        project_v3_t v3;
        project_v2_t v2;
        project_v1_t v1;
    } buf;
    uint32_t i, t;
    int bad = 0, ok;

    bad += check("layout: SLICER ids just before P_E0, P_E0 = old P_E0 + 4",
                 P_SLCR == P_DETUNE + 1 && P_SLDEPTH + 1 == P_E0 && P_E0 == 49 && P_COUNT == PROJ_NP_V2 + 4u);
    bad += check("format 4 fits one flash object", sizeof(project_t) <= 4096u - 256u);

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
    bad += check("FUN2 -> FUN4: converted, valid format 4 slot, no FM6 voices",
                 ok && proj_ok(&q) && q.magic == PROJ_MAGIC && !q.fm6_has);
    ok = q.sel == 2;
    for (i = 0; i < G_COUNT; i++)
        ok &= q.g[i] == (int16_t)(500 + i);
    bad += check("FUN2 -> FUN4: globals and selected track", ok);
    ok = 1;
    for (t = 0; t < NTRK; t++)
        ok &= track_ok(&q.t[t], &v2.t[t], t);
    bad += check("FUN2 -> FUN4: every parameter mapped, SLICER OFF (4 tracks)", ok);

    bad += check("FUN2 -> FUN4: engine bytes kept (WHEEL 7, ANALOG 0, TRIO 6), drum 0",
                 q.t[0].engine == 7 && q.t[1].engine == 0 && q.t[2].engine == 6 && q.t[3].engine == 0 &&
                 str_eq(ENGINES[7]->name, "WHEEL") && str_eq(ENGINES[6]->name, "TRIO") && NENGINES > 8);

    /* a FUN4 round trip: stored as is (an engine added since: GRAIN 8; an FM6 voice on part 3) */
    q.t[1].engine = 8;
    q.t[2].engine = 9;
    for (i = 0; i < 128u; i++)
        q.fm6[2][i] = (uint8_t)(i * 7u & 0x7Fu);
    q.fm6_on[2] = 0x2D;
    q.fm6_has = 4;
    q.sum = proj_sum(&q);
    memcpy(&buf, &q, sizeof q);
    bad += check("FUN4 -> FUN4: as stored (engine 8, the FM6 voice of part 3)",
                 proj_import(&q2, &buf, (int)sizeof q) && !memcmp(&q, &q2, sizeof q) && q2.t[1].engine == 8 &&
                     q2.fm6_has == 4 && q2.fm6[2][9] == 63 && str_eq(ENGINES[9]->name, "FM6"));

    /* format 3, as written before FM6: the same tracks, no voices */
    memset(&v3, 0, sizeof v3);
    v3.magic = PROJ_MAGIC_V3;
    v3.size = sizeof v3;
    memcpy(v3.g, q.g, sizeof v3.g);
    v3.sel = 1;
    memcpy(v3.t, q.t, sizeof v3.t);
    v3.sum = proj_hash(&v3, sizeof v3 - 4u);
    memcpy(&buf, &v3, sizeof v3);
    bad += check("FUN3 image is 2584 bytes (as stored)", sizeof v3 == 2584u);
    bad += check("FUN3 -> FUN4: tracks, globals, selection; no FM6 voices",
                 proj_import(&q2, &buf, (int)sizeof v3) && proj_ok(&q2) && !memcmp(q2.t, q.t, sizeof q.t) &&
                     !memcmp(q2.g, q.g, sizeof q.g) && q2.sel == 1 && !q2.fm6_has);
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
    memcpy(&buf, &q, sizeof q);
    buf.v4.magic = PROJ_MAGIC_V2;
    bad += check("FUN4 size with a FUN2 magic: refused", !proj_import(&q2, &buf, (int)sizeof q));

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
              q.t[t].p[P_E0] == ENGINES[trk_def_engine(t)]->edit[0].def && q.t[t].step[0].time == ST_REST;
    bad += check("FUN1 -> FUN4: track 1 mapped, tracks 2..4 defaults", ok);

    printf("%s\n", bad ? "PROJECT FORMAT TEST FAILED" : "project format test passed");
    return bad != 0;
}
