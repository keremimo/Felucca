/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* UAC1 stereo PCM transport. No hardware dependencies: callers serialize USB
 * service and each short audio block. Capture taps the synth before playback
 * is added, so DAW monitoring cannot feed itself through USB capture.
 *
 * The I2S clock is independent of USB SOF. A low-pass ring-fill servo adjusts
 * capture packet lengths and the playback endpoint's explicit 10.14 feedback.
 * This transfers original samples without inserting/dropping samples in normal
 * operation. The rings absorb the existing 256-frame render bursts. */
#include <stdint.h>
#define UA_RATE 44100u
#define UA_PACKET 184u                         /* up to 46 stereo 16-bit frames */
#define UA_RING 1024u
#define UA_TARGET 512u
#define UA_NOMINAL ((UA_RATE * 16384u) / 1000u)

static struct {
    uint8_t play_alt, cap_alt, play_ready, cap_ready;
    uint32_t pw, pr, cw, cr, cap_frac;
    int32_t play_fill_q8, cap_fill_q8;
    uint32_t play_underruns, play_overruns, cap_underruns, cap_overruns, bad_packets;
    uint32_t rx_packets, tx_packets, missed_frames;
    int16_t play[UA_RING * 2u], cap[UA_RING * 2u];
} ua;

static int32_t ua_clip(int32_t x)
{
    return x > 32767 ? 32767 : x < -32768 ? -32768 : x;
}

static void ua_play_reset(void)
{
    ua.pw = ua.pr = 0;
    ua.play_ready = 0;
    ua.play_fill_q8 = UA_TARGET * 256;
}

static void ua_cap_reset(void)
{
    ua.cw = ua.cr = ua.cap_frac = 0;
    ua.cap_ready = 0;
    ua.cap_fill_q8 = UA_TARGET * 256;
}

static void ua_reset(void)
{
    ua.play_alt = ua.cap_alt = 0;
    ua_play_reset();
    ua_cap_reset();
}

static void ua_receive(const uint8_t *p, uint32_t bytes)
{
    uint32_t i, n = bytes / 4u;
    if (!ua.play_alt)
        return;
    if (bytes > UA_PACKET || (bytes & 3u)) {
        ua.bad_packets++;
        return;
    }
    if (ua.pw - ua.pr + n > UA_RING) {
        ua.play_overruns++;
        ua_play_reset();                       /* re-prime, never replay stale data */
    }
    for (i = 0; i < n; i++) {
        uint32_t at = ((ua.pw + i) & (UA_RING - 1u)) * 2u;
        ua.play[at] = (int16_t)(p[4u * i] | (uint16_t)p[4u * i + 1u] << 8);
        ua.play[at + 1u] = (int16_t)(p[4u * i + 2u] | (uint16_t)p[4u * i + 3u] << 8);
    }
    ua.pw += n;
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

static uint32_t ua_rate(int32_t error_q8)
{
    int32_t correction = error_q8 / 16;         /* fill error / 1024, in 10.14 */
    if (correction > 8192)
        correction = 8192;
    if (correction < -8192)
        correction = -8192;
    return (uint32_t)((int32_t)UA_NOMINAL + correction);
}

static uint32_t ua_feedback(void)
{
    return ua_rate(UA_TARGET * 256 - ua.play_fill_q8);
}

static uint32_t ua_transmit(uint8_t *p)
{
    uint32_t i, n, take = 0;
    ua.cap_frac += ua_rate(ua.cap_fill_q8 - UA_TARGET * 256);
    n = ua.cap_frac >> 14;
    ua.cap_frac &= 16383u;
    if (!ua.cap_ready && ua.cw - ua.cr >= UA_TARGET)
        ua.cap_ready = 1;
    if (ua.cap_ready) {
        if (ua.cw - ua.cr >= n)
            take = n;
        else {
            ua.cap_underruns++;
            ua_cap_reset();
        }
    }
    for (i = 0; i < n; i++) {
        uint32_t at = ((ua.cr + i) & (UA_RING - 1u)) * 2u;
        uint16_t l = take ? (uint16_t)ua.cap[at] : 0;
        uint16_t r = take ? (uint16_t)ua.cap[at + 1u] : 0;
        p[4u * i] = (uint8_t)l;
        p[4u * i + 1u] = (uint8_t)(l >> 8);
        p[4u * i + 2u] = (uint8_t)r;
        p[4u * i + 3u] = (uint8_t)(r >> 8);
    }
    ua.cr += take;
    ua.tx_packets++;
    return n * 4u;
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
