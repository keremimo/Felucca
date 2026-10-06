/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * The synthesis restates OB-Xf (https://github.com/surge-synthesizer/OB-Xf, GPL-3.0-or-later): Copyright 2013-2025
 * by the authors of OB-Xd (Vadim Filatov, discoDSP) and of OB-Xf (the Surge Synth Team). */
/* OBXF core: OB-Xf's voice (two BLEP oscillators with sync, cross mod and ring mod, noise, the 2-pole and 4-pole
 * filters, two envelopes, LFO 2, portamento and the voice "slop") and its part-wide LFO 1, vibrato LFO and
 * smoothers, as OB-Xf computes them, in single-precision float on the FPU (the build targets pi32v2 r3: float
 * lives in the general registers, so no state is saved for it across interrupts).
 *
 * Kept as OB-Xf has it: the order of the operations, the per-sample noise in the oscillator pitch and the filter
 * cutoff, the 15- and 31-sample delays that line the envelopes and LFOs up with the oscillators, the per-voice slop.
 * Not here: OB-Xf's modulation matrix and MPE (Melodee has its own MOD matrix and its own controllers), microtuning,
 * the per-voice pans (a Melodee part is mono, panned as a whole), the 2x HQ mode. The BLEP tables are OB-Xf's
 * (obxf_blep.h, half of each: they are symmetric); the oscillator keeps one residual buffer for its saw and pulse
 * (the sum is the same; OB-Xf keeps one per waveform). The math library calls are replaced by the approximations
 * below (within a few float ulps; tests/obxf_parity.sh compares the renders with OB-Xf's own code). */
#include "obxf_blep.h"           /* OXF_BLEP[1025], OXF_BLAMP[1025]: the first half of OB-Xf's blep / blamp */

#define OXF_SR 44100.f
#define OXF_SRINV (1.f / 44100.f)
#define OXF_PI 3.14159265f
#define OXF_DC 1e-18f
#define OXF_BS 16                /* B_SAMPLES: the BLEP's half length */
#define OXF_NV NPOLY             /* voices of a part */
#define OXF_RC ((970.f / 44000.f) * 0.998865567f)   /* the 4-pole's damping: (970 / 44000) sqrt(44000 / rate) */
#define OXF_BLK CTL              /* the samples of a block (voice.c renders CTL at a time) */

/* --------------------------------------------------------------- math --- */
typedef union { float f; uint32_t u; } oxf_fu;

/* 2^x: x rounded to an integer n, 2^(x - n) by its series to the 7th power (|x - n| <= 1/2: < 1e-8) */
static inline float oxf_exp2(float x)
{
    oxf_fu r;
    int32_t n;
    float f;
    if (x < -125.f)
        x = -125.f;
    if (x > 125.f)
        x = 125.f;
    n = (int32_t)(x + (x >= 0.f ? 0.5f : -0.5f));
    f = (x - (float)n) * 0.693147181f;
    r.f = 1.f + f * (1.f + f * (0.5f + f * (1.f / 6.f + f * (1.f / 24.f + f * (1.f / 120.f + f * (1.f / 720.f +
          f * (1.f / 5040.f)))))));
    r.u += (uint32_t)n << 23;
    return r.f;
}
static inline float oxf_exp(float x) { return oxf_exp2(x * 1.44269504f); }

/* natural log (x > 0): the exponent, and the mantissa in [1/sqrt2, sqrt2) by atanh's series */
static float oxf_log(float x)
{
    oxf_fu m;
    int32_t e;
    float t, t2;
    if (x <= 0.f)
        return -87.f;
    m.f = x;
    e = (int32_t)((m.u >> 23) & 255u) - 127;
    m.u = (m.u & 0x7FFFFFu) | 0x3F800000u;           /* [1, 2) */
    if (m.f > 1.41421356f) {
        m.f *= 0.5f;
        e++;
    }
    t = (m.f - 1.f) / (m.f + 1.f);
    t2 = t * t;
    return (float)e * 0.693147181f + 2.f * t * (1.f + t2 * (1.f / 3.f + t2 * (1.f / 5.f + t2 * (1.f / 7.f +
           t2 * (1.f / 9.f)))));
}

/* tan(x), 0 <= x < pi/2: sin / cos of x or of pi/2 - x (whichever is below pi/4) by their series */
static inline float oxf_tan(float x)
{
    int inv = x > 0.785398163f;
    float y = inv ? (1.57079637f - x) - 4.37113883e-8f : x, y2 = y * y, s, c;
    s = y * (1.f + y2 * (-1.f / 6.f + y2 * (1.f / 120.f + y2 * (-1.f / 5040.f + y2 * (1.f / 362880.f)))));
    c = 1.f + y2 * (-0.5f + y2 * (1.f / 24.f + y2 * (-1.f / 720.f + y2 * (1.f / 40320.f + y2 * (-1.f / 3628800.f)))));
    return inv ? c / s : s / c;
}

/* atan(x): |x| folded below 1, then below tan(pi/12) (atan a = pi/6 + atan((a sqrt3 - 1) / (a + sqrt3))) */
static inline float oxf_atan(float x)
{
    float a = x < 0.f ? -x : x, r, t2, off = 0.f;
    int inv = a > 1.f;
    if (inv)
        a = 1.f / a;
    if (a > 0.267949192f) {
        a = (a * 1.73205081f - 1.f) / (a + 1.73205081f);
        off = 0.523598776f;
    }
    t2 = a * a;
    r = off + a * (1.f + t2 * (-1.f / 3.f + t2 * (1.f / 5.f + t2 * (-1.f / 7.f + t2 * (1.f / 9.f + t2 * (-1.f / 11.f))))));
    if (inv)
        r = 1.57079633f - r;
    return x < 0.f ? -r : r;
}

/* JUCE's FastMathApproximations::sin (OB-Xf's LFO), -pi..pi */
static inline float oxf_sin(float x)
{
    float x2 = x * x;
    float num = -x * (-11511339840.f + x2 * (1640635920.f + x2 * (-52785432.f + x2 * 479249.f)));
    float den = 11511339840.f + x2 * (277920720.f + x2 * (3177720.f + x2 * 18361.f));
    return num / den;
}

static inline float oxf_min(float a, float b) { return a < b ? a : b; }
static inline float oxf_max(float a, float b) { return a > b ? a : b; }
static inline float oxf_clamp(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float oxf_pitch(float semi) { return 440.f * oxf_exp2(semi * (1.f / 12.f)); }   /* getPitch */
/* OB-Xf's parameter curves */
static float oxf_linsc(float v, float lo, float hi) { return v * (hi - lo) + lo; }
static float oxf_logsc(float v, float lo, float hi, float roll)
{
    return ((oxf_exp(v * oxf_log(roll + 1.f)) - 1.f) / roll) * (hi - lo) + lo;
}

/* the one-pole TPT low-pass (tpt_process): k = cutoff / (1 + cutoff), prewarped or not by the caller */
static inline float oxf_tpt(float *st, float in, float k)
{
    float v = (in - *st) * k, res = v + *st;
    *st = res + v;
    return res;
}
static inline float oxf_tpt_k(float hz) { float c = hz * OXF_SRINV * OXF_PI; return c / (1.f + c); }   /* unwarped */

/* -------------------------------------------------------------- noise --- */
typedef struct {
    int32_t white;               /* the LCG */
    int32_t rows[10], sum, idx;  /* pink: Phil Burk's, 10 rows */
    float red;
} oxf_noise_t;
#define OXF_WHITE_K (4.6567e-10f * 0.5f)
static inline int32_t oxf_rand(int32_t *s) { return *s = (int32_t)((uint32_t)*s * 1103515245u + 12345u); }
static inline float oxf_white(int32_t *s) { return (float)oxf_rand(s) * OXF_WHITE_K; }
static float oxf_pink(oxf_noise_t *n)
{
    n->idx = (n->idx + 1) & 1023;
    if (n->idx) {
        int32_t row = 0, r = oxf_rand(&n->white) >> 8;
        while (!((n->idx >> row) & 1))           /* the trailing zeros */
            row++;
        n->sum += r - n->rows[row];
        n->rows[row] = r;
    }
    return (1.f / (11.f * 8388608.f)) * (float)(n->sum + (oxf_rand(&n->white) >> 8));
}
static float oxf_red(oxf_noise_t *n)
{
    n->red += oxf_white(&n->white) * 0.05f;
    if (n->red > 1.f)
        n->red = 2.f - n->red;
    else if (n->red < -1.f)
        n->red = -2.f - n->red;
    return n->red;
}

/* ------------------------------------------------------------ the patch --- */
/* every OB-Xf parameter, as OB-Xf's engine scales it (obxf_par_make: from the patch and the EDIT macros) */
typedef struct {
    float pitch1, pitch2, detune, pw, pw2ofs, xmod, env_pitch, env_pw, bright_k;
    float mix1, mix2, ring, noise;
    float cut, res, mode;                        /* the smoothers' targets (native: 0..120, 0..1, 0..1) */
    float env_amt, keytrack, inv_fenv;
    float fa, fd, fs, fr, fcurve, vel_flt;       /* ms, level */
    float aa, ad, as, ar, acurve, vel_amp;
    struct { float hz, raw, w1, w2, w3, pw, amt1, amt2, p1, p2, cut, pw1, pw2, vol, avol; uint8_t sync; } lfo[2];
    float volume, tune, porta_hz, uni_det, pb_up, pb_dn, vib_hz;
    float slop_porta, slop_cut, slop_env, slop_lvl;
    int8_t transpose;
    uint8_t saw1, pul1, saw2, pul2, sync, key2, ptch_both, ptch_inv, pw_both, pw_inv, ncolor;
    uint8_t bp_blend, push, four, xpander, xp_mode, legato, pb_osc2, vib_sq;
} oxf_par_t;

/* -------------------------------------------------------------- the LFO --- */
typedef struct {
    float ph, inc, smooth, target;
    float sine, square, saw, tri, sh, sg, hist;
    int32_t pos;
    uint32_t rng;
} oxf_lfo_t;
#define OXF_LFO_BLK 8

static float oxf_frand(uint32_t *r)                 /* juce::Random::nextFloat: 0..1 */
{
    *r = *r * 1664525u + 1013904223u;
    return (float)(*r >> 8) * (1.f / 16777216.f);
}
static float oxf_bend(float x, float d)
{
    float a;
    if (d == 0.f)
        return x;
    a = 0.5f * d;
    x = x - a * x * x + a;
    x = x - a * x * x + a;
    return x - a * x * x + a;
}
/* OB-Xf's LFO::update (its waves once every 8 samples); w: wave blends, pw, unipolar pulse */
static void oxf_lfo_update(oxf_lfo_t *l, float w1, float w2, float w3, float pw, int unipolar, int phase_only)
{
    float r;
    if (l->pos < OXF_LFO_BLK - 1) {
        l->pos++;
        return;
    }
    l->pos = 0;
    l->ph += OXF_LFO_BLK * (l->inc * 2.f * OXF_PI * OXF_SRINV);
    while (l->ph > OXF_PI) {
        l->ph -= 2.f * OXF_PI;
        l->hist = l->sh;
        l->sh = oxf_frand(&l->rng) * 2.f - 1.f;
    }
    l->sine = oxf_sin(l->ph);
    {
        float d = l->ph + 0.5f * OXF_PI - (l->ph > 0.5f * OXF_PI ? 2.f * OXF_PI : 0.f);
        l->tri = (2.f / OXF_PI) * (d < 0.f ? -d : d) - 1.f;
    }
    l->square = l->ph > OXF_PI * pw * 0.9f ? -1.f + (float)unipolar : 1.f;
    l->saw = oxf_bend(-l->ph * (1.f / OXF_PI), -pw);
    l->sg = l->hist + (l->sh - l->hist) * (OXF_PI + l->ph) * (1.f / (2.f * OXF_PI));
    r = w1 >= 0.f ? l->tri * w1 : l->sine * -w1;
    r += w2 >= 0.f ? l->saw * w2 : l->square * -w2;
    r += w3 >= 0.f ? l->sg * w3 : l->sh * -w3;
    l->target = r;
    if (phase_only)
        l->smooth = r;
}
#define OXF_K250 (250.f * OXF_SRINV * OXF_PI / (1.f + 250.f * OXF_SRINV * OXF_PI))
static inline float oxf_lfo_val(oxf_lfo_t *l) { return oxf_tpt(&l->smooth, l->target, OXF_K250); }

/* tempo-synced rates (OB-Xf's 21), in quarter notes per beat */
static const float OXF_SYNC_RATE[21] = {1.f / 12.f, 1.f / 8.f, 1.f / 6.f, 3.f / 16.f, 1.f / 4.f, 1.f / 3.f, 3.f / 8.f,
    1.f / 2.f, 2.f / 3.f, 3.f / 4.f, 1.f, 3.f / 2.f, 4.f / 3.f, 2.f, 8.f / 3.f, 3.f, 4.f, 6.f, 8.f, 12.f, 16.f};
static float oxf_lfo_hz(float hz, float raw, int sync, float bpm)
{
    int32_t k;
    if (!sync)
        return hz;
    k = (int32_t)(oxf_clamp(raw, 0.f, 1.f) * 20.f);
    return bpm / 60.f * OXF_SYNC_RATE[k];
}

/* ---------------------------------------------------------- the envelope --- */
enum { OXE_ATK = 1, OXE_DEC, OXE_SUS, OXE_REL, OXE_OFF };
typedef struct {
    float coef, coef_lin, out, out_lin;
    float a, d, s, r;            /* in force: ms with the slop factor (attack / 3), sustain level */
    uint8_t state;
} oxf_env_t;
#define OXE_MS (OXF_SR * 0.001f)

static void oxf_env_atk_coef(oxf_env_t *e)
{
    float exp_rate = oxf_log(0.1f) / oxf_log(0.001f), exp_time = e->a * exp_rate;
    float lin = (1.f - exp_rate * 0.1f) * exp_time * OXE_MS;
    e->coef = (oxf_log(0.001f) - oxf_log(1.3f)) / (OXE_MS * e->a);
    e->coef_lin = 0.9f / lin;
}
static float oxf_env_dec_coef(const oxf_env_t *e)
{
    return oxf_log(oxf_min(e->s + 0.0001f, 0.99f)) / (OXE_MS * e->d);
}
static float oxf_env_rel_coef(const oxf_env_t *e)
{
    return (oxf_log(0.00001f) - oxf_log(e->out + 0.0001f)) / (OXE_MS * e->r);
}
/* new times / sustain (OB-Xf's setters: a stage in progress takes its new coefficient) */
static void oxf_env_set(oxf_env_t *e, float a, float d, float s, float r, float factor)
{
    float na = a * factor * 3.f, nd = d * factor, nr = r * factor;
    int da = na != e->a, dd = nd != e->d || s != e->s, dr = nr != e->r;
    e->a = na;
    e->d = nd;
    e->s = s;
    e->r = nr;
    if (e->state == OXE_ATK && da)
        oxf_env_atk_coef(e);
    else if (e->state == OXE_DEC && dd)
        e->coef = oxf_env_dec_coef(e);
    else if (e->state == OXE_REL && dr)
        e->coef = oxf_env_rel_coef(e);
}
static void oxf_env_attack(oxf_env_t *e, float curve)
{
    e->state = OXE_ATK;
    oxf_env_atk_coef(e);
    if (e->out != 0.f) {                             /* OB-Xf: split the level between the curves */
        float x = e->out / ((1.f - curve) * (1.6f / 2.6f) + curve * (1.f / 2.6f));
        e->out = (1.6f / 2.6f) * x;
        e->out_lin = (1.f / 2.6f) * x;
    } else {
        e->out_lin = 0.f;
    }
}
static void oxf_env_release(oxf_env_t *e, float curve)
{
    if (e->state == OXE_ATK)
        e->out = oxf_min((1.f - curve) * e->out + curve * e->out_lin, 0.99f);
    if (e->state != OXE_REL)
        e->coef = oxf_env_rel_coef(e);
    e->state = OXE_REL;
}
static inline float oxf_env_tick(oxf_env_t *e, float curve)
{
    float res = e->out;
    switch (e->state) {
    case OXE_ATK:
        if (e->out - 1.f > -0.1f) {
            e->out = oxf_min((1.f - curve) * e->out + curve * e->out_lin, 0.99f);
            e->state = OXE_DEC;
            e->coef = oxf_env_dec_coef(e);
            goto dec;
        }
        e->out = e->out - (1.f - e->out) * e->coef;
        e->out_lin += e->coef_lin;
        res = (1.f - curve) * e->out + curve * e->out_lin;
        break;
    case OXE_DEC:
    dec:
        if (e->out - e->s < 10e-6f) {
            e->state = OXE_SUS;
        } else {
            e->out = e->out + e->out * e->coef;
            res = e->out;
        }
        break;
    case OXE_SUS:
        e->out = oxf_min(e->s, 0.9f);
        res = e->out;
        break;
    case OXE_REL:
        if (e->out > 20e-6f) {
            e->out = e->out + e->out * e->coef + OXF_DC;
            res = e->out;
        } else {
            e->state = OXE_OFF;
        }
        break;
    default:
        e->out = 0.f;
        res = 0.f;
        break;
    }
    return res;
}

/* ------------------------------------------------------------ the voice --- */
typedef struct {
    float buf[2 * OXF_BS];       /* the BLEP residual (saw and pulse, or triangle) */
    float dl[OXF_BS];            /* the waveform, delayed as the residual is */
    float ph, pw, slop;          /* phase, the pulse width of the sample before, tuning slop */
    uint8_t pos, hi;             /* residual position; pulse: past its width */
} oxf_osc_t;

typedef struct {
    oxf_osc_t o[2];
    float d_frac[OXF_BS], d_xmod[OXF_BS], d_pitch[OXF_BS];   /* the oscillator block's 15-sample delays */
    uint8_t d_sync[OXF_BS];
    float d_aenv[2 * OXF_BS], d_fenv[2 * OXF_BS], d_lfo1[2 * OXF_BS], d_lfo2[2 * OXF_BS];   /* the voice's 31 */
    oxf_noise_t nz;              /* the oscillators' noise */
    int32_t cut_nz;              /* the filter cutoff's noise */
    oxf_env_t fenv, aenv;
    oxf_lfo_t lfo2;
    float pole[4];
    float dcblk, bright, porta;  /* oscillator DC block, brightness, portamento states */
    float s_aenv, s_fenv, s_cut, s_porta, s_lvl;   /* the voice's slop, -0.5 .. 0.5 */
    float vel, env_fac;
    uint8_t i16, i32;            /* the delays' write positions */
    uint8_t note, gated, sounding, init;
} oxf_voice_t;

/* the part: LFO 1, the vibrato LFO, the smoothers, and per sample of the block what every voice reads */
typedef struct {
    oxf_voice_t v[OXF_NV];
    oxf_par_t par;
    oxf_lfo_t lfo1, vib;
    float sm_cut, sm_res, sm_mode, sm_pb, sm_mw;
    float b_lfo1[OXF_BLK], b_vib[OXF_BLK], b_cut[OXF_BLK], b_res2[OXF_BLK], b_res4[OXF_BLK], b_mode[OXF_BLK],
        b_pb[OXF_BLK];
    uint32_t seed;
    uint8_t any, init;
} oxf_part_t;

/* the BLEP tables: entry k of 2049, the second half mirrored */
static inline float oxf_tab(const float *h, uint32_t k) { return h[k <= 1024u ? k : 2048u - k]; }

/* mixInImpulseCenter (blep) / mixInBlampCenter (blamp, no sign change in the second half) */
static void oxf_mix(oxf_osc_t *o, const float *tab, int blamp, float offset, float scale)
{
    int32_t lp = (int32_t)(64.f * offset), i;
    float frac, f1s, frs;
    if (lp >= 64)
        lp = 63;
    if (lp < 0)
        lp = 0;
    frac = offset * 64.f - (float)lp;
    f1s = (1.f - frac) * scale;
    frs = frac * scale;
    for (i = 0; i < 2 * OXF_BS; i++) {
        uint32_t k = (uint32_t)(i * 64 + lp);
        float m = oxf_tab(tab, k) * f1s + oxf_tab(tab, k + 1u) * frs;
        uint32_t j = (o->pos + (uint32_t)i) & (2u * OXF_BS - 1u);
        if (i < OXF_BS || blamp)
            o->buf[j] += m;
        else
            o->buf[j] -= m;
    }
}
#define oxf_blep(o, off, sc) oxf_mix(o, OXF_BLEP, 0, off, sc)
#define oxf_blamp(o, off, sc) oxf_mix(o, OXF_BLAMP, 1, off, sc)

static inline float oxf_next(oxf_osc_t *o)          /* -aliasReduction */
{
    o->buf[o->pos] = 0.f;
    o->pos = (uint8_t)((o->pos + 1u) & (2u * OXF_BS - 1u));
    return o->buf[o->pos];
}

/* the transitions of one sample (x: the phase before its wrap); sync: the leader wrapped at sf (follower only) */
static void oxf_pulse_step(oxf_osc_t *o, float x, float d, float pw, int hs, float sf)
{
    float sum = d - (pw - o->pw);
    if (o->hi && x >= 1.f) {
        x -= 1.f;
        if (!hs || x / d > sf) {
            oxf_blep(o, x / d, 1.f);
            o->hi = 0;
        } else {
            x += 1.f;
        }
    }
    if (!o->hi && x >= pw && x - sum <= pw) {
        float frac = (x - pw) / sum;
        o->hi = 1;
        if (!hs || frac > sf)
            oxf_blep(o, frac, -1.f);
        else
            o->hi = 0;
    }
    if (o->hi && x >= 1.f) {
        x -= 1.f;
        if (!hs || x / d > sf) {
            oxf_blep(o, x / d, 1.f);
            o->hi = 0;
        } else {
            x += 1.f;
        }
    }
    if (hs) {
        oxf_blep(o, sf, o->hi ? 1.f : 0.f);
        o->hi = 0;
    }
}
static void oxf_saw_step(oxf_osc_t *o, float x, float d, int hs, float sf)
{
    if (x >= 1.f) {
        x -= 1.f;
        if (!hs || x / d > sf)
            oxf_blep(o, x / d, 1.f);
        else
            x += 1.f;
    }
    if (hs)
        oxf_blep(o, sf, x - d * sf);
}
static void oxf_tri_step(oxf_osc_t *o, float x, float d, int hs, float sf)
{
    int pass = 1;
    float k = 4.f * OXF_BS * d;
    if (x >= 1.f) {
        x -= 1.f;
        if (!hs || x / d > sf) {
            oxf_blamp(o, x / d, -k);
        } else {
            x += 1.f;
            pass = 0;
        }
    }
    if (x >= 0.5f && x - d < 0.5f && pass) {
        float frac = (x - 0.5f) / d;
        if (!hs || frac > sf)
            oxf_blamp(o, frac, k);
    }
    if (x >= 1.f && pass) {
        x -= 1.f;
        if (!hs || x / d > sf)
            oxf_blamp(o, x / d, -k);
        else
            x += 1.f;
    }
    if (hs) {
        float tr = x - d * sf, a = tr - 0.5f;
        if (tr > 0.5f)
            oxf_blamp(o, sf, -k);
        oxf_blep(o, sf, 0.5f - 2.f * (a < 0.f ? -a : a) + 0.5f);
    }
}
/* one sample of an oscillator: its transitions, the wrap, then the delayed waveform plus the residual */
static inline float oxf_osc_wave(float x, float pw, int saw, int pul)
{
    float w = 0.f, a;
    if (pul)
        w = pw - 1.f + (float)(x >= pw);
    if (saw)
        w += x - 0.5f;
    else if (!pul) {
        a = x - 0.5f;
        w = 0.5f - 2.f * (a < 0.f ? -a : a);
    }
    return w;
}

/* the voice's random constants (OB-Xf draws them once per voice) and its noise seeds */
static void oxf_voice_init(oxf_voice_t *v, uint32_t *seed)
{
    uint32_t r = *seed;
    v->s_aenv = oxf_frand(&r) - 0.5f;
    v->s_fenv = oxf_frand(&r) - 0.5f;
    v->s_cut = oxf_frand(&r) - 0.5f;
    v->s_porta = oxf_frand(&r) - 0.5f;
    v->s_lvl = oxf_frand(&r) - 0.5f;
    v->nz.white = (int32_t)(r * 2654435761u);
    v->o[0].slop = oxf_white(&v->nz.white);
    v->o[1].slop = oxf_white(&v->nz.white);
    v->o[0].ph = oxf_white(&v->nz.white);
    v->o[1].ph = oxf_white(&v->nz.white);
    v->cut_nz = (int32_t)(r * 40503u + 1u);
    v->lfo2.rng = r ^ 0x5bd1e995u;
    v->lfo2.sh = v->lfo2.hist = oxf_frand(&v->lfo2.rng) * 2.f - 1.f;
    v->lfo2.pos = OXF_LFO_BLK - 1;
    v->aenv.state = v->fenv.state = OXE_OFF;
    v->aenv.a = v->aenv.d = v->aenv.r = v->fenv.a = v->fenv.d = v->fenv.r = 0.0001f;
    v->aenv.s = v->fenv.s = 1.f;
    v->note = 60;
    v->init = 1;
    *seed = r;
}

/* the patch's times on the voice's envelopes (with its slop) */
static void oxf_voice_env(oxf_voice_t *v, const oxf_par_t *P)
{
    oxf_env_set(&v->aenv, P->aa, P->ad, P->as, P->ar, 1.f + v->s_aenv * P->slop_env);
    oxf_env_set(&v->fenv, P->fa, P->fd, P->fs, P->fr, 1.f + v->s_fenv * P->slop_env);
}

/* Voice::NoteOn. held: the voice still had its key down (a steal, a legato move): the envelopes go on unless
 * the legato mode retriggers them, the velocity stays */
static void oxf_note_on(oxf_part_t *p, oxf_voice_t *v, uint32_t note, float vel, int held)
{
    const oxf_par_t *P = &p->par;
    if (!v->init)
        oxf_voice_init(v, &p->seed);
    if (!v->sounding) {
        memset(v->d_aenv, 0, sizeof v->d_aenv);
        memset(v->d_fenv, 0, sizeof v->d_fenv);
        v->aenv.out = v->aenv.out_lin = v->fenv.out = v->fenv.out_lin = 0.f;
        v->aenv.state = v->fenv.state = OXE_OFF;
    }
    v->sounding = 1;
    if (!held)
        v->vel = vel;
    v->note = (uint8_t)note;
    oxf_voice_env(v, P);
    if (!held || (P->legato & 1u))
        oxf_env_attack(&v->aenv, P->acurve);
    if (!held || (P->legato & 2u))
        oxf_env_attack(&v->fenv, P->fcurve);
    v->lfo2.ph = -OXF_PI;                            /* LFO 2 starts at phase 0 */
    v->gated = 1;
    p->any = 1;
}

static void oxf_note_off(oxf_part_t *p, oxf_voice_t *v)
{
    oxf_env_release(&v->aenv, p->par.acurve);
    oxf_env_release(&v->fenv, p->par.fcurve);
    v->gated = 0;
}

#define OXF_SMK (0.003f * (44100.f / 44000.f))   /* OB-Xf's Smoother: PSSC, corrected for the rate */
/* the part's work per sample of a block: the smoothers (cutoff, resonance, filter mode, bend, wheel), LFO 1 and
 * the vibrato LFO. ext_cut: the cutoff knob's offset (native), bend -1..1, wheel 0..1, bpm for the synced rates */
static void oxf_part_block(oxf_part_t *p, uint32_t n, float bend, float wheel, float bpm)
{
    const oxf_par_t *P = &p->par;
    uint32_t i;
    float w1 = P->vib_sq ? 0.f : -1.f, w2 = P->vib_sq ? -1.f : 0.f;
    p->lfo1.inc = oxf_lfo_hz(P->lfo[0].hz, P->lfo[0].raw, P->lfo[0].sync, bpm);
    p->vib.inc = P->vib_hz;
    for (i = 0; i < n; i++) {
        float re;
        p->sm_cut += (P->cut - p->sm_cut) * OXF_SMK + OXF_DC;
        p->sm_res += (P->res - p->sm_res) * OXF_SMK + OXF_DC;
        p->sm_mode += (P->mode - p->sm_mode) * OXF_SMK + OXF_DC;
        p->sm_pb += (bend - p->sm_pb) * OXF_SMK + OXF_DC;
        re = 0.991f - oxf_logsc(1.f - oxf_clamp(p->sm_res, 0.f, 1.f), 0.f, 0.991f, 40.f);
        p->b_cut[i] = p->sm_cut;
        p->b_res2[i] = 1.f - re;
        p->b_res4[i] = 3.5f * re;
        p->b_mode[i] = p->sm_mode;
        p->b_pb[i] = p->sm_pb;
        p->sm_mw += (wheel - p->sm_mw) * OXF_SMK + OXF_DC;
        if (!p->any) {
            oxf_lfo_update(&p->lfo1, P->lfo[0].w1, P->lfo[0].w2, P->lfo[0].w3, P->lfo[0].pw, 0, 1);
            oxf_lfo_update(&p->vib, w1, w2, 0.f, 0.f, 1, 1);
            p->b_lfo1[i] = p->b_vib[i] = 0.f;
            continue;
        }
        oxf_lfo_update(&p->lfo1, P->lfo[0].w1, P->lfo[0].w2, P->lfo[0].w3, P->lfo[0].pw, 0, 0);
        oxf_lfo_update(&p->vib, w1, w2, 0.f, 0.f, 1, 0);
        p->b_lfo1[i] = oxf_lfo_val(&p->lfo1);
        p->b_vib[i] = oxf_lfo_val(&p->vib) * p->sm_mw * p->sm_mw * 4.f;
    }
}

/* n samples of a voice, added into Melodee's mix out (Voice::ProcessSample and OscillatorBlock::ProcessSample).
 * semi: the note's offset from Melodee (glide, LFO / ENV pitch, the matrix, unison spread, TUNE), cut: the
 * cutoff's (native units), pwm: the pulse width's; level: the block's gain ramp (OB-Xf's volume and the scale
 * to Melodee's voice level), lvl1 its end. 1 = the voice still sounds */
static int oxf_voice_render(oxf_part_t *p, oxf_voice_t *v, int32_t *out, uint32_t n, float semi, float cut, float pwm,
                            float lvl0, float lvl1, float bpm)
{
    const oxf_par_t *P = &p->par;
    oxf_osc_t *o1 = &v->o[0], *o2 = &v->o[1];
    float porta_k = oxf_tpt_k(P->porta_hz * (1.f + v->s_porta * P->slop_porta));
    float dc_k = oxf_tpt_k(12.f), lvl = 1.f - P->slop_lvl * v->s_lvl;
    float l2hz = oxf_lfo_hz(oxf_max(0.01f, P->lfo[1].hz), P->lfo[1].raw, P->lfo[1].sync, bpm);
    float dl = (lvl1 - lvl0) / (float)n;
    uint32_t i;
    v->lfo2.inc = l2hz;
    for (i = 0; i < n; i++) {
        float lfo1 = p->b_lfo1[i], lfo2, f_lfo1, f_lfo2, menv, cutoff, pb, note, pwenv, ptenv;
        float m1pw, m2pw, m1p, m2p, x, fs, pwc, o1out, o2out, s, aval;
        int sreset = 0;
        float sfrac = 0.f;
        uint32_t k16 = v->i16, k32 = v->i32;
        oxf_lfo_update(&v->lfo2, P->lfo[1].w1, P->lfo[1].w2, P->lfo[1].w3, P->lfo[1].pw, 0, 0);
        lfo2 = oxf_lfo_val(&v->lfo2);
        note = oxf_tpt(&v->porta, (float)v->note - 93.f, porta_k) + semi;
        pb = p->b_pb[i] < 0.f ? p->b_pb[i] * P->pb_dn : p->b_pb[i] * P->pb_up;
        /* the 31-sample delays: write here, read the oldest (DelayLine::feedReturn) */
        v->d_lfo1[k32] = lfo1;
        v->d_lfo2[k32] = lfo2;
        k32 = (k32 - 1u) & (2u * OXF_BS - 1u);
        f_lfo1 = v->d_lfo1[k32];
        f_lfo2 = v->d_lfo2[k32];
        menv = P->inv_fenv * oxf_env_tick(&v->fenv, P->fcurve) * (1.f - (1.f - v->vel) * P->vel_flt);
        v->d_fenv[v->i32] = menv;
        cutoff = oxf_pitch(P->lfo[0].cut * f_lfo1 * P->lfo[0].amt1 + P->lfo[1].cut * f_lfo2 * P->lfo[1].amt1 +
                           oxf_clamp(p->b_cut[i] + cut, 0.f, 120.f) + v->s_cut * P->slop_cut +
                           P->env_amt * v->d_fenv[k32] - 45.f + P->keytrack * (pb + note + 40.f));
        cutoff = oxf_min(cutoff + oxf_white(&v->cut_nz) * 3.365f, OXF_SR * 0.5f - 120.f);
        if (P->push)
            cutoff = oxf_min(cutoff, 19000.f);
        pwenv = P->pw_inv ? -menv : menv;
        m1pw = P->lfo[0].pw1 * lfo1 * P->lfo[0].amt2 + P->lfo[1].pw1 * lfo2 * P->lfo[1].amt2 +
               (P->pw_both ? P->env_pw * pwenv : 0.f);
        m2pw = P->lfo[0].pw2 * lfo1 * P->lfo[0].amt2 + P->lfo[1].pw2 * lfo2 * P->lfo[1].amt2 + P->env_pw * pwenv +
               P->pw2ofs;
        ptenv = P->ptch_inv ? -menv : menv;
        m1p = (!P->pb_osc2 ? pb : 0.f) + P->lfo[0].p1 * lfo1 * P->lfo[0].amt1 + P->lfo[1].p1 * lfo2 * P->lfo[1].amt1 +
              (P->ptch_both ? P->env_pitch * ptenv : 0.f) + p->b_vib[i];
        m2p = pb + P->lfo[0].p2 * lfo1 * P->lfo[0].amt1 + P->lfo[1].p2 * lfo2 * P->lfo[1].amt1 +
              P->env_pitch * ptenv + p->b_vib[i];

        /* ---- OscillatorBlock: oscillator 1, the leader */
        fs = oxf_min(oxf_pitch(0.1f * oxf_white(&v->nz.white) + note + P->pitch1 + m1p + P->tune +
                               (float)P->transpose + P->uni_det * o1->slop) * OXF_SRINV, 0.45f);
        x = o1->ph + fs;
        pwc = oxf_clamp((P->pw + pwm + m1pw) * 0.5f + 0.5f, 0.1f, 1.f);
        if (P->pul1)
            oxf_pulse_step(o1, x, fs, pwc, 0, 0.f);
        if (P->saw1)
            oxf_saw_step(o1, x, fs, 0, 0.f);
        else if (!P->pul1)
            oxf_tri_step(o1, x, fs, 0, 0.f);
        if (x >= 1.f) {
            x -= 1.f;
            sfrac = x / fs;
            sreset = 1;
        }
        o1->ph = x;
        o1->pw = pwc;
        sreset &= P->sync;
        v->d_sync[k16] = (uint8_t)sreset;
        v->d_frac[k16] = sfrac;
        o1->dl[k16] = oxf_osc_wave(x, pwc, P->saw1, P->pul1);
        k16 = (k16 - 1u) & (OXF_BS - 1u);
        sreset = v->d_sync[k16];
        sfrac = v->d_frac[k16];
        o1out = o1->dl[k16] + oxf_next(o1);

        /* ---- oscillator 2, the follower: its pitch delayed as oscillator 1 is */
        v->d_pitch[v->i16] = 0.1f * oxf_white(&v->nz.white) + (P->key2 ? note : -33.f) + P->detune + P->pitch2 +
                             m2p + o1out * P->xmod + P->tune + (float)P->transpose + P->uni_det * o2->slop;
        fs = oxf_min(oxf_pitch(v->d_pitch[k16]) * OXF_SRINV, 0.45f);
        pwc = oxf_clamp((P->pw + pwm + m2pw) * 0.5f + 0.5f, 0.1f, 1.f);
        x = o2->ph + fs;
        if (P->pul2)
            oxf_pulse_step(o2, x, fs, pwc, sreset, sfrac);
        if (P->saw2)
            oxf_saw_step(o2, x, fs, sreset, sfrac);
        else if (!P->pul2)
            oxf_tri_step(o2, x, fs, sreset, sfrac);
        if (x >= 1.f)
            x -= 1.f;
        o2->pw = pwc;
        if (sreset)
            x = fs * sfrac;
        o2->ph = x;
        v->d_xmod[v->i16] = o1out;
        o1out = v->d_xmod[k16];
        o2->dl[v->i16] = oxf_osc_wave(x, pwc, P->saw2, P->pul2);
        o2out = o2->dl[k16] + oxf_next(o2);
        v->i16 = (uint8_t)k16;

        {
            float nzv = P->ncolor == 1u ? oxf_pink(&v->nz) : P->ncolor == 2u ? oxf_red(&v->nz) : oxf_white(&v->nz.white);
            s = o1out * P->mix1 + o2out * P->mix2 + nzv * (P->noise + 0.0006f) + o1out * o2out * P->ring;
        }
        s = s * 3.f * lvl;

        /* ---- DC block, brightness, the filter */
        s = s - oxf_tpt(&v->dcblk, s, dc_k);
        s = oxf_tpt(&v->bright, s, P->bright_k);
        {
            float g = oxf_tan(cutoff * OXF_SRINV * OXF_PI), r2 = p->b_res2[i], r4 = p->b_res4[i], mm = p->b_mode[i];
            float *q = v->pole;
            if (P->four) {
                float lpc = g / (1.f + g), ml = 1.f / (1.f + g), y0, y1, y2, y3, y4, vv, res, o;
                float S = (lpc * (lpc * (lpc * q[0] + q[1]) + q[2]) + q[3]) * ml, G = lpc * lpc * lpc * lpc;
                y0 = (s - r4 * S) / (1.f + r4 * G);
                vv = (y0 - q[0]) * lpc;
                res = vv + q[0];
                q[0] = res + vv;
                q[0] = oxf_atan(q[0] * OXF_RC) * (1.f / OXF_RC);
                y1 = res;
                y2 = oxf_tpt(&q[1], y1, lpc);
                y3 = oxf_tpt(&q[2], y2, lpc);
                y4 = oxf_tpt(&q[3], y3, lpc);
                if (P->xpander) {
                    static const int8_t MIX[15][5] = {
                        {0, 0, 0, 0, 1}, {0, 0, 0, 1, 0}, {0, 0, 1, 0, 0}, {0, 1, 0, 0, 0}, {1, -3, 3, -1, 0},
                        {1, -2, 1, 0, 0}, {1, -1, 0, 0, 0}, {0, 0, 2, -4, 2}, {0, -2, 2, 0, 0}, {1, -2, 2, 0, 0},
                        {1, -3, 6, -4, 0}, {0, -1, 2, -1, 0}, {0, -1, 3, -3, 1}, {0, -1, 2, -2, 0}, {0, -1, 3, -6, 4}};
                    const int8_t *mx = MIX[P->xp_mode % 15u];
                    o = y0 * mx[0] + y1 * mx[1] + y2 * mx[2] + y3 * mx[3] + y4 * mx[4];
                } else {
                    int32_t mp = (int32_t)(mm * 3.f);
                    float xf = mm * 3.f - (float)mp;
                    o = mp == 0 ? (1.f - xf) * y4 + xf * y3 : mp == 1 ? (1.f - xf) * y3 + xf * y2
                      : mp == 2 ? (1.f - xf) * y2 + xf * y1 : mp == 3 ? y1 : 0.f;
                }
                s = o * (1.f + r4 * 0.45f);
            } else {
                float push = -1.f - (P->push ? 0.035f : 0.f), xq = q[0] * 0.0876f, t, vv, y1, y2;
                t = ((((0.0103592f * xq + 0.00920833f) * xq + 0.185f) * xq + 0.05f) * xq + 1.f) + push;
                vv = (s - 2.f * (q[0] * (r2 + t)) - g * q[0] - q[1]) / (1.f + g * (2.f * (r2 + t) + g));
                y1 = vv * g + q[0];
                q[0] = vv * g + y1;
                y2 = y1 * g + q[1];
                q[1] = y1 * g + y2;
                if (P->bp_blend)
                    s = 2.f * (mm < 0.5f ? (0.5f - mm) * y2 + mm * y1 : (1.f - mm) * y1 + (mm - 0.5f) * vv);
                else
                    s = (1.f - mm) * y2 + mm * vv;
            }
        }
        s *= 1.f - (P->lfo[0].vol * lfo1 * 0.5f + P->lfo[0].avol * 0.5f) * (P->lfo[0].amt2 * 1.4285714285714286f);
        s *= 1.f - (P->lfo[1].vol * lfo2 * 0.5f + P->lfo[1].avol * 0.5f) * (P->lfo[1].amt2 * 1.4285714285714286f);
        v->d_aenv[v->i32] = oxf_env_tick(&v->aenv, P->acurve) * (1.f - (1.f - v->vel) * P->vel_amp);
        aval = v->d_aenv[k32];
        v->i32 = (uint8_t)k32;
        out[i] += (int32_t)(s * aval * (lvl0 + dl * (float)i));
    }
    if (v->aenv.state == OXE_OFF)
        v->sounding = 0;
    return v->sounding;
}
