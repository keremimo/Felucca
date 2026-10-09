/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the delay bus (firmware/src/delay.c: fm1-x0x's tape delay in fixed point), same sources as the
 * firmware (through hostsim.c):   build/host/delay_test
 * 1. off: no dsend, nothing: the bus silent bit for bit, no line borrowed, no stereo side.
 * 2. DIGI: an impulse's echo at the time (1/8 and x0x's 1/8. at 120 BPM, within 2 samples: the half rate), at MIX's
 *    level; FDBK repeats it, each quieter and darker; a division longer than the line halves, still on the beat.
 * 3. TAPE: FDBK 120 (1.14) with TONE and WEAR at their ends self-oscillates bounded (x0x's tape_checks): 20 s, no
 *    runaway; WEAR swings the echo's pitch (the period of a 1 kHz sine's echo wanders), DIGI's does not.
 * 4. PP (DG-PP / TP-PP): the first echo left at the time, the right at twice it; twice the time past the line: centred.
 * 5. a TIME change glides (the read never jumps: no click), a TYPE change too.
 * 6. idle: once the tail is gone the line is given back; a new dsend starts from silence, bit for bit as a fresh bus.
 * 7. cost: instructions per sample of the bus (DIGI, TAPE, TP-PP) against ROOM's reverb.
 * Demos (WAV) into DEMO_DIR: a pluck line through each TYPE at x0x's defaults, the last 3 s the tail alone. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

static int bad;
static void check(const char *what, int ok)
{
    printf("delay: %-93s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}

#define NT (24u * FS)
static int32_t dsend[NT], out_l[NT], out_r[NT];
static uint32_t lend_max;                        /* the most samples the line was borrowed at once */

/* the bus from rest: no line, every state 0, the globals given */
static void bus_reset(int type, int time, int fdbk, int tone, int mix, int wear)
{
    host_tracks_init();
    if (dly_buf)
        resource_release(RES_DELAY);
    dly_buf = 0;
    memset(&dl, 0, sizeof dl);
    song.g[G_BPM] = 120;
    song.g[G_DTYPE] = (int16_t)type;
    song.g[G_DTIME] = (int16_t)time;
    song.g[G_DFDBK] = (int16_t)fdbk;
    song.g[G_DCOLOR] = (int16_t)tone;
    song.g[G_DMIX] = (int16_t)mix;
    song.g[G_DWEAR] = (int16_t)wear;
}
/* dsend[0 .. n) through dly_bus: left and right (the mid and the side as mix_block adds them) */
static void bus_run(uint32_t n)
{
    static int32_t w[CTL];
    uint32_t t, i;
    lend_max = 0;
    for (t = 0; t < n; t += CTL) {
        for (i = 0; i < CTL; i++)
            w[i] = 0;
        dly_bus(dsend + t, w, CTL);
        for (i = 0; i < CTL; i++) {
            out_l[t + i] = w[i] + (dl.side ? dl.sd[i] : 0);
            out_r[t + i] = w[i] - (dl.side ? dl.sd[i] : 0);
        }
        if (dly_buf && resource[RES_DELAY].size > lend_max)
            lend_max = resource[RES_DELAY].size;
    }
}
static uint32_t peak_at(const int32_t *x, uint32_t a, uint32_t b, int32_t *pk)
{
    uint32_t t, at = a;
    int32_t m = -1;
    for (t = a; t < b; t++)
        if (abs(x[t]) > m) {
            m = abs(x[t]);
            at = t;
        }
    if (pk)
        *pk = m;
    return at;
}
static int32_t peak(const int32_t *x, uint32_t a, uint32_t b)
{
    int32_t m;
    peak_at(x, a, b, &m);
    return m;
}
static void impulse(void)
{
    memset(dsend, 0, sizeof dsend);
    dsend[0] = dsend[1] = 16000;                    /* (two samples: one at the half rate, through the 1 2 1) */
}

static void test_off(void)
{
    uint32_t t, any = 0;
    bus_reset(1, 11, 120, 127, 127, 127);
    memset(dsend, 0, sizeof dsend);
    bus_run(4u * FS);
    for (t = 0; t < 4u * FS; t++)
        any |= (uint32_t)(out_l[t] | out_r[t]);
    check("no send: silent bit for bit, no line borrowed, no side (TAPE, FDBK 120, WEAR 127)", !any && !dly_buf && !lend_max);
}

static void test_digi(void)
{
    char what[200];
    int32_t p1, p2, p3;
    uint32_t a1, a2, a3;
    static const struct { int time; uint32_t at; const char *name; } T[] = {{1, 11025, "1/8"}, {11, 16538, "1/8."},
                                                                            {12, 33075, "1/4."}, {13, 14700, "4T"}};
    for (uint32_t k = 0; k < NELEM(T); k++) {
        bus_reset(0, T[k].time, 0, 127, 127, 64);
        impulse();
        bus_run(2u * FS);
        a1 = peak_at(out_l, 1000, 2u * FS, &p1);
        snprintf(what, sizeof what, "DIGI %s at 120 BPM: the echo at %u (expected %u), level %d, L = R, then gone (the low cut's tail)", T[k].name,
                 a1, T[k].at, p1);
        check(what, abs((int32_t)a1 - (int32_t)T[k].at) <= 3 && p1 > 3000 && !memcmp(out_l, out_r, 2u * FS * 4u) &&
              peak(out_l, a1 + 2000u, 2u * FS) < p1 / 1000);
    }
    bus_reset(0, 1, 120, 127, 127, 64);
    impulse();
    bus_run(2u * FS);
    a1 = peak_at(out_l, 1000, 16000, &p1);
    a2 = peak_at(out_l, 16000, 27000, &p2);
    a3 = peak_at(out_l, 27000, 38000, &p3);
    snprintf(what, sizeof what, "DIGI FDBK 120: repeats at %u %u %u, each quieter (%d %d %d)", a1, a2, a3, p1, p2, p3);
    check(what, abs((int32_t)(a2 - a1) - 11025) <= 2 && abs((int32_t)(a3 - a2) - 11025) <= 2 && p2 < p1 && p3 < p2 && p3 > p1 / 4);
    {   /* TONE: the repeats darker at 0 than at 127 (their sample-to-sample change against their level) */
        double r[2];
        for (int k = 0; k < 2; k++) {
            int32_t pk, st = 0;
            bus_reset(0, 1, 100, k ? 127 : 0, 127, 64);
            for (uint32_t t = 0; t < NT; t++)
                dsend[t] = t < 2000u ? (int32_t)(((t * 2654435761u) >> 16) & 0x3FFF) - 8192 : 0;   /* a click of noise */
            bus_run(FS);
            peak_at(out_l, 2u * 11025u, 2u * 11025u + 2500u, &pk);
            for (uint32_t t = 2u * 11025u; t < 2u * 11025u + 2500u; t++)
                st = abs(out_l[t + 1] - out_l[t]) > st ? abs(out_l[t + 1] - out_l[t]) : st;
            r[k] = pk ? (double)st / pk : 0;
        }
        snprintf(what, sizeof what, "  TONE: the second repeat's steps / its peak %.2f at TONE 0, %.2f at 127", r[0], r[1]);
        check(what, r[0] < 0.6 * r[1]);
    }
    {   /* 4BAR at 120 BPM: 16 beats, too long: halved to 2 beats (1 s), still on the beat */
        bus_reset(0, 9, 0, 127, 127, 64);
        impulse();
        bus_run(2u * FS);
        a1 = peak_at(out_l, 1000, 2u * FS, &p1);
        snprintf(what, sizeof what, "a division past the line (4BAR at 120 BPM) halves on the beat: the echo at %u (44100)", a1);
        check(what, abs((int32_t)a1 - 44100) <= 2 && p1 > 3000);
    }
    {   /* MIX: the echo's level follows it (0: none) */
        bus_reset(0, 1, 0, 127, 0, 64);
        impulse();
        bus_run(FS);
        check("MIX 0: no echo", peak(out_l, 0, FS) == 0);
    }
}

static void test_tape(void)
{
    char what[240];
    static const int W[3] = {0, 64, 127};
    for (uint32_t k = 0; k < 3u; k++) {   /* x0x's tape_checks: a snare-ish burst, FDBK at its top, 20 s */
        int32_t pk, late;
        bus_reset(1, 11, 120, 127, 127, W[k]);
        for (uint32_t t = 0; t < NT; t++)
            dsend[t] = t < 4000u ? (int32_t)((((t * 2654435761u) >> 16) & 0xFFFF) - 32768) * (int32_t)(4000u - t) / 2000 : 0;
        bus_run(20u * FS);
        pk = peak(out_l, 0, 20u * FS);
        late = peak(out_l, 18u * FS, 20u * FS);
        snprintf(what, sizeof what, "TAPE FDBK 120 (1.14) TONE 127 WEAR %d: self-oscillates bounded: peak %d, the last 2 s %d",
                 W[k], pk, late);
        check(what, pk < 4 * 32768 && late > 500 && late <= pk);
    }
    {   /* the swing: a 300 Hz sine's echo (FDBK 0), its period over 15 cycles at a time (zero crossings, between samples):
         * TAPE WEAR 127 wanders (wow and flutter), DIGI's stays (the resampling's own error averages out) */
        double sd[2];
        for (int k = 0; k < 2; k++) {
            uint32_t t, n = 0, c = 0;
            double x0 = -1, s = 0, s2 = 0;
            bus_reset(k, 1, 0, 127, 127, 127);
            for (t = 0; t < NT; t++)
                dsend[t] = (int32_t)(12000 * sin(2 * M_PI * 300.0 * t / FS));
            bus_run(6u * FS);
            for (t = 2u * FS; t < 6u * FS; t++)
                if (out_l[t - 1] < 0 && out_l[t] >= 0) {
                    double x = t - 1 + (double)-out_l[t - 1] / (out_l[t] - out_l[t - 1]);
                    if (x0 < 0)
                        x0 = x;
                    else if (++c == 15u) {
                        double p = (x - x0) / 15;
                        s += p;
                        s2 += p * p;
                        n++;
                        x0 = x;
                        c = 0;
                    }
                }
            sd[k] = n > 1 ? sqrt(fmax(s2 / n - (s / n) * (s / n), 0)) : 0;
        }
        snprintf(what, sizeof what, "TAPE WEAR 127 swings the echo's pitch: a 300 Hz echo's period +-%.3f samples (DIGI +-%.4f)", sd[1], sd[0]);
        check(what, sd[1] > 0.3 && sd[0] < 0.02);
    }
}

static void test_pp(void)
{
    char what[200];
    int32_t pl, pr;
    uint32_t al, ar;
    for (int tape = 0; tape < 2; tape++) {
        bus_reset(2 + tape, 1, 0, 127, 127, 0);
        impulse();
        bus_run(FS);
        al = peak_at(out_l, 1000, FS, &pl);
        ar = peak_at(out_r, 1000, FS, &pr);
        snprintf(what, sizeof what, "%s: the first echo left at %u (11025), right at %u (22050; TAPE: its swing); levels %d %d", tape ? "TP-PP" : "DG-PP",
                 al, ar, pl, pr);
        check(what, abs((int32_t)al - 11025) <= (tape ? 12 : 3) && abs((int32_t)ar - 22050) <= (tape ? 24 : 3) && pl > 3000 && pr > 3000 &&
              abs(out_r[al]) < pl / 8 && abs(out_l[ar]) < pr / 8);
    }
    bus_reset(2, 6, 0, 127, 127, 0);                 /* 1/2 at 120 BPM: 1 s, twice that past the line */
    impulse();
    bus_run(2u * FS);
    check("DG-PP with twice the time past the line (1/2 at 120 BPM): centred (L = R)", !memcmp(out_l, out_r, 2u * FS * 4u) &&
          peak(out_l, 0, 2u * FS) > 3000);
}

static void test_glide(void)
{
    char what[200];
    int32_t own = 0, step = 0;
    uint32_t t, i;
    bus_reset(0, 1, 60, 127, 127, 64);
    for (t = 0; t < NT; t++)
        dsend[t] = (int32_t)(12000 * sin(2 * M_PI * 220.0 * t / FS));
    /* 2 s, then TIME 1/16 at 2 s, TYPE TAPE at 3 s, TP-PP at 4 s, back to DIGI at 5 s: the largest step of the left
     * or right around a change against the largest step of the same signal before it */
    {
        static int32_t w[CTL];
        int32_t pl = 0, pr = 0;
        for (t = 0; t < 6u * FS; t += CTL) {
            if (t == 2u * FS) song.g[G_DTIME] = 2;
            if (t == 3u * FS) song.g[G_DTYPE] = 1;
            if (t == 4u * FS) song.g[G_DTYPE] = 3;
            if (t == 5u * FS) song.g[G_DTYPE] = 0;
            for (i = 0; i < CTL; i++)
                w[i] = 0;
            dly_bus(dsend + t, w, CTL);
            for (i = 0; i < CTL; i++) {
                int32_t l = w[i] + (dl.side ? dl.sd[i] : 0), r = w[i] - (dl.side ? dl.sd[i] : 0);
                int32_t s = abs(l - pl) > abs(r - pr) ? abs(l - pl) : abs(r - pr);
                if (t >= FS && t < 2u * FS)
                    own = s > own ? s : own;
                else if (t >= 2u * FS)
                    step = s > step ? s : step;
                pl = l;
                pr = r;
            }
        }
    }
    snprintf(what, sizeof what, "TIME and TYPE changes glide: the largest step %d after them (the echo's own %d)", step, own);
    check(what, step <= 2 * own);
}

static void test_idle(void)
{
    static int32_t first[FS], again[FS];
    char what[200];
    bus_reset(0, 1, 100, 60, 127, 64);
    impulse();
    bus_run(20u * FS);
    check("the tail gone: the line given back (DIGI FDBK 100)", !dly_buf && !resource[RES_DELAY].size && !dl.side);
    impulse();
    bus_run(FS);
    memcpy(again, out_l, sizeof again);
    bus_reset(0, 1, 100, 60, 127, 64);                /* a fresh bus (its write position elsewhere: the line all 0) */
    impulse();
    bus_run(FS);
    memcpy(first, out_l, sizeof first);
    snprintf(what, sizeof what, "a new send after the release: the echoes bit for bit a fresh bus's (borrowed %u B again)", lend_max);
    check(what, !memcmp(first, again, sizeof first) && lend_max == DL_N * 2u && peak(again, 0, FS) > 3000);
}

static double cost_of(int type, int rev)
{
    static int32_t c[CTL], d[CTL], r[CTL], w[CTL];
    uint32_t b, i, nb = 4u * FS / CTL;
    uint64_t i0;
    bus_reset(type, 11, 80, 64, 100, 64);
    for (i = 0; i < CTL; i++) {
        c[i] = 0;
        d[i] = (int32_t)((i * 2654435761u) >> 18) - 8192;
        r[i] = d[i];
    }
    rev_clear();
    song.g[G_RTYPE] = 0;
    fx.rtype = 0;
    i0 = instr_now();
    for (b = 0; b < nb; b++) {
        if (rev)
            rev_room(r, w, CTL);
        else
            dly_bus(d, w, CTL);
    }
    return i0 ? (double)(instr_now() - i0) / (nb * CTL) : 0;
}
static void test_cost(void)
{
    double room = cost_of(0, 1), dg = cost_of(0, 0), tp = cost_of(1, 0), pp = cost_of(3, 0);
    char what[200];
    if (!room) {
        printf("delay: cost: no instruction counter on this host\n");
        return;
    }
    snprintf(what, sizeof what, "cost: DIGI %.0f, TAPE %.0f, TP-PP %.0f instructions / sample (ROOM %.0f; ~%.2f %% CPU at TP-PP)",
             dg, tp, pp, room, pp * 0.017);
    check(what, pp <= 1.5 * room && pp * 0.017 <= 2.0);
}

static void demo(const char *dir, const char *name, int type, int wear)
{
    static const uint8_t PL[16] = {64, 0, 67, 0, 71, 0, 0, 74, 72, 0, 67, 0, 64, 0, 62, 0};
    char path[512];
    FILE *f;
    uint32_t t, i, frames = 10u * FS;
    int32_t o[2 * CTL];
    track_t *tr = &trk[0];
    memset(trk, 0, sizeof trk);
    bus_reset(type, 11, 70, 51, 85, wear);
    song.g[G_BPM] = 100;
    host_preset(tr, 0, 8);                                      /* ANALOG PLUCK */
    for (i = 0; i < 16u; i++)
        put_step(tr, i, PL[i] ? 1u : 0u, &PL[i], PL[i] ? ST_NOTE : ST_REST, 0);
    tr->p[P_DLY] = 90;
    tr->p[P_REV] = tr->p[P_CHOR] = 0;
    snprintf(path, sizeof path, "%s/%s.wav", dir, name);
    if (!(f = fopen(path, "wb")))
        return;
    wav_hdr(f, frames);
    transport_req = 1;
    for (t = 0; t < frames; t += CTL) {
        if (t <= 7u * FS && t + CTL > 7u * FS)
            transport_req = 2;                                  /* the last 3 s: the repeats alone */
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++)
            wav_put(f, o[2 * i], o[2 * i + 1]);
    }
    fclose(f);
    printf("delay: demo %s\n", path);
}

int main(int argc, char **argv)
{
    test_off();
    test_digi();
    test_tape();
    test_pp();
    test_glide();
    test_idle();
    test_cost();
    if (argc > 1) {
        demo(argv[1], "delay_digi", 0, 64);
        demo(argv[1], "delay_tape", 1, 64);
        demo(argv[1], "delay_tape_worn", 1, 127);
        demo(argv[1], "delay_tape_pingpong", 3, 64);
    }
    printf("%s\n", bad ? "DELAY TEST FAILED" : "delay test passed");
    return bad != 0;
}
