/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* OBXF: OB-Xf's synth (an OB-X / OB-Xd style polysynth) in a Melodee part. The synthesis is obxf_core.c; this file
 * is the Melodee engine around it.
 *
 * The patch is the sound: every track has one (obxf_patch: OB-Xf's parameters, OX_*), edited on the device (the
 * OBXF pages after EDIT 2), loaded from PTCH (OB-Xf's factory patches, CC0, imported by tools/obxf_import.py) and
 * saved inside projects. On the device the EDIT values are macros on top of it, neutral at 0 (then the track
 * plays the patch as OB-Xf does):
 *   CUT  the cutoff (the whole range), RES / ENV the resonance / filter envelope amount (-50 .. +50 %),
 *   ATK DEC REL  the envelope times of both envelopes (x 1/16 .. x 16), DTN osc 2's detune, PTCH the patch
 * Voices: OB-Xf's polyphony, unison and voice count are the patch's. A Melodee voice plays the unison voices of
 * a key (obxf_k of them; OB-Xf's unison stacks voices on one key), the part plays up to 8 of them in all: its
 * voice cap (engine_t.cap) is 8 / k keys. A unison patch with more keys than that keeps 4 keys of 2 voices (or
 * 2 of 4), its level made up for the voices it lost. The track's FLT moves the cutoff, SHP the pulse width,
 * PIT the pitch (glide, LFO, the matrix, UNISON's spread); the patch's envelopes are the voice's amplitude and
 * end it (engine_t.ownenv). Its state lives in the part's engine state (engines.c eng_state). */
#include "obxf_core.c"

#define ENGI_OBXF 14u            /* engines.c ENGINES[] (append-only) */

/* ------------------------------------------------------------ the patch --- */
/* every parameter: id, OB-Xf's name (the patch files' and tools/obxf_import.py's), the label, C (continuous:
 * stored as v * 16256, edited on the UI range) or D (a switch or a choice: stored as its value - lo), the UI
 * format and range, the init patch's value (C: normalized as OB-Xf has it; D: the value) */
#define OXF_PARAMS(X) \
    X(O1P, "Osc1Pitch", "PIT1", C, F_SEMI, -24, 24, 0.5f, 0) \
    X(O2P, "Osc2Pitch", "PIT2", C, F_SEMI, -24, 24, 0.5f, 0) \
    X(DET, "Osc2Detune", "DTUN", C, F_PCT, 0, 127, 0.f, 0) \
    X(KEY2, "Osc2Keytrack", "O2KEY", D, F_ONOFF, 0, 1, 1, 0) \
    X(SAW1, "Osc1SawWave", "SAW1", D, F_ONOFF, 0, 1, 1, 0) \
    X(PUL1, "Osc1PulseWave", "PUL1", D, F_ONOFF, 0, 1, 0, 0) \
    X(SAW2, "Osc2SawWave", "SAW2", D, F_ONOFF, 0, 1, 1, 0) \
    X(PUL2, "Osc2PulseWave", "PUL2", D, F_ONOFF, 0, 1, 0, 0) \
    X(PW, "OscPW", "PW", C, F_PCT, 0, 127, 0.f, 0) \
    X(PW2, "Osc2PWOffset", "PW2", C, F_PCT, 0, 127, 0.f, 0) \
    X(XMOD, "OscCrossmod", "XMOD", C, F_PCT, 0, 127, 0.f, 0) \
    X(SYNC, "OscSync", "SYNC", D, F_ONOFF, 0, 1, 0, 0) \
    X(EPIT, "EnvToPitchAmount", "PENV", C, F_PCT, 0, 127, 0.f, 0) \
    X(EPB, "EnvToPitchBothOscs", "P1+2", D, F_ONOFF, 0, 1, 1, 0) \
    X(EPI, "EnvToPitchInvert", "P.INV", D, F_ONOFF, 0, 1, 0, 0) \
    X(EPW, "EnvToPWAmount", "WENV", C, F_PCT, 0, 127, 0.f, 0) \
    X(EWB, "EnvToPWBothOscs", "W1+2", D, F_ONOFF, 0, 1, 1, 0) \
    X(EWI, "EnvToPWInvert", "W.INV", D, F_ONOFF, 0, 1, 0, 0) \
    X(BRT, "OscBrightness", "BRITE", C, F_PCT, 0, 127, 1.f, 0) \
    X(MIX1, "Osc1Mix", "OSC1", C, F_PCT, 0, 127, 1.f, 0) \
    X(MIX2, "Osc2Mix", "OSC2", C, F_PCT, 0, 127, 0.f, 0) \
    X(RING, "RingModMix", "RING", C, F_PCT, 0, 127, 0.f, 0) \
    X(NOISE, "NoiseMix", "NOISE", C, F_PCT, 0, 127, 0.f, 0) \
    X(NCOL, "NoiseColor", "COLOR", D, F_ENUM, 0, 2, 0, N_OXF_NCOL) \
    X(CUT, "FilterCutoff", "CUT", C, F_PCT, 0, 127, 1.f, 0) \
    X(RES, "FilterResonance", "RES", C, F_PCT, 0, 127, 0.f, 0) \
    X(FAMT, "FilterEnvAmount", "ENV", C, F_PCT, 0, 127, 0.f, 0) \
    X(KTRK, "FilterKeyFollow", "KEY", C, F_PCT, 0, 127, 0.f, 0) \
    X(MODE, "FilterMode", "MODE", C, F_PCT, 0, 127, 0.f, 0) \
    X(BPB, "Filter2PoleBPBlend", "BP", D, F_ONOFF, 0, 1, 0, 0) \
    X(PUSH, "Filter2PolePush", "PUSH", D, F_ONOFF, 0, 1, 0, 0) \
    X(FOUR, "Filter4PoleMode", "POLES", D, F_ENUM, 0, 1, 0, N_OXF_POLES) \
    X(XPD, "Filter4PoleXpander", "XPNDR", D, F_ONOFF, 0, 1, 0, 0) \
    X(XPM, "FilterXpanderMode", "XMODE", D, F_ENUM, 0, 14, 0, N_OXF_XPM) \
    X(FA, "FilterEnvAttack", "ATK", C, F_PCT, 0, 127, 0.f, 0) \
    X(FD, "FilterEnvDecay", "DEC", C, F_PCT, 0, 127, 0.f, 0) \
    X(FS, "FilterEnvSustain", "SUS", C, F_PCT, 0, 127, 1.f, 0) \
    X(FR, "FilterEnvRelease", "REL", C, F_PCT, 0, 127, 0.f, 0) \
    X(FCRV, "FilterEnvAttackCurve", "CURVE", C, F_PCT, 0, 127, 0.f, 0) \
    X(FVEL, "VelToFilterEnv", "VEL", C, F_PCT, 0, 127, 0.f, 0) \
    X(FINV, "FilterEnvInvert", "INV", D, F_ONOFF, 0, 1, 0, 0) \
    X(AA, "AmpEnvAttack", "ATK", C, F_PCT, 0, 127, 0.f, 0) \
    X(AD, "AmpEnvDecay", "DEC", C, F_PCT, 0, 127, 0.f, 0) \
    X(AS, "AmpEnvSustain", "SUS", C, F_PCT, 0, 127, 1.f, 0) \
    X(AR, "AmpEnvRelease", "REL", C, F_PCT, 0, 127, 0.f, 0) \
    X(ACRV, "AmpEnvAttackCurve", "CURVE", C, F_PCT, 0, 127, 0.f, 0) \
    X(AVEL, "VelToAmpEnv", "VEL", C, F_PCT, 0, 127, 0.f, 0) \
    OXF_LFO(X, L1, "LFO1") \
    OXF_LFO(X, L2, "LFO2") \
    X(VOL, "Volume", "VOL", C, F_PCT, 0, 127, 0.5f, 0) \
    X(TRNS, "Transpose", "TRNS", D, F_SEMI, -24, 24, 0, 0) \
    X(TUNE, "Tune", "TUNE", C, F_BIPCT, -63, 63, 0.5f, 0) \
    X(PORTA, "Portamento", "GLIDE", C, F_PCT, 0, 127, 0.f, 0) \
    X(UNI, "Unison", "UNI", D, F_ONOFF, 0, 1, 0, 0) \
    X(UNIV, "UnisonVoices", "U.VCE", D, F_INT, 1, 32, 8, 0) \
    X(UDET, "UnisonDetune", "U.DTN", C, F_PCT, 0, 127, 0.25f, 0) \
    X(POLY, "Polyphony", "POLY", D, F_INT, 1, 32, 8, 0) \
    X(LEG, "EnvLegatoMode", "LEGAT", D, F_ENUM, 0, 3, 0, N_OXF_LEG) \
    X(PRIO, "NotePriority", "PRIO", D, F_ENUM, 0, 2, 0, N_OXF_PRIO) \
    X(BUP, "PitchBendUp", "BEND+", C, F_INT, 0, 48, 2.f / 48.f, 0) \
    X(BDN, "PitchBendDown", "BEND-", C, F_INT, 0, 48, 2.f / 48.f, 0) \
    X(BO2, "BendOsc2Only", "B.OS2", D, F_ONOFF, 0, 1, 0, 0) \
    X(VWAV, "VibratoWave", "VWAVE", D, F_ENUM, 0, 1, 0, N_OXF_VWAV) \
    X(VRATE, "VibratoRate", "VRATE", C, F_PCT, 0, 127, 0.3f, 0) \
    X(SPOR, "PortamentoSlop", "GLIDE", C, F_PCT, 0, 127, 0.25f, 0) \
    X(SCUT, "FilterSlop", "CUT", C, F_PCT, 0, 127, 0.25f, 0) \
    X(SENV, "EnvelopeSlop", "ENV", C, F_PCT, 0, 127, 0.25f, 0) \
    X(SLVL, "LevelSlop", "LEVEL", C, F_PCT, 0, 127, 0.25f, 0) \
    X(HQ, "HQMode", "HQ", D, F_ONOFF, 0, 1, 0, 0)
#define OXF_LFO(X, L, N) \
    X(L##RATE, N "Rate", "RATE", C, F_PCT, 0, 127, 0.5f, 0) \
    X(L##SYNC, N "TempoSync", "SYNC", D, F_ONOFF, 0, 1, 0, 0) \
    X(L##W1, N "Wave1", "SIN>T", C, F_BIPCT, -63, 63, 0.f, 0) \
    X(L##W2, N "Wave2", "SQ>SW", C, F_BIPCT, -63, 63, 0.5f, 0) \
    X(L##W3, N "Wave3", "SH>SG", C, F_BIPCT, -63, 63, 0.5f, 0) \
    X(L##PW, N "PW", "PW", C, F_PCT, 0, 127, 0.f, 0) \
    X(L##A1, N "ModAmount1", "AMT1", C, F_PCT, 0, 127, 0.f, 0) \
    X(L##A2, N "ModAmount2", "AMT2", C, F_PCT, 0, 127, 0.f, 0) \
    X(L##P1, N "ToOsc1Pitch", "PIT1", D, F_ENUM, 0, 2, 0, N_OXF_TRI) \
    X(L##P2, N "ToOsc2Pitch", "PIT2", D, F_ENUM, 0, 2, 0, N_OXF_TRI) \
    X(L##CUT, N "ToFilterCutoff", "CUT", D, F_ENUM, 0, 2, 0, N_OXF_TRI) \
    X(L##PW1, N "ToOsc1PW", "PW1", D, F_ENUM, 0, 2, 0, N_OXF_TRI) \
    X(L##PW2, N "ToOsc2PW", "PW2", D, F_ENUM, 0, 2, 0, N_OXF_TRI) \
    X(L##VOL, N "ToVolume", "VOL", D, F_ENUM, 0, 2, 0, N_OXF_TRI)

static const char *const N_OXF_NCOL[] = {"WHITE", "PINK", "RED"};
static const char *const N_OXF_POLES[] = {"12DB", "24DB"};
static const char *const N_OXF_XPM[] = {"LP4", "LP3", "LP2", "LP1", "HP3", "HP2", "HP1", "BP4", "BP2", "N2", "PH3",
                                       "HP2L1", "HP3L1", "N2LP1", "PH3L1"};
static const char *const N_OXF_TRI[] = {"OFF", "ON", "INV"};
static const char *const N_OXF_LEG[] = {"BOTH", "FILT", "AMP", "RETRG"};
static const char *const N_OXF_VWAV[] = {"SIN", "SQR"};
static const char *const N_OXF_PRIO[] = {"LAST", "LOW", "HIGH"};

#define X_ENUM(id, xml, lab, k, f, lo, hi, init, names) OX_##id,
enum { OXF_PARAMS(X_ENUM) OX_NP };
#undef X_ENUM
_Static_assert(OX_NP == 95, "OBXF patch layout: the stores and tools/obxf_import.py");
#define OXF_ONE 16256u           /* 1.0 of a continuous value (127 << 7: a 7-bit step is exact, and 0.5) */
#define OXF_NAME 12u
#define OXF_C 1u
#define OXF_D 0u

static const uint8_t OXF_KIND[OX_NP] = {
#define X_KIND(id, xml, lab, k, f, lo, hi, init, names) OXF_##k,
    OXF_PARAMS(X_KIND)
#undef X_KIND
};
/* the UI's descriptor of each value (a continuous one: its UI steps, oxf_ui / oxf_from_ui) */
static const param_desc_t OXF_PD[OX_NP] = {
#define X_DESC(id, xml, lab, k, f, lo, hi, init, names) {lab, f, lo, hi, 0, names, 0},
    OXF_PARAMS(X_DESC)
#undef X_DESC
};
static const uint16_t OXF_INIT[OX_NP] = {
#define X_INIT(id, xml, lab, k, f, lo, hi, init, names) \
    (uint16_t)(OXF_##k ? (float)(init) * (float)OXF_ONE + 0.5f : (float)(init) - (float)(lo)),
    OXF_PARAMS(X_INIT)
#undef X_INIT
};

/* a stored value <-> its UI value (a continuous one: the nearest UI step; edits land on the steps) */
static int32_t oxf_ui(uint32_t k, uint32_t v)
{
    const param_desc_t *d = &OXF_PD[k % OX_NP];
    int32_t span = d->max - d->min;
    if (!OXF_KIND[k % OX_NP])
        return clamp((int32_t)v + d->min, d->min, d->max);
    return d->min + (int32_t)(((uint32_t)span * v + OXF_ONE / 2u) / OXF_ONE);
}
static uint16_t oxf_from_ui(uint32_t k, int32_t ui)
{
    const param_desc_t *d = &OXF_PD[k % OX_NP];
    uint32_t span = (uint32_t)(d->max - d->min);
    ui = clamp(ui, d->min, d->max);
    if (!OXF_KIND[k % OX_NP])
        return (uint16_t)(ui - d->min);
    return (uint16_t)(((uint32_t)(ui - d->min) * OXF_ONE + span / 2u) / span);
}

/* the factory patches: OXF_ROM[OXF_NROM] (name, values) and OBXF_PRESETS (tools/obxf_import.py) */
typedef struct {
    char name[OXF_NAME + 1];
    uint16_t v[OX_NP];
} oxf_rom_t;
#include "eng_obxf_rom.h"
#define OXF_NSLOT OXF_NROM       /* PTCH: the factory patches */

static uint16_t obxf_patch[NTRK][OX_NP];        /* the tracks' patches (main loop writes, then obxf_pgen) */
static char obxf_name[NTRK][OXF_NAME + 1];
static volatile uint8_t obxf_pgen[NTRK];         /* +1 after each write of obxf_patch[t] */
static uint8_t obxf_slot[NTRK];                  /* the PTCH value last loaded (main loop); 0xFF = none */

static void obxf_put(uint32_t tr, uint32_t k, uint32_t v)   /* one value (an edit: the sounding notes follow) */
{
    obxf_patch[tr % NTRK][k % OX_NP] = (uint16_t)v;
    RING_PUBLISH();
    obxf_pgen[tr % NTRK]++;
}
static void obxf_put_all(uint32_t tr, const uint16_t *v, const char *name)
{
    uint32_t k;
    tr %= NTRK;
    for (k = 0; k < OX_NP; k++)
        obxf_patch[tr][k] = v[k];
    for (k = 0; k < OXF_NAME; k++)
        obxf_name[tr][k] = name && name[k] ? name[k] : (char)(name ? ' ' : "INIT        "[k]);
    obxf_name[tr][OXF_NAME] = 0;
    RING_PUBLISH();
    obxf_pgen[tr]++;
}

static void obxf_load_slot(uint32_t tr, uint32_t s)
{
    if (s < OXF_NROM)
        obxf_put_all(tr, OXF_ROM[s].v, OXF_ROM[s].name);
    else
        obxf_put_all(tr, OXF_INIT, 0);
    obxf_slot[tr % NTRK] = (uint8_t)s;
}

/* OB-Xf's polyphony and unison in the part's 8 voices: k voices a key, keys at once (the part's voice cap). A unison
 * patch with more keys than fit keeps up to 4 keys (2 voices each, or 4 for 2 keys) */
static void obxf_shape(const uint16_t *v, uint32_t *k, uint32_t *keys)
{
    uint32_t vpk = v[OX_UNI] ? v[OX_UNIV] + 1u : 1u, poly = v[OX_POLY] + 1u, n;
    if (vpk > poly)
        vpk = poly;
    if (vpk == 1u) {
        *k = 1u;
        *keys = poly < OXF_NV ? poly : OXF_NV;
        return;
    }
    n = poly / vpk;
    if (n > 4u)
        n = 4u;
    *k = vpk < OXF_NV / n ? vpk : OXF_NV / n;
    *keys = n < OXF_NV / *k ? n : OXF_NV / *k;
}

/* a sound load put a PTCH value in (a preset, a user preset, undo, an engine change): its patch, and from a
 * preset (preset: 1) the patch's voice mode, priority (OB-Xf's: polyphony 1 is mono, legato unless it retriggers) */
static void obxf_track_loaded(track_t *t, int preset)
{
    uint32_t tr = (uint32_t)(t - trk);
    const uint16_t *v;
    if (tr >= NTRK || t->eng_req != ENGI_OBXF)
        return;
    obxf_load_slot(tr, (uint32_t)clamp(t->p[P_E7], 0, OXF_NSLOT - 1));
    if (!preset)
        return;
    v = obxf_patch[tr];
    {
        uint32_t k, keys;
        obxf_shape(v, &k, &keys);
        t->p[P_VOICE] = keys > 1u ? V_POLY : v[OX_LEG] == 3u ? V_MONO : V_LEGATO;
    }
    t->p[P_PRIO] = (int16_t)(v[OX_PRIO] % 3u);
    t->p[P_GLIDE] = 0;
    t->p[P_DETUNE] = 0;
}

static void obxf_init(void)
{
    uint32_t tr;
    for (tr = 0; tr < NTRK; tr++) {
        obxf_put_all(tr, OXF_INIT, 0);
        obxf_slot[tr] = 0xFFu;
    }
}

/* main loop: PTCH turned (a knob, the editor, MIDI, motion) -> that patch */
static void obxf_poll(void)
{
    uint32_t tr;
    for (tr = 0; tr < NTRK; tr++)
        if (trk[tr].eng_req == ENGI_OBXF && trk[tr].p[P_E7] != obxf_slot[tr])
            obxf_load_slot(tr, (uint32_t)clamp(trk[tr].p[P_E7], 0, OXF_NSLOT - 1));
}

/* -------------------------------------------------------- the audio side --- */
static oxf_part_t *obxf_part(uint32_t part);     /* engines.c: the part's engine state */
#define OXP(t) obxf_part((uint32_t)((t) - trk))
static struct {                                  /* what the part's oxf_par_t was made from */
    uint8_t gen, ok, k, keys;                    /* k: voices a key, keys: the voice cap (obxf_shape) */
    int16_t e[7];
} obxf_made[NTRK];

static float oxf_n(const uint16_t *v, uint32_t k) { return (float)v[k] * (1.f / (float)OXF_ONE); }
static float oxf_tri(uint32_t i) { return i == 1u ? 1.f : i == 2u ? -1.f : 0.f; }

/* the patch through the macros -> OB-Xf's scaled parameters (SynthEngine's process*) */
static void obxf_par_make(oxf_par_t *P, const uint16_t *v, const int16_t *e)
{
    uint32_t l;
    float ts_a = oxf_exp2((float)e[3] * (1.f / 16.f)), ts_d = oxf_exp2((float)e[4] * (1.f / 16.f));
    float ts_r = oxf_exp2((float)e[5] * (1.f / 16.f));
    P->pitch1 = oxf_n(v, OX_O1P) * 48.f;
    P->pitch2 = oxf_n(v, OX_O2P) * 48.f;
    P->detune = oxf_logsc(oxf_clamp(oxf_n(v, OX_DET) + (float)e[6] * (1.f / 127.f), 0.f, 1.f), 0.001f, 0.6f, 19.f);
    P->key2 = v[OX_KEY2] != 0;
    P->saw1 = v[OX_SAW1] != 0;
    P->pul1 = v[OX_PUL1] != 0;
    P->saw2 = v[OX_SAW2] != 0;
    P->pul2 = v[OX_PUL2] != 0;
    P->pw = oxf_linsc(oxf_n(v, OX_PW), 0.f, 0.95f);
    P->pw2ofs = oxf_linsc(oxf_n(v, OX_PW2), 0.f, 0.95f);
    P->xmod = oxf_n(v, OX_XMOD) * 48.f;
    P->sync = v[OX_SYNC] != 0;
    P->env_pitch = oxf_n(v, OX_EPIT) * 40.f;
    P->ptch_both = v[OX_EPB] != 0;
    P->ptch_inv = v[OX_EPI] != 0;
    P->env_pw = oxf_linsc(oxf_n(v, OX_EPW), 0.f, 1.055555555555555f);
    P->pw_both = v[OX_EWB] != 0;
    P->pw_inv = v[OX_EWI] != 0;
    {
        float c = oxf_tan(oxf_min(oxf_linsc(oxf_n(v, OX_BRT), 7000.f, 26000.f), OXF_SR * 0.5f - 10.f) * OXF_PI * OXF_SRINV);
        P->bright_k = c / (1.f + c);
    }
    P->mix1 = oxf_n(v, OX_MIX1);
    P->mix2 = oxf_n(v, OX_MIX2);
    P->ring = oxf_n(v, OX_RING);
    P->noise = oxf_n(v, OX_NOISE);
    P->ncolor = (uint8_t)(v[OX_NCOL] % 3u);
    P->cut = oxf_clamp(oxf_n(v, OX_CUT) * 120.f + (float)e[0] * (120.f / 127.f), 0.f, 120.f);
    P->res = oxf_clamp(oxf_n(v, OX_RES) + (float)e[1] * (0.5f / 64.f), 0.f, 1.f);
    P->mode = oxf_n(v, OX_MODE);
    P->env_amt = oxf_linsc(oxf_clamp(oxf_n(v, OX_FAMT) + (float)e[2] * (0.5f / 64.f), 0.f, 1.f), 0.f, 140.f);
    P->keytrack = oxf_n(v, OX_KTRK);
    P->bp_blend = v[OX_BPB] != 0;
    P->push = v[OX_PUSH] != 0;
    P->four = v[OX_FOUR] != 0;
    P->xpander = v[OX_XPD] != 0;
    P->xp_mode = (uint8_t)(v[OX_XPM] % 15u);
    P->fa = oxf_logsc(oxf_n(v, OX_FA), 1.f, 60000.f, 900.f) * ts_a;
    P->fd = oxf_logsc(oxf_n(v, OX_FD), 1.f, 60000.f, 900.f) * ts_d;
    P->fs = oxf_n(v, OX_FS);
    P->fr = oxf_logsc(oxf_n(v, OX_FR), 1.f, 60000.f, 900.f) * ts_r;
    P->fcurve = oxf_n(v, OX_FCRV);
    P->vel_flt = oxf_n(v, OX_FVEL);
    P->inv_fenv = v[OX_FINV] ? -1.f : 1.f;
    P->aa = oxf_logsc(oxf_n(v, OX_AA), 4.f, 60000.f, 900.f) * ts_a;
    P->ad = oxf_logsc(oxf_n(v, OX_AD), 4.f, 60000.f, 900.f) * ts_d;
    P->as = oxf_n(v, OX_AS);
    P->ar = oxf_logsc(oxf_n(v, OX_AR), 8.f, 60000.f, 900.f) * ts_r;
    P->acurve = oxf_n(v, OX_ACRV);
    P->vel_amp = oxf_n(v, OX_AVEL);
    for (l = 0; l < 2u; l++) {
        uint32_t b = l ? OX_L2RATE : OX_L1RATE;
        float a1 = oxf_n(v, b + (OX_L1A1 - OX_L1RATE));
        P->lfo[l].raw = oxf_n(v, b);
        P->lfo[l].hz = oxf_logsc(P->lfo[l].raw, 0.f, 250.f, 3775.f);
        P->lfo[l].sync = v[b + (OX_L1SYNC - OX_L1RATE)] != 0;
        P->lfo[l].w1 = oxf_linsc(oxf_n(v, b + (OX_L1W1 - OX_L1RATE)), -1.f, 1.f);
        P->lfo[l].w2 = oxf_linsc(oxf_n(v, b + (OX_L1W2 - OX_L1RATE)), -1.f, 1.f);
        P->lfo[l].w3 = oxf_linsc(oxf_n(v, b + (OX_L1W3 - OX_L1RATE)), -1.f, 1.f);
        P->lfo[l].pw = oxf_n(v, b + (OX_L1PW - OX_L1RATE));
        P->lfo[l].amt1 = oxf_logsc(oxf_logsc(a1, 0.f, 1.f, 60.f), 0.f, 60.f, 10.f);
        P->lfo[l].amt2 = oxf_linsc(oxf_n(v, b + (OX_L1A2 - OX_L1RATE)), 0.f, 0.7f);
        P->lfo[l].p1 = oxf_tri(v[b + (OX_L1P1 - OX_L1RATE)]);
        P->lfo[l].p2 = oxf_tri(v[b + (OX_L1P2 - OX_L1RATE)]);
        P->lfo[l].cut = oxf_tri(v[b + (OX_L1CUT - OX_L1RATE)]);
        P->lfo[l].pw1 = oxf_tri(v[b + (OX_L1PW1 - OX_L1RATE)]);
        P->lfo[l].pw2 = oxf_tri(v[b + (OX_L1PW2 - OX_L1RATE)]);
        P->lfo[l].vol = oxf_tri(v[b + (OX_L1VOL - OX_L1RATE)]);
        P->lfo[l].avol = P->lfo[l].vol < 0.f ? -P->lfo[l].vol : P->lfo[l].vol;
    }
    P->volume = oxf_linsc(oxf_n(v, OX_VOL), 0.f, 0.3f);
    P->transpose = (int8_t)((int32_t)v[OX_TRNS] - 24);
    P->tune = oxf_n(v, OX_TUNE) * 2.f - 1.f;
    P->porta_hz = oxf_logsc(1.f - oxf_n(v, OX_PORTA), 0.14f, 250.f, 150.f);
    P->uni_det = oxf_logsc(oxf_n(v, OX_UDET), 0.001f, 1.f, 19.f);
    P->legato = (uint8_t)(v[OX_LEG] & 3u);
    P->pb_up = oxf_n(v, OX_BUP) * 48.f;
    P->pb_dn = oxf_n(v, OX_BDN) * 48.f;
    P->pb_osc2 = v[OX_BO2] != 0;
    P->vib_sq = v[OX_VWAV] != 0;
    P->vib_hz = oxf_linsc(oxf_n(v, OX_VRATE), 2.f, 12.f);
    P->slop_porta = oxf_linsc(oxf_n(v, OX_SPOR), 0.f, 0.75f);
    P->slop_cut = oxf_linsc(oxf_n(v, OX_SCUT), 0.f, 18.f);
    P->slop_env = oxf_n(v, OX_SENV);
    P->slop_lvl = oxf_linsc(oxf_n(v, OX_SLVL), 0.f, 0.67f);
}

/* a part's state as at power-on, then the patch's parameters (the smoothers at their targets: no sweep) */
static void obxf_part_init(oxf_part_t *p, uint32_t tr)
{
    p->seed = 0x2545F491u * (tr + 1u);
    p->lfo1.rng = p->seed ^ 0x9E3779B9u;
    p->lfo1.sh = p->lfo1.hist = oxf_frand(&p->lfo1.rng) * 2.f - 1.f;
    p->vib.rng = p->seed ^ 0x7F4A7C15u;
    p->lfo1.pos = p->vib.pos = OXF_LFO_BLK - 1;
    p->init = 1;
}

/* the voice keys' level made up for the unison voices obxf_k left out (random phases: as the square root) */
static float obxf_makeup(const uint16_t *v, uint32_t k)
{
    uint32_t vpk = v[OX_UNI] ? v[OX_UNIV] + 1u : 1u, poly = v[OX_POLY] + 1u;
    float r;
    if (vpk > poly)
        vpk = poly;
    if (vpk <= k)
        return 1.f;
    r = (float)vpk / (float)k;                          /* sqrt by two Newton steps from r / 2 */
    {
        float s = 0.5f * (1.f + r);
        s = 0.5f * (s + r / s);
        s = 0.5f * (s + r / s);
        return 0.5f * (s + r / s);
    }
}

#define OXF_GAIN 65536.f         /* OB-Xf's full scale -> Melodee's mix */
static float obxf_gain[NTRK];

static void obxf_block(track_t *t)
{
    uint32_t tr = (uint32_t)(t - trk), i, k, keys;
    oxf_part_t *p = OXP(t);
    const uint16_t *v = obxf_patch[tr % NTRK];
    int16_t e[7];
    int fresh = 0;
    if (tr >= NTRK)
        return;
    if (!p->init) {
        obxf_part_init(p, tr);
        obxf_made[tr].ok = 0;
        fresh = 1;
    }
    for (i = 0; i < 7u; i++)
        e[i] = t->p[P_E0 + i];
    if (!obxf_made[tr].ok || obxf_made[tr].gen != obxf_pgen[tr] || memcmp(e, obxf_made[tr].e, sizeof e)) {
        obxf_made[tr].gen = obxf_pgen[tr];
        memcpy(obxf_made[tr].e, e, sizeof e);
        obxf_made[tr].ok = 1;
        obxf_par_make(&p->par, v, e);
        obxf_shape(v, &k, &keys);
        if (k != obxf_made[tr].k) {                     /* another unison size: the voices start over */
            for (i = 0; i < OXF_NV; i++)
                p->v[i].sounding = p->v[i].gated = 0;
            obxf_made[tr].k = (uint8_t)k;
        }
        obxf_made[tr].keys = (uint8_t)keys;
        obxf_gain[tr] = p->par.volume * OXF_GAIN * obxf_makeup(v, k);
        for (i = 0; i < OXF_NV; i++)
            if (p->v[i].init)
                oxf_voice_env(&p->v[i], &p->par);
        if (fresh) {
            p->sm_cut = p->par.cut;
            p->sm_res = p->par.res;
            p->sm_mode = p->par.mode;
        }
    }
    for (i = 0, p->any = 0; i < OXF_NV; i++)
        p->any |= p->v[i].sounding;
    oxf_part_block(p, CTL, (float)t->bend_raw * (1.f / 8192.f), (float)t->mw * (1.f / 127.f), (float)song.g[G_BPM]);
}

/* engine_t.cap / units: the keys the part plays, the voice budget units of one (2 per OB-Xf voice) */
static uint32_t obxf_cap(const track_t *t)
{
    uint32_t tr = (uint32_t)(t - trk) % NTRK, k, keys;
    if (obxf_made[tr].keys)
        return obxf_made[tr].keys;
    obxf_shape(obxf_patch[tr], &k, &keys);
    return keys;
}
static uint32_t obxf_units(const track_t *t)
{
    uint32_t tr = (uint32_t)(t - trk) % NTRK, k, keys;
    if (obxf_made[tr].k)
        return 2u * obxf_made[tr].k;
    obxf_shape(obxf_patch[tr], &k, &keys);
    return 2u * k;
}

/* the Melodee voice's OB-Xf voices */
static oxf_voice_t *obxf_sub(track_t *t, voice_t *v, uint32_t *n)
{
    uint32_t k = obxf_made[(uint32_t)(t - trk) % NTRK].k, vi = (uint32_t)(v - t->v);
    if (!k)
        k = 1u;
    *n = vi * k + k <= OXF_NV ? k : 0u;
    return &OXP(t)->v[(vi * k) % OXF_NV];
}

static void obxf_start(track_t *t, voice_t *v, int held)
{
    oxf_part_t *p = OXP(t);
    uint32_t n, i;
    oxf_voice_t *s = obxf_sub(t, v, &n);
    float vel = (float)(v->mvel ? v->mvel : v->vel) * (1.f / 127.f);
    for (i = 0; i < n; i++)
        oxf_note_on(p, &s[i], v->note, vel, held);
}
static void obxf_note_on(track_t *t, voice_t *v)
{
    uint32_t n, i;
    oxf_voice_t *s = obxf_sub(t, v, &n);
    if (voice_was == 0u)                                 /* a free voice: whatever it played is gone */
        for (i = 0; i < n; i++)
            s[i].sounding = 0;
    obxf_start(t, v, voice_was == 2u);
}
static void obxf_legato(track_t *t, voice_t *v) { obxf_start(t, v, 1); }

static int obxf_done(track_t *t, voice_t *v)
{
    uint32_t n, i;
    oxf_voice_t *s = obxf_sub(t, v, &n);
    for (i = 0; i < n; i++)
        if (s[i].sounding)
            return 0;
    return 1;
}

static void obxf_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    oxf_part_t *p = OXP(t);
    uint32_t ns, i, tr = (uint32_t)(t - trk) % NTRK;
    oxf_voice_t *s = obxf_sub(t, v, &ns);
    float semi = (float)m->plog * (12.f / 16777216.f) + (float)song.g[G_TUNE] * 0.01f;
    float cut = (float)m->cutoff * (120.f / (127.f * 256.f)), pwm = (float)(m->shape - (64 << 8)) * (0.95f / 16384.f);
    float g = obxf_gain[tr] * (1.f / 32768.f), bpm = (float)song.g[G_BPM];
    for (i = 0; i < ns; i++) {
        if (!v->gate && s[i].gated)
            oxf_note_off(p, &s[i]);
        if (s[i].sounding)
            oxf_voice_render(p, &s[i], out, n, semi, cut, pwm, (float)m->amp0 * g, (float)m->amp1 * g, bpm);
    }
}

/* PTCH: a name per factory patch */
static const engine_t ENG_OBXF = {
    .name = "OBXF",
    .page_title = {"MACRO", "PATCH"},
    .edit = {
        {"CUT", F_BIPCT, -64, 63, 0, 0, 0},
        {"RES", F_BIPCT, -64, 63, 0, 0, 0},
        {"ENV", F_BIPCT, -64, 63, 0, 0, 0},
        {"ATK", F_INT, -64, 63, 0, 0, 0},
        {"DEC", F_INT, -64, 63, 0, 0, 0},
        {"REL", F_INT, -64, 63, 0, 0, 0},
        {"DTN", F_BIPCT, -64, 63, 0, 0, 0},
        {"PTCH", F_ENUM, 0, OXF_NSLOT - 1, 0, N_OXF_PATCH, 0},
    },
    .presets = OBXF_PRESETS,
    .npresets = NELEM(OBXF_PRESETS),
    .note_on = obxf_note_on,
    .render = obxf_render,
    .knob = {P_E0, P_E1, P_E2, P_E7},
    .block = obxf_block,
    .ownenv = 1,
    .done = obxf_done,
    .legato = obxf_legato,
    .cap = obxf_cap,
    .units = obxf_units,
};
