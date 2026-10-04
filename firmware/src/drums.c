/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Drum part (track 4): an analog drum kit modelled on the TR-808's voice circuits, played by
 * its step pattern, the keys when track 4 is selected, and its own MIDI channel (GLO -> DRUMS,
 * default 10), on the General MIDI percussion keys (DR_GM). Like the machine, each instrument
 * is one circuit: a hit kicks it again while it may still ring (no voices to steal, outside the
 * parts' voice budget). Note-offs are ignored; a closed or pedal hi-hat chokes the open one.
 *
 * The models follow the circuits (K. J. Werner et al., "A physically-informed, circuit-bendable,
 * digital model of the Roland TR-808 bass drum circuit", DAFx-14, and "The TR-808 cymbal",
 * ICMC-SMC 2014; the service notes' frequencies and decay times):
 *   bridged-T voices (BD, SD, toms, congas, rim shot, claves): a resonator (Chamberlin state
 *     variable, Q30) kicked by the trigger pulse through the BD's pulse shaper (a low shelf, its
 *     falling edge clipped at a diode drop). BD: 49.4 Hz; for the first ~6 ms (the attack
 *     envelope grounds R165) it rings near 130 Hz, and while the network swings far one way
 *     (Q43 leaks) it rings higher: the "pitch sigh" of loud hits. TONE is a passive low-pass,
 *     the output a 6.8 Hz high-pass. SD: 173 + 336 Hz (the revised board), TONE mixes them,
 *     SNAPPY is the level of high-passed (2.7 kHz) noise. Toms and congas fall slightly in pitch
 *     as they decay (the toms add a little low-passed noise). Rim shot: 455 + 1667 Hz through a
 *     swing VCA; claves 2.5 kHz.
 *   noise voices: clap (1.1 kHz band-pass, a sawtooth envelope's three fast bursts and a longer
 *     last one, plus the "reverb" envelope), maracas (high-passed, a short rise and fall).
 *   metal voices: six free-running square oscillators (205.3 .. 800 Hz, one HD14584) shared by
 *     the cowbell (540 + 800 Hz, 880 Hz band-pass) and, through band-passes at 3.44 and 7.1 kHz,
 *     the cymbal (two bands, each its envelope and high-pass, then the output's +6 dB / octave)
 *     and the hi-hats. Their swing-type VCAs clip the band-passed squares hard, the envelope sets
 *     how far: the metallic fizz.
 * Velocity is the trigger level (velocity 127 = 13.5 V, the accent's top); the shaper, the sigh
 * and the swing VCAs make loud hits brighter, not only louder. The drum track's P_E0..P_E7 are
 * the kit's controls (DR_EDIT), each 0 at the stock setting (older projects hold zeros) and read
 * every block. LEVEL / REV: GLO > DRUMS (G_DRLVL, G_DRREV); PAN and MUTE: the drum track's P_PAN /
 * P_MUTE. Rendered from the audio ISR. drum_set: the SAMPLE engine's GM kit set (seq.c). */
enum { DR_BD, DR_SD, DR_LT, DR_MT, DR_HT, DR_LC, DR_MC, DR_HC, DR_RS, DR_CL, DR_CP, DR_MA, DR_CB, DR_CY,
       DR_OH, DR_CH, DR_N };
#define DR_X 0xFFu
#define DR_GM0 35u
static const uint8_t DR_GM[48] = {               /* GM percussion keys 35..82 -> instrument */
    DR_BD, DR_BD, DR_RS, DR_SD, DR_CP, DR_SD, DR_LT, DR_CH,       /* 35 kick 2, kick, side stick .. */
    DR_LT, DR_CH, DR_MT, DR_OH, DR_MT, DR_HT, DR_CY, DR_HT,       /* 43 */
    DR_CY, DR_CY, DR_CY, DR_MA, DR_CY, DR_CB, DR_CY, DR_X,        /* 51 ride, china, bell, tamb. .. */
    DR_CY, DR_HC, DR_MC, DR_HC, DR_MC, DR_LC, DR_X, DR_X,         /* 59 ride 2, bongos, congas */
    DR_X, DR_X, DR_MA, DR_MA, DR_X, DR_X, DR_X, DR_X,             /* 67 cabasa, maracas */
    DR_CL, DR_CL, DR_CL, DR_X, DR_X, DR_X, DR_X, DR_MA,           /* 75 claves, wood blocks; 82 shaker */
};
enum { DE_BTONE, DE_BDECAY, DE_STONE, DE_SNAPPY, DE_BTUNE, DE_TOMS, DE_OHDEC, DE_CYDEC };   /* P_E0.. */
static const param_desc_t DR_EDIT[8] = {
    {"BTONE", F_BIPCT, -64, 63, 0, 0, 0},        /* BD tone: low-pass 120 Hz .. 8 kHz, 1 kHz at 0 */
    {"BDCAY", F_BIPCT, -64, 63, 0, 0, 0},        /* BD decay: 47 ms .. 1.8 s, 300 ms at 0 (stock 50..800) */
    {"STONE", F_BIPCT, -64, 63, 0, 0, 0},        /* SD tone: from the 173 Hz to the 336 Hz oscillator */
    {"SNAPY", F_BIPCT, -64, 63, 0, 0, 0},        /* SD snappy: the noise, none at -100 % */
    {"BTUNE", F_SEMI, -12, 12, 0, 0, 0},         /* BD pitch (the 808's internal trim) */
    {"TOMS", F_SEMI, -12, 12, 0, 0, 0},          /* toms and congas */
    {"OHDCY", F_BIPCT, -64, 63, 0, 0, 0},        /* open hat decay: 76 .. 690 ms, 230 ms at 0 */
    {"CYDCY", F_BIPCT, -64, 63, 0, 0, 0},        /* cymbal decay: 270 ms .. 1.5 s, 650 ms at 0 */
};

#define DR_V1 16384                              /* 1 V of the trigger and the pulse shaper */
#define DR_P1 44u                                /* the trigger pulse: 1 ms */
#define DR_PEND 200u                             /* the shaped pulse has settled */
#define DR_MS(ms) ((uint32_t)(ms) * 441u / 10u)  /* ms -> samples */
#define DR_TAU(ms) (DR_MS(ms) * 10u / 46u)       /* time constant of a decay reaching -40 dB at ms */
#define DR_LVL(vel) ((int32_t)(vel) * 132104)    /* envelope level, Q24 (velocity 127 = 1.0) */
#define DR_NOISY ((1u << DR_SD) | (1u << DR_LT) | (1u << DR_MT) | (1u << DR_HT) | (1u << DR_CP) | (1u << DR_MA))
#define DR_BANDS ((1u << DR_CY) | (1u << DR_OH) | (1u << DR_CH))
#define DR_METAL (DR_BANDS | (1u << DR_CB))

typedef struct { int32_t e, k; } dre_t;          /* decay: value Q24, factor per block Q16 */
typedef struct { int16_t a1, a2, a3, k; } drf_t; /* trapezoidal SVF (A. Simper), Q13 */

typedef struct {
    uint32_t t;                  /* samples since the hit */
    int32_t v;                   /* its trigger level (DR_V1 units) */
    int32_t px;                  /* pulse shaper state */
    int32_t f, q;                /* resonator: 2 sin(pi fc / FS) and damping, Q30 */
    int32_t f2, q2;              /* the second resonator (SD, RS) */
    int32_t lp, bp, lp2, bp2;    /* their states */
    int32_t s[4];                /* filter / envelope states */
    dre_t e[2];                  /* envelopes */
} drv_t;

static struct {
    drv_t v[DR_N];
    uint32_t on;                 /* bit per instrument still sounding */
    uint32_t age;                /* hits so far */
    int16_t set;                 /* SMP_SETS index of "PERC" (seq.c), -1 = none, -2 = not looked up */
    uint8_t choke;               /* the open hat is being discharged by a closed one */
    int32_t last;                /* the kit's last output sample (before LEVEL) */
    int32_t tail;                /* declick: what drums_off cut, decaying */
    int32_t peak;                /* largest |output| since the UI last looked (TRACKS meter) */
    int32_t nst;                 /* the noise generator */
    uint32_t ph[6];              /* the six square oscillators (always running, like the hardware) */
    int32_t bpf1[2], bpf2[2], cbf[2], hat[2], cy[5];   /* filters of the shared metal section */
} drums = {.set = -2, .nst = 0x2F6E2B1};

static int32_t dr_nz[CTL], dr_m1[CTL], dr_m2[CTL], dr_cbx[CTL], dr_acc[CTL];   /* (dr_m2: through its swing VCA) */

static int32_t drum_set(void)
{
    uint32_t i;
    if (drums.set == -2) {
        drums.set = -1;
        for (i = 0; i < SMP_NSETS; i++)
            if (str_eq(SMP_SETS[i].name, "PERC"))
                drums.set = (int16_t)i;
    }
    return drums.set;
}

/* ------------------------------------------------------------ helpers --- */
static int32_t dr_freq(uint32_t hz16)            /* 2 sin(pi fc / FS), Q30; fc in Hz * 16 (<= 4 kHz) */
{
    int32_t x = (int32_t)(hz16 * 4781u);         /* pi fc / FS, Q30 */
    int32_t x3 = (int32_t)(((((int64_t)x * x) >> 30) * x) >> 30);
    return 2 * x - x3 / 3;                       /* 2 sin x ~ 2 x - x^3 / 3 */
}
#define DR_F(hz) dr_freq((uint32_t)((hz) * 16))

/* damping of a resonator ringing with time constant tau samples: f q = 2 / tau, q = 2^61 / (f tau) in
 * Q30. No 64-bit division on the target: f tau normalized to m 2^e (m in [2^30, 2^31)), 2^47 / m */
static int32_t dr_damp(int32_t f, uint32_t tau)
{
    uint64_t p = (uint64_t)(uint32_t)f * (tau ? tau : 1u);
    int32_t e = 0;
    uint32_t r;
    if (!p)
        return 3 << 29;
    while (p >= (1u << 31)) {
        p >>= 1;
        e++;
    }
    while (p < (1u << 30)) {
        p <<= 1;
        e--;
    }
    r = 0xFFFFFFFFu / (uint32_t)(p >> 15);       /* 2^47 / m */
    if (e >= 14)
        return (int32_t)(r >> (e - 14));
    return r >= (3u << 29) >> (14 - e) ? 3 << 29 : (int32_t)(r << (14 - e));
}

static int32_t dr_lp1(uint32_t hz)               /* one-pole low-pass coefficient ~ 1 - exp(-2 pi fc / FS), Q14 */
{
    int32_t w = (int32_t)((hz * 2390u) >> 10);   /* 2 pi fc / FS, Q14 */
    return w * 16384 / (16384 + w / 2);
}

static uint32_t dr_exp(uint32_t base, int32_t v, int32_t oct)   /* base * 2^(v / oct) */
{
    return (uint32_t)(((uint64_t)base * pow2_q16(v * 192 / oct)) >> 16);
}

/* one Chamberlin state-variable step, band-pass out; f, q Q30, rounded (no DC creep) */
static inline int32_t dr_res(int32_t *lp, int32_t *bp, int32_t f, int32_t q, int32_t x)
{
    int32_t b = *bp, l = *lp + (int32_t)(((int64_t)f * b + (1 << 29)) >> 30), h;
    h = x - l - (int32_t)(((int64_t)q * b + (1 << 29)) >> 30);
    *lp = l;
    *bp = b + (int32_t)(((int64_t)f * h + (1 << 29)) >> 30);
    return *bp;
}

/* trapezoidal SVF: band-pass (gain Q at fc) or high-pass; signals within +-2^16 */
static inline int32_t drf_bp(const drf_t *c, int32_t x, int32_t *s)
{
    int32_t v3 = x - s[1], v1 = (c->a1 * s[0] + c->a2 * v3) >> 13, v2 = s[1] + ((c->a2 * s[0] + c->a3 * v3) >> 13);
    s[0] = 2 * v1 - s[0];
    s[1] = 2 * v2 - s[1];
    return v1;
}
static inline int32_t drf_hp(const drf_t *c, int32_t x, int32_t *s)
{
    int32_t v3 = x - s[1], v1 = (c->a1 * s[0] + c->a2 * v3) >> 13, v2 = s[1] + ((c->a2 * s[0] + c->a3 * v3) >> 13);
    s[0] = 2 * v1 - s[0];
    s[1] = 2 * v2 - s[1];
    return x - ((c->k * v1) >> 13) - v2;
}
/* the fixed filters: g = tan(pi fc / FS), k = 1 / Q, a1 = 1 / (1 + g (g + k)), a2 = g a1, a3 = g a2 */
static const drf_t DRF_SDN = {6206, 1231, 244, 11587};          /* SD noise high-pass 2749 Hz, Q 0.71 */
static const drf_t DRF_CP = {7814, 636, 52, 4201};              /* clap band-pass 1140 Hz, Q 1.95 */
static const drf_t DRF_MA = {4921, 1831, 681, 11587};           /* maracas high-pass 5 kHz */
static const drf_t DRF_CB = {8055, 506, 32, 1707};              /* cowbell band-pass 880 Hz, Q 4.8 */
static const drf_t DRF_M1 = {7149, 1788, 447, 2731};            /* metal band-pass 1: 3440 Hz, Q 3 */
static const drf_t DRF_M2 = {5360, 2969, 1644, 3277};           /* metal band-pass 2: 7100 Hz, Q 2.5 */
static const drf_t DRF_CYA = {6050, 1313, 285, 11587};          /* cymbal band 1 high-pass 3 kHz */
static const drf_t DRF_CYB = {3110, 2886, 2677, 6827};          /* cymbal band 2 high-pass 10.5 kHz, Q 1.2 */
static const drf_t DRF_HAT = {4449, 2423, 1320, 8192};          /* hi-hat high-pass 7 kHz, Q 1 */
static const drf_t DRF_RS = {7558, 431, 25, 11587};             /* rim shot high-pass 800 Hz */

static int32_t dr_blk(uint32_t tau)              /* exp(-CTL / tau), Q16; tau in samples */
{
    uint32_t x, x2;
    if (tau < 2u * CTL)
        tau = 2u * CTL;
    x = ((uint32_t)CTL << 16) / tau;             /* <= 0.5 */
    x2 = (x * x) >> 16;
    return (int32_t)(65536u - x + (x2 >> 1) - ((x2 * x) >> 16) / 6u);
}
static void dre_hit(dre_t *e, int32_t level, uint32_t tau)   /* charged to level (Q24) unless above */
{
    if (level > e->e)
        e->e = level;
    e->k = dr_blk(tau);
}
static inline int32_t dre_next(dre_t *e, int32_t *e0)   /* this block's start (*e0) and its step << CTL_LOG2 */
{
    int32_t a = e->e;
    e->e = (int32_t)(((int64_t)a * e->k) >> 16);
    *e0 = a;
    return e->e - a;
}
#define DRE_AT(e0, de, i) (((e0) + (((de) * (int32_t)(i)) >> CTL_LOG2)) >> 9)   /* Q15 */
#define DRE_LOW (1 << 11)                        /* an envelope below -78 dB: done */
#define DR_QUIET 8                               /* a block peak below -72 dB (after the hit): done */
#define DR_LIFE DR_MS(6000)                      /* and no voice longer than this (rounding limit cycles) */

/* the trigger pulse (1 ms at the trigger level) through the BD's pulse shaper: a low shelf passes
 * the edges and 0.044 of the level; D53 keeps the falling edge above a diode drop (0.71 V) */
static inline int32_t dr_pulse(drv_t *d)
{
    int32_t p = d->t < DR_P1 ? d->v : 0, s;
    d->px += ((p - d->px) * 6650) >> 15;         /* 0.1 ms */
    s = p - d->px + ((p * 1442) >> 15);
    if (s < 0)                                   /* -> -0.71 V (727 = 0.71 V / 16) */
        s = s * 727 / (727 - (s >> 4));
    return s;
}

static void dr_off(drv_t *d)                     /* silent: back to rest */
{
    uint32_t k = (uint32_t)(d - drums.v);
    memset(d, 0, sizeof *d);
    drums.on &= ~(1u << k);
}

/* ---------------------------------------------------- bridged-T voices --- */
#define DR_BD_G 6570                             /* output gains (Q16): ~ the old kit's peaks */
#define DR_SIGH0 4000                            /* the sigh: above this swing the network rings higher, */
#define DR_SIGHS 1                               /* by (swing - DR_SIGH0) >> DR_SIGHS (Q15) */
#define DR_LEAK 7                                /* the shaped pulse past the network (the click) >> */

static void dr_bd(drv_t *d, int32_t *acc, uint32_t n)
{
    const int16_t *p = trk[TRK_DRUM].p;
    int32_t f0, q, g, c, tune = p[P_E0 + DE_BTUNE] * 16, pk = 0;
    uint32_t i;
    f0 = (int32_t)(((int64_t)DR_F(49.4) * pow2_q16(tune)) >> 16);
    q = dr_damp(f0, DR_TAU(dr_exp(300u, p[P_E0 + DE_BDECAY], 24)));
    g = (int32_t)(((int64_t)DR_BD_G * pow2_q16(-tune)) >> 16);         /* the same level at any pitch */
    c = dr_lp1(dr_exp(1000u, p[P_E0 + DE_BTONE], 21));                  /* TONE */
    for (i = 0; i < n; i++) {
        int32_t x = 0, m, y;
        if (d->t < DR_PEND) {
            x = dr_pulse(d) * 16;                /* 1 V = 2^18 in the network */
            if (d->t < DR_P1)
                d->s[0] = 32767;
        }
        d->s[0] -= (d->s[0] >> 6) + (d->s[0] > 0);   /* attack envelope: ~5 ms after the pulse */
        m = (d->s[0] * 53412) >> 15;             /* the attack: x 2.63, 130 Hz */
        if (d->s[3] > DR_SIGH0)
            m += (d->s[3] - DR_SIGH0) >> DR_SIGHS;
        y = dr_res(&d->lp, &d->bp, f0 + (int32_t)(((int64_t)f0 * m) >> 15), q, x);
        y = clamp((int32_t)(((int64_t)y * g) >> 16), -65535, 65535);
        d->s[3] = y;
        y += x >> DR_LEAK;
        d->s[1] += ((y - d->s[1]) * c) >> 14;    /* TONE */
        d->s[2] += (d->s[1] * 256 - d->s[2]) >> 10;   /* output high-pass 6.8 Hz, Q8 */
        y = d->s[1] - (d->s[2] >> 8);
        acc[i] += y;
        if (y > pk || -y > pk)
            pk = y < 0 ? -y : y;
        d->t++;
    }
    if (d->t > DR_PEND && pk < DR_QUIET)
        dr_off(d);
}

#define DR_SD_G 1950                             /* tone */
#define DR_SD_N 18000                            /* noise at SNAPPY 0, Q15 */
static void dr_sd(drv_t *d, int32_t *acc, uint32_t n)
{
    const int16_t *p = trk[TRK_DRUM].p;
    int32_t m = (p[P_E0 + DE_STONE] + 64) * 256, sn = (p[P_E0 + DE_SNAPPY] + 64) * DR_SD_N / 64;
    int32_t gl = (int32_t)(((int64_t)DR_SD_G * 2 * (32768 - m)) >> 15), gh = (int32_t)(((int64_t)DR_SD_G * m) >> 15);
    int32_t e0, de = dre_next(&d->e[0], &e0), pk = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t x = 0, y, a, b, w;
        if (d->t < DR_PEND)
            x = dr_pulse(d) * 16;
        a = dr_res(&d->lp, &d->bp, d->f, d->q, x);
        b = dr_res(&d->lp2, &d->bp2, d->f2, d->q2, x);
        y = (int32_t)(((int64_t)a * gl + (int64_t)b * gh) >> 16);
        w = (DRE_AT(e0, de, i) * sn) >> 15;
        y += (drf_hp(&DRF_SDN, dr_nz[i], d->s) * w) >> 15;
        acc[i] += y;
        if (y > pk || -y > pk)
            pk = y < 0 ? -y : y;
        d->t++;
    }
    if (d->t > DR_PEND && pk < DR_QUIET && d->e[0].e < DRE_LOW)
        dr_off(d);
}

/* toms and congas: Hz, the -40 dB time (ms), noise */
static const struct { uint16_t hz, ms; uint8_t nz, g; } DR_TOM[6] = {   /* g: level / 256 */
    {90, 200, 1, 255}, {135, 130, 1, 255}, {185, 100, 1, 255}, {185, 180, 0, 205}, {280, 100, 0, 205}, {400, 80, 0, 205},
};
#define DR_TOM_G 5700                            /* at 100 Hz (Q16; congas 0.8 of it), the same level at any pitch */
#define DR_TOM_N 2400                            /* toms' noise, Q15 */
#define DR_TOM_DROP 3932                         /* pitch at the hit: + 12 %, falling with the level */
static void dr_tom(drv_t *d, int32_t *acc, uint32_t n)
{
    uint32_t k = (uint32_t)(d - drums.v) - DR_LT, i;
    int32_t tune = trk[TRK_DRUM].p[P_E0 + DE_TOMS] * 16, f0, f, q, g, e0, n0, dn, pk = 0;
    f0 = (int32_t)(((int64_t)DR_F(DR_TOM[k].hz) * pow2_q16(tune)) >> 16);
    q = dr_damp(f0, DR_TAU(DR_TOM[k].ms));
    g = (int32_t)(((DR_TOM_G * DR_TOM[k].g >> 8) * 100 / DR_TOM[k].hz * pow2_q16(-tune)) >> 16);
    dre_next(&d->e[0], &e0);                     /* the pitch follows the level (the diodes starve it) */
    f = f0 + (int32_t)(((int64_t)f0 * ((e0 >> 9) * DR_TOM_DROP >> 15)) >> 15);
    dn = dre_next(&d->e[1], &n0);
    for (i = 0; i < n; i++) {
        int32_t x = 0, y;
        if (d->t < DR_PEND)
            x = dr_pulse(d) * 16;
        y = (int32_t)(((int64_t)dr_res(&d->lp, &d->bp, f, q, x) * g) >> 16);
        if (DR_TOM[k].nz) {
            d->s[0] += ((dr_nz[i] - d->s[0]) * 2700) >> 14;   /* ~1 kHz low-pass */
            y += (d->s[0] * ((DRE_AT(n0, dn, i) * DR_TOM_N) >> 15)) >> 15;
        } else {
            y += x >> (DR_LEAK + 2);             /* the congas' click */
        }
        acc[i] += y;
        if (y > pk || -y > pk)
            pk = y < 0 ? -y : y;
        d->t++;
    }
    if (d->t > DR_PEND && pk < DR_QUIET)
        dr_off(d);
}

#define DR_RS_G 5000
#define DR_RS_V 24                               /* into the swing VCA (Q4) */
#define DR_RS_O 15200                            /* out, Q15 */
static void dr_rs(drv_t *d, int32_t *acc, uint32_t n)
{
    int32_t e0, de = dre_next(&d->e[0], &e0), pk = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t x = 0, y;
        if (d->t < DR_PEND)
            x = dr_pulse(d) * 16;
        y = dr_res(&d->lp, &d->bp, d->f, d->q, x) + (dr_res(&d->lp2, &d->bp2, d->f2, d->q2, x) >> 1);
        y = softclip(clamp((int32_t)(((int64_t)y * DR_RS_G) >> 16) * DR_RS_V >> 4, -200000, 200000));
        y = (drf_hp(&DRF_RS, (y * DRE_AT(e0, de, i)) >> 15, d->s) * DR_RS_O) >> 15;
        acc[i] += y;
        if (y > pk || -y > pk)
            pk = y < 0 ? -y : y;
        d->t++;
    }
    if (d->t > DR_PEND && d->e[0].e < DRE_LOW)
        dr_off(d);
}

#define DR_CL_G 414
static void dr_cl(drv_t *d, int32_t *acc, uint32_t n)
{
    int32_t pk = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t x = 0, y;
        if (d->t < DR_PEND)
            x = dr_pulse(d) * 16;
        y = (int32_t)(((int64_t)dr_res(&d->lp, &d->bp, d->f, d->q, x) * DR_CL_G) >> 16);
        acc[i] += y;
        if (y > pk || -y > pk)
            pk = y < 0 ? -y : y;
        d->t++;
    }
    if (d->t > DR_PEND && pk < DR_QUIET)
        dr_off(d);
}

/* -------------------------------------------------------- noise voices --- */
#define DR_CP_GAP 441u                           /* the sawtooth: a burst every 10 ms, */
#define DR_CP_N 3u                               /* this many fast ones, then a slower last one */
#define DR_CP_REV 9000                           /* the reverb envelope, Q15 */
#define DR_CP_G 10445                            /* out, Q12 */
static void dr_cp(drv_t *d, int32_t *acc, uint32_t n)
{
    int32_t e0, de = dre_next(&d->e[0], &e0), pk = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t y, w;
        if (d->t <= DR_CP_N * DR_CP_GAP && d->t == (uint32_t)d->s[3]) {   /* the next burst */
            d->s[2] = (d->v * 151) >> 10;         /* Q15 of the trigger level (13.5 V -> 1.0) */
            d->s[3] += (int32_t)DR_CP_GAP;
        }
        d->s[2] -= (d->s[2] >> (d->t < DR_CP_N * DR_CP_GAP ? 7 : 8)) + (d->s[2] > 0);   /* 2.9 ms, the last 5.8 ms */
        w = d->s[2] + ((DRE_AT(e0, de, i) * DR_CP_REV) >> 15);
        y = (((drf_bp(&DRF_CP, dr_nz[i], d->s) * w) >> 15) * DR_CP_G) >> 12;
        acc[i] += y;
        if (y > pk || -y > pk)
            pk = y < 0 ? -y : y;
        d->t++;
    }
    if (d->t > DR_CP_N * DR_CP_GAP && d->s[2] < 8 && d->e[0].e < DRE_LOW)
        dr_off(d);
}

#define DR_MA_RISE 662u                          /* 15 ms */
#define DR_MA_G 23900                            /* out, Q15 */
static void dr_ma(drv_t *d, int32_t *acc, uint32_t n)
{
    int32_t step = ((d->v * 151) >> 10) / (int32_t)DR_MA_RISE, pk = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t y;
        if (d->t < DR_MA_RISE)
            d->s[2] += step;
        else
            d->s[2] -= (d->s[2] >> 7) + (d->s[2] > 0);   /* 2.9 ms */
        y = (((drf_hp(&DRF_MA, dr_nz[i], d->s) * d->s[2]) >> 15) * DR_MA_G) >> 15;
        acc[i] += y;
        if (y > pk || -y > pk)
            pk = y < 0 ? -y : y;
        d->t++;
    }
    if (d->t > DR_MA_RISE && d->s[2] < 8)
        dr_off(d);
}

/* -------------------------------------------------------- metal voices --- */
#define DR_DUTY 2060725309u                      /* 47.98 % (the HD14584 at 5 V) */
static const uint32_t DR_OSC[6] = {              /* phase increments: 205.3, 369.6, 304.4, 522.7, */
    19994485u, 35995916u, 29645987u, 50906562u, 77913239u, 52591436u,   /* 800 (#5), 540 Hz (#6) */
};
#define DR_MV 6                                  /* the swing VCAs' input gain (x) */
/* the oscillators and what the sounding voices take from them: the cowbell's two, band 1 (3.44 kHz,
 * the cymbal) and band 2 (7.1 kHz, cymbal and hats) through the clipping of its swing VCAs */
static void dr_metal(uint32_t n, uint32_t on)
{
    uint32_t i, k, ph[6];
    for (k = 0; k < 6u; k++)
        ph[k] = drums.ph[k];
    for (i = 0; i < n; i++) {                    /* the six; their sum in dr_m1 */
        int32_t sum = 0;
        for (k = 0; k < 6u; k++) {
            ph[k] += DR_OSC[k];
            sum += ph[k] < DR_DUTY ? 2048 : -2048;
        }
        dr_cbx[i] = (ph[4] < DR_DUTY ? 8192 : -8192) + (ph[5] < DR_DUTY ? 8192 : -8192);
        dr_m1[i] = sum;
    }
    for (k = 0; k < 6u; k++)
        drums.ph[k] = ph[k];
    if (on & DR_BANDS)
        for (i = 0; i < n; i++)
            dr_m2[i] = softclip(clamp(drf_bp(&DRF_M2, dr_m1[i], drums.bpf2) * DR_MV, -200000, 200000));
    if (on & (1u << DR_CY))
        for (i = 0; i < n; i++)
            dr_m1[i] = drf_bp(&DRF_M1, dr_m1[i], drums.bpf1);
}

#define DR_CB_G 26000
#define DR_CB_FAST 24576                         /* the two envelopes: 50 ms and 500 ms, Q15 */
static void dr_cb(drv_t *d, int32_t *acc, uint32_t n)
{
    int32_t e0, de = dre_next(&d->e[0], &e0), t0, dt = dre_next(&d->e[1], &t0), pk = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t y = softclip(clamp(drf_bp(&DRF_CB, dr_cbx[i], drums.cbf), -200000, 200000));
        int32_t w = ((DRE_AT(e0, de, i) * DR_CB_FAST) >> 15) + ((DRE_AT(t0, dt, i) * (32767 - DR_CB_FAST)) >> 15);
        y = (((y * w) >> 15) * DR_CB_G) >> 15;
        acc[i] += y;
        if (y > pk || -y > pk)
            pk = y < 0 ? -y : y;
    }
    d->t += n;
    if (d->e[0].e < DRE_LOW && d->e[1].e < DRE_LOW)
        dr_off(d);
}

#define DR_CY_G 50000
static void dr_cy(drv_t *d, int32_t *acc, uint32_t n)
{
    int32_t a0, da, b0, db;
    int32_t *s = drums.cy;
    uint32_t i;
    d->e[0].k = dr_blk(DR_TAU(dr_exp(650u, trk[TRK_DRUM].p[P_E0 + DE_CYDEC], 50)));   /* CYDCY */
    da = dre_next(&d->e[0], &a0);
    db = dre_next(&d->e[1], &b0);
    for (i = 0; i < n; i++) {
        int32_t a = softclip(clamp(dr_m1[i] * DR_MV, -200000, 200000));
        int32_t b = dr_m2[i], y;
        a = drf_hp(&DRF_CYA, (a * DRE_AT(a0, da, i)) >> 15, s);
        b = drf_hp(&DRF_CYB, (b * DRE_AT(b0, db, i)) >> 15, s + 2);
        y = (a * 5 + b * 4) >> 3;
        acc[i] += ((y - s[4]) * DR_CY_G) >> 15;  /* the level buffer: + 6 dB / octave */
        s[4] = y;
    }
    d->t += n;
    if (d->e[0].e < DRE_LOW && d->e[1].e < DRE_LOW)
        dr_off(d);
}

#define DR_HAT_G 27000
static void dr_hats(int32_t *acc, uint32_t n)    /* open and closed: one band, two VCAs, one high-pass */
{
    drv_t *o = &drums.v[DR_OH], *c = &drums.v[DR_CH];
    int32_t o0, dov = 0, c0, dc = 0;
    uint32_t i;
    o0 = c0 = 0;
    if (drums.on & (1u << DR_OH))
        dov = dre_next(&o->e[0], &o0);
    if (drums.on & (1u << DR_CH))
        dc = dre_next(&c->e[0], &c0);
    for (i = 0; i < n; i++) {
        int32_t y = (dr_m2[i] * ((DRE_AT(o0, dov, i) + DRE_AT(c0, dc, i)) >> 1)) >> 14;
        acc[i] += (drf_hp(&DRF_HAT, y, drums.hat) * DR_HAT_G) >> 15;
    }
    o->t += n;
    c->t += n;
    if ((drums.on & (1u << DR_OH)) && o->e[0].e < DRE_LOW) {
        dr_off(o);
        drums.choke = 0;
    }
    if ((drums.on & (1u << DR_CH)) && c->e[0].e < DRE_LOW)
        dr_off(c);
}

/* ---------------------------------------------------------- the hits --- */
static void dr_hat_coef(void)                    /* OHDCY, read every block (and the choke) */
{
    drv_t *o = &drums.v[DR_OH];
    o->e[0].k = drums.choke ? dr_blk(64u) : dr_blk(DR_TAU(dr_exp(230u, trk[TRK_DRUM].p[P_E0 + DE_OHDEC], 40)));
}

static void drum_on(uint32_t note, uint32_t vel)
{
    uint32_t k = note - DR_GM0;
    drv_t *d;
    if (k >= sizeof DR_GM || (k = DR_GM[k]) == DR_X)
        return;
    d = &drums.v[k];
    drums.age++;
    d->t = 0;
    d->v = (int32_t)vel * 1742;                  /* 13.5 V at 127 */
    switch (k) {
    case DR_SD:
        d->f = DR_F(173.3);
        d->q = dr_damp(d->f, DR_MS(18));         /* Q 10 */
        d->f2 = DR_F(336.0);
        d->q2 = dr_damp(d->f2, DR_MS(9));        /* Q 9.9 */
        dre_hit(&d->e[0], DR_LVL(vel), DR_MS(45));
        break;
    case DR_LT: case DR_MT: case DR_HT: case DR_LC: case DR_MC: case DR_HC: {
        uint32_t tau = DR_TAU(DR_TOM[k - DR_LT].ms);
        dre_hit(&d->e[0], DR_LVL(vel), tau);
        dre_hit(&d->e[1], DR_LVL(vel), tau * 7u / 10u);
        break;
    }
    case DR_RS:
        d->f = DR_F(455);
        d->q = dr_damp(d->f, DR_MS(12));
        d->f2 = DR_F(1667);
        d->q2 = dr_damp(d->f2, DR_MS(6));
        dre_hit(&d->e[0], DR_LVL(vel), DR_TAU(12));
        break;
    case DR_CL:
        d->f = DR_F(2500);
        d->q = dr_damp(d->f, DR_MS(9));
        break;
    case DR_CP:
        d->s[2] = 0;
        d->s[3] = 0;                              /* the first burst now */
        dre_hit(&d->e[0], DR_LVL(vel), DR_MS(25));
        break;
    case DR_MA:
        d->s[2] = 0;
        break;
    case DR_CB:
        dre_hit(&d->e[0], DR_LVL(vel), DR_TAU(50));
        dre_hit(&d->e[1], DR_LVL(vel), DR_TAU(500));
        break;
    case DR_CY:
        dre_hit(&d->e[0], DR_LVL(vel), 64u);    /* (dr_cy: CYDCY) */
        dre_hit(&d->e[1], DR_LVL(vel), DR_MS(40));
        break;
    case DR_OH:
        drums.choke = 0;
        dre_hit(&d->e[0], DR_LVL(vel), 64u);
        break;
    case DR_CH:
        dre_hit(&d->e[0], DR_LVL(vel) * 3 / 2, DR_TAU(50));   /* (its level knob above the open hat's) */
        if (drums.on & (1u << DR_OH))
            drums.choke = 1;                     /* the closed hat discharges the open one */
        break;
    }
    drums.on |= 1u << k;
}

static void drums_off(void)                      /* all sound off: silent now, the cut value fades (tail) */
{
    uint32_t k;
    if (!drums.on)
        return;
    drums.tail += drums.last;
    for (k = 0; k < DR_N; k++)
        dr_off(&drums.v[k]);
    drums.choke = 0;
}

#define drums_busy() (drums.on != 0)

/* renders the kit; adds it into the dry mix and the reverb send; mono != 0: into mono instead,
 * before the pan and the send (the SLICER, slicer.c slicer_drums, does those after it) */
static inline void drums_mix(int32_t *ml, int32_t *mr, int32_t *rev, int32_t *mono, uint32_t n)
{
    uint32_t i, k, on = drums.on;
    int32_t lvl = song.g[G_DRLVL] * 200, send = song.g[G_DRREV] * 258, pk = drums.peak, *acc = dr_acc;
    int32_t pan = trk[TRK_DRUM].p[P_PAN], gl = 4096 - (pan > 0 ? pan * 64 : 0), gr = 4096 + (pan < 0 ? pan * 64 : 0);
    if (!on && !drums.tail)
        return;
    if (n > CTL)
        n = CTL;
    for (i = 0; i < n; i++) {                    /* declick tail, ~0.4 ms */
        acc[i] = drums.tail;
        drums.tail -= drums.tail / 16 + (drums.tail > 0 ? 1 : drums.tail < 0 ? -1 : 0);
    }
    if (on & DR_NOISY)
        for (i = 0; i < n; i++)
            dr_nz[i] = (int32_t)(noise32(&drums.nst) >> 17) - 16384;
    if (on & DR_METAL)
        dr_metal(n, on);
    if (on & (1u << DR_OH))
        dr_hat_coef();
    for (k = 0; k < DR_N; k++) {
        drv_t *d = &drums.v[k];
        if (!((on >> k) & 1u))
            continue;
        switch (k) {
        case DR_BD: dr_bd(d, acc, n); break;
        case DR_SD: dr_sd(d, acc, n); break;
        case DR_RS: dr_rs(d, acc, n); break;
        case DR_CL: dr_cl(d, acc, n); break;
        case DR_CP: dr_cp(d, acc, n); break;
        case DR_MA: dr_ma(d, acc, n); break;
        case DR_CB: dr_cb(d, acc, n); break;
        case DR_CY: dr_cy(d, acc, n); break;
        case DR_OH: dr_hats(acc, n); break;
        case DR_CH:
            if (!((on >> DR_OH) & 1u))           /* (with the open hat: done above) */
                dr_hats(acc, n);
            break;
        default: dr_tom(d, acc, n); break;
        }
        if (d->t > DR_LIFE)
            dr_off(d);
    }
    drums.last = acc[n - 1];
    for (i = 0; i < n; i++) {
        int32_t s = mulq15(clamp(acc[i], -80000, 80000), lvl);
        if (s > pk || -s > pk)
            pk = s < 0 ? -s : s;
        if (mono) {
            mono[i] += s;
            continue;
        }
        ml[i] += (s * gl) >> 12;
        mr[i] += (s * gr) >> 12;
#if FELUCCA_USB_AUDIO
        track_capture[i * NTRK + TRK_DRUM] += s;
#endif
        if (send)
            rev[i] += mulq15(s, send);
    }
    drums.peak = pk;
}
static void drums_render(int32_t *ml, int32_t *mr, int32_t *rev, uint32_t n) { drums_mix(ml, mr, rev, 0, n); }
static void drums_render_mono(int32_t *mono, uint32_t n) { drums_mix(0, 0, 0, mono, n); }
