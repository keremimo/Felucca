/* SPDX-License-Identifier: GPL-3.0-only */
/* Clock and transport through the production MIDI queue and sequencer. */
#include <assert.h>
#define main hostsim_main
#include "hostsim.c"
#undef main

static void reset_clock_test(uint32_t source)
{
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    memset(&midi_clock, 0, sizeof midi_clock);
    memset(&um, 0, sizeof um);
    memset(midi_ch, 0, sizeof midi_ch);
    memset(midi_notes, 0, sizeof midi_notes);
    memset(midi_owners, 0, sizeof midi_owners);
    memset(midi_in_src, 0, sizeof midi_in_src);
    host_tracks_init();
    song.g[G_CLOCK] = (int16_t)source;
    fm1_ms = 0;
    mi_w = mi_r = 0;
    transport_req = panic_req = 0;
    midi_beat_samples = 0;
    events_block(CTL);
    for (uint32_t i = 0; i < NTRK; i++) {
        trk[i].p[P_SLEN] = 16;
        trk[i].p[P_SDIV] = 2;                 /* six clocks per sixteenth step */
    }
}

static void send_clock(uint32_t source, uint32_t status, uint32_t ms)
{
    fm1_ms = ms;
    if (source == 1u)
        usb_midi_rx_packet(0x0Fu | status << 8, ms);
    else
        um_byte(status);
    events_block(CTL);
}

static void send_wheel(uint32_t source, uint32_t ms)
{
    fm1_ms = ms;
    if (source == 1u)
        usb_midi_rx_packet(0x0Bu | 0xB0u << 8 | 1u << 16 | 127u << 24, ms);
    else {
        um_byte(0xB0u);
        um_byte(1u);
        um_byte(127u);
    }
    events_block(CTL);
}

static void clock_run(uint32_t source)
{
    uint32_t i, step, rendered;
    reset_clock_test(source);
    /* The unselected input must not take the transport or tempo. */
    send_clock(source == 1u ? 2u : 1u, 0xFAu, 0u);
    assert(!song.playing);
    send_clock(source, 0xFAu, 0u);
    assert(song.playing && trk[0].seq_idx == 0);
    send_wheel(source == 1u ? 2u : 1u, 0u);
    assert(trk[0].wheel_target == 127 * 256); /* other input still handles expression */
    for (i = 0; i <= 24u; i++) {
        uint32_t ms = i * 125u / 6u;        /* 24 clocks per 500 ms = 120 BPM */
        send_clock(source, 0xF8u, ms);
    }
    assert(song.g[G_BPM] == 120);
    assert(midi_beat_samples == FS / 2u);
    for (i = 0; i < NTRK; i++)
        assert(trk[i].seq_idx == 4u);       /* all tracks entered beat two */
    rendered = midi_clock.rendered;
    fm1_ms = 510u;
    events_block(CTL);
    assert(midi_clock.rendered > rendered + 400u && midi_clock.rendered < rendered + 500u);
    step = trk[0].seq_idx;
    send_clock(source, 0xFCu, 511u);
    assert(!song.playing);
    send_clock(source, 0xFBu, 600u);
    assert(song.playing && trk[0].seq_idx == step);
    send_clock(source, 0xF8u, 600u);
    assert(trk[0].seq_idx == step);
    for (i = 1; i <= 12u; i++) {
        send_clock(source, 0xF8u, 600u + 25u * i);   /* change to 100 BPM */
        if (i == 6u)
            assert(trk[0].seq_idx == step + 1u);
    }
    assert(song.g[G_BPM] == 100 && midi_beat_samples == 3u * FS / 5u);
    assert(trk[0].seq_idx == step + 2u);
    fm1_ms = 1501u;
    events_block(CTL);
    assert(!song.playing);                  /* clock loss releases transport */
    send_clock(source, 0xFAu, 1600u);
    assert(song.playing && trk[0].seq_idx == 0u);
    printf("%s clock: transport, 120 -> 100 BPM, shared tracks, timeout ok\n",
           source == 1u ? "USB" : "TRS");
}

int main(void)
{
    clock_run(1u);
    clock_run(2u);
    return 0;
}
