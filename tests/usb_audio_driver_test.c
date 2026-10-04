/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Exercise the production endpoint driver with a minimal indexed-SIE model.
 * This tests lifecycle and packet ownership, not the physical USB controller. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
/* Include the HAL once, then substitute only its hardware boundary. The real
 * usb.c (including EP0 setup/data/status handling) runs against this SIE model. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
#include "../firmware/hal/fm1_usb.h"
#pragma GCC diagnostic pop
static uint8_t regs[4][24], common[16], index_reg, read_reg, ep0_command;
static uint32_t USB_CON0, ep_cnt[4];
static void *ep_tadr[4], *ep_radr[4];
static void (*send_check)(uint32_t ep);
static void mock_enable(uint32_t eps) { USB_CON0 &= ~(eps << 19); }
static void mock_txbuf(uint32_t ep, void *p) { ep_tadr[ep] = p; }
static void mock_rxbuf(uint32_t ep, void *p) { ep_radr[ep] = p; }
static void mock_send(uint32_t ep, void *p, uint32_t n)
{
    ep_tadr[ep] = p;
    ep_cnt[ep] = n;
    if (send_check) send_check(ep);
}
static void mock_ep0_send(void *p, uint32_t n) { mock_send(0, p, n); }
static void mock_ep0_buf(void *p) { ep_radr[0] = p; }
static void mock_nop(void) {}
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
static void mock_attach(void *p) { (void)p; }
static uint32_t mock_one(void) { return 1; }
static uint32_t mock_zero(void) { return 0; }
static void mock_read(uint32_t r) { read_reg = (uint8_t)r; }
static uint32_t mock_data(void)
{
    uint32_t r = read_reg;
    uint8_t value = r < 16 ? common[r] : regs[index_reg][r];
    if (r == 2 || r == 4 || r == 6) common[r] = 0; /* read-clear IRQs */
    return value;
}
static void mock_write(uint32_t r, uint32_t v)
{
    if (r == 14)                            /* INDEX */
        index_reg = v;
    else if (r < 16)
        common[r] = v;
    else if (!index_reg && r == 17) {       /* EP0 CSR */
        ep0_command = (uint8_t)v;
        if (v == 0x80) regs[0][17] &= ~0x10u;
        else regs[0][17] = v == 0x60 ? 4 : (v & 2u);
    } else if ((r == 17 && (v & 8u)) || (r == 20 && (v & 16u)))
        regs[index_reg][r] = 0;             /* self-clearing flush */
    else
        regs[index_reg][r] = v;
}
#define fm1_usb_ep_enable mock_enable
#define fm1_usb_ep_txbuf mock_txbuf
#define fm1_usb_ep_rxbuf mock_rxbuf
#define fm1_usb_ep_send mock_send
#define fm1_usb_ep0_send mock_ep0_send
#define fm1_usb_ep0_buf mock_ep0_buf
#define fm1_usb_rx_sync mock_nop
#define fm1_usb_sie_on mock_one
#define fm1_usb_sie_done mock_one
#define fm1_usb_sie_wr_start mock_write
#define fm1_usb_sie_rd_start mock_read
#define fm1_usb_sie_data mock_data
#define fm1_usb_reset mock_nop
#define fm1_usb_off mock_nop
#define fm1_usb_attach mock_attach
#define fm1_usb_sof_take mock_zero
#define RING_PUBLISH() ((void)0)
#define FELUCCA_USB_AUDIO 1
#define FELUCCA_CDC 0
#define FELUCCA_OTA 0
#include "../firmware/src/usb.c"

static void frame(uint16_t n)
{
    common[S_FRAME1] = n & 255;
    common[S_FRAME2] = (n >> 8) & 7;
}

static void setup(uint8_t type, uint8_t request, uint16_t value, uint16_t index, uint16_t len)
{
    const uint8_t s[8] = {type, request, value & 255, value >> 8,
                         index & 255, index >> 8, len & 255, len >> 8};
    memcpy(ep0buf, s, 8);
    regs[0][S_CSR0] = 1;
    regs[0][S_COUNT0] = 8;
    ep0_service();
}

static void rate_data(uint32_t rate, uint8_t len)
{
    ep0buf[0] = rate & 255;
    ep0buf[1] = (rate >> 8) & 255;
    ep0buf[2] = rate >> 16;
    regs[0][S_CSR0] = 1;
    regs[0][S_COUNT0] = len;
    ep0_service();
}

static uint32_t get_rate(uint8_t ep, uint8_t request)
{
    setup(0xA2, request, 0x100, ep, 3);
    assert(ep0_command == 0x0A && ep_cnt[0] == 3);
    return ep0buf[0] | (uint32_t)ep0buf[1] << 8 | (uint32_t)ep0buf[2] << 16;
}

static void controls(void)
{
    uint32_t pw;
    ua_hw_stop();
    assert(get_rate(2, 0x81) == 44100 && get_rate(0x82, 0x81) == 44100);
    assert(get_rate(2, 0x82) == 44100 && get_rate(2, 0x83) == 44100);
    assert(get_rate(0x82, 0x84) == 0);
    /* A host may set the rate before enabling its alternate setting. */
    setup(0x22, 1, 0x100, 2, 3);
    assert(ua_rate_pending == 2 && ep0_command == 0x40);
    rate_data(44100, 3);
    assert(ep0_command == 0x48 && !ua_rate_pending && get_rate(2, 0x81) == 44100);
    assert(ua_feedback() == 44100u * 16384u / 1000u);
    assert(get_rate(0x82, 0x81) == 44100);
    setup(1, 11, 2, 3, 0);
    setup(1, 11, 2, 5, 0);
    assert(ua.play_alt == 2 && ua.cap_alt == 2 && ep_cnt[2] == 528);
    setup(0x81, 10, 0, 3, 1);
    assert(ep_cnt[0] == 1 && ep0buf[0] == 2);
    setup(0x81, 10, 0, 5, 1);
    assert(ep_cnt[0] == 1 && ep0buf[0] == 2);
    /* Each function's control interface (IF2, IF4) has only alternate 0. */
    setup(0x81, 10, 0, 4, 1);
    assert(ep0_command == 0x0A && ep0buf[0] == 0);
    setup(1, 11, 0, 4, 0);
    assert(ep0_command == 0x48);
    setup(1, 11, 1, 4, 0);
    assert(ep0_command == 0x60 && ua.cap_alt == 2);
    setup(1, 11, 0, 6, 0);
    assert(ep0_command == 0x60);
    setup(0x81, 10, 0, 6, 1);
    assert(ep0_command == 0x60);
    /* Device names of the two functions (iFunction / iInterface 3 and 4). */
    setup(0x80, 6, 0x0303, 0x0409, 255);
    assert(ep_cnt[0] == 24 && ep0buf[1] == 3 && !memcmp(ep0buf + 18, "O\0u\0t\0", 6));
    setup(0x80, 6, 0x0304, 0x0409, 255);
    assert(ep_cnt[0] == 22 && ep0buf[1] == 3 && !memcmp(ep0buf + 18, "I\0n\0", 4));
    setup(0x80, 6, 0x0305, 0x0409, 255);
    assert(ep0_command == 0x60);
    ua.pw = 600;
    ua.cw = 600;
    /* Fixed-rate SET_CUR does not flush either running direction. */
    pw = ua.pw;
    setup(0x22, 1, 0x100, 0x82, 3);
    rate_data(48000, 3);
    assert(ua.pw == pw && ua.cw == 600 && ep_cnt[2] == 528);
    for (uint32_t i = 0; i < ep_cnt[2]; i++) assert(((uint8_t *)ep_tadr[2])[i] == 0);
    assert(get_rate(0x82, 0x81) == 44100);
    setup(1, 11, 1, 3, 0);
    assert(ua.play_alt == 1 && ua.pw == 0);
    assert(get_rate(2, 0x81) == 44100 && ua.cap_alt == 2);
    setup(1, 11, 3, 3, 0);
    assert(ep0_command == 0x60 && ua.play_alt == 1);
    /* Reject malformed selectors, endpoints, lengths and unconfigured access. */
    setup(0x22, 1, 0x200, 2, 3); assert(ep0_command == 0x60);
    setup(0x22, 1, 0x101, 2, 3); assert(ep0_command == 0x60);
    setup(0x22, 1, 0x100, 0x83, 3); assert(ep0_command == 0x60);
    setup(0x22, 1, 0x100, 0x102, 3); assert(ep0_command == 0x60);
    setup(0x22, 1, 0x100, 2, 2); assert(ep0_command == 0x60);
    setup(0x22, 1, 0x100, 2, 3);
    rate_data(44100, 2);
    assert(ep0_command == 0x60 && !ua_rate_pending);
    assert(get_rate(2, 0x81) == 44100);
    setup(0x22, 1, 0x100, 2, 3);
    rate_data(46000, 3);                     /* UAC1 nearest-rate rounding */
    assert(get_rate(2, 0x81) == 44100);
    setup(0x22, 1, 0x100, 2, 3);
    rate_data(96000, 3);
    assert(get_rate(2, 0x81) == 44100 && ua_feedback() == UA_NOMINAL);
    /* Abandon the OUT stage, then send an unrelated request in the same poll. */
    setup(0x22, 1, 0x100, 2, 3);
    const uint8_t get_config[8] = {0x80, 8, 0, 0, 0, 0, 1, 0};
    memcpy(ep0buf, get_config, 8);
    regs[0][S_CSR0] = 0x11;
    regs[0][S_COUNT0] = 8;
    ep0_service();
    assert(!ua_rate_pending && ep_cnt[0] == 1 && ep0buf[0] == 1);
    setup(0x22, 1, 0x100, 2, 3);
    regs[0][S_CSR0] = 4;
    ep0_service();
    assert(!ua_rate_pending);               /* SentStall also cancels OUT */
    setup(0x22, 1, 0x100, 2, 3);
    common[S_INTRUSB] = 4;
    usb_poll();                            /* bus reset restores the defaults */
    assert(!ua_rate_pending && !usb.config && !ua.play_alt && !ua.cap_alt);
    setup(0x22, 1, 0x100, 2, 3);
    assert(ep0_command == 0x60);
    usb.config = 1;
    puts("USB audio EP0: formats, rates, malformed/aborted requests, bus reset: OK");
}

static uint32_t queued_cr, queued_pw, checked_sends;
static void capture_deadline(uint32_t ep)
{
    if (ep != 2u) return;
    /* Arming the prepared packet must precede either direction's PCM work.
     * Four-channel PCM24 IN transfers occupy nearly half a USB frame. */
    assert(ua.cr == queued_cr && ua.pw == queued_pw);
    checked_sends++;
}

static void capture_pipeline(void)
{
    uint8_t prepared[UA_PACKET], active[UA_PACKET];
    for (uint32_t alt = 1; alt <= 2; alt++) {
        ua_hw_stop();
        assert(ua_set_interface(3, alt) && ua_set_interface(5, alt));
        for (uint32_t i = 0; i < UA_RING; i++)
            for (uint32_t ch = 0; ch < UA_CAP_CHANNELS; ch++)
                ua.cap[i * UA_CAP_CHANNELS + ch] = (int16_t)(1000 * (ch + 1));
        ua.cw = 800;
        checked_sends = 0;
        for (uint32_t packet = 0; packet < 6; packet++) {
            void *pending = ua_tx[ua_tx_slot];
            uint32_t bytes = ua_tx_bytes;
            memcpy(prepared, pending, bytes);
            queued_cr = ua.cr;
            queued_pw = ua.pw;
            send_check = capture_deadline;
            regs[2][S_TXCSR1] = 0;         /* host completed the previous IN */
            regs[2][S_RXCSR1] = 1;         /* simultaneous playback packet */
            regs[2][S_RXCOUNT1] = 24 * 2 * ua_sample_bytes(alt);
            regs[2][S_RXCOUNT2] = 0;
            memset(ua_rx, 0, sizeof ua_rx);
            frame(packet);
            ua_hw_poll();
            send_check = NULL;
            assert(ep_tadr[2] == pending && ep_cnt[2] == bytes);
            assert(!memcmp(prepared, ep_tadr[2], bytes));
            assert(ua.cr > queued_cr);     /* packing only after IN is armed */
            assert(ua_tx[ua_tx_slot] != ep_tadr[2] && ua_tx_bytes);
            memcpy(active, ep_tadr[2], bytes);
            queued_cr = ua.cr;
            ua_hw_poll();                 /* busy DMA and staged PCM stay intact */
            assert(ua.cr == queued_cr && !memcmp(active, ep_tadr[2], bytes));
            if (packet >= 3)
                for (uint32_t ch = 0; ch < UA_CAP_CHANNELS; ch++)
                    assert(ua_decode(active + ch * ua_sample_bytes(alt), ua_sample_bytes(alt))
                           == (int16_t)(1000 * (ch + 1)));
        }
        assert(checked_sends == 6);
        assert(ua_set_interface(5, 0) && !ua_tx_bytes);
        assert(ua_set_interface(5, alt));
        for (uint32_t i = 0; i < ep_cnt[2]; i++)
            assert(((uint8_t *)ep_tadr[2])[i] == 0); /* no old prepared audio on restart */
        for (uint32_t i = 0; i < ua_tx_bytes; i++)
            assert(ua_tx[ua_tx_slot][i] == 0);
    }
    ua_hw_stop();
    puts("USB audio capture: prebuilt packets meet refill deadlines, DMA isolation, clean restart: OK");
}

int main(void)
{
    uint32_t count;
    uint8_t previous[UA_PACKET];
    usb.up = usb.config = 1;
    common[S_INTRRX1E] = 2;                    /* MIDI endpoint must survive */
    ua_hw_stop();
    assert(common[S_INTRRX1E] == 2);
    assert(!ua_set_interface(2, 1));
    assert(!ua_set_interface(4, 1));            /* control interfaces, not streams */
    assert(!ua_set_interface(3, 3));
    usb.config = 0;
    assert(!ua_set_interface(3, 1));
    usb.config = 1;
    assert(ua_set_interface(3, 1));
    assert(ua_set_interface(5, 1));
    assert(ua.play_alt && ua.cap_alt);
    /* MaxP 0xFF keeps single packet buffering (an exact MaxP doubles it and loses
     * every other OUT packet); IN packets are queued before the first IN token */
    assert(regs[2][S_RXMAXP] == 0xFF && regs[2][S_TXMAXP] == 0xFF && regs[3][S_TXMAXP] == 0xFF);
    assert(ua.tx_packets == 1 && ep_cnt[2] == 352 && ep_cnt[3] == 3);
    assert(common[S_INTRRX1E] == 6);
    assert(regs[2][S_RXCSR2] == 0x40 && regs[2][S_TXCSR2] == 0x40);   /* ISO; direction bit clear */
    assert(regs[3][S_TXCSR2] == 0x40);
    frame(2047);
    ua_hw_poll();
    assert(ep_cnt[2] == 352 && ep_cnt[3] == 3);
    assert(ep_tadr[2] == ua_tx[0] && ep_tadr[3] == ua_fb && ep_radr[2] == ua_rx);
    assert(regs[2][S_TXCSR1] == 1 && regs[3][S_TXCSR1] == 1);
    assert(ua_fb[0] == (UA_NOMINAL & 255));
    memcpy(previous, ep_tadr[2], sizeof previous);
    count = ua.tx_packets;
    ua_hw_poll();                           /* second poll in same frame */
    assert(ua.tx_packets == count);
    frame(0);                              /* frame number wraps; DMA still busy */
    ua_hw_poll();
    assert(!ua.missed_frames && ua.tx_packets == count);
    assert(memcmp(previous, ep_tadr[2], sizeof previous) == 0);
    regs[2][S_TXCSR1] = 0;                  /* host took the capture packet: refill in the same frame */
    ua_hw_poll();
    assert(ua.tx_packets == count + 1 && regs[2][S_TXCSR1] == 1 && !ua.missed_frames);
    count++;
    regs[2][S_TXCSR1] = regs[3][S_TXCSR1] = 0;
    regs[2][S_RXCSR1] = 1;
    regs[2][S_RXCOUNT1] = 176;
    memset(ua_rx, 0, sizeof ua_rx);
    frame(1);
    ua_hw_poll();
    assert(ua.pw == 44 && ua.rx_packets == 1 && ua.tx_packets == count + 1);
    assert(regs[2][S_RXCSR1] == 0);
    regs[2][S_RXCSR1] = 5;                   /* RX overrun: corrupted packet discarded */
    ua_hw_poll();
    assert(ua.pw == 44 && ua.bad_packets == 1);
    assert(ua_set_interface(3, 0));
    assert(!ua.play_alt && ua.cap_alt && ua.pw == 0);
    assert(common[S_INTRRX1E] == 2);          /* capture and MIDI remain available */
    assert(ua_set_interface(3, 1));
    usb.suspended = 1;
    ua_hw_poll();
    assert(ua.play_alt && ua.cap_alt && ua_paused && !ua_frame_valid);
    assert(regs[2][S_TXCSR1] == 0 && regs[3][S_TXCSR1] == 0);
    assert(ua.pw == ua.pr && ua.cw == ua.cr);
    count = ua.tx_packets;
    ua_hw_poll();
    assert(ua.tx_packets == count);
    usb.suspended = 0;
    frame(300);
    ua_hw_poll();
    assert(!ua_paused && ua.tx_packets == count + 1 && !ua.missed_frames);
    /* A stream that starts after an idle stretch has not missed those frames. */
    assert(ua_set_interface(3, 0) && ua_set_interface(5, 0));
    ua_hw_poll();
    assert(!ua_frame_valid);
    assert(ua_set_interface(5, 1));
    frame(1300);
    ua_hw_poll();
    assert(ua_frame_valid && !ua.missed_frames);
    frame(1302);
    ua_hw_poll();
    assert(ua.missed_frames == 1);
    ua_hw_stop();
    assert(!ua.play_alt && !ua.cap_alt && !ua_frame_valid);
    assert(common[S_INTRRX1E] == 2);
    puts("USB audio endpoints: ownership, alternate settings, suspend/reset, frame wrap: OK");
    controls();
    capture_pipeline();
    return 0;
}
