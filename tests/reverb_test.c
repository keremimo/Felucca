/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The reverb bus's two models (src/fx.c: REVERB TYPE, G_RTYPE) on the Mac, through hostsim.c as regress.c.
 *   build/host/reverb_test [DEMODIR]          (run_tests.sh: build/fx_demo)
 * 1. ROOM bit-identical: fx_buses against a copy of the buses as they were before SPRING (chorus, delay and the
 *    4-comb room in one loop), on noise sends with SIZE / DAMP / the delay and chorus settings changing, sample
 *    for sample. (The goldens of regress.c, all rendered with ROOM, say the same for the whole mix.)
 * 2. SPRING decay: the impulse response's RT60 (Schroeder integral, -5 .. -35 dB) rises with SIZE, within
 *    0.15 .. 1 s at SIZE 0 and 2 .. 6 s at 127.
 * 3. SPRING dispersion: the group delay of the first arrival rises with frequency (1 .. 5 kHz, each band later
 *    than the one below, 5 kHz at least 2 ms after 1 kHz; 500 Hz printed: the loop's low cut delays it a
 *    little), and the second arrival (one more pass round the loop) is more spread than the first: the
 *    chirp grows with each echo.
 * 4. SPRING stable at the corners: SIZE 127 with DAMP 0 and 127, 2 s of full-scale noise or square waves (the
 *    sum of four tracks' largest sends) then silence: bounded, and the tail dies away (no limit cycle, no
 *    offset).
 * 5. level: SPRING's tail within 6 dB of ROOM's (RMS of the first 1.5 s after a noise burst, defaults).
 * 6. a model change while the bus rings: no click (the old one's block fades out), the new one starts silent.
 * 7. cost: host instructions per sample of the reverb alone (rev_room, rev_spring): SPRING at most ROOM + 30 %;
 *    and of the whole bus stage (fx_buses), with the device estimate (1.7 % per 100, drum_test's ratio).
 * Demos (WAV) into DEMODIR: a drum pattern and a pluck through SPRING, the drums through ROOM to compare. */
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
    printf("reverb: %-92s %s\n", what, ok ? "ok" : "FAIL");
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
static uint32_t xs = 0x1234567u;
static int32_t noise(int32_t amp)
{
    xs ^= xs << 13;
    xs ^= xs >> 17;
    xs ^= xs << 5;
    return (int32_t)(((int64_t)(int32_t)xs * amp) >> 31);
}

/* ------------------------------------------------- the buses before SPRING --- */
static int16_t ref_cho[CHO_LEN], ref_comb[1116 + 1188 + 1277 + 1356], ref_ap[556 + 441];
static struct {
    uint32_t cho_w, cho_ph;
    uint16_t comb_i[4], ap_i[2];
    int32_t comb_lp[4];
} rf;
static void ref_buses(const int32_t *cho_in, const int32_t *rev_in, int32_t *wet, uint32_t n)
{
    uint32_t i, k;
    int32_t size = 25000 + song.g[G_RSIZE] * 50, damp = 32767 - song.g[G_RDAMP] * 200;
    int32_t cdepth = song.g[G_CDEPTH] * 6;
    uint32_t cinc = LFO_INC[song.g[G_CRATE] & 127] / CTL;
    for (i = 0; i < n; i++) {
        int32_t y = 0, r, a;
        ref_cho[rf.cho_w & (CHO_LEN - 1u)] = (int16_t)clamp(cho_in[i] >> 1, -32768, 32767);
        rf.cho_ph += cinc;
        r = (400 << 8) + ((osc_sine(rf.cho_ph) + 32768) * cdepth >> 8);
        {
            uint32_t ri = (uint32_t)r >> 8;
            int32_t f = r & 255, c0 = ref_cho[(rf.cho_w - ri) & (CHO_LEN - 1u)];
            int32_t c1 = ref_cho[(rf.cho_w - ri - 1u) & (CHO_LEN - 1u)];
            y += (c0 + (((c1 - c0) * f) >> 8)) << 1;
        }
        rf.cho_w++;
        a = 0;
        {
            int16_t *c = ref_comb;
            int32_t in = mulq15(rev_in[i], 2580);
            for (k = 0; k < 4u; k++) {
                int32_t o = c[rf.comb_i[k]];
                rf.comb_lp[k] = o + mulq15(rf.comb_lp[k] - o, 32767 - damp);
                c[rf.comb_i[k]] = (int16_t)clamp(in + mulq15(rf.comb_lp[k], size), -32768, 32767);
                if (++rf.comb_i[k] >= REV_COMB[k])
                    rf.comb_i[k] = 0;
                a += o;
                c += REV_COMB[k];
            }
            c = ref_ap;
            for (k = 0; k < 2u; k++) {
                int32_t o = c[rf.ap_i[k]];
                int32_t v = a + (o >> 1);
                c[rf.ap_i[k]] = (int16_t)clamp(v, -32768, 32767);
                a = o - a;
                if (++rf.ap_i[k] >= REV_AP[k])
                    rf.ap_i[k] = 0;
                c += REV_AP[k];
            }
        }
        y += a;
        wet[i] = y;
    }
}

static void test_room_identical(void)
{
    static int32_t c[CTL], d[CTL], r[CTL], w0[CTL], w1[CTL];
    uint32_t b, i, diff = 0, nb = 30u * FS / CTL;
    host_tracks_init();
    for (b = 0; b < nb; b++) {
        if (b % 700u == 0u) {                                   /* the settings move now and then */
            song.g[G_RSIZE] = (int16_t)((uint32_t)noise(1 << 30) % 128u);
            song.g[G_RDAMP] = (int16_t)((uint32_t)noise(1 << 30) % 128u);
            song.g[G_DTIME] = (int16_t)((uint32_t)noise(1 << 30) % 6u);
            song.g[G_DFDBK] = (int16_t)((uint32_t)noise(1 << 30) % 121u);
            song.g[G_CDEPTH] = (int16_t)((uint32_t)noise(1 << 30) % 128u);
        }
        for (i = 0; i < CTL; i++) {
            int32_t on = (b / 300u) % 3u != 2u;                 /* bursts and silences: the tails too */
            c[i] = on ? noise(60000) : 0;
            d[i] = on ? noise(60000) : 0;
            r[i] = on ? noise(b % 2000u < 1000u ? 90000 : 4000) : 0;
        }
        fx_buses(c, r, w0, CTL);
        ref_buses(c, r, w1, CTL);
        for (i = 0; i < CTL; i++)
            diff += w0[i] != w1[i];
    }
    check("ROOM: the buses bit for bit as before SPRING (30 s of noise sends, settings changing)", !diff && !fx.rtype);
}

/* ------------------------------------------------------------- SPRING --- */
#define IRN (12u * FS / CTL * CTL)
static int32_t ir[IRN];
static void spring_reset(int32_t size, int32_t damp)
{
    song.g[G_RTYPE] = 1;
    song.g[G_RSIZE] = (int16_t)size;
    song.g[G_RDAMP] = (int16_t)damp;
    rev_clear();
    fx.rtype = 1;
    fx.sp_w = 0;
    fx.sp_size = 0;
    fx.sp_ph = 0;
}
/* the spring's response to x[] (len samples, the rest silence) into ir[0 .. n) */
static void spring_run(const int32_t *x, uint32_t len, uint32_t n)
{
    static int32_t in[CTL];
    uint32_t t, i;
    for (t = 0; t < n; t += CTL) {
        for (i = 0; i < CTL; i++) {
            in[i] = t + i < len ? x[t + i] : 0;
            ir[t + i] = 0;
        }
        rev_spring(in, ir + t, CTL);
    }
}
static double rt60(const int32_t *h, uint32_t n)       /* Schroeder: -5 .. -35 dB, x2 */
{
    static double e[IRN];
    double s = 0, t5 = -1, t35 = -1;
    uint32_t i;
    for (i = n; i-- > 0;) {
        s += (double)h[i] * h[i];
        e[i] = s;
    }
    for (i = 0; i < n; i++) {
        double db = 10 * log10(e[i] / e[0] + 1e-30);
        if (t5 < 0 && db <= -5)
            t5 = i;
        if (t35 < 0 && db <= -35) {
            t35 = i;
            break;
        }
    }
    return t35 < 0 ? 99 : (t35 - t5) * 2.0 / FS;
}
/* group delay (samples) of h[a .. b) at f Hz: Re(sum n h e^-jwn / sum h e^-jwn) */
static double gdelay(const int32_t *h, uint32_t a, uint32_t b, double f)
{
    double w = 2 * M_PI * f / FS, ar = 0, ai = 0, br = 0, bi = 0;
    uint32_t i;
    for (i = a; i < b; i++) {
        double c = cos(w * i), s = -sin(w * i), v = h[i];
        ar += v * c;
        ai += v * s;
        br += i * v * c;
        bi += i * v * s;
    }
    return (br * ar + bi * ai) / (ar * ar + ai * ai);
}

static void test_spring(void)
{
    static int32_t imp[1] = {200000}, burst[2u * FS];
    static const int32_t SZ[3] = {0, 64, 127};
    double r[3];
    char what[200];
    uint32_t k, i;
    host_tracks_init();
    for (k = 0; k < 3u; k++) {
        spring_reset(SZ[k], 60);
        spring_run(imp, 1, IRN);
        r[k] = rt60(ir, IRN);
    }
    snprintf(what, sizeof what, "SPRING decay rises with SIZE: RT60 %.2f s (SIZE 0), %.2f (64), %.2f (127)", r[0], r[1], r[2]);
    check(what, r[0] < r[1] && r[1] < r[2] && r[0] >= 0.15 && r[0] <= 1.0 && r[2] >= 2.0 && r[2] <= 6.0);
    {   /* the chirp: the first arrival (main pickup at L / 2, before the second at 3 L / 4), and the next one */
        static const double F[6] = {500, 1000, 2000, 3000, 4000, 5000};
        double g1[6], g2[6];
        uint32_t L = 1323u + ((127u * 1323u) >> 7), a = L / 2u - 64u, b = (L * 3u) / 4u - 16u, rise = 1;
        spring_reset(127, 0);
        spring_run(imp, 1, 3u * L);
        for (i = 0; i < 6u; i++) {
            g1[i] = gdelay(ir, a, b, F[i]) - L / 2.0;
            g2[i] = gdelay(ir, a + L, b + L, F[i]) - L * 1.5;
            if (i >= 2u && (g1[i] <= g1[i - 1] + 0.5 || g2[i] <= g2[i - 1] + 0.5))
                rise = 0;
        }
        snprintf(what, sizeof what, "SPRING chirp: 1st arrival's group delay %.0f %.0f %.0f %.0f %.0f %.0f samples (0.5 1 2 3 4 5 kHz)",
                 g1[0], g1[1], g1[2], g1[3], g1[4], g1[5]);
        check(what, rise && (g1[5] - g1[1]) * 1000 / FS >= 2.0);
        snprintf(what, sizeof what, "  the next pass more spread: 2nd arrival 0.5 -> 5 kHz %.1f ms (1st %.1f)",
                 (g2[5] - g2[0]) * 1000 / FS, (g1[5] - g1[0]) * 1000 / FS);
        check(what, g2[5] - g2[0] > 1.5 * (g1[5] - g1[0]));
    }
    for (k = 0; k < 4u; k++) {   /* the corners: full-scale noise or square waves, then silence */
        int32_t pk = 0, tail = 0;
        for (i = 0; i < 2u * FS; i++)                   /* beyond any send (4 tracks' at most ~2^17 each) */
            burst[i] = k < 2u ? noise(1 << 19) : (i / (k == 2u ? 7u : 53u)) & 1u ? 1 << 19 : -(1 << 19);
        spring_reset(127, k & 1u ? 127 : 0);
        spring_run(burst, 2u * FS, IRN);
        for (i = 0; i < IRN; i++) {
            int32_t v = abs(ir[i]);
            pk = v > pk ? v : pk;
            if (i >= IRN - FS)
                tail = v > tail ? v : tail;
        }
        snprintf(what, sizeof what, "SPRING at SIZE 127 DAMP %d: 2 s of %s: peak %d, the last second %d",
                 k & 1u ? 127 : 0, k < 2u ? "full-scale noise" : k == 2u ? "a full-scale 3.2 kHz square" : "a 416 Hz square",
                 pk, tail);
        check(what, pk <= 6 * 32768 && tail <= 4);
    }
    {   /* the level against ROOM: the same noise burst, defaults */
        static int32_t in[CTL], o[CTL];
        double er = 0, es = 0;
        uint32_t t, m;
        for (m = 0; m < 2u; m++) {
            host_tracks_init();
            rev_clear();
            fx.sp_size = 0;
            xs = 99;
            for (t = 0; t < 2u * FS; t += CTL) {
                for (i = 0; i < CTL; i++) {
                    in[i] = t < FS / 4u ? noise(20000) : 0;
                    o[i] = 0;
                }
                if (m)
                    rev_spring(in, o, CTL);
                else
                    rev_room(in, o, CTL);
                for (i = 0; i < CTL; i++)
                    *(m ? &es : &er) += (double)o[i] * o[i];
            }
        }
        snprintf(what, sizeof what, "level: SPRING's tail %.1f dB from ROOM's (the same burst, SIZE 90 DAMP 60)", 10 * log10(es / er));
        check(what, fabs(10 * log10(es / er)) <= 6.0);
        rev_clear();
    }
}

/* --------------------------------------------------------------- HALL --- */
/* A host-only reference for the comparison, COMB8 (not in the firmware: over twice ROOM's memory): the classic
 * 8 damped combs (1116 .. 1617) and 4 allpasses (556 441 341 225) of which ROOM is the first half, mono, SIZE
 * and DAMP mapped as ROOM's, the input 3 dB lower (twice the combs) */
static const uint16_t C8_LEN[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
static const uint16_t C8_AP[4] = {556, 441, 341, 225};
static int16_t c8_buf[11024 + 1563];
static uint16_t c8_i[12];
static int32_t c8_lp[8];
static void comb8(const int32_t *rev_in, int32_t *out, uint32_t n)
{
    uint32_t i, k;
    int32_t size = 25000 + song.g[G_RSIZE] * 50, damp = 32767 - song.g[G_RDAMP] * 200;
    for (i = 0; i < n; i++) {
        int32_t a = 0, in = mulq15(rev_in[i], 1825);
        int16_t *c = c8_buf;
        for (k = 0; k < 8u; k++) {
            int32_t o = c[c8_i[k]];
            c8_lp[k] = o + mulq15(c8_lp[k] - o, 32767 - damp);
            c[c8_i[k]] = (int16_t)clamp(in + mulq15(c8_lp[k], size), -32768, 32767);
            if (++c8_i[k] >= C8_LEN[k])
                c8_i[k] = 0;
            a += o;
            c += C8_LEN[k];
        }
        for (k = 0; k < 4u; k++) {
            int32_t o = c[c8_i[8 + k]];
            c[c8_i[8 + k]] = (int16_t)clamp(a + (o >> 1), -32768, 32767);
            a = o - a;
            if (++c8_i[8 + k] >= C8_AP[k])
                c8_i[8 + k] = 0;
            c += C8_AP[k];
        }
        out[i] += a;
    }
}

enum { M_ROOM, M_SPRING, M_HALL, M_COMB8 };
static const char *const M_NAME[4] = {"ROOM", "SPRING", "HALL", "COMB8"};
static void model_reset(int m, int32_t size, int32_t damp)
{
    song.g[G_RTYPE] = (int16_t)(m == M_COMB8 ? 0 : m);
    song.g[G_RSIZE] = (int16_t)size;
    song.g[G_RDAMP] = (int16_t)damp;
    rev_clear();
    fx.rtype = (uint8_t)song.g[G_RTYPE];
    fx.sp_w = 0;
    fx.sp_size = 0;
    fx.sp_ph = 0;
    hl.ph = 0;
    hl.sz = 0;
    memset(c8_buf, 0, sizeof c8_buf);
    memset(c8_lp, 0, sizeof c8_lp);
}
/* one block of model m: mid added to out (the wet bus), the stereo difference into side (HALL's; 0 else) */
static void model_block(int m, const int32_t *in, int32_t *out, int32_t *side, uint32_t n)
{
    uint32_t i;
    hl.side = 0;
    if (m == M_HALL)
        rev_hall(in, out, n);
    else if (m == M_SPRING)
        rev_spring(in, out, n);
    else if (m == M_COMB8)
        comb8(in, out, n);
    else
        rev_room(in, out, n);
    for (i = 0; i < n; i++)
        side[i] = hl.side ? rev_side[i] : 0;
}
/* the response of model m to x[] (len samples, then silence): left (mid + side) into ir, side into irs */
static int32_t irs[IRN];
static void model_run(int m, const int32_t *x, uint32_t len, uint32_t n)
{
    static int32_t in[CTL];
    uint32_t t, i;
    for (t = 0; t < n; t += CTL) {
        for (i = 0; i < CTL; i++) {
            in[i] = t + i < len ? x[t + i] : 0;
            ir[t + i] = 0;
        }
        model_block(m, in, ir + t, irs + t, CTL);
        for (i = 0; i < CTL; i++)
            ir[t + i] += irs[t + i];
    }
}

/* the normalised echo density (Abel and Huang): the share of samples beyond one standard deviation in a 20 ms
 * window, over a Gaussian's (0.3173): 1 = as dense as noise. Its mean over [t0, t1) ms, and the first ms at
 * which it reaches 0.9 */
static double ned_mean(const int32_t *h, double t0, double t1, double *t90)
{
    uint32_t w = FS / 50u, c, a = (uint32_t)(t0 * FS / 1000), b = (uint32_t)(t1 * FS / 1000), cnt = 0;
    double s = 0;
    *t90 = -1;
    for (c = w / 2u; c + w / 2u < IRN && c < b; c += FS / 1000u) {
        double e = 0, sd, ned;
        uint32_t i, k = 0;
        for (i = c - w / 2u; i < c + w / 2u; i++)
            e += (double)h[i] * h[i];
        sd = sqrt(e / w);
        for (i = c - w / 2u; i < c + w / 2u; i++)
            k += fabs((double)h[i]) > sd;
        ned = sd > 0 ? k / (double)w / 0.3173 : 0;
        if (*t90 < 0 && ned >= 0.9)
            *t90 = c * 1000.0 / FS;
        if (c >= a) {
            s += ned;
            cnt++;
        }
    }
    return cnt ? s / cnt : 0;
}
/* the tail's ringing: the power spectrum of 8192-sample Hann frames (hop 2048, 5.4 Hz a bin) from 0.15 s to 1.35 s,
 * each frame normalised (the late ones count as much as the early ones), averaged over the frames and over 4
 * responses (excitations of different noise); then each bin in dB against its +-1/6 octave neighbourhood,
 * 200 Hz .. 6 kHz. A diffuse tail averages to a flat spectrum; modes that ring on stand out in the same bins in
 * every frame (the metallic tone). The deviation's RMS (dB), its largest peak. The flutter: the largest
 * normalised autocorrelation of the same stretch at lags of 2 .. 50 ms (echoes that come back at a fixed period) */
#define FFTN 8192u
static void fft(double *re, double *im)
{
    uint32_t i, j = 0, len, k;
    for (i = 1; i < FFTN; i++) {
        uint32_t bit = FFTN >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j |= bit;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (len = 2; len <= FFTN; len <<= 1) {
        double a = -2 * M_PI / len;
        for (i = 0; i < FFTN; i += len)
            for (k = 0; k < len / 2u; k++) {
                double c = cos(a * k), s = sin(a * k);
                double xr = re[i + k + len / 2u] * c - im[i + k + len / 2u] * s;
                double xi = re[i + k + len / 2u] * s + im[i + k + len / 2u] * c;
                re[i + k + len / 2u] = re[i + k] - xr;
                im[i + k + len / 2u] = im[i + k] - xi;
                re[i + k] += xr;
                im[i + k] += xi;
            }
    }
}
typedef struct { double ned, t90, ring, pk, flut, rt; } quality_t;
static quality_t quality(int m, int32_t size, int32_t damp)
{
    static double re[FFTN], im[FFTN], p[FFTN / 2u];
    static int32_t ex[64];
    uint32_t f, i, k, r, a = (uint32_t)(0.15 * FS), b = (uint32_t)(1.35 * FS), lo = 200u * FFTN / FS, hi = 6000u * FFTN / FS, cnt = 0;
    double s2 = 0, t90;
    quality_t q = {0, 0, 0, 0, 0, 0};
    memset(p, 0, sizeof p);
    for (r = 0; r < 4u; r++) {
        xs = 777u + r;
        for (i = 0; i < 64u; i++)
            ex[i] = noise(1 << 17);
        model_reset(m, size, damp);
        hl.ph = r * 0x40000000u;
        model_run(m, ex, 64, IRN);
        q.ned += ned_mean(ir, 50, 300, &t90) / 4;
        q.t90 += (t90 < 0 ? 300 : t90) / 4;
        q.rt += rt60(ir, IRN) / 4;
        for (f = a; f + FFTN <= b; f += FFTN / 4u) {
            double e = 0;
            for (i = 0; i < FFTN; i++) {
                re[i] = ir[f + i] * (0.5 - 0.5 * cos(2 * M_PI * i / FFTN));
                im[i] = 0;
            }
            fft(re, im);
            for (i = 1; i < FFTN / 2u; i++)
                e += re[i] * re[i] + im[i] * im[i];
            for (i = 1; i < FFTN / 2u && e > 0; i++)
                p[i] += (re[i] * re[i] + im[i] * im[i]) / e;
        }
        {   /* the flutter */
            double e0 = 0, mx = 0;
            uint32_t lag;
            for (i = a; i < b; i++)
                e0 += (double)ir[i] * ir[i];
            for (lag = 2u * FS / 1000u; lag <= 50u * FS / 1000u && e0 > 0; lag++) {
                double c = 0;
                for (i = a; i < b; i++)
                    c += (double)ir[i] * ir[i + lag];
                c = fabs(c) / e0;
                mx = c > mx ? c : mx;
            }
            q.flut += mx / 4;
        }
    }
    for (k = lo; k <= hi; k++) {
        uint32_t k0 = (uint32_t)(k / 1.1225), k1 = (uint32_t)(k * 1.1225) + 1u;
        double mm = 0, d;
        for (i = k0; i <= k1; i++)
            mm += p[i];
        mm /= k1 - k0 + 1u;
        if (mm <= 0)
            continue;
        d = 10 * log10(p[k] / mm + 1e-30);
        s2 += d * d;
        cnt++;
        q.pk = d > q.pk ? d : q.pk;
    }
    q.ring = cnt ? sqrt(s2 / cnt) : 99;
    return q;
}

/* RT60 in a band: the energy of 9 bins across f +-1/6 octave (Goertzel, 2048-sample Hann frames every 512)
 * over time; the slope of a line fitted from 5 to 25 dB below its loudest frame */
static double band_rt(const int32_t *h, double f)
{
    static double db[IRN / 512u];
    double mx = -1e9, sx = 0, sy = 0, sxx = 0, sxy = 0;
    uint32_t t, i, j, nf = IRN / 512u - 4u, cnt = 0;
    for (t = 0; t < nf; t++) {
        double e = 0;
        for (j = 0; j < 9u; j++) {
            double w = 2 * M_PI * f * pow(2, (j - 4.0) / 24) / FS, c = 2 * cos(w), s1 = 0, s2 = 0;
            for (i = 0; i < 2048u; i++) {
                double s = h[t * 512u + i] * (0.5 - 0.5 * cos(2 * M_PI * i / 2048)) + c * s1 - s2;
                s2 = s1;
                s1 = s;
            }
            e += s1 * s1 + s2 * s2 - c * s1 * s2;
        }
        db[t] = 10 * log10(e + 1e-9);
        mx = db[t] > mx ? db[t] : mx;
    }
    for (t = 0; t < nf && db[t] < mx - 0.01; t++)
        ;
    for (; t < nf && db[t] > mx - 25; t++)
        if (db[t] <= mx - 5) {
            sx += t, sy += db[t], sxx += (double)t * t, sxy += t * db[t];
            cnt++;
        }
    if (cnt < 3u)
        return 0;
    return -60 / ((cnt * sxy - sx * sy) / (cnt * sxx - sx * sx)) * 512.0 / FS;
}
static void test_hall(void)
{
    static int32_t imp[1] = {200000}, burst[2u * FS];
    static const int32_t SZ[4] = {0, 64, 90, 127};
    double r[4];
    quality_t qa[4];
    char what[240];
    uint32_t k, i, m;
    host_tracks_init();
    for (i = 0; i < 64u; i++)                           /* (a click of noise: the send of a drum hit) */
        burst[i] = noise(1 << 17);
    for (k = 0; k < 4u; k++) {
        model_reset(M_HALL, SZ[k], 60);
        model_run(M_HALL, burst, 64, IRN);
        r[k] = rt60(ir, IRN);
    }
    snprintf(what, sizeof what, "HALL decay rises with SIZE: RT60 %.2f s (SIZE 0), %.2f (64), %.2f (90), %.2f (127)",
             r[0], r[1], r[2], r[3]);
    check(what, r[0] < r[1] && r[1] < r[2] && r[2] < r[3] && r[0] >= 0.25 && r[0] <= 0.9 && r[2] >= 1.8 &&
          r[2] <= 3.5 && r[3] >= 3.5 && r[3] <= 8.0);
    {   /* DAMP: the decay at 500 Hz and at 3 kHz (DAMP 0, 60, 127): the highs die sooner the higher DAMP */
        static const int32_t DP[3] = {0, 60, 127};
        double lo[3], hi[3];
        for (k = 0; k < 3u; k++) {
            model_reset(M_HALL, 90, DP[k]);
            model_run(M_HALL, imp, 1, IRN);
            lo[k] = band_rt(ir, 500);
            hi[k] = band_rt(ir, 3000);
        }
        snprintf(what, sizeof what, "HALL DAMP: RT60 at 500 Hz / 3 kHz: %.2f / %.2f s (DAMP 0), %.2f / %.2f (60), %.2f / %.2f (127)",
                 lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]);
        check(what, hi[0] > hi[1] && hi[1] > hi[2] && hi[0] >= 0.3 * lo[0] && hi[2] <= 0.3 * lo[2]);
    }
    for (k = 0; k < 4u; k++) {   /* the corners: full-scale noise or square waves, then silence */
        int32_t pk = 0, tail = 0, tails = 0;
        for (i = 0; i < 2u * FS; i++)
            burst[i] = k < 2u ? noise(1 << 19) : (i / (k == 2u ? 7u : 53u)) & 1u ? 1 << 19 : -(1 << 19);
        model_reset(M_HALL, 127, k & 1u ? 127 : 0);
        model_run(M_HALL, burst, 2u * FS, IRN);
        for (i = 0; i < IRN; i++) {
            int32_t v = abs(ir[i]);
            pk = v > pk ? v : pk;
            if (i >= IRN - FS) {
                tail = v > tail ? v : tail;
                tails = abs(irs[i]) > tails ? abs(irs[i]) : tails;
            }
        }
        snprintf(what, sizeof what, "HALL at SIZE 127 DAMP %d: 2 s of %s: peak %d, the last second %d (side %d)",
                 k & 1u ? 127 : 0, k < 2u ? "full-scale noise" : k == 2u ? "a full-scale 3.2 kHz square" : "a 416 Hz square",
                 pk, tail, tails);
        check(what, pk <= 12 * 32768 && tail <= 4 && tails <= 4);   /* (the sum of 4 lines x HL_OUT, twice) */
    }
    {   /* no limit cycle anywhere: SIZE 0 / 64 / 127 x DAMP 0 / 60 / 127, 1 s of a chord, quiet, middling or
         * loud (the quiet one keeps the lines near where their rounding changes), then silence: the last
         * second of 12 exactly 0 (left and side) */
        uint32_t s0, d0, lv, worst = 0, held = 0;
        static const int32_t SS[3] = {0, 64, 127}, DD[3] = {0, 60, 127}, LV[3] = {600, 20000, 200000};
        for (s0 = 0; s0 < 3u; s0++)
            for (d0 = 0; d0 < 3u; d0++)
                for (lv = 0; lv < 3u; lv++) {
                    int32_t tail = 0;
                    for (i = 0; i < FS; i++)
                        burst[i] = (int32_t)(LV[lv] * (sin(2 * M_PI * 220 * i / FS) + sin(2 * M_PI * 277 * i / FS) +
                                                       sin(2 * M_PI * 330 * i / FS)) / 3);
                    model_reset(M_HALL, SS[s0], DD[d0]);
                    model_run(M_HALL, burst, FS, IRN);
                    for (i = IRN - FS; i < IRN; i++)
                        tail = abs(ir[i]) + abs(irs[i]) > tail ? abs(ir[i]) + abs(irs[i]) : tail;
                    held += tail != 0;
                    worst = (uint32_t)tail > worst ? (uint32_t)tail : worst;
                }
        snprintf(what, sizeof what, "HALL: silent after its tail at every SIZE x DAMP x level (27 runs): %u not silent, worst %u",
                 held, worst);
        check(what, !held);
    }
    {   /* the defaults after a loud burst: exact silence once the tail is gone, no offset on the way */
        int32_t last = 0, nz = -1;
        double dc = 0, e = 0;
        for (i = 0; i < FS / 4u; i++)
            burst[i] = noise(1 << 18);
        model_reset(M_HALL, 90, 60);
        model_run(M_HALL, burst, FS / 4u, IRN);
        for (i = 0; i < IRN; i++) {
            if (ir[i] || irs[i])
                nz = (int32_t)i;
            if (i >= 2u * FS && i < 4u * FS) {
                dc += ir[i];
                e += (double)ir[i] * ir[i];
            }
        }
        last = nz;
        dc /= 2.0 * FS;
        snprintf(what, sizeof what, "HALL (SIZE 90) after a loud burst: last sample not 0 at %.2f s; mean over 2 .. 4 s %.2f (RMS %.0f)",
                 last / (double)FS, dc, sqrt(e / (2.0 * FS)));
        check(what, last >= 0 && last < (int32_t)(IRN - FS) && fabs(dc) < 0.05 * sqrt(e / (2.0 * FS)) + 0.5);
    }
    {   /* level against ROOM (the left channel; ROOM's is its mono bus); stereo: left and right in the tail */
        static int32_t in[CTL], o[CTL], sd[CTL];
        double er = 0, eh = 0, lr = 0, ll = 0, rr = 0;
        uint32_t t;
        for (m = 0; m < 2u; m++) {
            model_reset(m ? M_HALL : M_ROOM, 90, 60);
            xs = 99;
            for (t = 0; t < 2u * FS; t += CTL) {
                for (i = 0; i < CTL; i++) {
                    in[i] = t < FS / 4u ? noise(20000) : 0;
                    o[i] = 0;
                }
                model_block(m ? M_HALL : M_ROOM, in, o, sd, CTL);
                for (i = 0; i < CTL; i++) {
                    double L = o[i] + sd[i], R = o[i] - sd[i];
                    *(m ? &eh : &er) += L * L;
                    if (m && t >= FS / 2u) {
                        lr += L * R;
                        ll += L * L;
                        rr += R * R;
                    }
                }
            }
        }
        snprintf(what, sizeof what, "level: HALL's tail %.1f dB from ROOM's (the same burst, SIZE 90 DAMP 60)", 10 * log10(eh / er));
        check(what, fabs(10 * log10(eh / er)) <= 3.0);
        snprintf(what, sizeof what, "  stereo: left / right correlation of the tail %.2f", lr / sqrt(ll * rr));
        check(what, fabs(lr / sqrt(ll * rr)) < 0.5);
    }
    /* the comparison: echo density, the tail's ringing and flutter of ROOM, SPRING, HALL and COMB8 (SIZE 90, DAMP 60) */
    for (m = 0; m < 4u; m++) {
        qa[m] = quality((int)m, 90, 60);
        snprintf(what, sizeof what, "  %-6s: echo density %.2f (50 .. 300 ms; 0.9 at %3.0f ms), RT60 %.2f s, ringing %.2f dB RMS (peak %4.1f), flutter %.2f",
                 M_NAME[m], qa[m].ned, qa[m].t90, qa[m].rt, qa[m].ring, qa[m].pk, qa[m].flut);
        check(what, 1);
    }
    check("HALL: as dense as ROOM, less ringing than ROOM and COMB8, less flutter than ROOM",
          qa[M_HALL].ned >= qa[M_ROOM].ned - 0.02 && qa[M_HALL].ring < 0.5 * qa[M_ROOM].ring &&
          qa[M_HALL].ring < qa[M_COMB8].ring && qa[M_HALL].flut < qa[M_ROOM].flut);
    model_reset(M_ROOM, 90, 60);
    host_tracks_init();
}

/* --------------------------------------------------------- model change --- */
/* rev_clear: every sample of both buffers silent (rev_u's int16 view has an odd count, 556 + 441: clearing it
 * through its int32 view left the last sample, which ROOM then played back after a model change) */
static void test_clear(void)
{
    uint32_t i, left = 0;
    for (i = 0; i < sizeof rev_u.ap / 2u; i++)
        rev_u.ap[i] = 1000;
    for (i = 0; i < sizeof rev_comb / 2u; i++)
        rev_comb[i] = 1000;
    rev_clear();
    for (i = 0; i < sizeof rev_u.ap / 2u; i++)
        left += rev_u.ap[i] != 0;
    for (i = 0; i < sizeof rev_comb / 2u; i++)
        left += rev_comb[i] != 0;
    check("rev_clear: every sample of the combs and the allpasses silent", !left);
}

/* model changes while the bus rings, through fx_buses and the side as mix_block adds it: the old model's tail
 * fades out within a block (no step larger than the tail's own), the new model starts silent */
static void test_switch(void)
{
    static const int8_t SEQ[5] = {0, 1, 2, 1, 0}, SEQ2[5] = {0, 2, 1, 2, 0};
    static int32_t c[CTL], d[CTL], r[CTL], w[CTL];
    uint32_t b, i, k, q;
    char what[200];
    for (q = 0; q < 2u; q++)
        for (k = 0; k + 1u < 5u; k++) {
            int from = (q ? SEQ2 : SEQ)[k], to = (q ? SEQ2 : SEQ)[k + 1u];
            int32_t prev = 0, step = 0, own = 0, after = 0;
            if (q && (from != 2 && to != 2))
                continue;
            host_tracks_init();
            rev_clear();
            chorus_prepare(); memset(cho_buf, 0, CHO_LEN * sizeof(int16_t));   /* (the chorus quiet: only the reverb) */
            song.g[G_RTYPE] = (int16_t)from;
            fx.rtype = (uint8_t)from;
            for (b = 0; b < 4u * FS / CTL; b++) {
                if (b == 2u * FS / CTL)
                    song.g[G_RTYPE] = (int16_t)to;
                for (i = 0; i < CTL; i++) {
                    c[i] = d[i] = 0;
                    r[i] = b < FS / CTL ? (int32_t)(30000 * sin(2 * M_PI * 220 * (b * CTL + i) / FS)) : 0;
                }
                fx_buses(c, r, w, CTL);
                for (i = 0; i < CTL; i++) {
                    int32_t L = w[i] + (hl.side ? rev_side[i] : 0), s = abs(L - prev);   /* the left channel */
                    if (b >= 2u * FS / CTL - 4u && b <= 2u * FS / CTL + 4u)
                        step = s > step ? s : step;
                    else if (b > FS / CTL + 100u && b < 2u * FS / CTL - 4u)
                        own = s > own ? s : own;
                    if (b > 2u * FS / CTL)
                        after = abs(w[i]) + (hl.side ? abs(rev_side[i]) : 0) > after ? abs(w[i]) + (hl.side ? abs(rev_side[i]) : 0) : after;
                    prev = L;
                }
            }
            snprintf(what, sizeof what, "%s -> %s while the tail rings: largest step %d (the tail's own %d), silent after: %d",
                     M_NAME[from], M_NAME[to], step, own, after);
            check(what, step <= 2 * own + 64 && after == 0 && fx.rtype == to);
        }
    song.g[G_RTYPE] = 0;
    fx_buses(c, r, w, CTL);
    check("  and back to ROOM", fx.rtype == 0 && !hl.side);
}

/* ---------------------------------------------------------------- cost --- */
static double cost_of(int m, int bus)        /* model m alone (rev_room, rev_spring, rev_hall), or the bus stage */
{
    static int32_t c[CTL], d[CTL], r[CTL], w[CTL];
    uint32_t b, i, nb = 4u * FS / CTL;
    uint64_t i0;
    host_tracks_init();
    rev_clear();
    song.g[G_RTYPE] = (int16_t)m;
    fx.rtype = (uint8_t)m;
    for (i = 0; i < CTL; i++) {
        c[i] = noise(30000);
        d[i] = noise(30000);
        r[i] = noise(30000);
    }
    i0 = instr_now();
    for (b = 0; b < nb; b++) {
        if (bus) {
            fx_buses(c, r, w, CTL);
            if (hl.side)
                rev_side_mix(c, d, CTL);                /* (as mix_block: HALL's stereo) */
        } else if (m == M_HALL)
            rev_hall(r, w, CTL);
        else if (m == M_SPRING)
            rev_spring(r, w, CTL);
        else
            rev_room(r, w, CTL);
    }
    return i0 ? (double)(instr_now() - i0) / (nb * CTL) : 0;
}
static void test_cost(void)
{
    double room = cost_of(M_ROOM, 0), spr = cost_of(M_SPRING, 0), hall = cost_of(M_HALL, 0);
    double broom = cost_of(M_ROOM, 1), bspr = cost_of(M_SPRING, 1), bhall = cost_of(M_HALL, 1);
    char what[200];
    if (!room) {
        printf("reverb: cost: no instruction counter on this host\n");
        return;
    }
    snprintf(what, sizeof what, "cost: the reverb alone ROOM %.0f, SPRING %.0f instructions / sample (+%.0f %%, limit +30 %%)",
             room, spr, (spr / room - 1) * 100);
    check(what, spr <= room * 1.30);
    snprintf(what, sizeof what, "  HALL %.0f (+%.0f %%, limit +30 %%)", hall, (hall / room - 1) * 100);
    check(what, hall <= room * 1.30);
    snprintf(what, sizeof what, "  the bus stage (chorus, reverb) ROOM %.0f (~%.1f %%), SPRING %.0f (~%.1f %%): +%.0f (~%.2f %% CPU)",
             broom, broom * 0.017, bspr, bspr * 0.017, bspr - broom, (bspr - broom) * 0.017);
    check(what, (bspr - broom) * 0.017 <= 2.0);
    snprintf(what, sizeof what, "  .. HALL %.0f (~%.1f %%, with its stereo): +%.0f (~%.2f %% CPU)",
             bhall, bhall * 0.017, bhall - broom, (bhall - broom) * 0.017);
    check(what, (bhall - broom) * 0.017 <= 2.0);
}

/* ------------------------------------------------------------- demos --- */
static void demo(const char *dir, const char *name, int rtype, int pluck)
{
    static const uint8_t PL[16] = {64, 0, 67, 0, 71, 0, 0, 74, 72, 0, 67, 0, 64, 0, 62, 0};
    char path[512];
    FILE *f;
    uint32_t t, i, frames = 8u * FS;
    int32_t o[2 * CTL];
    track_t *tr = &trk[0];
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    rev_clear();
    fx.rtype = (uint8_t)rtype;
    song.g[G_RTYPE] = (int16_t)rtype;
    song.g[G_BPM] = 100;
    if (pluck) {
        host_preset(tr, 0, 8);                                     /* ANALOG PLUCK */
        for (i = 0; i < 16u; i++)
            put_step(tr, i, PL[i] ? 1u : 0u, &PL[i], PL[i] ? ST_NOTE : ST_REST, 0);
    } else {
        host_808_kit(tr);                               /* the GM kit */
        for (i = 0; i < 16u; i++) {
            uint8_t dn[3], k = 0;
            if (i % 4u == 0u || i == 10u) dn[k++] = 36;
            if (i == 4u || i == 12u) dn[k++] = 38;
            if (i % 2u == 0u) dn[k++] = 42;
            put_step(tr, i, k, dn, k ? ST_NOTE : ST_REST, 0);
        }
    }
    tr->p[P_REV] = 90;
    tr->p[P_DLY] = tr->p[P_CHOR] = 0;
    snprintf(path, sizeof path, "%s/%s.wav", dir, name);
    if (!(f = fopen(path, "wb")))
        return;
    wav_hdr(f, frames);
    transport_req = 1;
    for (t = 0; t < frames; t += CTL) {
        if (t == 6u * FS)
            transport_req = 2;                                     /* the last 2 s: the tail alone */
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++)
            wav_put(f, o[2 * i], o[2 * i + 1]);
    }
    fclose(f);
    printf("reverb: demo %s\n", path);
}

int main(int argc, char **argv)
{
    test_room_identical();
    test_spring();
    test_hall();
    test_clear();
    test_switch();
    test_cost();
    if (argc > 1) {
        demo(argv[1], "spring_drums", 1, 0);
        demo(argv[1], "spring_pluck", 1, 1);
        demo(argv[1], "room_drums", 0, 0);
    }
    printf("%s\n", bad ? "REVERB TEST FAILED" : "reverb test passed");
    return bad != 0;
}
