/* SPDX-License-Identifier: GPL-3.0-only */
/* MIDI queue -> actual scale mapping, live recording, arp and voices. */
#include <assert.h>
#define main hostsim_main
#include "hostsim.c"
#undef main

static void reset(void)
{
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    memset(midi_notes, 0, sizeof midi_notes);
    memset(midi_ch, 0, sizeof midi_ch);
    memset(midi_owners, 0, sizeof midi_owners);
    memset(live_refs, 0, sizeof live_refs);
    memset(&um, 0, sizeof um);
    host_tracks_init();
    fm1_in.notes = kb_prev = 0;
    mi_w = mi_r = mo_w = mo_r = 0;
    panic_req = transport_req = 0;
    usb.config = 1;
}

static void send(uint32_t status, uint32_t note, uint32_t vel)
{
    midi_in_q[mi_w++ % MQ] = (status >> 4) | status << 8 | note << 16 | vel << 24;
    events_block(0);
}

static int gated(const track_t *t, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].gate && t->v[i].note == note) return 1;
    return 0;
}

static void mapping_test(void)
{
    static const int8_t DEGREE[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};
    uint32_t s, root, note;
    int trans;
    reset();
    trk[0].p[P_QUANT] = Q_WHITE;
    song.octave = 3;                      /* external pitches ignore the panel octave */
    for (s = 0; s <= (uint32_t)TP[P_SCALE].max; s++)
        for (root = 0; root < 12; root++)
            for (trans = -24; trans <= 24; trans += 24) {
                trk[0].p[P_SCALE] = (int16_t)s;
                trk[0].p[P_ROOT] = (int16_t)root;
                trk[0].p[P_TRANS] = (int16_t)trans;
                for (note = 0; note < 128; note++) {
                    int degree = DEGREE[note % 12], offset = 0;
                    uint32_t got = midi_map(&trk[0], note);
                    if (degree < 0) { assert(got == KB_SILENT); continue; }
                    degree += ((int)note / 12 - 5) * 7;
                    /* Independent oracle: walk semitones from the root until
                     * the requested number of scale notes has been passed. */
                    while (degree) {
                        int dir = degree > 0 ? 1 : -1;
                        offset += dir;
                        if (SCALE_MASK[s] & (1u << ((offset % 12 + 12) % 12))) degree -= dir;
                    }
                    assert(got == (uint32_t)clamp(60 + (int)root + trans + offset, 0, 127));
                }
            }
    trk[0].p[P_QUANT] = 0;
    for (note = 0; note < 128; note++) assert(midi_map(&trk[0], note) == note);
    trk[0].p[P_QUANT] = Q_SNAP;           /* SNAP is a panel-keyboard mode: MIDI passes through */
    for (note = 0; note < 128; note++) assert(midi_map(&trk[0], note) == note);
    TDRUM->p[P_QUANT] = Q_WHITE;
    for (note = 0; note < 128; note++) assert(midi_map(TDRUM, note) == note);
    trk[0].engine = trk[0].eng_req = 4;
    trk[0].p[P_QUANT] = Q_WHITE;
    if (drum_set() >= 0) {
        trk[0].p[P_E0] = (int16_t)drum_set();
        for (note = 0; note < 128; note++) assert(midi_map(&trk[0], note) == note);
    }
    puts("MIDI scales: 16 scales, 12 roots, all 128 input notes, transpose, octave isolation, SNAP and bypasses ok");
}

static void all_mapping_test(void)
{
    uint32_t s, root, note;
    int trans;
    reset();
    trk[0].p[P_QUANT] = Q_ALL;
    song.octave = -3;                      /* panel octave never shifts external MIDI */
    for (s = 0; s <= (uint32_t)TP[P_SCALE].max; s++)
        for (root = 0; root < 12; root++)
            for (trans = -24; trans <= 24; trans += 12) {
                int previous = -1;
                trk[0].p[P_SCALE] = (int16_t)s;
                trk[0].p[P_ROOT] = (int16_t)root;
                trk[0].p[P_TRANS] = (int16_t)trans;
                for (note = 0; note < 128; note++) {
                    int degree = (int)note - 60, offset = 0, want;
                    uint32_t actual = midi_map(&trk[0], note);
                    while (degree) {
                        int dir = degree > 0 ? 1 : -1;
                        offset += dir;
                        if (SCALE_MASK[s] & (1u << ((offset % 12 + 12) % 12))) degree -= dir;
                    }
                    want = 60 + (int)root + trans + offset;
                    if (want < 0 || want > 127) {
                        assert(actual == KB_SILENT);
                    } else {
                        assert(actual == (uint32_t)want && want > previous);
                        previous = want;
                    }
                }
            }
    TDRUM->p[P_QUANT] = Q_ALL;
    for (note = 0; note < 128; note++) assert(midi_map(TDRUM, note) == note);
    trk[0].engine = trk[0].eng_req = 4;
    if (drum_set() >= 0) {
        trk[0].p[P_E0] = (int16_t)drum_set();
        for (note = 0; note < 128; note++) assert(midi_map(&trk[0], note) == note);
    }
    puts("MIDI ALL: all 128 keys, 16 scales, 12 roots and transpose; no duplicate pitches or clamped endpoints");
}

static void routing_test(void)
{
    uint32_t ch;
    reset();
    song.sel = 2;
    for (ch = 0; ch < NPART; ch++) {
        trk[ch].p[P_QUANT] = Q_WHITE;
        trk[ch].p[P_SCALE] = 2;
        trk[ch].p[P_ROOT] = (int16_t)ch;
    }
    for (ch = 0; ch < 16; ch++) {
        uint32_t track = ch == 9 ? TRK_DRUM : ch < NPART ? ch : 2;
        uint32_t note = track == TRK_DRUM ? 64 : 63 + track;
        send(0x90 | ch, 64, 99);
        assert(midi_notes[ch][64] == ((track + 1) << 8 | note));
        assert(live_refs[track][note] == 1);
        send(0x80 | ch, 64, 0);
        assert(live_refs[track][note] == 0 && midi_notes[ch][64] == 0);
    }
    assert(mo_w == 0);                     /* do not echo external input into a MIDI loop */
    puts("MIDI scales: all channels keep part/drum/selected-track routing; no MIDI echo");
}

static void release_test(void)
{
    track_t *t = &trk[0];
    reset();
    t->p[P_QUANT] = Q_WHITE; t->p[P_SCALE] = 2;
    song.playing = song.rec = 1;
    send(0x93, 64, 115);                    /* channel 4 follows the selected track */
    assert(gated(t, 63));
    assert(t->step[0].n == 1 && t->step[0].note[0] == 63 && t->step[0].vel == 115);
    assert(t->step[0].flags & SF_ACCENT);
    song.sel = 1;
    t->p[P_SCALE] = 9; t->p[P_ROOT] = 11; t->p[P_TRANS] = 12; t->p[P_QUANT] = 0;
    send(0x93, 64, 0);                     /* velocity-zero note-on is note-off */
    assert(!gated(t, 63) && live_refs[0][63] == 0);
    reset();
    t->p[P_QUANT] = Q_WHITE; t->p[P_SCALE] = 2;
    send(0x90, 61, 100);                   /* muted black key does not record or sound */
    assert(midi_notes[0][61] == 0 && !gated(t, 60) && !gated(t, 61));
    t->p[P_QUANT] = 0;
    send(0x90, 61, 0);
    assert(live_refs[0][61] == 0);
    t->p[P_QUANT] = Q_WHITE;
    send(0x90, 64, 100);
    t->p[P_ROOT] = 2;
    send(0x90, 64, 90);                    /* repeated on replaces its old pitch */
    assert(!gated(t, 63) && gated(t, 65) && live_refs[0][65] == 1);
    song.g[G_DRCH] = 1;                   /* drum-channel reassignment cannot misroute release */
    send(0x80, 64, 0);
    assert(!gated(t, 65));
    puts("MIDI scales: velocity/recording, muted keys, repeated notes and release after settings/routing changes ok");
}

static void overlap_test(void)
{
    uint32_t arp;
    for (arp = 0; arp < 2; arp++) {
        reset();
        trk[0].p[P_QUANT] = Q_WHITE;
        trk[0].p[P_ROOT] = 11;
        trk[0].p[P_TRANS] = 24;
        trk[0].p[P_AMODE] = (int16_t)arp;
        send(0x90, 125, 100);
        send(0x90, 127, 100);              /* both clamp to 127 */
        assert(live_refs[0][127] == 2);
        if (arp) assert(trk[0].nheld == 1 && trk[0].arp_phys == 2);
        send(0x80, 125, 0);
        assert(live_refs[0][127] == 1 && gated(&trk[0], 127));
        if (arp) assert(trk[0].nheld == 1 && trk[0].arp_phys == 1);
        send(0x80, 127, 0);
        assert(!gated(&trk[0], 127));
        if (arp) assert(trk[0].nheld == 0 && trk[0].arp_phys == 0);
        reset();
        trk[0].p[P_AMODE] = (int16_t)arp;
        fm1_in.notes = 1u << 7;            /* local C4 plus MIDI C4 */
        events_block(0);
        send(0x90, 60, 100);
        send(0x93, 60, 100);               /* another channel, same selected track */
        assert(live_refs[0][60] == 3);
        send(0x80, 60, 0);
        send(0x83, 60, 0);
        assert(live_refs[0][60] == 1 && gated(&trk[0], 60));
        fm1_in.notes = 0;
        events_block(0);
        assert(!gated(&trk[0], 60));
        if (arp) assert(trk[0].arp_phys == 0 && trk[0].nheld == 0);
    }
    puts("MIDI scales: clamped pitches and overlapping local/MIDI/channel notes release only on the last key-up");
}

static void panic_test(void)
{
    reset();
    send(0x90, 60, 100);
    fm1_in.notes = 1u << 7;
    events_block(0);
    panic_req = 1;
    events_block(0);
    assert(midi_notes[0][60] == 0 && live_refs[0][60] == 0 && !gated(&trk[0], 60));
    send(0x93, 60, 100);
    send(0x80, 60, 0);                     /* stale MIDI/local releases must not stop a new note */
    fm1_in.notes = 0;
    events_block(0);
    assert(live_refs[0][60] == 1 && gated(&trk[0], 60));
    send(0x83, 60, 0);
    assert(!gated(&trk[0], 60));
    puts("MIDI scales: preset/project panic clears mappings and stale releases cannot cut off new notes");
}

static void all_events_test(void)
{
    uint32_t arp;
    for (arp = 0; arp < 2; arp++) {
        reset();
        trk[0].p[P_QUANT] = Q_ALL;
        trk[0].p[P_SCALE] = 2;
        trk[0].p[P_AMODE] = (int16_t)arp;
        song.playing = song.rec = 1;
        send(0x90, 61, 113);               /* black C# -> D */
        assert(midi_notes[0][61] == 0x013E && live_refs[0][62] == 1);
        assert(gated(&trk[0], 62));
        assert(trk[0].step[0].n == 1 && trk[0].step[0].note[0] == 62);
        assert(trk[0].step[0].vel == 113);
        trk[0].p[P_QUANT] = Q_WHITE;
        song.sel = 1;
        send(0x90, 61, 0);
        assert(!gated(&trk[0], 62) && !live_refs[0][62] && !trk[0].nheld);
        send(0x90, 61, 100);               /* WHITE black key muted, even if released in ALL */
        trk[0].p[P_QUANT] = Q_ALL;
        send(0x80, 61, 0);
        assert(!live_refs[0][62]);
        send(0x90, 127, 100);              /* out-of-range degree must not sound/record */
        assert(!midi_notes[0][127] && !live_refs[0][127] && trk[0].step[0].n == 1);
        send(0x80, 127, 0);
        assert(mo_w == 0);
    }
    puts("MIDI ALL: black-key voices/arp/recording, mode changes, velocity-zero release and range limits ok");
}

static void trs_scale_test(void)
{
    static const uint8_t bytes[] = {0x90, 64, 100, 61, 100, 61, 0, 64, 0};
    uint32_t i;
    reset();
    trk[0].p[P_QUANT] = Q_WHITE; trk[0].p[P_SCALE] = 2;
    for (i = 0; i < 3; i++) um_byte(bytes[i]);
    events_block(0);
    assert(gated(&trk[0], 63));
    for (; i < sizeof bytes; i++) um_byte(bytes[i]);
    events_block(0);
    assert(!gated(&trk[0], 63) && !live_refs[0][63] && !live_refs[0][61]);
    reset();
    trk[0].p[P_QUANT] = Q_ALL; trk[0].p[P_SCALE] = 2;
    for (i = 0; i < 5; i++) um_byte(bytes[i]); /* E -> G, C# -> D (running status) */
    events_block(0);
    assert(gated(&trk[0], 67) && gated(&trk[0], 62));
    assert(midi_notes[0][64] == 0x0143 && midi_notes[0][61] == 0x013E);
    trk[0].p[P_QUANT] = Q_WHITE;
    for (; i < sizeof bytes; i++) um_byte(bytes[i]);
    events_block(0);
    assert(!gated(&trk[0], 67) && !gated(&trk[0], 62));
    assert(!live_refs[0][67] && !live_refs[0][62]);
    puts("MIDI scales: TRS parser/running status uses the same mapping and release path");
}

static void mapped_sustain_test(void)
{
    reset();
    trk[0].p[P_QUANT] = Q_WHITE;
    trk[0].p[P_SCALE] = 2;
    send(0xB4, 64, 127);               /* sustain on a selected-track channel */
    send(0x94, 64, 100);               /* E maps to E-flat in natural minor */
    assert(midi_notes[4][64] == 0x013F && live_refs[0][63] == 1);
    song.sel = 1;
    trk[0].p[P_ROOT] = 5;
    send(0x84, 64, 0);
    assert(gated(&trk[0], 63) && midi_notes[4][64] & MIDI_PEDAL_NOTE);
    send(0xB4, 64, 0);
    assert(!gated(&trk[0], 63) && !live_refs[0][63] && !midi_notes[4][64]);
    puts("MIDI scales: sustain releases original mapped pitch after track/scale changes");
}

int main(void)
{
    mapping_test(); all_mapping_test(); routing_test(); release_test(); overlap_test(); panic_test();
    all_events_test(); trs_scale_test(); mapped_sustain_test();
    return 0;
}
