/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the real keyboard, recording, arp and MIDI-out paths on the host.
 * Build with the same generated headers and flags as hostsim.c. */
#include <assert.h>
#define main hostsim_main
#include "hostsim.c"
#undef main

static const uint8_t WHITE_KEYS[] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const struct { uint8_t count, notes[12]; } EXPECTED[] = {
    {12, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
    {7, {0, 2, 4, 5, 7, 9, 11}}, {7, {0, 2, 3, 5, 7, 8, 10}},
    {7, {0, 2, 3, 5, 7, 9, 10}}, {7, {0, 2, 4, 5, 7, 9, 10}},
    {5, {0, 2, 4, 7, 9}}, {5, {0, 3, 5, 7, 10}},
    {7, {0, 2, 3, 5, 7, 8, 11}}, {7, {0, 1, 3, 5, 7, 8, 10}},
    {7, {0, 2, 4, 6, 7, 9, 11}}, {7, {0, 1, 3, 5, 6, 8, 10}},
    {7, {0, 2, 3, 5, 7, 9, 11}}, {6, {0, 3, 5, 6, 7, 10}},
    {6, {0, 2, 4, 6, 8, 10}}, {8, {0, 1, 3, 4, 6, 7, 9, 10}},
    {8, {0, 2, 3, 5, 6, 8, 9, 11}},
};

static void mapping_test(void)
{
    track_t *t = &trk[0];
    uint32_t s, k, w;
    int root, oct, trans;
    assert(TP[P_SCALE].max + 1 == sizeof EXPECTED / sizeof EXPECTED[0]);
    assert(sizeof SCALE_MASK / sizeof SCALE_MASK[0] == sizeof EXPECTED / sizeof EXPECTED[0]);
    t->p[P_QUANT] = 2;
    for (s = 0; s <= (uint32_t)TP[P_SCALE].max; s++) {
        uint32_t mask = 0;
        t->p[P_SCALE] = (int16_t)s;
        for (k = 0; k < EXPECTED[s].count; k++)
            mask |= 1u << EXPECTED[s].notes[k];
        assert(scale_mask(t) == mask);
        for (root = 0; root < 12; root++)
            for (oct = -3; oct <= 3; oct++)
                for (trans = -24; trans <= 24; trans++) {
                    t->p[P_ROOT] = (int16_t)root;
                    song.octave = (int8_t)oct;
                    t->p[P_TRANS] = (int16_t)trans;
                    for (k = w = 0; k < 27u; k++) {
                        uint32_t actual = kb_map(t, k);
                        if (k == WHITE_KEYS[w]) {
                            /* Four white keys precede C4; use a positive cycle
                             * offset to independently handle the lower degrees. */
                            int d = (int)w - 4 + 12 * EXPECTED[s].count;
                            int want = 60 + root + 12 * (oct + d / EXPECTED[s].count - 12)
                                + EXPECTED[s].notes[d % EXPECTED[s].count] + trans;
                            assert(actual == (uint32_t)clamp(want, 0, 127));
                            w++;
                        } else {
                            assert(actual == KB_SILENT);
                        }
                    }
                }
    }
    t->p[P_QUANT] = 0;
    song.octave = 0;
    t->p[P_TRANS] = -5;
    for (k = 0; k < 27u; k++)
        assert(kb_map(t, k) == 48u + k);
    t->p[P_QUANT] = 2;
    for (k = 0; k < 27u; k++) {
        TDRUM->p[P_QUANT] = 2;
        assert(kb_map(TDRUM, k) == DRUM_KEYS[k]);
    }
    t->engine = t->eng_req = 4;
    if (drum_set() >= 0) {
        t->p[P_E0] = (int16_t)drum_set();
        for (k = 0; k < 27u; k++)
            assert(kb_map(t, k) == 36u + k);
    }
    t->engine = t->eng_req = 0;                /* SNAP (QNT 1, the old ON): every key, rounded down */
    t->p[P_QUANT] = 1;
    t->p[P_SCALE] = 2;                         /* C minor */
    t->p[P_ROOT] = 0;
    t->p[P_TRANS] = 0;
    song.octave = 0;
    assert(kb_map(t, 11) == 63u && kb_map(t, 10) == 63u && kb_map(t, 7) == 60u && kb_map(t, 8) == 60u);
    puts("scales: all 16 scales, 12 roots, octave/transpose ranges, bypass, drums and SNAP ok");
}

static void all_mapping_test(void)
{
    track_t *t = &trk[0];
    uint32_t s, k;
    int root, oct, trans;
    t->engine = t->eng_req = 0;
    t->p[P_QUANT] = Q_ALL;
    assert(TP[P_QUANT].max == Q_ALL);
    assert(!strcmp(TP[P_QUANT].names[Q_SNAP], "SNAP"));
    assert(!strcmp(TP[P_QUANT].names[Q_WHITE], "WHITE"));
    assert(!strcmp(TP[P_QUANT].names[Q_ALL], "ALL"));
    for (s = 0; s < sizeof EXPECTED / sizeof EXPECTED[0]; s++) {
        t->p[P_SCALE] = (int16_t)s;
        for (root = 0; root < 12; root++)
            for (oct = -3; oct <= 3; oct++)
                for (trans = -24; trans <= 24; trans++) {
                    int previous = -1;
                    t->p[P_ROOT] = (int16_t)root;
                    t->p[P_TRANS] = (int16_t)trans;
                    song.octave = (int8_t)oct;
                    for (k = 0; k < 27u; k++) {
                        int d = (int)k - 7 + 12 * EXPECTED[s].count;
                        int want = 60 + root + trans + 12 * (oct + d / EXPECTED[s].count - 12)
                            + EXPECTED[s].notes[d % EXPECTED[s].count];
                        uint32_t actual = kb_map(t, k);
                        if (want < 0 || want > 127) {
                            assert(actual == KB_SILENT);
                        } else {
                            assert(actual == (uint32_t)want && want > previous);
                            previous = want;
                        }
                    }
                }
    }
    song.octave = 0;
    TDRUM->p[P_QUANT] = Q_ALL;
    for (k = 0; k < 27u; k++) assert(kb_map(TDRUM, k) == DRUM_KEYS[k]);
    t->engine = t->eng_req = 4;
    if (drum_set() >= 0) {
        t->p[P_E0] = (int16_t)drum_set();
        for (k = 0; k < 27u; k++) assert(kb_map(t, k) == 36u + k);
    }
    puts("scales ALL: every local key, all scales/roots/octaves/transpositions; strictly unique pitches and silent range limits ok");
}

static void key_events_test(void)
{
    track_t *t = &trk[0];
    uint32_t before;
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    host_tracks_init();
    kb_prev = 0;
    usb.config = 1;
    mo_w = mo_r = 0;
    t->p[P_QUANT] = 2;
    t->p[P_SCALE] = 2;                      /* C minor: E key plays Eb */
    t->p[P_AMODE] = 1;
    song.playing = song.rec = 1;
    fm1_in.notes = 1u << 8;                /* C# is silent */
    keyboard_block();
    assert(t->arp_phys == 0 && t->nheld == 0 && t->step[0].n == 0 && mo_w == 0);
    t->p[P_QUANT] = 0;                    /* releasing a muted key stays silent */
    fm1_in.notes = 0;
    keyboard_block();
    assert(mo_w == 0);
    t->p[P_QUANT] = 2;
    fm1_in.notes = (1u << 11) | (1u << 10); /* E and D#: only Eb sounds/records */
    keyboard_block();
    assert(t->arp_phys == 1 && t->nheld == 1 && t->held[0] == 63);
    assert(t->step[0].n == 1 && t->step[0].note[0] == 63);
    assert(mo_w == 1 && ((midi_out_q[0] >> 16) & 127u) == 63);
    t->p[P_SCALE] = 9;
    t->p[P_ROOT] = 6;
    t->p[P_TRANS] = 12;
    song.octave = 1;
    song.sel = 1;                          /* key-up follows the original note/part */
    fm1_in.notes = 0;
    keyboard_block();
    assert(t->arp_phys == 0 && t->nheld == 0);
    assert(mo_w == 2 && ((midi_out_q[1] >> 16) & 127u) == 63);
    assert(((midi_out_q[1] >> 8) & 255u) == 0x80u);
    song.sel = 0;
    t->p[P_QUANT] = 0;
    fm1_in.notes = 1u << 8;                /* held black key must release after enabling mode */
    keyboard_block();
    before = mo_w;
    assert(t->arp_phys == 1);
    t->p[P_QUANT] = 2;
    fm1_in.notes = 0;
    keyboard_block();
    assert(t->arp_phys == 0 && t->nheld == 0 && mo_w == before + 1);
    puts("scales: silent keys, arp, live recording, MIDI out and held-note changes ok");
}

static void all_key_events_test(void)
{
    track_t *t = &trk[0];
    uint32_t k;
    static const uint8_t expected[] = {60, 62, 63};
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    memset(live_refs, 0, sizeof live_refs);
    host_tracks_init();
    kb_prev = 0;
    mo_w = mo_r = 0;
    usb.config = 1;
    t->p[P_QUANT] = Q_ALL;
    t->p[P_SCALE] = 2;
    t->p[P_AMODE] = 1;
    song.playing = song.rec = 1;
    fm1_in.notes = (1u << 7) | (1u << 8) | (1u << 9); /* C, C#, D -> C, D, Eb */
    keyboard_block();
    assert(t->nheld == 3 && t->arp_phys == 3 && t->step[0].n == 3 && mo_w == 3);
    for (k = 0; k < 3; k++) {
        assert(t->held[k] == expected[k] && t->step[0].note[k] == expected[k]);
        assert(((midi_out_q[k] >> 16) & 127u) == expected[k]);
    }
    t->p[P_QUANT] = Q_WHITE;              /* black key still releases its ALL pitch */
    song.sel = 1;
    fm1_in.notes = 0;
    keyboard_block();
    assert(!t->nheld && !t->arp_phys && mo_w == 6);
    for (k = 0; k < 3; k++) {
        assert(!live_refs[0][expected[k]]);
        assert(((midi_out_q[k + 3] >> 16) & 127u) == expected[k]);
    }
    puts("scales ALL: black keys feed arp, recording and MIDI out; mode/track changes preserve releases");
}

int main(void)
{
    host_tracks_init();
    mapping_test();
    all_mapping_test();
    key_events_test();
    all_key_events_test();
    return 0;
}
