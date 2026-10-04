/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM6: six-operator FM that plays DX7 voices.
 *
 * A part's sound is one DX7 voice in an edit buffer, fm6_ed: the 155 parameters of a
 * DX7 single-voice dump (VCED order, OP6 first), then the switches of OP1..OP6. VOICE
 * (P_E0) picks where the buffer is loaded from: the factory voices (eng_fm6_rom.h) or the
 * user bank (32 voices in flash, filled by a DX7 bank dump or STORE: fm6_store.c). The
 * FM6 pages (params.c) edit the buffer, so does a DX7 single-voice dump or parameter
 * change over USB-MIDI, and projects save it. Rendering reads it every block: every
 * edit is heard at once.
 *
 * The behaviour is the DX7's as measured for MSFA (Raph Levien) and Dexed (Pascal
 * Gauthier): envelopes in the log domain with the DX7 attack curve and static times,
 * output, level and rate key scaling, the velocity curve, the 32 algorithms, LFO and
 * pitch envelope. Their measured tables are marked "DX7 data" below and in
 * tools/gen_tables.py (Apache License 2.0, see LICENSING.md); the code is Felucca's:
 * 32-sample blocks, 32-bit phase, the interpolated Q15 sine (dsp.c). As in MSFA,
 * algorithms 4 and 6 (a feedback loop over three operators) feed OP6 back into itself.
 *
 * Units: logs in Q24 (1 << 24 = one octave or 6 dB); an operator's output is Q24 with
 * 1 << 24 = a unit sine, added to the next operator's phase as 1 << 24 = one cycle
 * (OUTPUT 99 at the top of its envelope: 2.0, a 4 pi index). */

/* ------------------------------------------------------------ the voice --- */
#define FM6_NP 161                                       /* VCED 0..154, the switches of OP1..OP6 */
enum { FO_R1, FO_R2, FO_R3, FO_R4, FO_L1, FO_L2, FO_L3, FO_L4, FO_BP, FO_LD, FO_RD, FO_LC, FO_RC, FO_RS,
       FO_AMS, FO_KVS, FO_OL, FO_MODE, FO_CRS, FO_FINE, FO_DET, FO_N };   /* one operator */
enum { FV_PR = 126, FV_PL = 130, FV_ALG = 134, FV_FB, FV_OKS, FV_LFS, FV_LFD, FV_LPMD, FV_LAMD, FV_LFKS,
       FV_LFW, FV_LPMS, FV_TRNSP, FV_NAME, FV_ON = 155 };
#define FM6_OPB(n) ((6u - (n)) * FO_N)                   /* buffer offset of OP n (1..6): OP6 comes first */
#define FM6_NUSER 32u                                    /* user bank */

static int16_t fm6_ed[NPART][FM6_NP];                    /* the parts' voices (int16: the pages edit them) */
static int16_t fm6_cur[NPART];                           /* VOICE + 1 the buffer was loaded from, 0 = load it */
static uint8_t fm6_bank[FM6_NUSER][128] __attribute__((section(".pool")));   /* the user bank, DX7 packed (VMEM) */

/* a factory voice: OP1..OP6 in buffer order within an operator (DET 0..14, 7 = none), ALG 0..31,
 * TRNSP 24 = no transposition */
typedef struct {
    uint8_t op[6][FO_N];
    uint8_t pr[4], pl[4], alg, fb, oks, lfs, lfd, lpmd, lamd, lfks, lfw, lpms, trnsp;
    char name[11];
} fm6_rom_t;
#include "eng_fm6_rom.h"                                 /* FM6_ROM[FM6_NROM] */
#define FM6_NVOICE (FM6_NROM + FM6_NUSER)                /* VOICE: factory voices, then the user bank */

/* parameter ranges in buffer order */
static const uint8_t FM6_OPMAX[FO_N] = {99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 3, 3, 7, 3, 7, 99, 1, 31, 99, 14};
static const uint8_t FM6_GMAX[FV_NAME - FV_PR] = {99, 99, 99, 99, 99, 99, 99, 99, 31, 7, 1, 99, 99, 99, 99, 1, 5, 7, 48};
static int32_t fm6_min(uint32_t i) { return i >= FV_NAME && i < FV_NAME + 10u ? 32 : 0; }
static int32_t fm6_max(uint32_t i)
{
    return i < FV_PR ? FM6_OPMAX[i % FO_N] : i < FV_NAME ? FM6_GMAX[i - FV_PR] : i < FV_ON ? 126 : 1;
}
static void fm6_set(int16_t *ed, uint32_t i, int32_t v) { ed[i] = (int16_t)clamp(v, fm6_min(i), fm6_max(i)); }

static void fm6_from_rom(int16_t *ed, const fm6_rom_t *r)
{
    uint32_t n, i;
    for (n = 1; n <= 6u; n++)
        for (i = 0; i < FO_N; i++)
            fm6_set(ed, FM6_OPB(n) + i, r->op[n - 1u][i]);
    for (i = 0; i < 4u; i++) {
        fm6_set(ed, FV_PR + i, r->pr[i]);
        fm6_set(ed, FV_PL + i, r->pl[i]);
    }
    fm6_set(ed, FV_ALG, r->alg);
    fm6_set(ed, FV_FB, r->fb);
    fm6_set(ed, FV_OKS, r->oks);
    fm6_set(ed, FV_LFS, r->lfs);
    fm6_set(ed, FV_LFD, r->lfd);
    fm6_set(ed, FV_LPMD, r->lpmd);
    fm6_set(ed, FV_LAMD, r->lamd);
    fm6_set(ed, FV_LFKS, r->lfks);
    fm6_set(ed, FV_LFW, r->lfw);
    fm6_set(ed, FV_LPMS, r->lpms);
    fm6_set(ed, FV_TRNSP, r->trnsp);
    for (i = 0; i < 10u; i++)
        fm6_set(ed, FV_NAME + i, r->name[i] ? r->name[i] : ' ');
    for (i = 0; i < 6u; i++)
        ed[FV_ON + i] = 1;
}

/* DX7 packed voice (128 bytes, VMEM) <-> buffer. Unpacking clamps every value; packing a
 * switched-off operator keeps its level (the switches are not part of a DX7 voice) */
static void fm6_unpack(int16_t *ed, const uint8_t *b)
{
    uint32_t n, i;
    for (n = 1; n <= 6u; n++) {
        const uint8_t *s = b + (6u - n) * 17u;
        uint32_t o = FM6_OPB(n);
        for (i = 0; i < 11u; i++)
            fm6_set(ed, o + i, s[i] & 127);
        fm6_set(ed, o + FO_LC, s[11] & 3);
        fm6_set(ed, o + FO_RC, (s[11] >> 2) & 3);
        fm6_set(ed, o + FO_RS, s[12] & 7);
        fm6_set(ed, o + FO_DET, (s[12] >> 3) & 15);
        fm6_set(ed, o + FO_AMS, s[13] & 3);
        fm6_set(ed, o + FO_KVS, (s[13] >> 2) & 7);
        fm6_set(ed, o + FO_OL, s[14] & 127);
        fm6_set(ed, o + FO_MODE, s[15] & 1);
        fm6_set(ed, o + FO_CRS, (s[15] >> 1) & 31);
        fm6_set(ed, o + FO_FINE, s[16] & 127);
    }
    for (i = 0; i < 8u; i++)
        fm6_set(ed, FV_PR + i, b[102 + i] & 127);
    fm6_set(ed, FV_ALG, b[110] & 31);
    fm6_set(ed, FV_FB, b[111] & 7);
    fm6_set(ed, FV_OKS, (b[111] >> 3) & 1);
    for (i = 0; i < 4u; i++)
        fm6_set(ed, FV_LFS + i, b[112 + i] & 127);
    fm6_set(ed, FV_LFKS, b[116] & 1);
    fm6_set(ed, FV_LFW, (b[116] >> 1) & 7);
    fm6_set(ed, FV_LPMS, (b[116] >> 4) & 7);
    fm6_set(ed, FV_TRNSP, b[117] & 127);
    for (i = 0; i < 10u; i++)
        fm6_set(ed, FV_NAME + i, b[118 + i] & 127);
    for (i = 0; i < 6u; i++)
        ed[FV_ON + i] = 1;
}

static void fm6_pack(uint8_t *b, const int16_t *ed)
{
    uint32_t n, i;
    for (n = 1; n <= 6u; n++) {
        uint8_t *d = b + (6u - n) * 17u;
        const int16_t *s = &ed[FM6_OPB(n)];
        for (i = 0; i < 11u; i++)
            d[i] = (uint8_t)s[i];
        d[11] = (uint8_t)(s[FO_LC] | s[FO_RC] << 2);
        d[12] = (uint8_t)(s[FO_RS] | s[FO_DET] << 3);
        d[13] = (uint8_t)(s[FO_AMS] | s[FO_KVS] << 2);
        d[14] = (uint8_t)s[FO_OL];
        d[15] = (uint8_t)(s[FO_MODE] | s[FO_CRS] << 1);
        d[16] = (uint8_t)s[FO_FINE];
    }
    for (i = 0; i < 9u; i++)
        b[102 + i] = (uint8_t)ed[FV_PR + i];                    /* pitch EG, ALG */
    b[111] = (uint8_t)(ed[FV_FB] | ed[FV_OKS] << 3);
    for (i = 0; i < 4u; i++)
        b[112 + i] = (uint8_t)ed[FV_LFS + i];
    b[116] = (uint8_t)(ed[FV_LFKS] | ed[FV_LFW] << 1 | ed[FV_LPMS] << 4);
    b[117] = (uint8_t)ed[FV_TRNSP];
    for (i = 0; i < 10u; i++)
        b[118 + i] = (uint8_t)ed[FV_NAME + i];
}

static void fm6_name(char *d, const int16_t *ed)         /* the voice name, trailing spaces cut */
{
    uint32_t i, n = 0;
    for (i = 0; i < 10u; i++) {
        d[i] = (char)ed[FV_NAME + i];
        if (d[i] != ' ')
            n = i + 1u;
    }
    d[n] = 0;
}

static void fm6_load(uint32_t p, uint32_t voice)                /* VOICE -> part p's buffer */
{
    voice %= FM6_NVOICE;
    if (voice < FM6_NROM)
        fm6_from_rom(fm6_ed[p], &FM6_ROM[voice]);
    else
        fm6_unpack(fm6_ed[p], fm6_bank[voice - FM6_NROM]);
    fm6_cur[p] = (int16_t)(voice + 1u);
}

static int fm6_slot_is(uint32_t k, const char *name)            /* user slot k holds a voice of that name */
{
    static int16_t ed[FM6_NP];
    char nm[12];
    fm6_unpack(ed, fm6_bank[k % FM6_NUSER]);
    fm6_name(nm, ed);
    return str_eq(nm, name);
}

/* the slot the STORE page offers for part p: the user voice it plays, while the buffer still carries
 * that voice's name (an edit of it); else the first INIT VOICE slot (a voice from SysEx, a factory
 * one); else k, the last one picked */
static uint32_t fm6_store_slot(uint32_t p, uint32_t k)
{
    char nm[12];
    uint32_t v = (uint32_t)trk[p].p[P_E0], i;
    fm6_name(nm, fm6_ed[p]);
    if (v >= FM6_NROM && v < FM6_NVOICE && fm6_slot_is(v - FM6_NROM, nm))
        return v - FM6_NROM;
    for (i = 0; i < FM6_NUSER; i++)
        if (fm6_slot_is(i, "INIT VOICE"))
            return i;
    return k % FM6_NUSER;
}

/* ---------------------------------------------------------- DX7 data --- */
/* DX7 data, measured for MSFA (Copyright 2012 Google Inc.) and Dexed (Copyright 2013-2017
 * Pascal Gauthier), Apache License 2.0: output levels below 20; the static times of the
 * envelope (samples at 44.1 kHz, rates 0..76); the velocity curve; the exponential
 * key-scaling curve; pitch-modulation sensitivity; the pitch envelope's rates and steps
 * (1/32 octave) */
static const uint8_t FM6_LEVELLUT[20] = {0, 5, 9, 13, 17, 20, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 42, 43, 45, 46};
static const int32_t FM6_STATICS[77] = {
    1764000, 1764000, 1411200, 1411200, 1190700, 1014300, 992250, 882000, 705600, 705600,
    584325, 507150, 502740, 441000, 418950, 352800, 308700, 286650, 253575, 220500,
    220500, 176400, 145530, 145530, 125685, 110250, 110250, 88200, 88200, 74970,
    61740, 61740, 55125, 48510, 44100, 37485, 31311, 30870, 27562, 27562,
    22050, 18522, 17640, 15435, 14112, 13230, 11025, 9261, 9261, 7717,
    6615, 6615, 5512, 5512, 4410, 3969, 3969, 3439, 2866, 2690,
    2249, 1984, 1896, 1808, 1411, 1367, 1234, 1146, 926, 837,
    837, 705, 573, 573, 529, 441, 441};
static const uint8_t FM6_VELOCITY[64] = {
    0, 70, 86, 97, 106, 114, 121, 126, 132, 138, 142, 148, 152, 156, 160, 163,
    166, 170, 173, 174, 178, 181, 184, 186, 189, 190, 194, 196, 198, 200, 202, 205,
    206, 209, 211, 214, 216, 218, 220, 222, 224, 225, 227, 229, 230, 232, 233, 235,
    237, 238, 240, 241, 242, 243, 244, 246, 246, 248, 249, 250, 251, 252, 253, 254};
static const uint8_t FM6_EXPSCALE[33] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 14, 16, 19, 23, 27, 33,
                                         39, 47, 56, 66, 80, 94, 110, 126, 142, 158, 174, 190, 206, 222, 238, 250};
static const uint8_t FM6_PMS[8] = {0, 10, 20, 33, 55, 92, 153, 255};
static const uint32_t FM6_AMS[4] = {0, 4342338, 7171437, 16777216};   /* Q24 */
static const uint8_t FM6_PEG_RATE[100] = {
    1, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11,
    12, 12, 13, 13, 14, 14, 15, 16, 16, 17, 18, 18, 19, 20, 21, 22, 23, 24, 25, 26,
    27, 28, 30, 31, 33, 34, 36, 37, 38, 39, 41, 42, 44, 46, 47, 49, 51, 53, 54, 56,
    58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 79, 82, 85, 88, 91, 94, 98, 102, 106, 110,
    115, 120, 125, 130, 135, 141, 147, 153, 159, 165, 171, 178, 185, 193, 202, 211, 232, 243, 254, 255};
static const int8_t FM6_PEG_STEP[100] = {
    -128, -116, -104, -95, -85, -76, -68, -61, -56, -52, -49, -46, -43, -41, -39, -37, -35, -33, -32, -31,
    -30, -29, -28, -27, -26, -25, -24, -23, -22, -21, -20, -19, -18, -17, -16, -15, -14, -13, -12, -11,
    -10, -9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
    10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
    30, 31, 32, 33, 34, 35, 38, 40, 43, 46, 49, 53, 58, 65, 73, 82, 92, 103, 115, 127};
#define FM6_PEG_UNIT 572                                 /* CTL << 24 / (21.3 FS): pitch envelope step */
#define FM6_LFO_UNIT 18279u                              /* CTL * 25190424 / FS: LFO delay step */

/* The 32 algorithms, operators in the order they run (OP6 .. OP1): bits 0-1 where the output
 * goes (0 = the voice, 1 / 2 = bus A / B), bit 2 adds to it, bits 4-5 the bus that modulates
 * the operator, bit 6 its own feedback. Made from the DX7 diagrams (tests/fm6_test.c checks
 * the modulation graph of each against them) */
static const uint8_t FM6_ALG[32][6] = {
    {0x41, 0x11, 0x11, 0x14, 0x01, 0x14}, {0x01, 0x11, 0x11, 0x14, 0x41, 0x14}, {0x41, 0x11, 0x14, 0x01, 0x11, 0x14},
    {0x41, 0x11, 0x14, 0x01, 0x11, 0x14}, {0x41, 0x14, 0x01, 0x14, 0x01, 0x14}, {0x41, 0x14, 0x01, 0x14, 0x01, 0x14},
    {0x41, 0x11, 0x05, 0x14, 0x01, 0x14}, {0x01, 0x11, 0x45, 0x14, 0x01, 0x14}, {0x01, 0x11, 0x05, 0x14, 0x41, 0x14},
    {0x01, 0x05, 0x14, 0x41, 0x11, 0x14}, {0x41, 0x05, 0x14, 0x01, 0x11, 0x14}, {0x01, 0x05, 0x05, 0x14, 0x41, 0x14},
    {0x41, 0x05, 0x05, 0x14, 0x01, 0x14}, {0x41, 0x05, 0x11, 0x14, 0x01, 0x14}, {0x01, 0x05, 0x11, 0x14, 0x41, 0x14},
    {0x41, 0x11, 0x02, 0x25, 0x05, 0x14}, {0x01, 0x11, 0x02, 0x25, 0x45, 0x14}, {0x01, 0x11, 0x11, 0x45, 0x05, 0x14},
    {0x41, 0x14, 0x14, 0x01, 0x11, 0x14}, {0x01, 0x05, 0x14, 0x41, 0x14, 0x14}, {0x01, 0x14, 0x14, 0x41, 0x14, 0x14},
    {0x41, 0x14, 0x14, 0x14, 0x01, 0x14}, {0x41, 0x14, 0x14, 0x01, 0x14, 0x04}, {0x41, 0x14, 0x14, 0x14, 0x04, 0x04},
    {0x41, 0x14, 0x14, 0x04, 0x04, 0x04}, {0x41, 0x05, 0x14, 0x01, 0x14, 0x04}, {0x01, 0x05, 0x14, 0x41, 0x14, 0x04},
    {0x04, 0x41, 0x11, 0x14, 0x01, 0x14}, {0x41, 0x14, 0x01, 0x14, 0x04, 0x04}, {0x04, 0x41, 0x11, 0x14, 0x04, 0x04},
    {0x41, 0x14, 0x04, 0x04, 0x04, 0x04}, {0x44, 0x04, 0x04, 0x04, 0x04, 0x04}};
static int fm6_carrier(uint32_t alg, uint32_t n) { return !(FM6_ALG[alg & 31u][6u - n] & 3u); }   /* OP n */

/* 2^(x / 65536) in Q16, x from -16 to +15.99 octaves */
static uint32_t fm6_pow2(int32_t x)
{
    int32_t ip = x >> 16;
    uint32_t fr = (uint32_t)x & 0xFFFFu, i = fr >> 8, y;
    y = FM6_EXP2[i] + (uint32_t)(((uint64_t)(FM6_EXP2[i + 1u] - FM6_EXP2[i]) * (fr & 255u)) >> 8);   /* Q30 */
    if (ip > 15)
        return 0xFFFFFFFFu;
    return ip >= 14 ? y << (ip - 14) : ip < -16 ? 0u : y >> (14 - ip);
}

/* -------------------------------------------------- scaling (DX7 rules) --- */
static int32_t fm6_scaleout(int32_t l) { return l >= 20 ? 28 + l : FM6_LEVELLUT[l < 0 ? 0 : l]; }

static int32_t fm6_curve(int32_t group, int32_t depth, int32_t curve)   /* key level scaling, 0.75 dB steps */
{
    int32_t s = curve == 0 || curve == 3 ? (group * depth * 329) >> 12
                                         : (FM6_EXPSCALE[group > 32 ? 32 : group] * depth * 329) >> 15;
    return curve < 2 ? -s : s;
}

/* an operator's output level for a note and velocity, in 0.023 dB steps (99 * 32 = full) */
static int32_t fm6_outlevel(const int16_t *op, uint32_t note, uint32_t vel)
{
    int32_t off = (int32_t)note - op[FO_BP] - 17, l, v;
    l = fm6_scaleout(op[FO_OL]) + (off >= 0 ? fm6_curve((off + 1) / 3, op[FO_RD], op[FO_RC])
                                            : fm6_curve(-(off - 1) / 3, op[FO_LD], op[FO_LC]));
    v = FM6_VELOCITY[(vel > 127u ? 127u : vel) >> 1] - 239;
    l = ((l > 127 ? 127 : l) << 5) + (((op[FO_KVS] * v + 7) >> 3) << 4);
    return l < 0 ? 0 : l;
}

static int32_t fm6_rscale(const int16_t *op, uint32_t note)   /* key rate scaling, in qrate units */
{
    int32_t x = (int32_t)note / 3 - 7;
    return (op[FO_RS] * clamp(x, 0, 31)) >> 3;
}

/* ------------------------------------------------------------ envelopes --- */
typedef struct {
    int32_t level, hold;                                 /* Q24 log2; samples a static segment has left */
    uint8_t ix;                                          /* segment 0..3 (L1..L4), 4 = done */
} fm6_eg_t;

typedef struct {
    uint32_t ph[6];                                      /* phases, gains, envelopes: index 0 = OP1 */
    int32_t gain[6];                                     /* Q14 at the end of the last block */
    fm6_eg_t eg[6];
    int32_t fb[2];                                       /* the feedback operator's last two outputs */
    int32_t pitch;                                       /* pitch envelope, Q24 octaves */
    uint8_t pix, down;
} fm6_voice_t;
static fm6_voice_t fm6_v[NPART][NVOICE] __attribute__((section(".pool")));

static int32_t fm6_target(const int16_t *op, uint32_t ix, int32_t outlevel)
{
    int32_t a = ((fm6_scaleout(op[FO_L1 + ix]) >> 1) << 6) + outlevel - 4256;
    return (a < 16 ? 16 : a) << 16;
}

/* segment ix begins. Where it would not move (or an attack to L1 0), the DX7 waits instead */
static void fm6_enter(fm6_eg_t *e, const int16_t *op, uint32_t ix, int32_t outlevel, int32_t rs)
{
    e->ix = (uint8_t)ix;
    e->hold = 0;
    if (ix < 4u) {
        int32_t lv = op[FO_L1 + ix];
        if (fm6_target(op, ix, outlevel) == e->level || (!ix && !lv)) {
            int32_t r = clamp(op[FO_R1 + ix] + rs, 0, 99);
            e->hold = r < 77 ? FM6_STATICS[r] : 20 * (99 - r);
            if (r < 77 && !ix && !lv)
                e->hold /= 20;
        }
    }
}

/* one block of an operator envelope; returns its level. Segments 0..2 run while the key
 * is held, 3 after the release */
static int32_t fm6_eg_step(fm6_eg_t *e, const int16_t *op, int32_t outlevel, int32_t rs, int down)
{
    if (e->hold) {
        e->hold -= CTL;
        if (e->hold <= 0)
            fm6_enter(e, op, e->ix + 1u, outlevel, rs);
    }
    if (!e->hold && (e->ix < 3u || (e->ix == 3u && !down))) {
        int32_t t = fm6_target(op, e->ix, outlevel);
        uint32_t q = (uint32_t)clamp(((op[FO_R1 + e->ix] * 41) >> 6) + rs, 0, 63);
        int32_t inc = (int32_t)(4u + (q & 3u)) << (2u + CTL_LOG2 + (q >> 2));
        if (t > e->level) {                              /* the DX7 attack: from -48 dB, faster when low */
            if (e->level < (1716 << 16))
                e->level = 1716 << 16;
            e->level += (((17 << 24) - e->level) >> 24) * inc;
            if (e->level >= t) {
                e->level = t;
                fm6_enter(e, op, e->ix + 1u, outlevel, rs);
            }
        } else {
            e->level -= inc;
            if (e->level <= t) {
                e->level = t;
                fm6_enter(e, op, e->ix + 1u, outlevel, rs);
            }
        }
    }
    return e->level;
}

static int fm6_eg_on(const fm6_eg_t *e, const int16_t *op) { return e->ix < 4u || op[FO_L4] > 0; }

static int32_t fm6_peg_step(fm6_voice_t *s, const int16_t *ed)   /* pitch envelope, Q24 octaves */
{
    if (s->pix < 3u || (s->pix == 3u && !s->down)) {
        int32_t t = FM6_PEG_STEP[ed[FV_PL + s->pix]] * (1 << 19), inc = FM6_PEG_RATE[ed[FV_PR + s->pix]] * FM6_PEG_UNIT;
        if (t > s->pitch) {
            s->pitch += inc;
            if (s->pitch >= t) {
                s->pitch = t;
                s->pix++;
            }
        } else {
            s->pitch -= inc;
            if (s->pitch <= t) {
                s->pitch = t;
                s->pix++;
            }
        }
    }
    return s->pitch;
}

/* ------------------------------------------------------------------ LFO --- */
static struct {
    uint32_t ph, dly;                                    /* phase; delay ramp (DX7 two-slope) */
    int32_t val, depth;                                  /* Q24, 0..1 << 24 */
    uint8_t rnd;
} fm6_lfo[NPART];

static void fm6_lfo_block(uint32_t p, const int16_t *ed)
{
    uint32_t old = fm6_lfo[p].ph, ph = old + FM6_LFO_INC[ed[FV_LFS]], a = 99u - (uint32_t)ed[FV_LFD], d1, d2;
    uint64_t d;
    int32_t x;
    fm6_lfo[p].ph = ph;
    switch (ed[FV_LFW]) {
    case 0:                                              /* triangle */
        x = (int32_t)((ph >> 7) ^ (uint32_t)-(int32_t)(ph >> 31)) & ((1 << 24) - 1);
        break;
    case 1:                                              /* saw down */
        x = (int32_t)((~ph ^ (1u << 31)) >> 8);
        break;
    case 2:                                              /* saw up */
        x = (int32_t)((ph ^ (1u << 31)) >> 8);
        break;
    case 3:                                              /* square */
        x = (int32_t)(((~ph) >> 7) & (1u << 24));
        break;
    case 4:                                              /* sine */
        x = (1 << 23) + sine_i(ph) * 256;
        break;
    default:                                             /* sample & hold */
        if (ph < old)
            fm6_lfo[p].rnd = (uint8_t)(fm6_lfo[p].rnd * 179u + 17u);
        x = ((fm6_lfo[p].rnd ^ 0x80) + 1) << 16;
        break;
    }
    fm6_lfo[p].val = x;
    if (a == 99u) {                                      /* DELAY 0 */
        fm6_lfo[p].depth = 1 << 24;
        return;
    }
    a = (16u + (a & 15u)) << (1u + (a >> 4));
    d1 = FM6_LFO_UNIT * a;
    d2 = FM6_LFO_UNIT * (a & 0xFF80u ? a & 0xFF80u : 0x80u);
    d = (uint64_t)fm6_lfo[p].dly + (fm6_lfo[p].dly < (1u << 31) ? d1 : d2);
    if (d > 0xFFFFFFFFu) {
        fm6_lfo[p].depth = 1 << 24;
        return;
    }
    fm6_lfo[p].dly = (uint32_t)d;
    fm6_lfo[p].depth = d < (1u << 31) ? 0 : (int32_t)((d >> 7) & ((1u << 24) - 1u));
}

/* -------------------------------------------------------------- render --- */
#define FM6_THR 2                                        /* Q14 gain below which an operator is silent */
static int32_t fm6_bus[2][CTL], fm6_sum[CTL];
static const int32_t fm6_zero[CTL];

/* one operator over the block: sine of phase + in[] (Q24, 1 << 24 = a cycle), gain g0 -> g1 (Q14, up to 4.0) */
static uint32_t fm6_op(int32_t *out, const int32_t *in, uint32_t ph, uint32_t inc, int32_t g0, int32_t g1, int add)
{
    int32_t g = g0, dg = (g1 - g0 + (CTL >> 1)) >> CTL_LOG2;
    uint32_t i;
    if (add)
        for (i = 0; i < CTL; i++) {
            g += dg;
            out[i] += (sine_i(ph + ((uint32_t)in[i] << 8)) * g) >> 5;
            ph += inc;
        }
    else
        for (i = 0; i < CTL; i++) {
            g += dg;
            out[i] = (sine_i(ph + ((uint32_t)in[i] << 8)) * g) >> 5;
            ph += inc;
        }
    return ph;
}

/* the feedback operator: its last two outputs, averaged, modulate it (FEEDBACK 7: half a cycle) */
static uint32_t fm6_op_fb(int32_t *out, int32_t *fb, uint32_t ph, uint32_t inc, int32_t g0, int32_t g1, int add,
                          uint32_t sh)
{
    int32_t g = g0, dg = (g1 - g0 + (CTL >> 1)) >> CTL_LOG2, y0 = fb[0], y = fb[1];
    uint32_t i;
    for (i = 0; i < CTL; i++) {
        int32_t m = (y0 + y) >> sh;
        y0 = y;
        g += dg;
        y = (sine_i(ph + ((uint32_t)m << 8)) * g) >> 5;
        out[i] = add ? out[i] + y : y;
        ph += inc;
    }
    fb[0] = y0;
    fb[1] = y;
    return ph;
}

static uint32_t fm6_inc_ratio(uint32_t base, const int16_t *op, uint32_t note)
{
    int32_t lg = FM6_COARSE[op[FO_CRS]] + FM6_FINE[op[FO_FINE]] + (op[FO_DET] - 7) * FM6_DETUNE[note];
    uint64_t x = ((uint64_t)base * fm6_pow2(lg >> 8)) >> 16;
    return x > 0x7FFFFFFFu ? 0x7FFFFFFFu : (uint32_t)x;
}

static uint32_t fm6_inc_fixed(const int16_t *op)   /* 1 Hz .. 9.77 kHz: 10 ^ (COARSE % 4 + FINE / 100) */
{
    int32_t lg = (4458616 * ((op[FO_CRS] & 3) * 100 + op[FO_FINE])) >> 3;
    if (op[FO_DET] > 7)
        lg += 13457 * (op[FO_DET] - 7);
    return (uint32_t)(((uint64_t)fm6_pow2((lg >> 8) - (2 << 16)) * 389566u) >> 16);   /* 2^32 / FS = 389566 / 4 */
}

static fm6_voice_t *fm6_state(const track_t *t, const voice_t *v) { return &fm6_v[t - trk][v - t->v]; }
static uint32_t fm6_note(const track_t *t, const voice_t *v)   /* the note the DX7 rules see: + TRANSPOSE */
{
    return (uint32_t)clamp((int32_t)v->note + fm6_ed[t - trk][FV_TRNSP] - 24, 0, 127);
}

/* EDIT macros: MOD shifts the modulators' levels (0.375 dB per step), M.TIM / C.TIM the
 * modulators' / carriers' envelope rates (up = slower, 4 steps an octave of time) */
static int32_t fm6_time(const track_t *t, int car) { return -t->p[car ? P_E3 : P_E2] / 5; }

static void fm6_note_on(track_t *t, voice_t *v)
{
    uint32_t p = (uint32_t)(t - trk), k, note = fm6_note(t, v);
    fm6_voice_t *s = fm6_state(t, v);
    const int16_t *ed = fm6_ed[p];
    int fresh = !v->env_out;                             /* nothing sounding here: start from silence */
    for (k = 0; k < 6u; k++) {
        const int16_t *op = &ed[FM6_OPB(k + 1u)];
        if (fresh) {
            s->eg[k].level = 0;
            s->gain[k] = 0;
        }
        if (ed[FV_OKS]) {                                /* KEY SYNC: phases from 0 (and a fade-in) */
            s->ph[k] = 0;
            s->gain[k] = 0;
        }
        fm6_enter(&s->eg[k], op, 0, fm6_outlevel(op, note, v->vel),
                  fm6_rscale(op, note) + fm6_time(t, fm6_carrier((uint32_t)ed[FV_ALG], k + 1u)));
    }
    if (fresh || ed[FV_OKS])
        s->fb[0] = s->fb[1] = 0;
    s->pitch = FM6_PEG_STEP[ed[FV_PL + 3]] * (1 << 19);  /* the pitch envelope starts from L4 */
    s->pix = 0;
    s->down = 1;
    if (ed[FV_LFKS])
        fm6_lfo[p].ph = (1u << 31) - 1u;
    fm6_lfo[p].dly = 0;
}

/* the voice amplitude: the release starts the DX7 envelopes' fourth segment; once every carrier
 * has ended, or is released and 60 dB down without coming back (L4 0), the voice ends (its last
 * block fades out). The ADSR stays an overall shape */
#define FM6_END (5 << 24)                                /* 10 doublings under OUTPUT 99 */
static int32_t fm6_amp(track_t *t, voice_t *v, int32_t adsr)
{
    uint32_t p = (uint32_t)(t - trk), k, note = fm6_note(t, v), alg = (uint32_t)fm6_ed[p][FV_ALG];
    fm6_voice_t *s = fm6_state(t, v);
    const int16_t *ed = fm6_ed[p];
    int on = 0;
    if (!v->gate && s->down) {
        s->down = 0;
        s->pix = 3;
        for (k = 0; k < 6u; k++) {
            const int16_t *op = &ed[FM6_OPB(k + 1u)];
            fm6_enter(&s->eg[k], op, 3, fm6_outlevel(op, note, v->vel),
                      fm6_rscale(op, note) + fm6_time(t, fm6_carrier(alg, k + 1u)));
        }
    }
    for (k = 0; k < 6u; k++) {
        const int16_t *op = &ed[FM6_OPB(k + 1u)];
        on |= fm6_carrier(alg, k + 1u) && fm6_eg_on(&s->eg[k], op) &&
              (s->down || s->eg[k].level >= FM6_END || op[FO_L4] > 0);
    }
    if (!on) {
        v->stage = 0;
        v->active = 0;
        return 0;
    }
    return adsr;
}

static void fm6_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    uint32_t p = (uint32_t)(t - trk), note = fm6_note(t, v), alg = (uint32_t)fm6_ed[p][FV_ALG] & 31u;
    uint32_t k, i, has = 0, base, inc[6];
    uint32_t fb = (uint32_t)fm6_ed[p][FV_FB];
    fm6_voice_t *s = fm6_state(t, v);
    const int16_t *ed = fm6_ed[p];
    int32_t gain[6], pm, amod;
    int32_t mod = (t->p[P_E1] + (m->shape >> 8) - 64) * (1 << 20);   /* MOD and the SHP modulation, Q24 */
    {   /* pitch: the envelope, LFO (PMD x PMS, after the delay) and TRANSPOSE on the part's pitch */
        int32_t lfo = fm6_lfo[p].val - (1 << 23);
        uint32_t pmd = ((uint32_t)ed[FV_LPMD] * 165u) >> 6, dep = (uint32_t)fm6_lfo[p].depth;
        pm = fm6_peg_step(s, ed) + (ed[FV_TRNSP] - 24) * ((1 << 24) / 12);
        pm += (int32_t)(((int64_t)(pmd * dep) * (FM6_PMS[ed[FV_LPMS]] * lfo)) >> 39);
        amod = (int32_t)((((uint32_t)ed[FV_LAMD] * 165u >> 6) * dep) >> 8);
        amod = (int32_t)(((int64_t)amod * ((1 << 24) - fm6_lfo[p].val)) >> 24);
        {
            uint64_t b = ((uint64_t)m->inc * fm6_pow2(pm >> 8)) >> 16;
            base = b > 0x7FFFFFFFu ? 0x7FFFFFFFu : (uint32_t)b;
        }
    }
    for (k = 0; k < 6u; k++) {                           /* envelopes, gains, frequencies (index 0 = OP1) */
        const int16_t *op = &ed[FM6_OPB(k + 1u)];
        int car = fm6_carrier(alg, k + 1u);
        int32_t lv = fm6_eg_step(&s->eg[k], op, fm6_outlevel(op, note, v->vel),
                                 fm6_rscale(op, note) + fm6_time(t, car), s->down);
        if (amod && op[FO_AMS]) {                        /* AMS: the LFO takes away a part of the level */
            int32_t x = (int32_t)(((uint64_t)(uint32_t)amod * FM6_AMS[op[FO_AMS]]) >> 32);   /* Q16 */
            lv -= (int32_t)(((int64_t)lv * fm6_pow2(((x * 1655) >> 8) - 419379)) >> 16);
        }
        if (!car)
            lv += mod;
        lv = clamp(lv, 0, 16 << 24);
        gain[k] = ed[FV_ON + k] ? (int32_t)fm6_pow2((lv >> 8) - (16 << 16)) : 0;   /* Q14 */
        inc[k] = op[FO_MODE] ? fm6_inc_fixed(op) : fm6_inc_ratio(base, op, note);
    }
    for (k = 0; k < 6u; k++) {                           /* the algorithm: OP6 first */
        uint32_t f = FM6_ALG[alg][k], o = 5u - k, dst = f & 3u, src = (f >> 4) & 3u;
        int add = (f & 4u) && ((has >> dst) & 1u);
        int32_t *buf = dst ? fm6_bus[dst - 1u] : fm6_sum, g0 = s->gain[o], g1 = gain[o];
        s->gain[o] = g1;
        if (g0 < FM6_THR && g1 < FM6_THR) {              /* silent: keep time, skip the work */
            s->ph[o] += inc[o] << CTL_LOG2;
            if (!(f & 4u))
                has &= ~(1u << dst);
            if (f & 0x40u)
                s->fb[0] = s->fb[1] = 0;
            continue;
        }
        if (src && !((has >> src) & 1u))
            src = 0;
        if ((f & 0x40u) && fb && !src)
            s->ph[o] = fm6_op_fb(buf, s->fb, s->ph[o], inc[o], g0, g1, add, 9u - fb);
        else
            s->ph[o] = fm6_op(buf, src ? fm6_bus[src - 1u] : fm6_zero, s->ph[o], inc[o], g0, g1, add);
        has |= 1u << dst;
    }
    if (!(has & 1u))
        return;
    for (i = 0; i < n; i++)                              /* a carrier at full level: a full-scale sine */
        out[i] += mulq15(mulq15(clamp(fm6_sum[i] >> 10, -65535, 65535), amp_at(m, i)), VOICE_FS);
}

/* per part and block: VOICE changed (or the buffer was never loaded) -> load it; the LFO */
static void fm6_block(track_t *t)
{
    uint32_t p = (uint32_t)(t - trk), want = (uint32_t)clamp(t->p[P_E0], 0, FM6_NVOICE - 1);
    if (fm6_cur[p] != (int32_t)want + 1)
        fm6_load(p, want);
    fm6_lfo_block(p, fm6_ed[p]);
}

/* ------------------------------------------------------------ the engine --- */
static const char *const N_FM6V[] = {
    "R01", "R02", "R03", "R04", "R05", "R06", "R07", "R08", "R09", "R10", "R11", "R12", "R13", "R14", "R15", "R16",
    "U01", "U02", "U03", "U04", "U05", "U06", "U07", "U08", "U09", "U10", "U11", "U12", "U13", "U14", "U15", "U16",
    "U17", "U18", "U19", "U20", "U21", "U22", "U23", "U24", "U25", "U26", "U27", "U28", "U29", "U30", "U31", "U32"};
_Static_assert(sizeof N_FM6V / sizeof N_FM6V[0] == FM6_NVOICE, "FM6: a VOICE name per voice");

/* the ADSR opens at once and rings 10 s: the DX7 envelopes shape the sound and end the voice */
static const preset_t FM6_PRESETS[] = {
    /* VOICE MOD M.TIM C.TIM - - - - */
    {"TINE EP", {0, 0, 0, 0, 0, 0, 0, 0}, {0, 127, 127, 127}, 0, 0, FX(0, 50, 25, 35), PAT(6)},
    {"BRASS SECT", {1, 0, 0, 0, 0, 0, 0, 0}, {0, 127, 127, 127}, 0, 0, FX(0, 20, 20, 40), PAT(6)},
    {"SOLID BASS", {2, 0, 0, 0, 0, 0, 0, 0}, {0, 127, 127, 127}, 0, 1, FX(0, 0, 10, 10), PAT(2)},
    {"BELLS", {3, 0, 0, 0, 0, 0, 0, 0}, {0, 127, 127, 127}, 0, 0, FX(0, 0, 30, 70), PAT(7)},
    {"MARIMBA", {4, 0, 0, 0, 0, 0, 0, 0}, {0, 127, 127, 127}, 0, 0, FX(0, 0, 25, 40), PAT(3)},
    {"CLAVINET", {5, 0, 0, 0, 0, 0, 0, 0}, {0, 127, 127, 127}, 0, 0, FX(10, 20, 30, 20), PAT(6)},
    {"DRAWBARS", {6, 0, 0, 0, 0, 0, 0, 0}, {0, 127, 127, 127}, 0, 0, FX(10, 40, 0, 30), PAT(6)},
    {"STRINGS", {7, 0, 0, 0, 0, 0, 0, 0}, {0, 127, 127, 127}, 0, 0, FX(0, 60, 30, 70), PAT(5)},
    {"GLASS PAD", {8, 0, 0, 0, 0, 0, 0, 0}, {0, 127, 127, 127}, 0, 0, FX(0, 60, 40, 80), PAT(5)},
    {"SYNC LEAD", {9, 0, 0, 0, 0, 0, 0, 0}, {0, 127, 127, 127}, 0, 1, FX(10, 20, 40, 30), PAT(6)},
    {"HARP", {10, 0, 0, 0, 0, 0, 0, 0}, {0, 127, 127, 127}, 0, 0, FX(0, 30, 30, 60), PAT(7)},
    {"KALIMBA", {11, 0, 0, 0, 0, 0, 0, 0}, {0, 127, 127, 127}, 0, 0, FX(0, 0, 35, 45), PAT(3)},
    {"FLUTE", {12, 0, 0, 0, 0, 0, 0, 0}, {0, 127, 127, 127}, 0, 1, FX(0, 20, 30, 50), PAT(5)},
    {"STEEL DRUM", {13, 0, 0, 0, 0, 0, 0, 0}, {0, 127, 127, 127}, 0, 0, FX(0, 0, 30, 40), PAT(3)},
    {"SAW BASS", {14, 0, 0, 0, 0, 0, 0, 0}, {0, 127, 127, 127}, 0, 1, FX(20, 0, 10, 10), PAT(2)},
    {"TUBULAR", {15, 0, 0, 0, 0, 0, 0, 0}, {0, 127, 127, 127}, 0, 0, FX(0, 0, 30, 80), PAT(7)},
};

static const engine_t ENG_FM6 = {
    "FM6", {"PATCH", "-"},
    {
        {"VOICE", F_ENUM, 0, FM6_NVOICE - 1, 0, N_FM6V, 0},
        {"MOD", F_BIPCT, -64, 63, 0, 0, 0},
        {"M.TIM", F_BIPCT, -64, 63, 0, 0, 0},
        {"C.TIM", F_BIPCT, -64, 63, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},
    },
    FM6_PRESETS, sizeof(FM6_PRESETS) / sizeof(FM6_PRESETS[0]), -1, fm6_note_on, fm6_render,
    0x5D7F, {P_E1, P_E2, P_E3, P_E0}, 0, fm6_amp, 0, fm6_block, 1,
};

/* ------------------------------------------------------ DX7 SysEx in --- */
/* Frames that start F0 43 (Yamaha), collected by the USB ISR (usb.c sysex_byte) for the main
 * loop (fm6_store.c fm6_service): a 32-voice dump is the longest. One frame at a time; a frame
 * arriving while one is waiting is dropped */
#define FM6_RX 4104u
static uint8_t fm6_rx[FM6_RX] __attribute__((section(".pool")));
static uint32_t fm6_rx_n;
static volatile uint8_t fm6_rx_ready;
static uint8_t fm6_rx_on;

static void fm6_sx_byte(uint8_t b)
{
    if (b == 0xF0) {
        fm6_rx_on = !fm6_rx_ready;
        if (fm6_rx_on)
            fm6_rx_n = 0;                               /* a pending frame belongs to the main loop */
    }
    if (!fm6_rx_on)
        return;
    if (fm6_rx_n >= FM6_RX || (fm6_rx_n == 1u && b != 0x43)) {
        fm6_rx_on = 0;                                   /* too long, or not Yamaha */
        return;
    }
    fm6_rx[fm6_rx_n++] = b;
    if (b == 0xF7) {
        fm6_rx_on = 0;
        RING_PUBLISH();
        fm6_rx_ready = 1;
    }
}
