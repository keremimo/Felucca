/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* MIDI IN on the TRS jack: PH8 -> input channel 1 -> UART1 RX, 31250 baud,
 * RX DMA into a 128-byte ring, polled from the TIMER5 ISR (no UART IRQ).
 * Built only with FELUCCA_UART=1. Channel messages go into midi_in_q next to
 * USB, as USB-MIDI packets (cable 0, CIN = status >> 4), so seq.c routes them by
 * channel as it does USB; clock and transport are queued in order with notes.
 * Other realtime, system common and SysEx are dropped.
 *
 * The ring is read by content, not by the DMA count: every slot the parser has
 * not yet been given holds UM_EMPTY, and the reader takes bytes until it meets
 * one. The count (HRXCNT, latched every poll rather than on RPND / OTPND as the
 * SDK does) once missed a byte on an FM-1 while the byte itself was in the
 * ring: the reader stayed one byte behind until reboot, so each message ended
 * only with the next byte (an MPC pad sounded on its first aftertouch and its
 * note-off held the note until the next pad). Following the content cannot drift. */
#include "../hal/fm1_uart.h"   /* registers; relative, so the host tests find it too */

#define UM_RING 128u
#define UM_EMPTY 0xFDu         /* undefined MIDI realtime byte: never sent, marks unwritten slots */
static volatile uint8_t um_ring[UM_RING] __attribute__((aligned(16)));
static struct {
    uint32_t rd, drops, msgs;
    volatile uint32_t bytes;                        /* ISR counter, read by the UI activity indicator */
    uint8_t st, need, got, d0, sysex;
} um;

static void uart_midi_init(void)                   /* before timer5_start(): PORTH RMW */
{
    uint32_t i;
    for (i = 0; i < UM_RING; i++)
        um_ring[i] = UM_EMPTY;
    fm1_uart1_midi_init(um_ring, UM_RING);
}

static uint32_t um_len(uint32_t s)                 /* data bytes of a channel status */
{
    return (s & 0xE0u) == 0xC0u ? 1u : 2u;         /* Cx Dx: 1, else 2 */
}

static void um_byte(uint32_t b)
{
    if (b >= 0xF8u) {
        if (b == 0xF8u || b == 0xFAu || b == 0xFBu || b == 0xFCu) {
            if (midi_in_enqueue(0x0Fu | b << 8, 2u, fm1_ms))
                um.msgs++;
            else
                um.drops++;
        }
        return;                                    /* realtime leaves running status intact */
    }
    if (b & 0x80u) {
        um.sysex = b == 0xF0u;
        um.st = b < 0xF0u ? (uint8_t)b : 0;        /* system common/SysEx cancel running status */
        um.need = (uint8_t)um_len(b);
        um.got = 0;
        return;
    }
    if (um.sysex || !um.st)
        return;
    if (um.need == 2u && !um.got) {
        um.d0 = (uint8_t)b;
        um.got = 1;
        return;
    }
    {
        uint32_t d1 = um.need == 2u ? um.d0 : b, d2 = um.need == 2u ? b : 0u;
        uint32_t pkt = (um.st >> 4) | (uint32_t)um.st << 8 | d1 << 16 | d2 << 24;
        um.got = 0;
        if (midi_in_enqueue(pkt, 2u, fm1_ms))
            um.msgs++;
        else
            um.drops++;
    }
}

/* the bytes the DMA has written since the last call; a received UM_EMPTY (line
 * noise) is passed over once the byte after it has landed */
static void um_drain(void)
{
    uint32_t n;
    for (n = 0; n < UM_RING; n++) {
        uint32_t b = um_ring[um.rd];
        if (b == UM_EMPTY) {
            if (um_ring[(um.rd + 1u) & (UM_RING - 1u)] == UM_EMPTY)
                break;
            b = um_ring[um.rd];                    /* the DMA writes in order: this one has landed too */
        }
        um_ring[um.rd] = UM_EMPTY;
        um.rd = (um.rd + 1u) & (UM_RING - 1u);
        um.bytes++;
        um_byte(b);                                /* a UM_EMPTY is an unknown realtime byte: ignored */
    }
}

static void uart_midi_poll(void)                   /* TIMER5 ISR, same context as usb_poll */
{
    fm1_uart1_rx_take();                           /* acknowledges the pendings; the count is not used */
    um_drain();
}
