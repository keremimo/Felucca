/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The FM6 side of the parity test (tests/fm6_parity.sh): renders a score (tests/fm6_score.h) on
 * part 1 through track_render, as the firmware does, and writes what the voices sum to before
 * FM6's output stage (Q24, 1 << 24 = a unit sine; Dexed's audiobuf before its clip), one int64 a
 * sample, for tests/fm6_parity.py to set against tests/dexed_ref.cc's render of the same score.
 *   fm6_parity SCORE OUT */
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

#define FM6_E 9u                                         /* FM6's engine number */

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
        t->cc_press = (uint8_t)e->a;
        break;
    case SC_CC:
        if (e->a == 1)
            t->wheel_target = e->b * 256;
        else if (e->a == 2)
            t->cc_breath = (uint8_t)e->b;
        else if (e->a == 4)
            t->cc_foot = (uint8_t)e->b;
        else if (e->a == 5)
            fm6_ed[0][FN_PTIME] = (int16_t)e->b;
        else if (e->a == 65)
            t->cc_porta = e->b >= 64;
        break;
    }
}

int main(int argc, char **argv)
{
    static score_t s;
    track_t *t = &trk[0];
    int16_t *ed = fm6_ed[0];
    int32_t b[CTL];
    uint32_t i, k, ev = 0;
    FILE *f;
    if (argc == 3 && !strcmp(argv[1], "--rom")) {        /* a factory voice as score "voice" values */
        fm6_from_rom(ed, &FM6_ROM[atoi(argv[2]) % FM6_NROM]);
        for (i = 0; i < 155u; i++)
            printf("%d%c", ed[i], i < 154u ? ' ' : '\n');
        return 0;
    }
    if (argc < 3 || score_read(&s, argv[1])) {
        fprintf(stderr, "usage: fm6_parity SCORE OUT\n");
        return 2;
    }
    host_tracks_init();
    host_preset(t, FM6_E, 0);
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = 0;
    t->p[P_E4] = (int16_t)s.engine;
    fm6_cur[0] = (int16_t)(t->p[P_E0] + 1);              /* keep the score's voice in the buffer */
    for (i = 0; i < 155u; i++)
        ed[i] = s.voice[i];
    for (i = 0; i < 6u; i++)
        ed[FV_ON + i] = (int16_t)(s.ops[i] == '1');
    fm6_fn_reset(ed);
    fm6_fnok[0] = 1;
    ed[FN_PBUP] = (int16_t)s.pb_up;
    ed[FN_PBDN] = (int16_t)s.pb_down;
    ed[FN_PBSTEP] = (int16_t)s.pb_step;
    ed[FN_PTIME] = (int16_t)s.porta_time;
    ed[FN_GLISS] = (int16_t)s.porta_gliss;
    {
        const sc_mod_t *m[4] = {&s.wheel, &s.foot, &s.breath, &s.at};
        for (k = 0; k < 4u; k++) {
            ed[FN_MWR + 2u * k] = (int16_t)m[k]->range;
            ed[FN_MWA + 2u * k] = (int16_t)((m[k]->pitch ? 1 : 0) | (m[k]->amp ? 2 : 0) | (m[k]->eg ? 4 : 0));
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
