/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM6 parity scores: one DX7 voice, the player's settings and timed MIDI events, read the same
 * way by the FM6 side (tests/fm6_parity.c) and the Dexed side (tests/dexed_ref.cc), so both
 * render the same performance. Text, one directive per line, '#' starts a comment:
 *   engine E                 0 MODERN, 1 MARK I, 2 OPL (Dexed's engine resolutions)
 *   voice v0 .. v154         a DX7 voice in VCED order (OP6 first), 155 values
 *   ops 111111               operator switches, OP1 .. OP6
 *   mono M                   0 poly, 1 mono
 *   pb UP DOWN STEP          pitch-bend range up / down (semitones) and step
 *   porta TIME GLISS         portamento time (0..127, CC 5) and glissando
 *   mod SRC RANGE P A E      SRC wheel | foot | breath | at: range 0..99, assign pitch / amp / EG bias
 *   tune T                   master tune, Q24 log (1 << 24 = an octave)
 *   len B                    render B blocks of 64 samples
 *   at B on N V | off N | cc C V | bend V | press V    an event at block B (in block order)
 */
#ifndef FM6_SCORE_H
#define FM6_SCORE_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SC_N 64                                          /* samples in a score block (Dexed's N) */
#define SC_MAXEV 4096
enum { SC_ON, SC_OFF, SC_CC, SC_BEND, SC_PRESS };
typedef struct { int block, type, a, b; } sc_event_t;
typedef struct { int range, pitch, amp, eg; } sc_mod_t;
typedef struct {
    int engine, mono, pb_up, pb_down, pb_step, porta_time, porta_gliss, tune, len;
    unsigned char voice[155];
    char ops[7];                                         /* OP1..OP6, '0' / '1' */
    sc_mod_t wheel, foot, breath, at;
    int nev;
    sc_event_t ev[SC_MAXEV];
} score_t;

static int score_read(score_t *s, const char *path)
{
    char line[2048];
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    memset(s, 0, sizeof *s);
    s->engine = 1;
    s->pb_up = s->pb_down = 3;
    strcpy(s->ops, "111111");
    while (fgets(line, sizeof line, f)) {
        char *c = strchr(line, '#'), *p = line, w[16];
        int n;
        if (c)
            *c = 0;
        if (sscanf(p, "%15s%n", w, &n) != 1)
            continue;
        p += n;
        if (!strcmp(w, "engine"))
            s->engine = atoi(p);
        else if (!strcmp(w, "mono"))
            s->mono = atoi(p);
        else if (!strcmp(w, "tune"))
            s->tune = atoi(p);
        else if (!strcmp(w, "len"))
            s->len = atoi(p);
        else if (!strcmp(w, "pb"))
            sscanf(p, "%d %d %d", &s->pb_up, &s->pb_down, &s->pb_step);
        else if (!strcmp(w, "porta"))
            sscanf(p, "%d %d", &s->porta_time, &s->porta_gliss);
        else if (!strcmp(w, "ops"))
            sscanf(p, "%6s", s->ops);
        else if (!strcmp(w, "voice")) {
            int i, v;
            for (i = 0; i < 155; i++) {
                if (sscanf(p, "%d%n", &v, &n) != 1)
                    break;
                s->voice[i] = (unsigned char)v;
                p += n;
            }
            if (i != 155) {
                fclose(f);
                return -2;
            }
        } else if (!strcmp(w, "mod")) {
            char src[16];
            sc_mod_t m;
            if (sscanf(p, "%15s %d %d %d %d", src, &m.range, &m.pitch, &m.amp, &m.eg) == 5)
                *(!strcmp(src, "wheel") ? &s->wheel : !strcmp(src, "foot") ? &s->foot
                  : !strcmp(src, "breath") ? &s->breath : &s->at) = m;
        } else if (!strcmp(w, "at") && s->nev < SC_MAXEV) {
            sc_event_t *e = &s->ev[s->nev];
            char t[16];
            if (sscanf(p, "%d %15s %d %d", &e->block, t, &e->a, &e->b) < 3)
                continue;
            e->type = !strcmp(t, "on") ? SC_ON : !strcmp(t, "off") ? SC_OFF : !strcmp(t, "cc") ? SC_CC
                    : !strcmp(t, "bend") ? SC_BEND : SC_PRESS;
            s->nev++;
        }
    }
    fclose(f);
    return 0;
}
#endif
