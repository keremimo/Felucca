/* SPDX-License-Identifier: GPL-3.0-only */
/* CZ-1 is a separate engine: native Casio tone data, never PHASE knob conversion.
 * Envelope state and note reset utilities are shared with the phase family. */
#include "cz_native.c"
static int (*cz_user_bank_read)(uint32_t,uint32_t,uint8_t *);
static uint16_t cz_user_pick[NTRK];
static const char *const N_CZ_BANK[]={"A","B","C","D","E","F","G","H"};
static void cz_track_accept(track_t *t){cz_user_pick[(uint32_t)(t-trk)%NTRK]=(uint16_t)(t->p[P_E0]*17+t->p[P_E1]);}
static void cz_bank_poll(void);
static int cz_native_done(track_t *t, voice_t *v)
{
    cz_voice_t *c = cz_voice(t, v);
    const uint8_t *b = cz_patch[(uint32_t)(t - trk) % NTRK].raw;
    uint32_t ls = b[0] & 3u;
    uint32_t a = ls == 1u ? 1u : 0u, z = ls >= 2u ? 1u : a;
    return c->eg[a][2].stage > (b[CZ_ENV_END[a][2]] & 7u) &&
        c->eg[z][2].stage > (b[CZ_ENV_END[ls == 2u ? 0u : z][2]] & 7u);
}
static const preset_t CZ_PRESETS[] = {
    {"INIT TONE", {0, 0, 0, 0, 0, 0, 0, CZ_NATIVE}, {0, 70, 127, 60}, 0, 0, FX(0, 0, 0, 0), PAT(1)},
};
static const engine_t ENG_CZ = {
    .name = "CZ-1", .page_title = {"CZ-1", "TONE"},
    .edit = {
        {"BANK", F_ENUM, 0, 7, 0, N_CZ_BANK, 0}, {"PTCH", F_INT, 0, 16, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0}, {"TONE", F_INT, CZ_NATIVE, CZ_NATIVE, CZ_NATIVE, 0, 0},
    },
    .presets = CZ_PRESETS, .npresets = NELEM(CZ_PRESETS),
    .ownenv = 1, .done = cz_native_done, .keep = 0x0fu,
    .note_on = phase_note_on, .render = cz_native_render,
    .knob = {P_E0, P_E1, P_E7, P_E7},
};
