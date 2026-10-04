/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* USB0 full-speed isochronous service, included by usb.c after the SIE helpers.
 * The AC79 usb_phy.h defines bit 14 as ISOCHRONOUS in TX/RX CSR. As in the
 * SDK and the MIDI endpoints, MaxP is written as 0xFF and TX bit 13 (direction)
 * stays clear: an exact MaxP turns on double packet buffering, and then every
 * other OUT packet never reaches ua_rx. RX DMA keeps the same four-byte guard
 * as USB MIDI. All SIE access stays in TIMER5, including SET_INTERFACE and
 * reset handling; register access goes through hal/fm1_usb.h. */
#define UA_IF_PLAY 3u                           /* streaming interfaces (usb_audio_desc.h) */
#define UA_IF_CAP 5u
#define UA_DMA_PACKET ((UA_PACKET + 3u) & ~3u)
static uint8_t ua_tx[UA_DMA_PACKET] __attribute__((aligned(4)));
static uint8_t ua_rx[((UA_PLAY_PACKET + 3u) & ~3u) + 4u] __attribute__((aligned(4)));
static uint8_t ua_fb[4] __attribute__((aligned(4)));
static uint16_t ua_frame;
static uint8_t ua_frame_valid, ua_paused;
static uint8_t ua_rate_pending, ua_rate_reply[3];

static void ua_play_config(void)
{
    fm1_usb_ep_rxbuf(2, ua_rx);
    sie_wr(S_INDEX, 2);
    sie_wr(S_RXMAXP, 0xFF);
    sie_wr(S_RXCSR1, 0x90);                     /* flush FIFO, clear toggle */
    sie_wr(S_RXCSR2, 0x40);                     /* ISOCHRONOUS */
    fm1_usb_ep_txbuf(3, ua_fb);
    sie_wr(S_INDEX, 3);
    sie_wr(S_TXMAXP, 0xFF);
    sie_wr(S_TXCSR1, 0x48);
    sie_wr(S_TXCSR2, 0x40);                     /* ISOCHRONOUS */
    sie_wr(S_INTRRX1E, sie_rd(S_INTRRX1E) | 0x04u);
    fm1_usb_ep_enable((1u << 2) | (1u << 3));
}

static void ua_cap_config(void)
{
    fm1_usb_ep_txbuf(2, ua_tx);
    sie_wr(S_INDEX, 2);
    sie_wr(S_TXMAXP, 0xFF);
    sie_wr(S_TXCSR1, 0x48);
    sie_wr(S_TXCSR2, 0x40);
    fm1_usb_ep_enable(1u << 2);
}

static void ua_hw_stop(void)
{
    ua_reset();
    ua_rate_pending = 0;
    ua_frame_valid = ua_paused = 0;
    sie_wr(S_INTRRX1E, sie_rd(S_INTRRX1E) & ~0x04u);
    sie_wr(S_INDEX, 2);
    sie_wr(S_RXCSR1, 0x90);
    sie_wr(S_TXCSR1, 0x48);
    sie_wr(S_INDEX, 3);
    sie_wr(S_TXCSR1, 0x48);
}

/* Keep one packet queued on each IN endpoint, refilled as soon as the host has
 * taken the last one. An IN token that finds nothing gets an empty packet: on
 * capture that costs the host a frame of input timeline, and macOS then drops
 * output it believes is late (about 2 ms in every 23 ms IO cycle). */
static void ua_tx_fill(void)
{
    uint32_t n;
    if (ua.play_alt) {
        sie_wr(S_INDEX, 3);
        if (!(sie_rd(S_TXCSR1) & 1u)) {
            n = ua_feedback();
            ua_fb[0] = (uint8_t)n;
            ua_fb[1] = (uint8_t)(n >> 8);
            ua_fb[2] = (uint8_t)(n >> 16);
            fm1_usb_ep_send(3, ua_fb, 3);
            sie_wr(S_TXCSR1, 1);
        }
    }
    if (ua.cap_alt) {
        sie_wr(S_INDEX, 2);
        if (!(sie_rd(S_TXCSR1) & 1u)) {
            n = ua_transmit(ua_tx);
            fm1_usb_ep_send(2, ua_tx, n);
            sie_wr(S_TXCSR1, 1);
        }
    }
}

static int ua_set_interface(uint16_t interface, uint16_t alt)
{
    if (!usb.config || alt > 2u || (interface != UA_IF_PLAY && interface != UA_IF_CAP))
        return 0;
    if (interface == UA_IF_PLAY) {
        ua_play_reset();
        ua.play_alt = (uint8_t)alt;
        if (alt)
            ua_play_config();
        else {
            sie_wr(S_INTRRX1E, sie_rd(S_INTRRX1E) & ~0x04u);
            sie_wr(S_INDEX, 2);
            sie_wr(S_RXCSR1, 0x90);
            sie_wr(S_INDEX, 3);
            sie_wr(S_TXCSR1, 0x48);
        }
    } else {
        ua_cap_reset();
        ua.cap_alt = (uint8_t)alt;
        if (alt)
            ua_cap_config();
        else {
            sie_wr(S_INDEX, 2);
            sie_wr(S_TXCSR1, 0x48);
        }
    }
    if (alt)
        ua_tx_fill();                           /* ready for the host's first IN token */
    return 1;
}

/* UAC1 endpoint sampling-frequency control; depth is selected by the AS
 * alternate setting (1 = PCM16, 2 = packed PCM24). Each direction owns its
 * rate so host setup order and mixed-rate duplex streams are both supported. */
static int ua_control_setup(const uint8_t *s)
{
    uint32_t rate;
    if (!usb.config || s[2] || s[3] != 1u || s[5] || s[6] != 3u || s[7] ||
        (s[4] != 0x02u && s[4] != 0x82u))
        return 0;
    if (s[0] == 0x22u && s[1] == 1u) {       /* SET_CUR: three-byte OUT stage */
        ua_rate_pending = s[4];
        sie_wr(S_INDEX, 0);
        sie_wr(S_CSR0, 0x40);
        return 1;
    }
    if (s[0] != 0xA2u)
        return 0;
    switch (s[1]) {
    case 0x81: rate = s[4] == 2u ? ua.play_rate : ua.cap_rate; break;
    case 0x82: rate = UA_RATE; break;          /* GET_MIN */
    case 0x83: rate = 48000u; break;          /* GET_MAX */
    case 0x84: rate = 3900u; break;           /* GET_RES */
    default: return 0;
    }
    ua_rate_reply[0] = (uint8_t)rate;
    ua_rate_reply[1] = (uint8_t)(rate >> 8);
    ua_rate_reply[2] = (uint8_t)(rate >> 16);
    e0_send(ua_rate_reply, 3, 3);
    return 1;
}

static int ua_control_data(const uint8_t *p, uint32_t n)
{
    uint8_t ep = ua_rate_pending;
    uint32_t rate;
    ua_rate_pending = 0;
    if (!usb.config || !ep || n != 3u)
        return 0;
    rate = p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16;
    /* UAC1 requires unsupported values to round to the closest discrete rate. */
    rate = rate <= 46050u ? UA_RATE : 48000u;
    if (ep == 2u && ua.play_rate != rate) {
        ua.play_rate = rate;
        ua_set_interface(UA_IF_PLAY, ua.play_alt);
    } else if (ep == 0x82u && ua.cap_rate != rate) {
        ua.cap_rate = rate;
        ua_set_interface(UA_IF_CAP, ua.cap_alt);
    }
    return 1;
}

static void ua_hw_poll(void)
{
    uint32_t csr, n, f;
    if (!usb.config)
        return;
    if (usb.suspended) {
        if (!ua_paused) {
            ua_play_reset();
            ua_cap_reset();
            if (ua.play_alt)
                ua_play_config();
            if (ua.cap_alt)
                ua_cap_config();
            ua_paused = 1;
            ua_frame_valid = 0;
        }
        return;
    }
    if (ua_paused) {
        if (ua.play_alt)
            ua_play_config();
        if (ua.cap_alt)
            ua_cap_config();
        ua_paused = 0;
    }
    if (!ua.play_alt && !ua.cap_alt) {
        ua_frame_valid = 0;                     /* idle frames are not missed ones */
        return;
    }
    if (ua.play_alt) {
        sie_wr(S_INDEX, 2);
        csr = sie_rd(S_RXCSR1);
        if (csr & 1u) {
            n = sie_rd(S_RXCOUNT1) | sie_rd(S_RXCOUNT2) << 8;
            fm1_usb_rx_sync();
            if (csr & 0x0Cu)
                ua.bad_packets++;
            else
                ua_receive(ua_rx, n);
            sie_wr(S_RXCSR1, 0x10);             /* discard errors, release DMA packet */
        }
    }
    /* FRAME1 may roll over between reads: retry with a stable high byte. */
    do {
        n = sie_rd(S_FRAME2) & 7u;
        f = sie_rd(S_FRAME1);
    } while (n != (sie_rd(S_FRAME2) & 7u) && usb.up);
    f |= n << 8;
    if (!ua_frame_valid || f != ua_frame) {
        if (ua_frame_valid) {
            uint32_t gap = (f - ua_frame) & 2047u;
            if (gap > 1u)
                ua.missed_frames += gap - 1u;
        }
        ua_frame = (uint16_t)f;
        ua_frame_valid = 1;
        ua_sof();
    }
    ua_tx_fill();
}
