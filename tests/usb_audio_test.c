/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/src/usb_audio_stream.c"

static void pcm(uint8_t *p, uint32_t frames, int16_t l, int16_t r)
{
    uint32_t i;
    for (i = 0; i < frames; i++) {
        p[i * 4] = (uint8_t)l;
        p[i * 4 + 1] = (uint8_t)((uint16_t)l >> 8);
        p[i * 4 + 2] = (uint8_t)r;
        p[i * 4 + 3] = (uint8_t)((uint16_t)r >> 8);
    }
}

static int16_t sample(const uint8_t *p)
{
    return (int16_t)(p[0] | (uint16_t)p[1] << 8);
}

static void block(int32_t l, int32_t r, uint32_t master, int32_t *out)
{
    uint32_t i;
    for (i = 0; i < 32; i++) {
        out[2 * i] = l;
        out[2 * i + 1] = r;
    }
    ua_audio(out, 32, master);
}

static void routing(void)
{
    uint8_t packet[UA_PACKET];
    int32_t out[64];
    uint32_t i;
    memset(&ua, 0, sizeof ua);
    ua_reset();
    ua.play_alt = ua.cap_alt = 1;
    pcm(packet, 32, 2000, -2000);
    for (i = 0; i < 16; i++)
        ua_receive(packet, 128);
    block(1000, -1000, 4096, out);
    assert(out[0] == 3000 && out[1] == -3000);
    assert(ua.cap[0] == 1000 && ua.cap[1] == -1000); /* no playback loopback */
    block(1000, -1000, 0, out);
    assert(out[0] == 1000 && out[1] == -1000);
    block(32000, -32000, 4096, out);
    assert(out[0] == 32767 && out[1] == -32768);
    ua.play_alt = 0;
    block(1000, -1000, 4096, out);
    assert(out[0] == 1000 && out[1] == -1000);
    ua_reset();
    assert(!ua.play_alt && !ua.cap_alt && ua.pw == ua.pr && ua.cw == ua.cr);
}

/* Model a host honoring feedback every 16 ms, with independent I2S clock and
 * the real 256-frame render bursts. Exercise both clock drift directions and
 * each stream alone: playback feedback cannot depend on capture being open. */
static void drift(int ppm, int play, int cap)
{
    uint8_t packet[UA_PACKET], captured[UA_PACKET];
    int32_t out[64];
    uint64_t device_phase = 0;
    uint32_t host_phase = 0, feedback = UA_NOMINAL, ms, i, bytes;
    uint64_t step = (uint64_t)UA_RATE * (1000000 + ppm);
    memset(&ua, 0, sizeof ua);
    ua_reset();
    ua.play_alt = play;
    ua.cap_alt = cap;
    for (ms = 0; ms < 120000; ms++) {
        device_phase += step;
        while (device_phase >= 256ull * 1000000000ull) {
            device_phase -= 256ull * 1000000000ull;
            for (i = 0; i < 8; i++) {
                block(1000, -1000, 4096, out);
                if (play && ms > 1000)
                    assert(out[0] == 3000 && out[1] == -3000);
            }
        }
        if (play) {
            host_phase += feedback;
            pcm(packet, host_phase >> 14, 2000, -2000);
            ua_receive(packet, (host_phase >> 14) * 4u);
            host_phase &= 16383;
        }
        ua_sof();
        if (play && ms % 16u == 0)
            feedback = ua_feedback();
        if (cap) {
            bytes = ua_transmit(captured);
            assert(bytes <= UA_PACKET && bytes >= 172 && bytes % 4u == 0);
            if (ms > 1000)
                for (i = 0; i < bytes; i += 4) {
                    assert(sample(captured + i) == 1000);
                    assert(sample(captured + i + 2) == -1000);
                }
        }
        assert(ua.pw - ua.pr <= UA_RING && ua.cw - ua.cr <= UA_RING);
    }
    assert(!ua.play_underruns && !ua.play_overruns);
    assert(!ua.cap_underruns && !ua.cap_overruns && !ua.bad_packets);
    printf("USB audio: 120 s, drift %+d ppm, playback %d capture %d: OK\n", ppm, play, cap);
}

static void recovery(void)
{
    uint8_t packet[UA_PACKET];
    int32_t out[64];
    uint32_t i;
    memset(&ua, 0, sizeof ua);
    ua_reset();
    ua.play_alt = ua.cap_alt = 1;
    ua_receive(packet, UA_PACKET + 4);
    ua_receive(packet, 3);
    assert(ua.bad_packets == 2 && ua.pw == 0);
    pcm(packet, 32, 2000, -2000);
    for (i = 0; i < 33; i++)
        ua_receive(packet, 128);
    assert(ua.play_overruns == 1 && !ua.play_ready);
    for (i = 0; i < 16; i++)
        ua_receive(packet, 128);
    for (i = 0; i < 18; i++)
        block(1000, -1000, 4096, out);
    assert(ua.play_underruns == 1 && out[0] == 1000);
    for (i = 0; i < 16; i++)
        ua_receive(packet, 128);
    block(1000, -1000, 4096, out);
    assert(out[0] == 3000);                     /* playback re-primes */
    for (i = 0; i < 33; i++)
        block(1000, -1000, 4096, out);
    assert(ua.cap_overruns > 0);                /* unpolled capture stays bounded */
    ua_cap_reset();
    ua_transmit(packet);
    for (i = 0; i < 176; i++)
        assert(packet[i] == 0);                 /* no stale audio after restart */
    /* Unsigned ring indices must survive long-running streams. */
    ua_play_reset();
    ua.pw = ua.pr = 0xFFFFFF00u;
    pcm(packet, 32, 2000, -2000);
    for (i = 0; i < 16; i++)
        ua_receive(packet, 128);
    block(1000, -1000, 4096, out);
    assert(out[0] == 3000 && ua.pw - ua.pr == 480);
}

int main(void)
{
    routing();
    recovery();
    drift(-1000, 1, 1);
    drift(0, 1, 1);
    drift(1000, 1, 1);
    drift(-1000, 1, 0);
    drift(1000, 0, 1);
    puts("USB audio routing, drift, packet bounds and recovery: OK");
    return 0;
}
