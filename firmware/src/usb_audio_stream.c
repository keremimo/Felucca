/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* UAC1 stereo PCM transport. No hardware dependencies: callers serialize USB
 * service and each short audio block. Capture taps the synth before playback
 * is added, so DAW monitoring cannot feed itself through USB capture.
 *
 * The I2S clock is independent of USB SOF. A low-pass ring-fill servo adjusts
 * capture packet lengths and the playback endpoint's explicit 10.14 feedback.
 * The rings always hold native PCM16 at 44.1 kHz. At 48 kHz, rational FIR
 * conversion runs in USB service, outside the audio ISR's masked copy. */
#include <stdint.h>
#define UA_RATE 44100u
#define UA_PACKET 294u                         /* up to 49 stereo packed PCM24 frames */
#define UA_RING 1024u
#define UA_TARGET 512u
#define UA_NOMINAL ((UA_RATE * 16384u) / 1000u)
#ifndef UA_POOL
#define UA_POOL __attribute__((section(".pool")))   /* zeroed RAM; the host tests define it empty */
#endif
#include "usb_audio_filter.h"
#if UA_TAPS % 4u
#error "ua_src_sample takes four taps per pass"
#endif

/* The taps run from RAM (ua_init): each 48 kHz sample reads a 96-byte row, and
 * from XIP every row would be a flash-cache miss inside TIMER5 that also evicts
 * the synth's code. */
static int16_t ua_play_taps[147][UA_TAPS] UA_POOL, ua_cap_taps[160][UA_TAPS] UA_POOL;

struct ua_converter {
    /* Residual output-clock ticks, denominator 147 (play) or 160 (capture).
     * History runs newest first from head and is stored twice, so the FIR
     * window never wraps. Exact 147:160 timing avoids drift. */
    uint32_t phase, head;
    int16_t history[UA_TAPS * 4u];
};

static struct {
    uint8_t play_alt, cap_alt, play_ready, cap_ready;
    uint32_t play_rate, cap_rate;
    struct ua_converter play_src, cap_src;
    uint32_t pw, pr, cw, cr, cap_frac;
    int32_t play_fill_q8, cap_fill_q8;
    uint32_t play_underruns, play_overruns, cap_underruns, cap_overruns, bad_packets;
    uint32_t rx_packets, tx_packets, missed_frames;
    uint32_t poll_max_ticks, service_max_ticks;
    int16_t play[UA_RING * 2u], cap[UA_RING * 2u];
} ua;

static int32_t ua_clip(int32_t x)
{
    return x > 32767 ? 32767 : x < -32768 ? -32768 : x;
}

static void ua_init(void)                       /* before USB service starts */
{
    uint32_t p, k;
    for (p = 0; p < 160u; p++)
        for (k = 0; k < UA_TAPS; k++) {
            if (p < 147u)
                ua_play_taps[p][k] = ua_play_filter[p][k];
            ua_cap_taps[p][k] = ua_cap_filter[p][k];
        }
}

static void ua_src_reset(struct ua_converter *s)
{
    uint32_t i;
    s->phase = s->head = 0;
    for (i = 0; i < UA_TAPS * 4u; i++)
        s->history[i] = 0;
}

static void ua_src_push(struct ua_converter *s, int16_t l, int16_t r)
{
    int16_t *h;
    s->head = s->head ? s->head - 1u : UA_TAPS - 1u;
    h = &s->history[s->head * 2u];
    h[0] = h[UA_TAPS * 2u] = l;
    h[1] = h[UA_TAPS * 2u + 1u] = r;
}

/* 64-bit sums compile to the pi32v2 multiply-accumulate: with four taps per
 * pass, 6 instructions per tap for both channels. */
static void ua_src_sample(const struct ua_converter *s, const int16_t *coeff, int16_t *out)
{
    const int16_t *x = &s->history[s->head * 2u], *end = coeff + UA_TAPS;
    int64_t l = 0, r = 0;
    do {
        l += (int64_t)x[0] * coeff[0];
        r += (int64_t)x[1] * coeff[0];
        l += (int64_t)x[2] * coeff[1];
        r += (int64_t)x[3] * coeff[1];
        l += (int64_t)x[4] * coeff[2];
        r += (int64_t)x[5] * coeff[2];
        l += (int64_t)x[6] * coeff[3];
        r += (int64_t)x[7] * coeff[3];
        x += 8;
        coeff += 4;
    } while (coeff != end);
    out[0] = (int16_t)ua_clip((int32_t)((l + 8192) >> 14));
    out[1] = (int16_t)ua_clip((int32_t)((r + 8192) >> 14));
}

static uint32_t ua_sample_bytes(uint8_t alt) { return alt == 2u ? 3u : 2u; }

/* Packed little-endian PCM24 uses the same full-scale level as PCM16. The
 * low byte is discarded on playback; recording pads it with zero. */
static int16_t ua_decode(const uint8_t *p, uint32_t width)
{
    p += width - 2u;
    return (int16_t)(p[0] | (uint16_t)p[1] << 8);
}

static void ua_encode(uint8_t *p, uint32_t width, int16_t sample)
{
    if (width == 3u)
        *p++ = 0;
    p[0] = (uint8_t)sample;
    p[1] = (uint8_t)((uint16_t)sample >> 8);
}

static void ua_play_reset(void)
{
    ua.pw = ua.pr = 0;
    ua.play_ready = 0;
    ua.play_fill_q8 = UA_TARGET * 256;
    ua_src_reset(&ua.play_src);
}

static void ua_cap_reset(void)
{
    ua.cw = ua.cr = ua.cap_frac = 0;
    ua.cap_ready = 0;
    ua.cap_fill_q8 = UA_TARGET * 256;
    ua_src_reset(&ua.cap_src);
}

static void ua_reset(void)
{
    ua.play_alt = ua.cap_alt = 0;
    ua.play_rate = ua.cap_rate = UA_RATE;
    ua_play_reset();
    ua_cap_reset();
}

static void ua_receive(const uint8_t *p, uint32_t bytes)
{
    uint32_t i, width = ua_sample_bytes(ua.play_alt), frame = width * 2u;
    uint32_t n = bytes / frame;
    if (!ua.play_alt)
        return;
    if (n > 49u || bytes % frame) {
        ua.bad_packets++;
        return;
    }
    if (ua.pw - ua.pr + n > UA_RING) {
        ua.play_overruns++;
        ua_play_reset();                       /* re-prime, never replay stale data */
    }
    for (i = 0; i < n; i++) {
        uint32_t at = (ua.pw & (UA_RING - 1u)) * 2u;
        int16_t l = ua_decode(p + frame * i, width);
        int16_t r = ua_decode(p + frame * i + width, width);
        if (ua.play_rate == 48000u) {
            ua_src_push(&ua.play_src, l, r);
            ua.play_src.phase += 147u;
            if (ua.play_src.phase < 160u)
                continue;
            ua.play_src.phase -= 160u;
            ua_src_sample(&ua.play_src, ua_play_taps[ua.play_src.phase], &ua.play[at]);
        } else {
            ua.play[at] = l;
            ua.play[at + 1u] = r;
        }
        ua.pw++;
    }
    ua.rx_packets++;
}

/* Called once per observed USB frame, not once per poll or audio callback. */
static void ua_sof(void)
{
    if (ua.play_ready)
        ua.play_fill_q8 += ((int32_t)(ua.pw - ua.pr) * 256 - ua.play_fill_q8) / 32;
    if (ua.cap_ready)
        ua.cap_fill_q8 += ((int32_t)(ua.cw - ua.cr) * 256 - ua.cap_fill_q8) / 32;
}

static uint32_t ua_rate(uint32_t rate, int32_t error_q8)
{
    int32_t correction = error_q8 / 16;         /* fill error / 1024, in 10.14 */
    if (correction > 8192)
        correction = 8192;
    if (correction < -8192)
        correction = -8192;
    return (uint32_t)((int32_t)((rate * 16384u) / 1000u) + correction);
}

static uint32_t ua_feedback(void)
{
    return ua_rate(ua.play_rate, UA_TARGET * 256 - ua.play_fill_q8);
}

static uint32_t ua_transmit(uint8_t *p)
{
    uint32_t i, n, need, take = 0, width = ua_sample_bytes(ua.cap_alt);
    ua.cap_frac += ua_rate(ua.cap_rate, ua.cap_fill_q8 - UA_TARGET * 256);
    n = ua.cap_frac >> 14;
    ua.cap_frac &= 16383u;
    /* ceil((147 * USB frames - residual) / 160) native frames at 48 kHz. */
    need = ua.cap_rate == 48000u ? (n * 147u + 159u - ua.cap_src.phase) / 160u : n;
    if (!ua.cap_ready && ua.cw - ua.cr >= UA_TARGET)
        ua.cap_ready = 1;
    if (ua.cap_ready) {
        if (ua.cw - ua.cr >= need)
            take = 1;
        else {
            ua.cap_underruns++;
            ua_cap_reset();
        }
    }
    for (i = 0; i < n; i++) {
        int16_t sample[2] = {0, 0};
        if (take) {
            if (ua.cap_rate == 48000u) {
                if (ua.cap_src.phase < 147u) {
                    uint32_t at = (ua.cr++ & (UA_RING - 1u)) * 2u;
                    ua_src_push(&ua.cap_src, ua.cap[at], ua.cap[at + 1u]);
                    ua.cap_src.phase += 160u;
                }
                ua.cap_src.phase -= 147u;
                ua_src_sample(&ua.cap_src, ua_cap_taps[ua.cap_src.phase], sample);
            } else {
                uint32_t at = (ua.cr++ & (UA_RING - 1u)) * 2u;
                sample[0] = ua.cap[at];
                sample[1] = ua.cap[at + 1u];
            }
        }
        ua_encode(p + i * width * 2u, width, sample[0]);
        ua_encode(p + i * width * 2u + width, width, sample[1]);
    }
    ua.tx_packets++;
    return n * width * 2u;
}

/* Stereo Q15, after synth master processing; playback follows the MASTER knob.
 * Call in blocks of CTL (32), with USB service excluded during this copy. */
static void ua_audio(int32_t *out, uint32_t n, uint32_t master_q12)
{
    uint32_t i, capture = ua.cap_alt, playback = 0;
    if (capture && ua.cw - ua.cr + n > UA_RING) {
        ua.cap_overruns++;
        ua_cap_reset();
    }
    if (ua.play_alt) {
        if (!ua.play_ready && ua.pw - ua.pr >= UA_TARGET)
            ua.play_ready = 1;
        if (ua.play_ready) {
            if (ua.pw - ua.pr >= n)
                playback = 1;
            else {
                ua.play_underruns++;
                ua_play_reset();
            }
        }
    }
    for (i = 0; i < n; i++) {
        uint32_t ci = ((ua.cw + i) & (UA_RING - 1u)) * 2u;
        uint32_t pi = ((ua.pr + i) & (UA_RING - 1u)) * 2u;
        int32_t l = out[2u * i], r = out[2u * i + 1u];
        if (capture) {
            ua.cap[ci] = (int16_t)ua_clip(l);
            ua.cap[ci + 1u] = (int16_t)ua_clip(r);
        }
        if (playback) {
            l += (ua.play[pi] * (int32_t)master_q12) >> 12;
            r += (ua.play[pi + 1u] * (int32_t)master_q12) >> 12;
            out[2u * i] = ua_clip(l);
            out[2u * i + 1u] = ua_clip(r);
        }
    }
    if (capture)
        ua.cw += n;
    if (playback)
        ua.pr += n;
}
