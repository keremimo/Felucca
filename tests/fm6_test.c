/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM6 (eng_fm6.c) against the DX7: the 32 algorithms against their diagrams, packed voices,
 * pitch, ratios, fixed frequencies, detune and transpose, output-level, velocity and key
 * scaling steps, envelope times, release, feedback, LFO, pitch envelope, key sync.
 * Renders part 1 alone (track_render: no FX, no master), through hostsim.c. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>

#define FM6_E 9u                                         /* FM6's engine number */
static track_t *const T = &trk[0];
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); puts(""); } } while (0)

/* ------------------------------------------------------- algorithms --- */
/* each algorithm as the DX7 manual draws it: the operators modulating each operator,
 * the carriers, the operator with feedback (4, 6: a loop through OP6; FM6 feeds OP6 back) */
static const struct { uint8_t mod[7]; uint8_t car, fb; } DIAGRAM[32] = {
    /* mod[n]: bit m = OP m modulates OP n; car: bit n = OP n is a carrier */
    {{0, 1 << 2, 0, 1 << 4, 1 << 5, 1 << 6, 0}, 1 << 1 | 1 << 3, 6},                       /* 1 */
    {{0, 1 << 2, 0, 1 << 4, 1 << 5, 1 << 6, 0}, 1 << 1 | 1 << 3, 2},                       /* 2 */
    {{0, 1 << 2, 1 << 3, 0, 1 << 5, 1 << 6, 0}, 1 << 1 | 1 << 4, 6},                       /* 3 */
    {{0, 1 << 2, 1 << 3, 0, 1 << 5, 1 << 6, 0}, 1 << 1 | 1 << 4, 6},                       /* 4 */
    {{0, 1 << 2, 0, 1 << 4, 0, 1 << 6, 0}, 1 << 1 | 1 << 3 | 1 << 5, 6},                   /* 5 */
    {{0, 1 << 2, 0, 1 << 4, 0, 1 << 6, 0}, 1 << 1 | 1 << 3 | 1 << 5, 6},                   /* 6 */
    {{0, 1 << 2, 0, 1 << 4 | 1 << 5, 0, 1 << 6, 0}, 1 << 1 | 1 << 3, 6},                   /* 7 */
    {{0, 1 << 2, 0, 1 << 4 | 1 << 5, 0, 1 << 6, 0}, 1 << 1 | 1 << 3, 4},                   /* 8 */
    {{0, 1 << 2, 0, 1 << 4 | 1 << 5, 0, 1 << 6, 0}, 1 << 1 | 1 << 3, 2},                   /* 9 */
    {{0, 1 << 2, 1 << 3, 0, 1 << 5 | 1 << 6, 0, 0}, 1 << 1 | 1 << 4, 3},                   /* 10 */
    {{0, 1 << 2, 1 << 3, 0, 1 << 5 | 1 << 6, 0, 0}, 1 << 1 | 1 << 4, 6},                   /* 11 */
    {{0, 1 << 2, 0, 1 << 4 | 1 << 5 | 1 << 6, 0, 0, 0}, 1 << 1 | 1 << 3, 2},               /* 12 */
    {{0, 1 << 2, 0, 1 << 4 | 1 << 5 | 1 << 6, 0, 0, 0}, 1 << 1 | 1 << 3, 6},               /* 13 */
    {{0, 1 << 2, 0, 1 << 4, 1 << 5 | 1 << 6, 0, 0}, 1 << 1 | 1 << 3, 6},                   /* 14 */
    {{0, 1 << 2, 0, 1 << 4, 1 << 5 | 1 << 6, 0, 0}, 1 << 1 | 1 << 3, 2},                   /* 15 */
    {{0, 1 << 2 | 1 << 3 | 1 << 5, 0, 1 << 4, 0, 1 << 6, 0}, 1 << 1, 6},                   /* 16 */
    {{0, 1 << 2 | 1 << 3 | 1 << 5, 0, 1 << 4, 0, 1 << 6, 0}, 1 << 1, 2},                   /* 17 */
    {{0, 1 << 2 | 1 << 3 | 1 << 4, 0, 0, 1 << 5, 1 << 6, 0}, 1 << 1, 3},                   /* 18 */
    {{0, 1 << 2, 1 << 3, 0, 1 << 6, 1 << 6, 0}, 1 << 1 | 1 << 4 | 1 << 5, 6},              /* 19 */
    {{0, 1 << 3, 1 << 3, 0, 1 << 5 | 1 << 6, 0, 0}, 1 << 1 | 1 << 2 | 1 << 4, 3},          /* 20 */
    {{0, 1 << 3, 1 << 3, 0, 1 << 6, 1 << 6, 0}, 1 << 1 | 1 << 2 | 1 << 4 | 1 << 5, 3},     /* 21 */
    {{0, 1 << 2, 0, 1 << 6, 1 << 6, 1 << 6, 0}, 1 << 1 | 1 << 3 | 1 << 4 | 1 << 5, 6},     /* 22 */
    {{0, 0, 1 << 3, 0, 1 << 6, 1 << 6, 0}, 1 << 1 | 1 << 2 | 1 << 4 | 1 << 5, 6},          /* 23 */
    {{0, 0, 0, 1 << 6, 1 << 6, 1 << 6, 0}, 0x3E, 6},                                       /* 24 */
    {{0, 0, 0, 0, 1 << 6, 1 << 6, 0}, 0x3E, 6},                                            /* 25 */
    {{0, 0, 1 << 3, 0, 1 << 5 | 1 << 6, 0, 0}, 1 << 1 | 1 << 2 | 1 << 4, 6},               /* 26 */
    {{0, 0, 1 << 3, 0, 1 << 5 | 1 << 6, 0, 0}, 1 << 1 | 1 << 2 | 1 << 4, 3},               /* 27 */
    {{0, 1 << 2, 0, 1 << 4, 1 << 5, 0, 0}, 1 << 1 | 1 << 3 | 1 << 6, 5},                   /* 28 */
    {{0, 0, 0, 1 << 4, 0, 1 << 6, 0}, 1 << 1 | 1 << 2 | 1 << 3 | 1 << 5, 6},               /* 29 */
    {{0, 0, 0, 1 << 4, 1 << 5, 0, 0}, 1 << 1 | 1 << 2 | 1 << 3 | 1 << 6, 5},               /* 30 */
    {{0, 0, 0, 0, 0, 1 << 6, 0}, 0x3E, 6},                                                 /* 31 */
    {{0, 0, 0, 0, 0, 0, 0}, 0x7E, 6},                                                      /* 32 */
};

static void algorithms(void)
{
    uint32_t a, k;
    for (a = 0; a < 32u; a++) {
        uint8_t bus[3] = {0, 0, 0}, mod[7] = {0}, car = 0, fb = 0;
        for (k = 0; k < 6u; k++) {                       /* run the routing on operator sets */
            uint32_t f = FM6_ALG[a][k], op = 6u - k, dst = f & 3u, src = (f >> 4) & 3u;
            mod[op] = src ? bus[src] : 0;
            if (f & 0x40u)
                fb = (uint8_t)op;
            if (!dst)
                car |= (uint8_t)(1u << op);
            else
                bus[dst] = (uint8_t)((f & 4u ? bus[dst] : 0) | 1u << op);
        }
        CHECK(!memcmp(mod, DIAGRAM[a].mod, 7) && car == DIAGRAM[a].car && fb == DIAGRAM[a].fb,
              "algorithm %u does not match its diagram", a + 1);
        for (k = 1; k <= 6u; k++)
            CHECK(fm6_carrier(a, k) == ((car >> k) & 1u), "fm6_carrier(%u, %u)", a + 1, k);
    }
    puts("FM6 algorithms: the 32 DX7 diagrams (modulators, carriers, feedback): OK");
}

/* ---------------------------------------------------------- voices --- */
static void voices(void)
{
    static int16_t ed[FM6_NP], back[FM6_NP];
    uint8_t b[128];
    uint32_t v, i;
    for (v = 0; v < FM6_NROM; v++) {
        fm6_from_rom(ed, &FM6_ROM[v]);
        fm6_pack(b, ed);
        for (i = 0; i < 128u; i++)
            CHECK(b[i] < 128u, "voice %u byte %u = %u (SysEx data is 7 bits)", v, i, b[i]);
        fm6_unpack(back, b);
        CHECK(!memcmp(ed, back, sizeof ed), "voice %u: pack / unpack round trip", v);
        for (i = 0; i < FV_ON; i++)
            CHECK(ed[i] >= fm6_min(i) && ed[i] <= fm6_max(i), "voice %u parameter %u = %d out of range", v, i, ed[i]);
    }
    memset(b, 0x7F, sizeof b);                           /* garbage in: every value clamped */
    fm6_unpack(ed, b);
    for (i = 0; i < FV_ON; i++)
        CHECK(ed[i] >= fm6_min(i) && ed[i] <= fm6_max(i), "unpack clamps parameter %u (%d)", i, ed[i]);
    printf("FM6 voices: %u factory voices pack to 7-bit DX7 data and back; bad data clamped: OK\n",
           (unsigned)FM6_NROM);
}

/* ------------------------------------------------------- rendering --- */
#define NS (FS * 2)
static int32_t wave_l[NS];
static int16_t *ED;                                      /* part 1's buffer */

static void fresh(void)                                  /* part 1 = FM6 with the init voice, nothing else */
{
    uint32_t i;
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    host_preset(T, FM6_E, 0);
    ED = fm6_ed[0];
    fm6_from_rom(ED, &FM6_INIT);
    fm6_cur[0] = (int16_t)(T->p[P_E0] + 1);              /* the block keeps this buffer */
    for (i = 0; i < 8u; i++)
        T->p[P_E0 + i] = (int16_t)(i ? 0 : T->p[P_E0]);
}

static int16_t *opp(uint32_t n) { return &ED[FM6_OPB(n)]; }

static void render(int32_t *w, uint32_t frames)          /* part 1's dry output, mono */
{
    int32_t b[CTL];
    uint32_t i, k;
    for (i = 0; i < frames; i += CTL) {
        track_render(T, b, CTL);
        for (k = 0; k < CTL && i + k < frames; k++)
            w[i + k] = b[k];
    }
}

static double freq(const int32_t *w, uint32_t a, uint32_t b)   /* from the rising zero crossings */
{
    uint32_t i, first = 0, last = 0, n = 0;
    double f0 = 0, f1 = 0;
    for (i = a + 1u; i < b; i++)
        if (w[i - 1] < 0 && w[i] >= 0) {
            double t = (double)(i - 1) + (double)-w[i - 1] / (double)(w[i] - w[i - 1]);
            if (!n++)
                f0 = t, first = i;
            f1 = t, last = i;
        }
    (void)first;
    (void)last;
    return n > 1 ? (n - 1) * (double)FS / (f1 - f0) : 0;
}

static double peak(const int32_t *w, uint32_t a, uint32_t b)
{
    double p = 0;
    for (uint32_t i = a; i < b; i++)
        if (fabs((double)w[i]) > p)
            p = fabs((double)w[i]);
    return p;
}

static double tone(const int32_t *w, uint32_t a, uint32_t b, double f)   /* amplitude at f (Hann window) */
{
    double re = 0, im = 0, ws = 0;
    for (uint32_t i = a; i < b; i++) {
        double h = 0.5 - 0.5 * cos(6.283185307179586 * (i - a) / (b - a)), ph = 6.283185307179586 * f * i / FS;
        re += w[i] * h * cos(ph);
        im += w[i] * h * sin(ph);
        ws += h;
    }
    return 2 * sqrt(re * re + im * im) / ws;
}

static double db(double x) { return 20 * log10(x); }

static void pitch(void)
{
    double f;
    fresh();
    trk_note_on(T, 69, 100);
    render(wave_l, FS / 2);
    f = freq(wave_l, 2000, FS / 2);
    CHECK(fabs(f - 440) < 0.2, "A4: %.3f Hz", f);
    fresh();
    opp(1)[FO_CRS] = 3;
    opp(1)[FO_FINE] = 17;                                /* 3 x 1.17 = 3.51 */
    trk_note_on(T, 69, 100);
    render(wave_l, FS / 2);
    f = freq(wave_l, 2000, FS / 2);
    CHECK(fabs(f / (440 * 3.51) - 1) < 0.001, "ratio 3.51: %.2f Hz", f);
    fresh();
    opp(1)[FO_CRS] = 0;                                  /* 0.5 */
    trk_note_on(T, 69, 100);
    render(wave_l, FS / 2);
    f = freq(wave_l, 2000, FS / 2);
    CHECK(fabs(f - 220) < 0.2, "ratio 0.5: %.3f Hz", f);
    for (uint32_t note = 40; note <= 80u; note += 40u) {
        fresh();
        opp(1)[FO_MODE] = 1;
        opp(1)[FO_CRS] = 2;                              /* 100 Hz at any key */
        trk_note_on(T, note, 100);
        render(wave_l, FS / 2);
        f = freq(wave_l, 2000, FS / 2);
        CHECK(fabs(f - 100) < 0.2, "fixed 100 Hz on note %u: %.3f Hz", note, f);
    }
    fresh();
    opp(1)[FO_MODE] = 1;
    opp(1)[FO_CRS] = 3;
    opp(1)[FO_FINE] = 30;                                /* 1 kHz x 10^0.3 */
    trk_note_on(T, 60, 100);
    render(wave_l, FS / 2);
    f = freq(wave_l, 2000, FS / 2);
    CHECK(fabs(f / 1995.26 - 1) < 0.002, "fixed 1995 Hz: %.2f Hz", f);
    fresh();
    opp(1)[FO_DET] = 14;                                 /* +7 */
    trk_note_on(T, 69, 100);
    render(wave_l, FS / 2);
    f = freq(wave_l, 2000, FS / 2);
    CHECK(f > 441.0 && f < 443.5, "detune +7 at A4: %.3f Hz", f);
    fresh();
    ED[FV_TRNSP] = 36;                                   /* +12 */
    trk_note_on(T, 69, 100);
    render(wave_l, FS / 2);
    f = freq(wave_l, 2000, FS / 2);
    CHECK(fabs(f - 880) < 0.4, "transpose +12: %.3f Hz", f);
    puts("FM6 pitch: A4, ratios 3.51 / 0.5, fixed 100 Hz and 1995 Hz on any key, detune, transpose: OK");
}

static void levels(void)
{
    double a99, a90, lo, hi;
    fresh();
    trk_note_on(T, 69, 100);
    render(wave_l, FS / 4);
    a99 = peak(wave_l, FS / 8, FS / 4);
    fresh();
    opp(1)[FO_OL] = 90;
    trk_note_on(T, 69, 100);
    render(wave_l, FS / 4);
    a90 = peak(wave_l, FS / 8, FS / 4);
    CHECK(fabs(db(a90 / a99) + 6.77) < 0.2, "OUTPUT 99 -> 90: %.2f dB (DX7: -6.77)", db(a90 / a99));
    fresh();
    opp(1)[FO_KVS] = 7;
    trk_note_on(T, 69, 127);
    render(wave_l, FS / 4);
    hi = peak(wave_l, FS / 8, FS / 4);
    fresh();
    opp(1)[FO_KVS] = 7;
    trk_note_on(T, 69, 64);
    render(wave_l, FS / 4);
    lo = peak(wave_l, FS / 8, FS / 4);
    CHECK(fabs(db(lo / hi) + 15.81) < 0.3, "velocity 127 -> 64 at sensitivity 7: %.2f dB (DX7: -15.8)",
          db(lo / hi));
    fresh();                                             /* -LIN right depth 99 from C4: down above it */
    opp(1)[FO_BP] = 39;
    opp(1)[FO_RD] = 99;
    opp(1)[FO_RC] = 0;
    trk_note_on(T, 84, 100);
    render(wave_l, FS / 4);
    lo = peak(wave_l, FS / 8, FS / 4);
    fresh();
    trk_note_on(T, 84, 100);
    render(wave_l, FS / 4);
    hi = peak(wave_l, FS / 8, FS / 4);
    /* two octaves over the break point: group (84 - 39 - 17 + 1) / 3 = 9, 9 x 99 x 329 >> 12 = 71 steps of 0.75 dB */
    CHECK(fabs(db(lo / hi) + 71 * 32 / 256.0 * 6.0206) < 0.3, "key scaling -LIN 99, two octaves up: %.2f dB",
          db(lo / hi));
    puts("FM6 levels: OUTPUT steps, velocity curve, key level scaling: OK");
}

static void envelopes(void)
{
    uint32_t i, t60 = 0;
    double top;
    fresh();
    opp(1)[FO_R2] = 50;
    opp(1)[FO_L2] = 0;                                   /* decay from L1 99 to 0 at RATE 50 */
    trk_note_on(T, 69, 100);
    render(wave_l, NS);
    top = peak(wave_l, 0, 2048);
    for (i = 0; i + 512u < NS; i += 256u)
        if (peak(wave_l, i, i + 512u) < top / 1000) {
            t60 = i;
            break;
        }
    /* RATE 50: qrate 32, 4 << 15 per block of 32: 10 doublings in 1280 blocks, 0.93 s */
    CHECK(t60 > FS * 85 / 100 && t60 < FS * 103 / 100, "RATE 50 decay to -60 dB: %.3f s (0.93)", (double)t60 / FS);
    fresh();
    opp(1)[FO_R4] = 60;
    trk_note_on(T, 69, 100);
    render(wave_l, FS / 4);
    trk_note_off(T, 69);
    for (i = 0; i < FS * 3u && T->v[0].active; i += CTL)
        render(wave_l, CTL);
    /* RATE 60: qrate 38, 6 << 16: from OUTPUT 99 down 10 doublings in 427 blocks, 0.31 s */
    CHECK(!T->v[0].active && i > FS / 4 && i < FS / 2, "RATE 60 release frees the voice: %.3f s", (double)i / FS);
    fresh();                                             /* the attack: RATE 99 at the top within 3 ms */
    trk_note_on(T, 69, 100);
    render(wave_l, FS / 8);
    CHECK(peak(wave_l, 0, FS * 3 / 1000) > 0.9 * peak(wave_l, FS / 16, FS / 8), "RATE 99 attack");
    fresh();                                             /* a held L3 holds */
    opp(1)[FO_R2] = 70;
    opp(1)[FO_L2] = 80;
    opp(1)[FO_L3] = 80;
    trk_note_on(T, 69, 100);
    render(wave_l, NS);
    CHECK(fabs(db(peak(wave_l, FS, FS * 3 / 2) / peak(wave_l, FS * 3 / 2, NS))) < 0.1 &&
              fabs(db(peak(wave_l, FS, NS) / top) + 9 * 64 / 256.0 * 6.0206) < 0.3,
          "sustain at L3 80: %.2f dB", db(peak(wave_l, FS, NS) / top));
    puts("FM6 envelopes: attack, decay time, sustain level, release and voice end: OK");
}

static void modulation(void)
{
    double h0, h7, f, lo, hi;
    uint32_t i;
    fresh();                                             /* OP6 alone (algorithm 32 feeds it back) */
    ED[FV_ALG] = 31;
    opp(1)[FO_OL] = 0;
    opp(6)[FO_OL] = 99;
    opp(6)[FO_L4] = 0;
    trk_note_on(T, 57, 100);
    render(wave_l, FS / 2);
    h0 = tone(wave_l, FS / 8, FS / 2, 440) / tone(wave_l, FS / 8, FS / 2, 220);
    fresh();
    ED[FV_ALG] = 31;
    ED[FV_FB] = 7;
    opp(1)[FO_OL] = 0;
    opp(6)[FO_OL] = 99;
    trk_note_on(T, 57, 100);
    render(wave_l, FS / 2);
    h7 = tone(wave_l, FS / 8, FS / 2, 440) / tone(wave_l, FS / 8, FS / 2, 220);
    CHECK(db(h0) < -60 && db(h7) > -12, "feedback 0 / 7: 2nd harmonic %.1f / %.1f dB", db(h0), db(h7));
    fresh();                                             /* OP2 -> OP1 at 1:1: brighter with OP2's level */
    opp(2)[FO_OL] = 70;
    trk_note_on(T, 57, 100);
    render(wave_l, FS / 2);
    lo = tone(wave_l, FS / 8, FS / 2, 440) / tone(wave_l, FS / 8, FS / 2, 220);
    fresh();
    opp(2)[FO_OL] = 85;
    trk_note_on(T, 57, 100);
    render(wave_l, FS / 2);
    hi = tone(wave_l, FS / 8, FS / 2, 440) / tone(wave_l, FS / 8, FS / 2, 220);
    {   /* OUTPUT 70 at the top of its envelope: 2^(2912 / 256 - 14) cycles of phase, a 1.02 rad index;
         * OUTPUT 85: 3392, 3.74 rad. A 1:1 pair puts J1 + J3 at 2f, J0 - J2 at f */
        double b70 = 6.283185307179586 * pow(2, 2912 / 256.0 - 14), b85 = 6.283185307179586 * pow(2, 3392 / 256.0 - 14);
        double w70 = fabs((jn(1, b70) + jn(3, b70)) / (jn(0, b70) - jn(2, b70)));
        double w85 = fabs((jn(1, b85) + jn(3, b85)) / (jn(0, b85) - jn(2, b85)));
        CHECK(fabs(db(lo) - db(w70)) < 0.3 && fabs(db(hi) - db(w85)) < 0.5,
              "modulation index: 2f/f %.2f dB at OUTPUT 70 (Bessel %.2f), %.2f dB at 85 (%.2f)", db(lo), db(w70),
              db(hi), db(w85));
    }
    fresh();                                             /* vibrato: PMD 99, PMS 7, no delay */
    ED[FV_LFS] = 35;
    ED[FV_LPMD] = 99;
    ED[FV_LPMS] = 3;
    ED[FV_LFW] = 4;
    trk_note_on(T, 69, 100);
    render(wave_l, NS);
    lo = 1e9;
    hi = 0;
    for (i = FS / 4; i + 1024u < NS; i += 512u) {
        f = freq(wave_l, i, i + 1024u);
        lo = f < lo ? f : lo;
        hi = f > hi ? f : hi;
    }
    /* PMD 99 x PMS 3: 255 x 33 x 2^47 >> 39 = 0.128 octave each way */
    CHECK(hi / lo > 1.05 && hi / lo < pow(2, 2 * 0.128) + 0.01, "LFO vibrato: %.1f .. %.1f Hz", lo, hi);
    fresh();                                             /* tremolo: AMD 99 on AMS 3 */
    ED[FV_LFS] = 35;
    ED[FV_LAMD] = 99;
    ED[FV_LFW] = 0;
    opp(1)[FO_AMS] = 3;
    trk_note_on(T, 69, 100);
    render(wave_l, NS);
    lo = 1e9;
    hi = 0;
    for (i = FS / 4; i + 256u < NS; i += 256u) {
        f = peak(wave_l, i, i + 256u);
        lo = f < lo ? f : lo;
        hi = f > hi ? f : hi;
    }
    CHECK(db(lo / hi) < -12, "LFO tremolo (AMS 3): %.1f dB", db(lo / hi));
    fresh();                                             /* pitch envelope: from +1 oct (L4, L1) back to the note */
    ED[FV_PL + 3] = 82;                                  /* +32/32: an octave; the envelope starts at L4 */
    ED[FV_PL + 0] = 82;
    ED[FV_PR + 1] = 70;                                  /* 79 x 572 per block: back down in 0.27 s */
    ED[FV_PL + 1] = 50;
    ED[FV_PL + 2] = 50;
    trk_note_on(T, 57, 100);
    render(wave_l, FS);
    f = freq(wave_l, 0, 400);
    CHECK(f > 440 * 0.97 && f < 440 * 1.001, "pitch envelope at L4 / L1 82: %.1f Hz (an octave up)", f);
    f = freq(wave_l, FS / 2, FS);
    CHECK(fabs(f - 220) < 0.3, "pitch envelope back to the note: %.2f Hz", f);
    puts("FM6 modulation: feedback, modulation index, LFO vibrato and tremolo, pitch envelope: OK");
}

static void every_algorithm(void)
{
    uint32_t a, n, k;
    for (a = 0; a < 32u; a++) {
        double pk;
        fresh();
        ED[FV_ALG] = (int16_t)a;
        ED[FV_FB] = 7;
        for (n = 1; n <= 6u; n++) {
            opp(n)[FO_OL] = 99;
            opp(n)[FO_CRS] = (int16_t)n;
        }
        for (k = 0; k < 4u; k++)
            trk_note_on(T, 48 + 7 * k, 127);
        render(wave_l, FS / 4);
        pk = peak(wave_l, 0, FS / 4);
        CHECK(pk > 1000 && pk < 4 * 65536.0 * VOICE_FS / 32768, "algorithm %u, everything at 99: peak %.0f", a + 1, pk);
    }
    puts("FM6: every algorithm at full levels and feedback stays bounded: OK");
}

static void sysex_pending(void)
{
    static const uint8_t voice[] = {0xF0, 0x43, 0, 0, 1, 0x1B, 42, 0xF7};
    static const uint8_t ping[] = {0xF0, 0x7D, 0x46, 0x4C, 25, 0xF7};
    uint32_t i;
    fm6_rx_ready = fm6_rx_on = 0;
    fm6_rx_n = 0;
    for (i = 0; i < sizeof voice; i++) fm6_sx_byte(voice[i]);
    CHECK(fm6_rx_ready && fm6_rx_n == sizeof voice, "voice queued for the main loop");
    for (i = 0; i < sizeof ping; i++) fm6_sx_byte(ping[i]);
    CHECK(fm6_rx_ready && fm6_rx_n == sizeof voice && !memcmp(fm6_rx, voice, sizeof voice),
          "editor traffic must not discard a pending DX7 frame");
    fm6_sx_byte(0xF0); fm6_sx_byte(0x43); fm6_sx_byte(0xF7);
    CHECK(fm6_rx_ready && fm6_rx_n == sizeof voice && !memcmp(fm6_rx, voice, sizeof voice),
          "a second Yamaha frame must not discard the pending frame");
    fm6_rx_ready = 0;                              /* main loop consumed it */
    fm6_sx_byte(0xF0); fm6_sx_byte(0x43); fm6_sx_byte(0xF7);
    CHECK(fm6_rx_ready && fm6_rx_n == 3 && fm6_rx[2] == 0xF7, "receiver accepts the next frame after consumption");
    fm6_rx_ready = 0;
}

int main(void)
{
    sysex_pending();
    algorithms();
    voices();
    pitch();
    levels();
    envelopes();
    modulation();
    every_algorithm();
    if (fails) {
        printf("FM6: %d checks failed\n", fails);
        return 1;
    }
    puts("FM6: all checks passed");
    return 0;
}
