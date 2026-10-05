/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The FM6 side of the parity test (tests/fm6_parity.sh): renders a score (tests/fm6_score.h) on
 * track 1 through track_render, as the firmware does, and writes what the voices sum to before
 * FM6's output stage (Q24, 1 << 24 = a unit sine; Dexed's audiobuf before its clip), one int64 a
 * sample, for tests/fm6_parity.py to set against tests/dexed_ref.cc's render of the same score.
 *   fm6_parity SCORE OUT
 *   fm6_parity --rom N          Melodee's factory voice N (PTCH F9 + N) as the 155 score "voice" values */
#include <stdint.h>
static int64_t tap[32];
#define FM6_TAP(b, n)                                                                              \
    do {                                                                                           \
        for (uint32_t ti = 0; ti < (n); ti++)                                                      \
            tap[ti] += (b)[ti];                                                                    \
    } while (0)
#define main hostsim_main
#include "hostsim.c"
#undef main
#include "fm6_score.h"

static void event(track_t *t, const sc_event_t *e)
{
    switch (e->type) {
    case SC_ON:
        trk_note_on(t, (uint32_t)e->a, (uint32_t)e->b);
        break;
    case SC_OFF:
        trk_note_off(t, (uint32_t)e->a);
        break;
    case SC_BEND:
        t->bend_raw = (int16_t)(e->a - 8192);
        break;
    case SC_PRESS:
        t->at = (uint8_t)e->a;
        break;
    case SC_CC:
        if (e->a == 1)
            t->mw = (uint8_t)e->b;
        else if (e->a == 2)
            t->breath = (uint8_t)e->b;
        else if (e->a == 4)
            t->foot = (uint8_t)e->b;
        else if (e->a == 5)
            fm6_fn_set(FN_PTIME, e->b);
        else if (e->a == 65)
            t->porta = e->b >= 64;
        break;
    }
}

int main(int argc, char **argv)
{
    static score_t s;
    track_t *t = &trk[0];
    uint8_t v[FP_SIZE + 1u];
    int32_t b[CTL];
    uint32_t i, k, ev = 0;
    FILE *f;
    if (argc == 3 && !strcmp(argv[1], "--rom")) {        /* a factory voice as score "voice" values */
        fm6_rom_patch(&FM6_ROM[atoi(argv[2]) % FM6_NROM], v);
        for (i = 0; i < 155u; i++)
            printf("%d%c", v[i], i < 154u ? ' ' : '\n');
        return 0;
    }
    if (argc < 3 || score_read(&s, argv[1])) {
        fprintf(stderr, "usage: fm6_parity SCORE OUT\n");
        return 2;
    }
    host_tracks_init();
    host_preset(t, ENGI_FM6, 0);
    for (i = 0; i < 8u; i++)                             /* the macros neutral: the patch as it is */
        t->p[P_E0 + i] = 0;
    for (i = 0; i < 155u; i++)
        v[i] = (uint8_t)s.voice[i];
    fm6_put_patch(0, v, 1);
    fm6_slot[0] = (uint8_t)t->p[P_E7];                   /* (fm6_poll would keep it: the track's own) */
    fm6_on[0] = 0;
    for (i = 0; i < 6u; i++)                             /* ops[i]: OP i + 1, fm6_on bit 6 - n for OP n */
        fm6_on[0] |= (uint8_t)((s.ops[i] == '1') << (5u - i));
    fm6_fn_reset();
    fm6_fn_set(FN_ENGINE, s.engine);
    fm6_fn_set(FN_PBUP, s.pb_up);
    fm6_fn_set(FN_PBDN, s.pb_down);
    fm6_fn_set(FN_PBSTEP, s.pb_step);
    fm6_fn_set(FN_PTIME, s.porta_time);
    fm6_fn_set(FN_GLISS, s.porta_gliss);
    {
        const sc_mod_t *m[4] = {&s.wheel, &s.foot, &s.breath, &s.at};
        for (k = 0; k < 4u; k++) {
            fm6_fn_set(FN_MWR + 2u * k, m[k]->range);
            fm6_fn_set(FN_MWA + 2u * k, (m[k]->pitch ? 1 : 0) | (m[k]->amp ? 2 : 0) | (m[k]->eg ? 4 : 0));
        }
    }
    t->p[P_VOICE] = s.mono ? V_LEGATO : V_POLY;          /* Dexed's mono: legato, the highest key */
    t->p[P_PRIO] = 2;
    song.g[G_TUNE] = 0;
    f = fopen(argv[2], "wb");
    if (!f)
        return 2;
    for (int blk = 0; blk < s.len; blk++) {
        while (ev < (uint32_t)s.nev && s.ev[ev].block <= blk)
            event(t, &s.ev[ev++]);
        for (k = 0; k < SC_N / CTL; k++) {
            memset(tap, 0, sizeof tap);
            track_render(t, b, CTL);
            fwrite(tap, sizeof tap[0], CTL, f);
        }
    }
    fclose(f);
    return 0;
}
