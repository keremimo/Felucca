/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#include <assert.h>
#include <math.h>
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
static void drift(int ppm, int play, int cap, uint32_t play_rate, uint32_t cap_rate)
{
    uint8_t packet[UA_PACKET], captured[UA_PACKET];
    int32_t out[64];
    uint64_t device_phase = 0;
    uint32_t host_phase = 0, feedback = play_rate * 16384u / 1000u, ms, i, bytes;
    uint32_t pw = play == 2 ? 3u : 2u, cw = cap == 2 ? 3u : 2u;
    uint64_t step = (uint64_t)UA_RATE * (1000000 + ppm);
    memset(&ua, 0, sizeof ua);
    ua_reset();
    ua.play_alt = play;
    ua.cap_alt = cap;
    ua.play_rate = play_rate;
    ua.cap_rate = cap_rate;
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
            for (i = 0; i < host_phase >> 14; i++) {
                ua_encode(packet + i * pw * 2u, pw, 2000);
                ua_encode(packet + i * pw * 2u + pw, pw, -2000);
            }
            ua_receive(packet, (host_phase >> 14) * pw * 2u);
            host_phase &= 16383;
        }
        ua_sof();
        if (play && ms % 16u == 0)
            feedback = ua_feedback();
        if (cap) {
            bytes = ua_transmit(captured);
            assert(bytes <= 49u * cw * 2u && bytes >= 43u * cw * 2u && bytes % (cw * 2u) == 0);
            if (ms > 1000)
                for (i = 0; i < bytes; i += cw * 2u) {
                    assert(ua_decode(captured + i, cw) == 1000);
                    assert(ua_decode(captured + i + cw, cw) == -1000);
                }
        }
        assert(ua.pw - ua.pr <= UA_RING && ua.cw - ua.cr <= UA_RING);
    }
    assert(!ua.play_underruns && !ua.play_overruns);
    assert(!ua.cap_underruns && !ua.cap_overruns && !ua.bad_packets);
    printf("USB audio: 120 s, drift %+d ppm, playback %d/%u capture %d/%u: OK\n",
           ppm, play, play_rate, cap, cap_rate);
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

static void pcm24(void)
{
    static const uint8_t samples[][3] = {
        {0xFF, 0xFF, 0x7F}, {0, 0, 0x80}, {0xFF, 0xFF, 0xFF},
        {0xFF, 0, 0}, {0, 0x34, 0x12}, {0, 0xCC, 0xED}
    };
    static const int16_t values[] = {32767, -32768, -1, 0, 0x1234, -0x1234};
    uint8_t packet[UA_PACKET];
    int32_t out[64];
    ua_reset();
    ua.play_alt = ua.cap_alt = 2;
    for (uint32_t i = 0; i < 6; i++) {
        memcpy(packet + i * 6u, samples[i], 3);
        memcpy(packet + i * 6u + 3u, samples[i], 3);
    }
    ua_receive(packet, 36);
    for (uint32_t i = 0; i < 6; i++)
        assert(ua.play[2u * i] == values[i] && ua.play[2u * i + 1u] == values[i]);
    uint32_t bad = ua.bad_packets;
    ua_receive(packet, 293);
    ua_receive(packet, 300);
    assert(ua.bad_packets == bad + 2u && ua.pw == 6);
    ua_receive(packet, 0);                   /* legal empty isochronous packet */
    for (uint32_t i = 0; i < 16; i++) block(-32768, 32767, 0, out);
    uint32_t n = ua_transmit(packet);
    assert(n == 264);
    for (uint32_t i = 0; i < n; i += 6u) {
        assert(packet[i] == 0 && packet[i + 1u] == 0 && packet[i + 2u] == 0x80);
        assert(packet[i + 3u] == 0 && packet[i + 4u] == 0xFF && packet[i + 5u] == 0x7F);
    }
    /* Maximum PCM24 packet fits, including across unsigned ring-index wrap. */
    ua_play_reset();
    ua.pw = ua.pr = 0xFFFFFFF0u;
    memset(packet, 0, sizeof packet);
    ua_receive(packet, sizeof packet);
    assert(ua.pw - ua.pr == 49 && ua.bad_packets == bad + 2u);
}

struct measurement { double re, im, energy; uint32_t count, used; };

static void measure(struct measurement *m, int32_t sample, double frequency, uint32_t rate)
{
    double phase = 6.283185307179586 * frequency * m->count++ / rate;
    if (m->count < 256) return;              /* exclude filter startup */
    m->re += sample * cos(phase);
    m->im += sample * sin(phase);
    m->energy += (double)sample * sample;
    m->used++;
}

/* Exercise actual packet -> ring -> engine and engine -> ring -> packet paths.
 * Correlation checks pitch as well as level; RMS detects out-of-band aliases. */
static void tone(int capture, double frequency)
{
    uint8_t packet[UA_PACKET];
    int32_t out[96];
    struct measurement m = {0};
    uint32_t source_rate = capture ? 44100u : 48000u;
    uint32_t destination_rate = capture ? 48000u : 44100u;
    ua_reset();
    ua.play_alt = capture ? 0 : 2;
    ua.cap_alt = capture ? 2 : 0;
    ua.play_rate = ua.cap_rate = 48000;
    for (uint32_t input = 0; input < source_rate;) {
        uint32_t n = capture ? 32u : 48u;
        if (n > source_rate - input) n = source_rate - input;
        for (uint32_t i = 0; i < n; i++) {
            int16_t sample = (int16_t)lrint(10000 * sin(6.283185307179586 * frequency * input++ / source_rate));
            if (capture) out[i * 2u] = out[i * 2u + 1u] = sample;
            else {
                ua_encode(packet + i * 6u, 3, sample);
                ua_encode(packet + i * 6u + 3u, 3, sample);
            }
        }
        if (capture) {
            ua_audio(out, n, 0);
            while (ua.cw - ua.cr >= UA_TARGET + 64u) {
                uint32_t bytes = ua_transmit(packet);
                for (uint32_t i = 0; i < bytes; i += 6u)
                    measure(&m, ua_decode(packet + i, 3), frequency, destination_rate);
            }
        } else {
            ua_receive(packet, n * 6u);
            while (ua.pw - ua.pr >= UA_TARGET + 32u) {
                block(0, 0, 4096, out);
                for (uint32_t i = 0; i < 32u; i++)
                    measure(&m, out[i * 2u], frequency, destination_rate);
            }
        }
    }
    if (capture)
        assert(ua.cr * 160u - m.count * 147u == ua.cap_src.phase);
    else
        assert(ua.pw == 44100 && ua.play_src.phase == 0);
    double gain = 2 * sqrt(m.re * m.re + m.im * m.im) / m.used / 10000;
    double rms = sqrt(2 * m.energy / m.used) / 10000;
    if (frequency < 18000) {
        assert(gain > 0.995 && gain < 1.005);
        assert(rms > 0.995 && rms < 1.005);
    } else {
        assert(rms < 0.01);                 /* >40 dB rejection at 23 kHz */
    }
    printf("USB audio conversion: %s %.0f Hz, gain %.5f, RMS %.5f: OK\n",
           capture ? "capture" : "playback", frequency, gain, rms);
}

int main(void)
{
    routing();
    recovery();
    pcm24();
    tone(0, 1000);
    tone(0, 10000);
    tone(0, 23000);
    tone(1, 1000);
    tone(1, 10000);
    for (uint32_t rate = 44100; rate <= 48000; rate += 3900)
        for (int alt = 1; alt <= 2; alt++) {
            drift(-1000, alt, alt, rate, rate);
            drift(0, alt, alt, rate, rate);
            drift(1000, alt, alt, rate, rate);
            drift(-1000, alt, 0, rate, rate);
            drift(1000, 0, alt, rate, rate);
        }
    drift(1000, 2, 1, 48000, 44100);
    drift(-1000, 1, 2, 44100, 48000);
    puts("USB audio routing, drift, packet bounds and recovery: OK");
    return 0;
}
