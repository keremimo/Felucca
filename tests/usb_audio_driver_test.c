/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Exercise the production endpoint driver with a minimal indexed-SIE model.
 * This tests lifecycle and packet ownership, not the physical USB controller. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/src/usb_audio_stream.c"

enum { S_INTRRX1E = 9, S_FRAME1 = 12, S_FRAME2 = 13, S_INDEX = 14,
       S_TXMAXP = 16, S_TXCSR1 = 17, S_TXCSR2 = 18, S_RXMAXP = 19,
       S_RXCSR1 = 20, S_RXCSR2 = 21, S_RXCOUNT1 = 22, S_RXCOUNT2 = 23 };
static uint8_t regs[4][24], common[16], index_reg;
static struct { uint8_t up, config, suspended; } usb = {1, 1, 0};
static uint32_t USB_CON0, ep_cnt[4];        /* hal/fm1_usb.h stand-ins: endpoint DMA state */
static void *ep_tadr[4], *ep_radr[4];
static void fm1_usb_ep_enable(uint32_t eps) { USB_CON0 &= ~(eps << 19); }
static void fm1_usb_ep_txbuf(uint32_t ep, void *p) { ep_tadr[ep] = p; }
static void fm1_usb_ep_rxbuf(uint32_t ep, void *p) { ep_radr[ep] = p; }
static void fm1_usb_ep_send(uint32_t ep, void *p, uint32_t n) { ep_tadr[ep] = p; ep_cnt[ep] = n; }
static void fm1_usb_rx_sync(void) {}

static uint32_t sie_rd(uint32_t r)
{
    return r < 16 ? common[r] : regs[index_reg][r];
}

static void sie_wr(uint32_t r, uint32_t v)
{
    if (r == S_INDEX)
        index_reg = v;
    else if (r < 16)
        common[r] = v;
    else if ((r == S_TXCSR1 && (v & 8u)) || (r == S_RXCSR1 && (v & 16u)))
        regs[index_reg][r] = 0;                 /* model self-clearing flush */
    else
        regs[index_reg][r] = v;
}

#include "../firmware/src/usb_audio.c"

static void frame(uint16_t n)
{
    common[S_FRAME1] = n & 255;
    common[S_FRAME2] = (n >> 8) & 7;
}

int main(void)
{
    uint32_t count;
    uint8_t previous[UA_PACKET];
    common[S_INTRRX1E] = 2;                    /* MIDI endpoint must survive */
    ua_hw_stop();
    assert(common[S_INTRRX1E] == 2);
    assert(!ua_set_interface(2, 1));
    assert(!ua_set_interface(3, 2));
    usb.config = 0;
    assert(!ua_set_interface(3, 1));
    usb.config = 1;
    assert(ua_set_interface(3, 1));
    assert(ua_set_interface(4, 1));
    assert(ua.play_alt && ua.cap_alt);
    /* MaxP 0xFF keeps single packet buffering (an exact MaxP doubles it and loses
     * every other OUT packet); IN packets are queued before the first IN token */
    assert(regs[2][S_RXMAXP] == 0xFF && regs[2][S_TXMAXP] == 0xFF && regs[3][S_TXMAXP] == 0xFF);
    assert(ua.tx_packets == 1 && ep_cnt[2] == 176 && ep_cnt[3] == 3);
    assert(common[S_INTRRX1E] == 6);
    assert(regs[2][S_RXCSR2] == 0x40 && regs[2][S_TXCSR2] == 0x40);   /* ISO; direction bit clear */
    assert(regs[3][S_TXCSR2] == 0x40);
    frame(2047);
    ua_hw_poll();
    assert(ep_cnt[2] == 176 && ep_cnt[3] == 3);
    assert(ep_tadr[2] == ua_tx && ep_tadr[3] == ua_fb && ep_radr[2] == ua_rx);
    assert(regs[2][S_TXCSR1] == 1 && regs[3][S_TXCSR1] == 1);
    assert(ua_fb[0] == (UA_NOMINAL & 255));
    memcpy(previous, ua_tx, sizeof previous);
    count = ua.tx_packets;
    ua_hw_poll();                           /* second poll in same frame */
    assert(ua.tx_packets == count);
    frame(0);                              /* frame number wraps; DMA still busy */
    ua_hw_poll();
    assert(!ua.missed_frames && ua.tx_packets == count);
    assert(memcmp(previous, ua_tx, sizeof previous) == 0);
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
    ua_hw_stop();
    assert(!ua.play_alt && !ua.cap_alt && !ua_frame_valid);
    assert(common[S_INTRRX1E] == 2);
    puts("USB audio endpoints: ownership, alternate settings, suspend/reset, frame wrap: OK");
    return 0;
}
