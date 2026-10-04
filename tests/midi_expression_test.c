/* SPDX-License-Identifier: GPL-3.0-only */
/* Real queue -> sequencer -> voices, for USB-format packets and TRS byte input. */
#include <assert.h>
#define main hostsim_main
#include "hostsim.c"
#undef main

static int via_trs;
static int32_t render_buf[CTL];

static void reset_test(void)
{
    uint32_t i;
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    memset(midi_ch, 0, sizeof midi_ch);
    memset(midi_notes, 0, sizeof midi_notes);
    memset(midi_owners, 0, sizeof midi_owners);
    memset(live_refs, 0, sizeof live_refs);
    memset(&um, 0, sizeof um);
    memset(sl, 0, sizeof sl);
    memset(&drums, 0, sizeof drums);
    drums.set = -2;
    host_tracks_init();
    mi_r = mi_w = kb_prev = fm1_in.notes = panic_req = transport_req = 0;
    for (i = 0; i < NPART; i++) {
        host_preset(&trk[i], 0, 1);
        trk[i].p[P_VOICE] = V_POLY;
        trk[i].p[P_AMODE] = 0;
        trk[i].p[P_ATK] = 0;
        trk[i].p[P_SUS] = 127;
        trk[i].p[P_REL] = 100;
        trk[i].p[P_E0] = 3;               /* sine, no detune/noise */
        trk[i].p[P_E1] = trk[i].p[P_E3] = 0;
    }
}

static void send_midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (via_trs) {
        um_byte(st); um_byte(d1); um_byte(d2);
    } else {
        assert(mi_w - mi_r < MQ);
        midi_in_q[mi_w++ % MQ] = (st >> 4) | (st << 8) | (d1 << 16) | (d2 << 24);
    }
    events_block(CTL);
}

static int gated(uint32_t part, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (trk[part].v[i].active && trk[part].v[i].gate && trk[part].v[i].note == note)
            return 1;
    return 0;
}

static void settle(track_t *t)
{
    uint32_t i;
    for (i = 0; i < 100; i++)
        track_render(t, render_buf, CTL);
}

static void pitch_test(void)
{
    track_t *t = &trk[0];
    uint32_t p0, i;
    reset_test();
    send_midi(0x90, 69, 90);
    send_midi(0xE0, 127, 127);
    assert(t->bend_target == 512 && trk[1].bend_target == 0);
    track_render(t, render_buf, CTL);
    assert(t->bend_q8 > 0 && t->bend_q8 < 512); /* smoothed, not an abrupt jump */
    settle(t);
    p0 = t->v[0].ph[0];
    track_render(t, render_buf, CTL);
    assert(t->v[0].ph[0] - p0 == PITCH_INC[71 * 16] * CTL);
    assert(t->v[0].note == 69);                   /* note-off identity did not bend */
    send_midi(0xE0, 0, 0);
    settle(t);
    assert(t->bend_q8 == -512);
    p0 = t->v[0].ph[0];
    track_render(t, render_buf, CTL);
    assert(t->v[0].ph[0] - p0 == PITCH_INC[67 * 16] * CTL);
    send_midi(0xE0, 0, 64);
    settle(t);
    assert(t->bend_q8 == 0);
    send_midi(0xE0, 80, 64);                     /* less than 1/16 semitone: fine correction still moves pitch */
    settle(t);
    p0 = t->v[0].ph[0];
    track_render(t, render_buf, CTL);
    assert(t->v[0].ph[0] - p0 > PITCH_INC[69 * 16] * CTL);
    assert(t->v[0].ph[0] - p0 < PITCH_INC[69 * 16 + 1] * CTL);
    send_midi(0xB0, 6, 12);                      /* data entry without RPN ignored */
    assert(midi_ch[0].semis == 2);
    send_midi(0xB0, 101, 0); send_midi(0xB0, 100, 0);
    send_midi(0xB0, 6, 12); send_midi(0xB0, 38, 50);
    send_midi(0xE0, 127, 127);
    assert(t->bend_target == 3200);
    send_midi(0xB0, 101, 127); send_midi(0xB0, 100, 127);
    send_midi(0xB0, 6, 3);
    assert(midi_ch[0].semis == 12);
    send_midi(0x80, 69, 0);
    assert(!gated(0, 69));
    /* All shipping engines render held notes under fractional bends without overflow. */
    for (i = 0; i < NENGINES; i++) {
        reset_test();
        host_preset(t, i, 0);
        t->p[P_AMODE] = 0;
        send_midi(0x90, 60, 90);
        send_midi(0xE0, 33, 100);
        settle(t);
        send_midi(0x80, 60, 0);
    }
    puts("expression: pitch endpoints, smoothing, RPN, isolation, all engines ok");
}

static void wheel_test(void)
{
    track_t *t = &trk[0];
    int16_t saved[P_COUNT];
    int32_t lo = 9999, hi = -9999;
    uint32_t i;
    reset_test();
    memcpy(saved, t->p, sizeof saved);
    send_midi(0xB0, 1, 127);
    for (i = 0; i < 1400; i++) {
        int32_t p = midi_pitch_tick(t, CTL);
        if (p < lo) lo = p;
        if (p > hi) hi = p;
    }
    assert(lo < -120 && hi > 120 && lo >= -128 && hi <= 128);
    assert(!memcmp(saved, t->p, sizeof saved));
    assert(trk[1].wheel_target == 0);
    send_midi(0xB0, 1, 0);
    settle(t);
    assert(t->wheel_q8 == 0 && midi_pitch_tick(t, CTL) == 0);
    puts("expression: independent +/-50-cent wheel vibrato; presets unchanged ok");
}

static void panic_test(void)
{
    uint32_t i;
    reset_test();
    send_midi(0x90, 60, 90); send_midi(0x91, 64, 90);
    settle(&trk[0]);
    send_midi(0xB0, 123, 0);
    assert(!gated(0, 60) && trk[0].v[0].active && gated(1, 64));
    send_midi(0x90, 60, 90); send_midi(0xB0, 64, 127);
    send_midi(0xB0, 123, 0);
    assert(gated(0, 60));                      /* All Notes Off honours sustain */
    send_midi(0xB0, 120, 0);
    track_render(&trk[0], render_buf, CTL);
    for (i = 0; i < NVOICE; i++) assert(!trk[0].v[i].active);
    assert(!midi_notes[0][60] && gated(1, 64));
    send_midi(0x99, 36, 100); send_midi(0xB9, 120, 0);
    for (i = 0; i < NDRUM; i++) assert(!drums.v[i].active);
    trk[0].p[P_AMODE] = trk[0].p[P_AHOLD] = 1;
    send_midi(0x90, 65, 90); send_midi(0xB0, 120, 0);
    assert(!trk[0].nheld && !trk[0].arp_note && !trk[0].arp_phys);
    /* Reset controllers releases pedal notes but preserves physically held keys. */
    trk[0].p[P_AMODE] = trk[0].p[P_AHOLD] = 0;
    send_midi(0x90, 60, 90); send_midi(0x80, 60, 0);
    send_midi(0x90, 62, 90);
    send_midi(0xE0, 127, 127); send_midi(0xB0, 1, 127);
    send_midi(0xB0, 121, 0);
    assert(!gated(0, 60) && gated(0, 62));
    assert(!trk[0].bend_target && !trk[0].wheel_target && !midi_ch[0].pedal);
    puts("expression: CC123 release, CC120 pedal override/drums/arp, CC121 reset ok");
}

static void sustain_test(void)
{
    uint32_t mode;
    for (mode = V_POLY; mode <= V_UNISON; mode++) {
        reset_test();
        trk[0].p[P_VOICE] = mode;
        send_midi(0x90, 60, 90); send_midi(0xB0, 64, 64);
        send_midi(0x90, 64, 90); send_midi(0x80, 64, 0);
        assert(gated(0, 64));
        send_midi(0xB0, 64, 63);
        assert(gated(0, 60) && !gated(0, 64));
        send_midi(0x80, 60, 0);
        assert(!gated(0, 60));
    }
    reset_test();
    send_midi(0xB4, 64, 127); send_midi(0x94, 60, 90);
    song.sel = 1;
    send_midi(0x94, 60, 0);                   /* velocity zero, original track */
    assert(gated(0, 60));
    send_midi(0x94, 60, 90);                  /* retrigger on new selected track */
    assert(!gated(0, 60) && gated(1, 60));
    send_midi(0xB4, 64, 0);                  /* key is down: do not release it */
    assert(gated(1, 60));
    send_midi(0x84, 60, 0);
    assert(!gated(1, 60));
    /* Same pitch, two channels: releasing one must not silence the other. */
    song.sel = 0;
    send_midi(0x90, 60, 90); send_midi(0x94, 60, 90);
    send_midi(0x80, 60, 0);
    assert(gated(0, 60));
    send_midi(0x84, 60, 0); assert(!gated(0, 60));
    trk[0].p[P_AMODE] = 1;
    send_midi(0xB0, 64, 127); send_midi(0x90, 60, 90);
    send_midi(0x80, 60, 0); assert(trk[0].nheld == 1);
    send_midi(0xB0, 64, 0); assert(!trk[0].nheld && !trk[0].arp_phys);
    send_midi(0xB0, 64, 127); send_midi(0x90, 60, 90); send_midi(0x80, 60, 0);
    panic_req = 1; events_block(CTL);
    assert(!midi_notes[0][60]);
    send_midi(0xB0, 64, 0); assert(!trk[0].nheld);
    puts("expression: pedal threshold, all voice modes, repeated notes, track switches, arp, patch panic ok");
}

static void ownership_test(void)
{
    uint32_t i, first;
    for (first = 0; first < 2; first++) {
        reset_test();
        trk[0].p[P_AMODE] = 1;
        if (first) {
            fm1_in.notes = 1u << 7; keyboard_block(); /* C4 */
        }
        send_midi(0x90, 60, 90);
        if (!first) {
            fm1_in.notes = 1u << 7; keyboard_block();
        }
        assert(trk[0].nheld == 1 && trk[0].arp_phys == 2);
        send_midi(0x80, 60, 0);
        assert(trk[0].nheld == 1 && trk[0].arp_phys == 1);
        fm1_in.notes = 0; keyboard_block();
        assert(trk[0].nheld == 0 && trk[0].arp_phys == 0);
    }
    reset_test();
    send_midi(0x94, 60, 90); send_midi(0x84, 60, 0);
    song.sel = 1; send_midi(0xE4, 127, 127);
    assert(trk[0].bend_target == 0 && trk[1].bend_target == 512);
    reset_test();
    trk[0].p[P_AMODE] = 1;
    send_midi(0xB0, 64, 127);
    for (i = 0; i < 300; i++) {
        send_midi(0x90, 60, 90); send_midi(0x80, 60, 0);
        assert(midi_owners[0] == 1 && trk[0].arp_phys == 1);
    }
    send_midi(0xB0, 64, 0);
    assert(!midi_owners[0] && !trk[0].arp_phys && !midi_ch[0].targets);
    /* Full pitch range under the pedal, including voice stealing. */
    reset_test();
    send_midi(0xB0, 64, 127);
    for (i = 0; i < 128; i++) {
        send_midi(0x90, i, 90); send_midi(0x80, i, 0);
    }
    assert(midi_owners[0] == 128 && midi_ch[0].owned[0] == 128);
    send_midi(0xB0, 64, 0);
    assert(!midi_owners[0]);
    for (i = 0; i < NVOICE; i++) assert(!trk[0].v[i].gate);
    puts("expression: local/MIDI overlap, route cleanup, repeated pedal retriggers, 128-note stress ok");
}

int main(void)
{
    for (via_trs = 0; via_trs < 2; via_trs++) {
        printf("== %s\n", via_trs ? "TRS parser" : "USB-format input queue");
        pitch_test(); wheel_test(); panic_test(); sustain_test(); ownership_test();
    }
    puts("MIDI EXPRESSION TESTS PASSED");
    return 0;
}
