/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Parameter descriptors, formatting and the page table. */
static const char *const N_LWAVE[] = {"SIN", "TRI", "SAW", "SQR", "S&H"};
static const char *const N_AMODE[] = {"OFF", "UP", "DN", "UPDN", "RND", "ORD"};
static const char *const N_DIV[] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T"};
static const char *const N_SCALE[] = {"CHR", "MAJ", "MIN", "DOR", "MIX", "PEN", "MPEN", "HARM",
                                    "PHRY", "LYD", "LOC", "MEL", "BLUES", "WHOLE", "DIMHW", "DIMWH"};
static const char *const N_ONOFF[] = {"OFF", "ON"};
static const char *const N_QUANT[] = {"OFF", "SNAP", "WHITE", "ALL", "MPC"}; /* 1 = legacy SNAP */
static const char *const N_VOICE[] = {"POLY", "MONO", "LEG", "UNI"};   /* V_POLY .. V_UNISON */
static const char *const N_GLMODE[] = {"RATE", "TIME"};
static const char *const N_PRIO[] = {"LAST", "LOW", "HIGH"};
static const char *const N_ALLOC[] = {"ROT", "REUSE"};
static const char *const N_ORDER[] = {"NOTE", "PLAY"};
static const char *const N_CLOCK[] = {"INT", "USB", "TRS"};
static const char *const N_NOTE[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const N_DASH[] = {"--"};
static const char *const N_GO[] = {"--", "GO"};
static const char *const N_SLCR[] = {"OFF", "GATE", "STUT"};             /* SL_OFF .. SL_STUT (slicer.c) */
static const char *const N_SLDIV[] = {"1/8", "1/16", "1/32", "8T", "16T", "32T"};   /* SL_DEN */
static const char *const N_ENGNAME[] = {"ANALOG", "DIGITAL", "PHASE", "LOFI", "SAMPLE", "VOICE", "TRIO", "WHEEL", "GRAIN",
                                             "FM6",
#if MELODEE_SLICE
                                             "SLICE",
#endif
};

#define PD(l, f, mn, mx, df) {l, f, mn, mx, df, 0, 0}
#define PE(l, n, df) {l, F_ENUM, 0, (int16_t)(sizeof(n) / sizeof(n[0]) - 1), df, n, 0}

/* FM6 voice pages (eng_fm6.c): the selected part's edit buffer, DX7 ranges. The operator pages
 * show fm6_op (PRESETS knob); the STORE page writes the user bank slot fm6_slot */
static const char *const N_FM6ALG[32] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15",
                                        "16", "17", "18", "19", "20", "21", "22", "23", "24", "25", "26", "27", "28",
                                        "29", "30", "31", "32"};
static const char *const N_FMCRV[] = {"-LIN", "-EXP", "+EXP", "+LIN"};
static const char *const N_FMMODE[] = {"RATIO", "FIXED"};
static const char *const N_FMLFW[] = {"TRI", "SAWDN", "SAWUP", "SQR", "SIN", "S&H"};
static uint8_t fm6_opsel, fm6_slot;                     /* OP1..OP6 = 0..5; user slot 0..31 */
static const param_desc_t FM6_OPD[FO_N + 1] = {     /* an operator; FO_N: its switch */
    [FO_R1] = PD("R1", F_INT, 0, 99, 99), [FO_R2] = PD("R2", F_INT, 0, 99, 99),
    [FO_R3] = PD("R3", F_INT, 0, 99, 99), [FO_R4] = PD("R4", F_INT, 0, 99, 99),
    [FO_L1] = PD("L1", F_INT, 0, 99, 99), [FO_L2] = PD("L2", F_INT, 0, 99, 99),
    [FO_L3] = PD("L3", F_INT, 0, 99, 99), [FO_L4] = PD("L4", F_INT, 0, 99, 0),
    [FO_BP] = PD("BRK", F_FMNOTE, 0, 99, 39), [FO_LD] = PD("LDEP", F_INT, 0, 99, 0),
    [FO_RD] = PD("RDEP", F_INT, 0, 99, 0), [FO_LC] = PE("LCRV", N_FMCRV, 0), [FO_RC] = PE("RCRV", N_FMCRV, 0),
    [FO_RS] = PD("RSCL", F_INT, 0, 7, 0), [FO_AMS] = PD("AMS", F_INT, 0, 3, 0), [FO_KVS] = PD("VEL", F_INT, 0, 7, 0),
    [FO_OL] = PD("OUT", F_INT, 0, 99, 99), [FO_MODE] = PE("MODE", N_FMMODE, 0),
    [FO_CRS] = PD("CRS", F_FMFRQ, 0, 31, 1), [FO_FINE] = PD("FINE", F_FMFRQ, 0, 99, 0),
    [FO_DET] = PD("DTN", F_OFS, 0, 14, 7), [FO_N] = PE("ON", N_ONOFF, 1),
};
static const param_desc_t FM6_GD[FV_NAME - FV_PR] = {   /* the voice: FV_PR .. FV_TRNSP */
    PD("R1", F_INT, 0, 99, 99), PD("R2", F_INT, 0, 99, 99), PD("R3", F_INT, 0, 99, 99), PD("R4", F_INT, 0, 99, 99),
    PD("L1", F_INT, 0, 99, 50), PD("L2", F_INT, 0, 99, 50), PD("L3", F_INT, 0, 99, 50), PD("L4", F_INT, 0, 99, 50),
    PE("ALG", N_FM6ALG, 0), PD("FB", F_INT, 0, 7, 0), PE("SYNC", N_ONOFF, 1),
    PD("SPEED", F_INT, 0, 99, 35), PD("DELAY", F_INT, 0, 99, 0), PD("PMD", F_INT, 0, 99, 0), PD("AMD", F_INT, 0, 99, 0),
    PE("KSYNC", N_ONOFF, 1), PE("WAVE", N_FMLFW, 0), PD("PMS", F_INT, 0, 7, 3), {"TRNS", F_OFS, 0, 48, 24, 0, "st"},
};

static const char *const N_FMPM[] = {"PEDAL", "ON"};   /* portamento: while CC 65 is down (Dexed), always */
static const char *const N_FMDEST[] = {"-", "P", "A", "PA", "E", "PE", "AE", "PAE"};   /* pitch, amp, EG bias */
static const param_desc_t FM6_FD[FM6_NFN] = {          /* the part's DX7 functions: FN_PBUP .. FN_VNORM */
    {"BEND+", F_INT, 0, 12, 3, 0, "st"}, {"BEND-", F_INT, 0, 12, 3, 0, "st"}, PD("STEP", F_INT, 0, 12, 0),
    PE("PORTA", N_FMPM, 0), PD("TIME", F_INT, 0, 127, 0), PE("GLISS", N_ONOFF, 0),
    PD("WHEEL", F_INT, 0, 99, 99), PE("W.DEST", N_FMDEST, 1), PD("FOOT", F_INT, 0, 99, 0), PE("F.DEST", N_FMDEST, 0),
    PD("BREATH", F_INT, 0, 99, 0), PE("B.DEST", N_FMDEST, 0), PD("AFTER", F_INT, 0, 99, 0), PE("A.DEST", N_FMDEST, 0),
    PE("DX VEL", N_ONOFF, 0),
};

/* the frequency of operator fm6_opsel for its CRS and FINE columns: a ratio (CRS: its step 0.5, 1, 2 ..;
 * FINE: the whole ratio) or, FIXED, Hz (CRS: 1, 10, 100, 1000) */
static void fm6_freq_text(char *val, const char **unit, int coarse)
{
    const int16_t *op = &fm6_ed[song.sel % NPART][FM6_OPB(fm6_opsel + 1u)];
    int32_t c = op[FO_CRS], f = coarse ? 0 : op[FO_FINE];
    if (!op[FO_MODE]) {
        if (coarse && c)
            fmt_int(val, c);
        else
            fmt_fix(val, (c ? c * 100 : 50) * (100 + f) / 100, 2);
        *unit = "";
    } else {                                          /* Hz x 100 = 2^(log2(10) (COARSE % 4 + FINE / 100) + log2(100)) */
        uint32_t h = fm6_pow2(217706 * ((c & 3) * 100 + f) / 100 + 435412 - (16 << 16));
        if (h < 1000u)
            fmt_fix(val, (int32_t)h, 2);
        else if (h < 100000u)
            fmt_fix(val, (int32_t)(h / 10u), 1);
        else
            fmt_int(val, (int32_t)(h / 100u));
        *unit = "Hz";
    }
}

static const param_desc_t TP[P_COUNT] = {
    [P_LEVEL] = PD("LVL", F_DB, 0, 127, 104),
    [P_ATK] = PD("ATK", F_TIME, 0, 127, 10),
    [P_DEC] = PD("DEC", F_TIME, 0, 127, 70),
    [P_SUS] = PD("SUS", F_PCT, 0, 127, 90),
    [P_REL] = PD("REL", F_TIME, 0, 127, 60),
    [P_ED_FLT] = PD("FLT", F_BIPCT, -64, 63, 0),
    [P_ED_PIT] = PD("PIT", F_BIPCT, -64, 63, 0),
    [P_ED_SHP] = PD("SHP", F_BIPCT, -64, 63, 0),
    [P_ED_FX] = PD("FX", F_BIPCT, -64, 63, 0),
    [P_LRATE] = PD("RATE", F_LFOHZ, 0, 127, 60),
    [P_LWAVE] = PE("WAVE", N_LWAVE, 0),
    [P_LPHASE] = PD("PHS", F_INT, 0, 127, 0),
    [P_LFADE] = PD("FADE", F_TIME, 0, 127, 0),
    [P_LD_PIT] = PD("PIT", F_BIPCT, -64, 63, 0),
    [P_LD_FLT] = PD("FLT", F_BIPCT, -64, 63, 0),
    [P_LD_SHP] = PD("SHP", F_BIPCT, -64, 63, 0),
    [P_LD_AMP] = PD("AMP", F_PCT, 0, 127, 0),
    [P_AMODE] = PE("MODE", N_AMODE, 0),
    [P_ARATE] = PE("RATE", N_DIV, 2),
    [P_AOCT] = PD("OCT", F_INT, 1, 4, 1),
    [P_AGATE] = PD("GATE", F_PCT, 1, 127, 64),
    [P_ASWING] = PD("SWG", F_PCT, 0, 100, 0),
    [P_APROB] = PD("PROB", F_PCT, 0, 127, 127),
    [P_AHOLD] = PE("HOLD", N_ONOFF, 0),
    [P_AORDER] = PE("ORD", N_ORDER, 0),
    [P_ROOT] = PD("ROOT", F_NOTE, 0, 11, 0),
    [P_SCALE] = PE("SCL", N_SCALE, 0),
    [P_QUANT] = PE("QNT", N_QUANT, Q_OFF),
    [P_TRANS] = PD("TRN", F_SEMI, -24, 24, 0),
    [P_SLEN] = PD("LEN", F_STEPS, 1, NSTEP, 16),
    [P_SDIV] = PE("DIV", N_DIV, 2),
    [P_SSWING] = PD("SWG", F_PCT, 0, 100, 0),
    [P_SGATE] = PD("GATE", F_PCT, 1, 127, 64),
    [P_DIST] = PD("DST", F_PCT, 0, 127, 0),
    [P_CHOR] = PD("CHO", F_PCT, 0, 127, 0),
    [P_DLY] = PD("DLY", F_PCT, 0, 127, 0),
    [P_REV] = PD("REV", F_PCT, 0, 127, 0),
    [P_VOICE] = PE("VCE", N_VOICE, 0),
    [P_GLIDE] = PD("GLD", F_TIME, 0, 127, 0),
    [P_GLMODE] = PE("GLMOD", N_GLMODE, 0),
    [P_PRIO] = PE("PRIO", N_PRIO, 0),
    [P_ALLOC] = PE("ALLOC", N_ALLOC, 0),
    [P_DETUNE] = PD("DTUNE", F_INT, 0, 127, 40),
    [P_PAN] = PD("PAN", F_BIPCT, -64, 63, 0),
    [P_MUTE] = PE("MUTE", N_ONOFF, 0),
    [P_SLCR] = PE("SLCR", N_SLCR, 0),
    [P_SLPAT] = PD("PAT", F_INT, 1, 16, 1),        /* SL_PAT[] */
    [P_SLRATE] = PE("RATE", N_SLDIV, 1),
    [P_SLDEPTH] = PD("DEPTH", F_PCT, 0, 127, 127),
    [P_MPCDEG] = PD("DEG", F_INT, 1, 12, 1),
};

static const param_desc_t GP[G_COUNT] = {
    [G_BPM] = PD("BPM", F_BPM, 40, 240, 120),
    [G_SWING] = PD("SWG", F_PCT, 0, 100, 0),
    [G_CLOCK] = PE("CLK", N_CLOCK, 0),
    [G_TUNE] = PD("TUNE", F_INT, -50, 50, 0),
    [G_DTIME] = PE("TIME", N_DIV, 1),
    [G_DFDBK] = PD("FDBK", F_PCT, 0, 120, 60),
    [G_DCOLOR] = PD("COLR", F_PCT, 0, 127, 70),
    [G_DMIX] = PD("MIX", F_PCT, 0, 127, 90),
    [G_RSIZE] = PD("SIZE", F_PCT, 0, 127, 90),
    [G_RDAMP] = PD("DAMP", F_PCT, 0, 127, 60),
    [G_CRATE] = PD("CRT", F_LFOHZ, 0, 127, 40),
    [G_CDEPTH] = PD("CDP", F_PCT, 0, 127, 60),
    [G_MIDI] = PE("MIDI", N_DASH, 0),
    [G_USBOUT] = PE("OUT", N_DASH, 0),              /* device settings, not song.g: ui_input.c, ui_draw.c */
    [G_USBIN] = PE("IN", N_DASH, 0),
    [G_INFO] = PD("CPU", F_INT, 0, 0, 0),
    [G_SLOT] = PD("SLOT", F_INT, 1, 4, 1),
    [G_NAME] = PE("NAME", N_DASH, 0),
    [G_LOAD] = PE("LOAD", N_GO, 0),
    [G_SAVE] = PE("SAVE", N_GO, 0),
    [G_ENGSEL] = PE("ENG", N_ENGNAME, 0),
    [G_LIGHTS] = PE("KEYS", N_DASH, 0),             /* device setting, not song.g: as OUT / IN */
    [G_CLRSEQ] = PE("CLRSQ", N_GO, 0),
    [G_INITSND] = PE("INIT", N_GO, 0),
    [G_DRCH] = PD("CH", F_INT, 0, 16, 10),            /* drum part MIDI channel (GM keys), 0 = off */
    [G_DRLVL] = PD("LVL", F_INT, 0, 127, 100),
    [G_DRREV] = PD("REV", F_INT, 0, 127, 16),
};

static uint32_t scale_count(const track_t *t);          /* seq.c */
static const param_desc_t *track_desc(const track_t *t, uint32_t id)
{
    if (id == P_MPCDEG) {
        static param_desc_t degree;
        degree = TP[P_MPCDEG];
        degree.max = (int16_t)scale_count(t);
        return &degree;
    }
    if (id >= P_E0 && id <= P_E7 && is_drum(t))       /* the drum track: the kit's controls (drums.c) */
        return &DR_EDIT[id - P_E0];
    if (id >= P_E0 && id <= P_E7) {                   /* the engine asked for (t->engine follows after a fade) */
        const engine_t *e = ENGINES[t->eng_req % NENGINES];
        const param_desc_t *d = e->desc ? e->desc(t, id - P_E0) : 0;   /* a mode-dependent label / names */
        return d ? d : &e->edit[id - P_E0];
    }
    return &TP[id];
}

/* value string (<= 5 chars) and unit for a parameter value */
static void param_format(const param_desc_t *d, int32_t v, char *val, const char **unit)
{
    *unit = "";
    switch (d->fmt) {
    case F_PCT:
        fmt_int(val, (v * 100 + 63) / 127);
        *unit = "%";
        break;
    case F_BIPCT:
        fmt_int(val, v * 100 / 64);
        if (v > 0) {
            char t[8];
            fmt_int(t, v * 100 / 64);
            val[0] = '+';
            str_cpy(val + 1, t, 6);
        }
        *unit = "%";
        break;
    case F_TIME: {
        uint32_t ms10 = TIME_MS_X10[v & 127];
        if (ms10 < 100u) {
            fmt_fix(val, (int32_t)ms10, 1);
            *unit = "ms";
        } else if (ms10 < 10000u) {
            fmt_int(val, (int32_t)((ms10 + 5u) / 10u));
            *unit = "ms";
        } else {
            fmt_fix(val, (int32_t)(ms10 / 100u), 2);
            if (ms10 >= 100000u)
                fmt_fix(val, (int32_t)(ms10 / 1000u), 1);
            *unit = "s";
        }
        break;
    }
    case F_LFOHZ: {
        uint32_t h = LFO_HZ_X100[v & 127];
        if (h < 1000u)
            fmt_fix(val, (int32_t)h, 2);
        else
            fmt_fix(val, (int32_t)(h / 10u), 1);
        *unit = "Hz";
        break;
    }
    case F_CUTOFF: {
        uint32_t h = CUTOFF_HZ[v & 127];
        if (h < 1000u) {
            fmt_int(val, (int32_t)h);
            *unit = "Hz";
        } else {
            fmt_fix(val, (int32_t)(h / 100u), 1);
            *unit = "kHz";
        }
        break;
    }
    case F_DB:
        if (v <= 0) {
            str_cpy(val, "OFF", 6);
        } else {
            fmt_fix(val, LEVEL_DB_X10[v], 1);
            *unit = "dB";
        }
        break;
    case F_SEMI:
        fmt_int(val, v);
        if (v > 0) {
            char t[8];
            fmt_int(t, v);
            val[0] = '+';
            str_cpy(val + 1, t, 6);
        }
        *unit = "st";
        break;
    case F_ENUM:
        str_cpy(val, d->names[v < d->min ? d->min : v > d->max ? d->max : v], 6);
        if (d->unit)
            *unit = d->unit;
        break;
    case F_BPM:
        fmt_int(val, v);
        *unit = "BPM";
        break;
    case F_NOTE:
        str_cpy(val, N_NOTE[v % 12], 6);
        break;
    case F_ONOFF:
        str_cpy(val, N_ONOFF[v ? 1 : 0], 6);
        break;
    case F_STEPS:
        fmt_int(val, v);
        *unit = "STEP";
        break;
    case F_OFS: {                                     /* the middle of the range is 0 (DETUNE, TRANSPOSE) */
        int32_t o = v - (d->min + d->max) / 2;
        char t[8];
        fmt_int(t, o);
        val[0] = '+';
        str_cpy(o > 0 ? val + 1 : val, t, 6);
        if (d->unit)
            *unit = d->unit;
        break;
    }
    case F_FMNOTE:                                    /* DX7 break point: 0 = A-1, 39 = C4 here (MIDI 60) */
        str_cpy(val, N_NOTE[(v + 21) % 12], 6);
        fmt_int(val + str_len(val), (v + 21) / 12 - 1);
        break;
    case F_FMFRQ:                                     /* the operator's frequency: CRS its step, FINE the result */
        fm6_freq_text(val, unit, d == &FM6_OPD[FO_CRS]);
        break;
    default:
        if (d->names) {                               /* F_INT with a 0-terminated name list: the range */
            uint32_t k = 0;                           /* split evenly over the names (engine desc hooks) */
            while (d->names[k])
                k++;
            str_cpy(val, d->names[(uint32_t)(clamp(v, d->min, d->max) - d->min) * k / (uint32_t)(d->max - d->min + 1)], 6);
        } else {
            fmt_int(val, v);
        }
        if (d->unit)
            *unit = d->unit;
        break;
    }
}

/* ------------------------------------------------------------ pages --- */
enum { FAM_HOME, FAM_ENV, FAM_LFO, FAM_FX, FAM_SCL, FAM_EDIT, FAM_GLO, FAM_SAVE, FAM_ARP, FAM_SEQ, FAM_TRK,
       FAM_COUNT };
enum { SC_TRACK, SC_GLOBAL, SC_ENGINE, SC_STEP, SC_TRK,      /* SC_TRK: the TRACKS page (ui_input.c tracks_edit) */
       SC_FM6, SC_FMOP };                                      /* FM6: the voice; its operator fm6_opsel */
enum { GR_NONE, GR_ADSR, GR_LFO, GR_STEPS, GR_ARP, GR_SCALE, GR_FX, GR_ROLL, GR_BROWSE, GR_SLOTS, GR_USER, GR_TRK,
       GR_SLCR, GR_FMALG, GR_FMEG, GR_FMPEG, GR_FMSTORE, GR_MPC };

typedef struct {
    const char *title;
    uint8_t fam, scope, graph;
    uint8_t id[4];               /* param ids; 0xFF = empty slot */
} page_t;

static const page_t PAGES[] = {
    {"ENV", FAM_ENV, SC_TRACK, GR_ADSR, {P_ATK, P_DEC, P_SUS, P_REL}},
    {"ENV DEST", FAM_ENV, SC_TRACK, GR_NONE, {P_ED_FLT, P_ED_PIT, P_ED_SHP, 0xFF}},   /* (P_ED_FX: nothing reads it) */
    {"LFO", FAM_LFO, SC_TRACK, GR_LFO, {P_LRATE, P_LWAVE, P_LPHASE, P_LFADE}},
    {"LFO DEST", FAM_LFO, SC_TRACK, GR_NONE, {P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP}},
    {"FX", FAM_FX, SC_TRACK, GR_FX, {P_DIST, P_CHOR, P_DLY, P_REV}},
    {"SLICER", FAM_FX, SC_TRACK, GR_SLCR, {P_SLCR, P_SLPAT, P_SLRATE, P_SLDEPTH}},   /* drum track too */
    {"DLY", FAM_FX, SC_GLOBAL, GR_NONE, {G_DTIME, G_DFDBK, G_DCOLOR, G_DMIX}},
    {"REV/CHO", FAM_FX, SC_GLOBAL, GR_NONE, {G_RSIZE, G_RDAMP, G_CRATE, G_CDEPTH}},
    {"SCL", FAM_SCL, SC_TRACK, GR_SCALE, {P_ROOT, P_SCALE, P_QUANT, P_TRANS}},
    {"MPC", FAM_SCL, SC_TRACK, GR_MPC, {P_MPCDEG, 0xFF, 0xFF, 0xFF}},
    {"EDIT 1", FAM_EDIT, SC_ENGINE, GR_NONE, {P_E0, P_E1, P_E2, P_E3}},
    {"EDIT 2", FAM_EDIT, SC_ENGINE, GR_NONE, {P_E4, P_E5, P_E6, P_E7}},
    /* FM6 only (page_shown): STORE right after the PATCH page (keeping an imported voice), then the
     * voice and OP1..OP6 (the PRESETS knob, or EDIT + a white key, picks one) */
    {"STORE", FAM_EDIT, SC_FM6, GR_FMSTORE, {0xF0, 0xF1, 0xF2, 0xF3}},   /* SLOT STORE SEND INIT (ui_input.c) */
    {"ALGO", FAM_EDIT, SC_FM6, GR_FMALG, {FV_ALG, FV_FB, FV_OKS, FV_TRNSP}},
    {"FREQ", FAM_EDIT, SC_FMOP, GR_FMALG, {FO_CRS, FO_FINE, FO_DET, FO_MODE}},
    {"OUT", FAM_EDIT, SC_FMOP, GR_FMALG, {FO_OL, FO_KVS, FO_AMS, FO_N}},
    {"EG RATE", FAM_EDIT, SC_FMOP, GR_FMEG, {FO_R1, FO_R2, FO_R3, FO_R4}},
    {"EG LVL", FAM_EDIT, SC_FMOP, GR_FMEG, {FO_L1, FO_L2, FO_L3, FO_L4}},
    {"SCALE", FAM_EDIT, SC_FMOP, GR_FMALG, {FO_BP, FO_LD, FO_RD, FO_RS}},
    {"CURVE", FAM_EDIT, SC_FMOP, GR_FMALG, {FO_LC, FO_RC, 0xFF, 0xFF}},
    {"PITCH EG", FAM_EDIT, SC_FM6, GR_FMPEG, {FV_PR, FV_PR + 1, FV_PR + 2, FV_PR + 3}},
    {"PITCH LV", FAM_EDIT, SC_FM6, GR_FMPEG, {FV_PL, FV_PL + 1, FV_PL + 2, FV_PL + 3}},
    {"FM LFO", FAM_EDIT, SC_FM6, GR_NONE, {FV_LFW, FV_LFS, FV_LFD, FV_LFKS}},
    {"FM LFO 2", FAM_EDIT, SC_FM6, GR_NONE, {FV_LPMD, FV_LAMD, FV_LPMS, 0xFF}},
    {"VOICE", FAM_EDIT, SC_TRACK, GR_NONE, {P_VOICE, P_GLIDE, P_GLMODE, P_PRIO}},
    {"VOICE 2", FAM_EDIT, SC_TRACK, GR_NONE, {P_ALLOC, P_DETUNE, P_PAN, P_MUTE}},
    /* FM6's DX7 functions (Dexed's): pitch bend, portamento, the controllers (wheel, foot, breath,
     * aftertouch: range and target) */
    {"FM BEND", FAM_EDIT, SC_FM6, GR_NONE, {FN_PBUP, FN_PBDN, FN_PBSTEP, FN_VNORM}},
    {"FM PORTA", FAM_EDIT, SC_FM6, GR_NONE, {FN_PMODE, FN_PTIME, FN_GLISS, 0xFF}},
    {"FM WH/FT", FAM_EDIT, SC_FM6, GR_NONE, {FN_MWR, FN_MWA, FN_FCR, FN_FCA}},
    {"FM BR/AT", FAM_EDIT, SC_FM6, GR_NONE, {FN_BCR, FN_BCA, FN_ATR, FN_ATA}},
    {"GLOBAL", FAM_GLO, SC_GLOBAL, GR_NONE, {G_BPM, G_SWING, G_CLOCK, G_TUNE}},
#if MELODEE_USB_AUDIO
    {"SYSTEM", FAM_GLO, SC_GLOBAL, GR_NONE, {G_MIDI, G_USBOUT, G_USBIN, G_INFO}},   /* + Melodee Out / In on/off */
#else
    {"SYSTEM", FAM_GLO, SC_GLOBAL, GR_NONE, {G_MIDI, 0xFF, 0xFF, G_INFO}},
#endif
    {"LIGHTS", FAM_GLO, SC_GLOBAL, GR_NONE, {G_LIGHTS, 0xFF, 0xFF, 0xFF}},      /* key backlight on / off */
    {"DRUMS", FAM_GLO, SC_GLOBAL, GR_NONE, {G_DRCH, G_DRLVL, G_DRREV, 0xFF}},   /* GM kit on MIDI ch 10 */
    {"PRESETS", FAM_SAVE, SC_GLOBAL, GR_BROWSE, {0xFF, 0xFF, 0xFF, 0xFF}},   /* browser: PRESETS knob / KNOB 1 */
    {"USER", FAM_SAVE, SC_GLOBAL, GR_USER, {0xFF, 0xFF, 0xFF, 0xFF}},       /* user presets: SLOT LOAD ERASE SAVE */
    {"PROJECT", FAM_SAVE, SC_GLOBAL, GR_SLOTS, {G_SLOT, 0xFF, G_LOAD, G_SAVE}},
    {"TOOLS", FAM_SAVE, SC_GLOBAL, GR_NONE, {G_CLRSEQ, G_INITSND, 0xFF, 0xFF}},
    {"ARP", FAM_ARP, SC_TRACK, GR_ARP, {P_AMODE, P_ARATE, P_AOCT, P_AGATE}},
    {"ARP 2", FAM_ARP, SC_TRACK, GR_NONE, {P_ASWING, P_APROB, P_AHOLD, P_AORDER}},
    {"STEP", FAM_SEQ, SC_STEP, GR_ROLL, {0, 1, 2, 3}},
    {"PATTERN", FAM_SEQ, SC_TRACK, GR_STEPS, {P_SLEN, P_SDIV, P_SSWING, P_SGATE}},
    {"TRACKS", FAM_TRK, SC_TRK, GR_TRK, {0, 1, 2, 3}},   /* REC button; TRACK LEVEL LEN PAN */
};
#define NPAGES (sizeof(PAGES) / sizeof(PAGES[0]))

/* the drum track has no preset sound: it uses the global pages (not the preset pages, nor
 * TOOLS > INIT: page_desc), EDIT 1 / 2 (the kit's controls, drums.c DR_EDIT), STEP, PATTERN,
 * SLICER and TRACKS; every other page shows "DRUM TRACK" */
static int page_for_drum(const page_t *pg)
{
    if (pg->scope == SC_GLOBAL)
        return pg->graph != GR_BROWSE && pg->graph != GR_USER;
    if (pg->scope == SC_ENGINE)
        return 1;
    return pg->scope != SC_FM6 && pg->scope != SC_FMOP &&
           (pg->scope != SC_TRACK || pg->fam == FAM_SEQ || pg->graph == GR_SLCR);
}

static int fm6_shown(void) { return !is_drum(TSEL) && ENGINES[TSEL->eng_req % NENGINES] == &ENG_FM6; }

/* pages the selected track has: the FM6 pages with FM6 only; EDIT 2 not when the engine has nothing there */
static int page_shown(const page_t *pg)
{
    const engine_t *e = ENGINES[TSEL->eng_req % NENGINES];
    uint32_t k, any = 0;
    if (pg->graph == GR_MPC)
        return !is_drum(TSEL) && TSEL->p[P_QUANT] == Q_MPC;
    if (pg->scope == SC_FM6 || pg->scope == SC_FMOP)
        return fm6_shown();
    if (pg->scope != SC_ENGINE || pg->id[0] != P_E4 || is_drum(TSEL))
        return 1;
    for (k = 4; k < 8u; k++)
        any |= e->edit[k].max > e->edit[k].min;
    return (int)any;
}

static const param_desc_t *page_desc(const page_t *pg, uint32_t slot, int16_t **valp)
{
    uint32_t id = pg->id[slot];
    if (id == 0xFFu || (is_drum(TSEL) && (!page_for_drum(pg) || (pg->scope == SC_GLOBAL && id == G_INITSND)))) {
        *valp = 0;
        return 0;
    }
    if (pg->scope == SC_STEP || pg->scope == SC_TRK) {
        *valp = 0;
        return 0;
    }
    if (pg->scope == SC_FM6 || pg->scope == SC_FMOP) {
        int16_t *ed = fm6_ed[song.sel % NPART];
        *valp = 0;
        if (!fm6_shown())
            return 0;
        if (pg->scope == SC_FMOP) {
            *valp = id == FO_N ? &ed[FV_ON + fm6_opsel] : &ed[FM6_OPB(fm6_opsel + 1u) + id];
            return &FM6_OPD[id];
        }
        if (id >= FN_PBUP && id < FM6_NP) {
            *valp = &ed[id];
            return &FM6_FD[id - FN_PBUP];
        }
        if (id < FV_PR || id >= FV_NAME)
            return 0;                                 /* (STORE: ui_input.c, ui_draw.c) */
        *valp = &ed[id];
        return &FM6_GD[id - FV_PR];
    }
    if (pg->scope == SC_GLOBAL) {
        *valp = &song.g[id];
        return &GP[id];
    }
    *valp = &TSEL->p[id];
    return track_desc(TSEL, id);
}
