/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM-1 in-package SDRAM probe. An AC791x part whose memory digit is A or B
 * (datasheet 4.1: 1Mx16 / 4Mx16 SDRAM) carries an SDRAM die on the internal
 * ports PE/PF/PG, which have no package pins. The stock flash head boots with
 * ENABLE_SDRAM=0 and the installer never rewrites the head, so the SPL leaves
 * the controller off; this brings it up from the app. The sequence is the AC79
 * SDK sdram_init (cpu/wl82/liba/cpu.a, LLVM bitcode) on its SDRAM path with
 * the system clock as source: DRAM power gate ramp, then per phase CLK_CON1
 * phase bits, IOMAP, PE/PF/PG drive, JL_SDR CON0..CON3. The SDK's own test and
 * its phase grid (m 1..3, i 5..6, j 0..3, k 0..3) find a working phase.
 *
 *   fm1_sdram_probe(r, keep)   main loop, IRQs on; ~0.4 s. Every register it
 *                              touches is put back afterwards, unless keep is
 *                              set and SDRAM was found (then it stays up at
 *                              0x04000000 cached / 0x08000000 uncached).
 *
 * Tests run through the uncached window, so the data cache is never involved.
 * The bus-invalid guard (fm1_guard.h) is off while the window is touched: with
 * no SDRAM die the window may not decode.
 */
#pragma once
#include <stdint.h>
#include "fm1_gpio.h"
#include "fm1_time.h"
#include "fm1_irq.h"
#include "fm1_sys.h"
#include "fm1_guard.h"

#define FM1_SDR_CON(n)    (*(volatile uint32_t *)(0x40400u + 4u * (n)))   /* JL_SDR; CON1 is write-only */
#define FM1_SDR_CLK_CON1  (*(volatile uint32_t *)0x10010u)   /* [21:20] SDRAM clock source, [26:25] j, [31:30] k */
#define FM1_SDR_SYS_DIV   (*(volatile uint32_t *)0x10008u)
#define FM1_SDR_IOMAP0    (*(volatile uint32_t *)0x5101Cu)   /* b29: ports to the SDRAM controller */
#define FM1_SDR_IOMAP3    (*(volatile uint32_t *)0x51028u)   /* b13, b17: set by the SDK with b29 */
#define FM1_SDR_CLK_BITS    0xC6300000u
#define FM1_SDR_IOMAP0_BITS 0x20000000u
#define FM1_SDR_IOMAP3_BITS 0x00022000u
#define FM1_SDR_CACHE_CON (*(volatile uint32_t *)0x1EEE008u)
#define FM1_SDR_CACHED    0x04000000u
#define FM1_SDR_UNCACHED  0x08000000u                        /* NO_CACHE_ADDR(0x04000000) */
#define FM1_P3_DRPG_CON0  0x0Cu                              /* DRAM power gate */
#define FM1_P3_DRPG_CON1  0x0Du
#define FM1_SDR_DQ_TRM    3u        /* refresh setting (SDK: 2 120 MHz .. 5 240 MHz); lower refreshes more often */
#define FM1_SDR_GEOM_8M   0xE01u    /* SDK sdram_cfg_table: CON0 = 0xE00 | cfg, 8 MB cfg 1, 2 / 4 MB cfg 0 */
#define FM1_SDR_GEOM_2M   0xE00u
#define FM1_SDR_PHASES    96u       /* index ((m - 1) * 2 + i - 5) * 16 + j * 4 + k */
#define FM1_SDR_SWEEP_WORDS 1024u   /* 4 KB per phase, as the SDK's default test size */

typedef struct {
    uint32_t found;         /* 0 none, 1 found and every check passed, 2 the sweep passed, a check failed */
    uint32_t geom;          /* CON0 base of the passing geometry (FM1_SDR_GEOM_8M / _2M), 0 none */
    uint32_t pass[3];       /* sweep pass bits of the last geometry swept */
    uint32_t best;          /* chosen phase index, 0xFFFFFFFF none */
    uint32_t errors;        /* word errors: 64 KB at every MiB of the geometry's size */
    uint32_t narrow;        /* byte / halfword errors in 256 bytes */
    uint32_t retain;        /* word errors in 256 KB read 300 ms after writing */
    uint32_t alias[8];      /* word at k MiB after writing 0x5D000000 | k at each MiB, k = 0..7 */
    uint32_t first_wr, first_rd;   /* first word of the SDK's default phase (8 MB): written, read back */
    uint32_t cache_con, clk_con1, sys_div, iomap0, drpg;   /* as found; drpg = CON0 | CON1 << 8 */
    uint32_t dbg_msg;       /* DBG_MSG after touching the window (bus-invalid bits 4, 5, 16..21) */
    uint32_t us;            /* duration */
} fm1_sdram_probe_t;

typedef struct {
    uint32_t clk1, iomap0, iomap3, sdr0, sdr2, sdr3;
    uint32_t port[3][4];    /* PE, PF, PG: DIR, DIE, HD0, HD */
    uint8_t drpg0, drpg1;
} fm1__sdr_saved_t;

static const uint8_t FM1__SDR_PREG[4] = { FM1_DIR, FM1_DIE, FM1_HD0, FM1_HD };

static void fm1__sdr_save(fm1__sdr_saved_t *s)
{
    uint32_t p, r;
    s->clk1 = FM1_SDR_CLK_CON1;
    s->iomap0 = FM1_SDR_IOMAP0;
    s->iomap3 = FM1_SDR_IOMAP3;
    s->sdr0 = FM1_SDR_CON(0);
    s->sdr2 = FM1_SDR_CON(2);
    s->sdr3 = FM1_SDR_CON(3);
    for (p = 0; p < 3u; p++)
        for (r = 0; r < 4u; r++)
            s->port[p][r] = FM1_PR(4u + p, FM1__SDR_PREG[r]);
    fm1_irq_off();                                   /* P33: nothing else may use it meanwhile */
    s->drpg0 = fm1_p33_read(FM1_P3_DRPG_CON0);
    s->drpg1 = fm1_p33_read(FM1_P3_DRPG_CON1);
    fm1_irq_on();
}

/* controller off first, then the pins, the clock bits and the power gate */
static void fm1__sdr_restore(const fm1__sdr_saved_t *s)
{
    uint32_t p, r;
    FM1_SDR_CON(0) = s->sdr0;
    FM1_SDR_CON(1) = 0;                               /* write-only: the SDK's uninit value */
    FM1_SDR_CON(2) = s->sdr2;
    FM1_SDR_CON(3) = s->sdr3;
    for (p = 0; p < 3u; p++)
        for (r = 0; r < 4u; r++)
            FM1_PR(4u + p, FM1__SDR_PREG[r]) = s->port[p][r];
    /* only the bits fm1__sdr_phase sets: CLK_CON1 also holds the USB and UART clocks */
    FM1_SDR_IOMAP0 = (FM1_SDR_IOMAP0 & ~FM1_SDR_IOMAP0_BITS) | (s->iomap0 & FM1_SDR_IOMAP0_BITS);
    FM1_SDR_IOMAP3 = (FM1_SDR_IOMAP3 & ~FM1_SDR_IOMAP3_BITS) | (s->iomap3 & FM1_SDR_IOMAP3_BITS);
    FM1_SDR_CLK_CON1 = (FM1_SDR_CLK_CON1 & ~FM1_SDR_CLK_BITS) | (s->clk1 & FM1_SDR_CLK_BITS);
    fm1_irq_off();
    fm1_p33_write(FM1_P3_DRPG_CON0, s->drpg0);
    fm1_p33_write(FM1_P3_DRPG_CON1, s->drpg1);
    fm1_irq_on();
}

/* SDK sdram_init, before its clock setup: the power gate in steps, then one OS tick */
static void fm1__sdr_power_on(void)
{
    static const uint8_t c0[] = { 2, 6, 10, 14, 1 }, c1[] = { 3, 7, 15, 31 };
    uint32_t n;
    fm1_irq_off();
    for (n = 0; n < sizeof c0; n++)
        fm1_p33_write(FM1_P3_DRPG_CON0, c0[n]);
    for (n = 0; n < sizeof c1; n++)
        fm1_p33_write(FM1_P3_DRPG_CON1, c1[n]);
    fm1_irq_on();
    fm1_delay_ms(10);
}

/* SDK SDRAM_PHASE block (4-phase path). The SDK waits 5 nops between the
 * controller writes; 2 us each here, longer is harmless for SDRAM. */
static void fm1__sdr_phase(uint32_t geom, uint32_t i, uint32_t j, uint32_t k, uint32_t m)
{
    uint32_t p;
    FM1_SDR_CLK_CON1 &= ~0x00300000u;                               /* clock source: system */
    FM1_SDR_CLK_CON1 = (FM1_SDR_CLK_CON1 & ~0x06000000u) | (j & 3u) << 25;
    FM1_SDR_CLK_CON1 = (FM1_SDR_CLK_CON1 & 0x3FFFFFFFu) | (k & 3u) << 30;
    FM1_SDR_IOMAP3 |= 0x22000u;
    FM1_SDR_IOMAP0 |= 0x20000000u;
    for (p = 4; p <= 6u; p++) {                                     /* PE, PF, PG */
        FM1_PR(p, FM1_DIE) = 0xFFFFu;
        FM1_PR(p, FM1_DIR) = 0;
        FM1_PR(p, FM1_HD) = 0xFFFFu;
        FM1_PR(p, FM1_HD0) = 0;
    }
    FM1_SDR_CON(0) = geom;
    FM1_SDR_CON(1) = 0x5A588000u;
    FM1_SDR_CON(2) = 0x51060977u;
    fm1_delay_us(2);
    FM1_SDR_CON(1) = 0x5A588000u | FM1_SDR_DQ_TRM;
    fm1_delay_us(2);
    FM1_SDR_CON(0) = geom | 0x310040u;
    fm1_delay_us(2);
    FM1_SDR_CON(0) = geom | 0x100u;
    fm1_delay_us(2);
    FM1_SDR_CON(3) = i << 28 | m << 8 | 0x01080012u;
    fm1_delay_us(2);
}

static uint32_t fm1__sdr_rnd(uint32_t x)
{
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

/* write every word first, then read back: a floating bus keeps only the last value */
static uint32_t fm1__sdr_words(uint32_t off, uint32_t words, uint32_t seed)
{
    volatile uint32_t *w = (volatile uint32_t *)(FM1_SDR_UNCACHED + off);
    uint32_t i, x = seed, bad = 0;
    for (i = 0; i < words; i++)
        w[i] = x = fm1__sdr_rnd(x);
    for (i = 0, x = seed; i < words; i++)
        bad += w[i] != (x = fm1__sdr_rnd(x));
    return bad;
}

static uint32_t fm1__sdr_narrow(uint32_t off)
{
    volatile uint8_t *b = (volatile uint8_t *)(FM1_SDR_UNCACHED + off);
    volatile uint16_t *h = (volatile uint16_t *)(FM1_SDR_UNCACHED + off + 128u);
    uint32_t i, bad = 0;
    for (i = 0; i < 128u; i++)
        b[i] = (uint8_t)(i * 37u + 11u);
    for (i = 0; i < 64u; i++)
        h[i] = (uint16_t)(i * 4099u + 0x5A5Au);
    for (i = 0; i < 128u; i++)
        bad += b[i] != (uint8_t)(i * 37u + 11u);
    for (i = 0; i < 64u; i++)
        bad += h[i] != (uint16_t)(i * 4099u + 0x5A5Au);
    return bad;
}

static void fm1__sdr_phase_ix(uint32_t geom, uint32_t ix)
{
    uint32_t mi = ix >> 4;
    fm1__sdr_phase(geom, 5u + (mi & 1u), (ix >> 2) & 3u, ix & 3u, 1u + (mi >> 1));
}

/* sweep the SDK's grid; 1 when any phase passed */
static int fm1__sdr_sweep(uint32_t geom, uint32_t pass[3])
{
    uint32_t ix, any = 0;
    pass[0] = pass[1] = pass[2] = 0;
    for (ix = 0; ix < FM1_SDR_PHASES; ix++) {
        fm1__sdr_phase_ix(geom, ix);
        if (fm1__sdr_words(0, FM1_SDR_SWEEP_WORDS, 0x9E3779B9u ^ ix) == 0) {
            pass[ix >> 5] |= 1u << (ix & 31u);
            any = 1;
        }
    }
    return (int)any;
}

/* the passing phase with the most passing neighbours (j +-1, k +-1, same m and i) */
static uint32_t fm1__sdr_best(const uint32_t pass[3])
{
    uint32_t ix, best = 0xFFFFFFFFu, top = 0;
    for (ix = 0; ix < FM1_SDR_PHASES; ix++) {
        uint32_t j = (ix >> 2) & 3u, k = ix & 3u, score = 0, dj, dk;
        if (!(pass[ix >> 5] >> (ix & 31u) & 1u))
            continue;
        for (dj = 0; dj < 3u; dj++)
            for (dk = 0; dk < 3u; dk++) {
                uint32_t jj = j + dj - 1u, kk = k + dk - 1u, n;
                if (jj > 3u || kk > 3u)                              /* -1 wraps to 0xFFFFFFFF */
                    continue;
                n = (ix & ~15u) | jj << 2 | kk;
                score += pass[n >> 5] >> (n & 31u) & 1u;
            }
        if (score > top) {
            top = score;
            best = ix;
        }
    }
    return best;
}

static void fm1_sdram_probe(fm1_sdram_probe_t *r, int keep)
{
    fm1__sdr_saved_t s;
    uint32_t t0 = fm1_ticks(), dbg_en, k, mb;
    volatile uint32_t *w = (volatile uint32_t *)FM1_SDR_UNCACHED;
    uint32_t *p = (uint32_t *)r;
    /* the bus-invalid enables; volatile so ~bus never becomes an immediate:
     * 0xFFC0FFCF lies in the mask-ROM window build.py rejects */
    volatile uint32_t bus = (0x3Fu << 16) | (0x3u << 4);
    for (k = 0; k < sizeof *r / 4u; k++)
        p[k] = 0;
    r->best = 0xFFFFFFFFu;
    fm1__sdr_save(&s);
    r->cache_con = FM1_SDR_CACHE_CON;
    r->clk_con1 = s.clk1;
    r->sys_div = FM1_SDR_SYS_DIV;
    r->iomap0 = s.iomap0;
    r->drpg = s.drpg0 | (uint32_t)s.drpg1 << 8;

    fm1_irq_off();                                   /* bus-invalid guard off while the window is touched */
    fm1__dbg_unlock();
    dbg_en = FM1_DBG_EN;
    FM1_DBG_EN = dbg_en & ~bus;
    FM1_DBG_MSG_CLR = 0xFFFFFFFFu;
    fm1__dbg_lock();
    fm1_irq_on();

    fm1__sdr_power_on();
    fm1__sdr_phase(FM1_SDR_GEOM_8M, 6, 3, 0, 2);     /* the SDK table's default phase for 8 MB */
    w[0] = r->first_wr = 0xC0FFEE11u;
    w[1] = 0x3FA5115Au;                              /* something else on the bus before reading back */
    r->first_rd = w[0];

    if (fm1__sdr_sweep(FM1_SDR_GEOM_8M, r->pass))
        r->geom = FM1_SDR_GEOM_8M;
    else if (fm1__sdr_sweep(FM1_SDR_GEOM_2M, r->pass))
        r->geom = FM1_SDR_GEOM_2M;
    if (r->geom) {
        r->best = fm1__sdr_best(r->pass);
        fm1__sdr_phase_ix(r->geom, r->best);
        mb = r->geom == FM1_SDR_GEOM_8M ? 8u : 2u;
        for (k = 0; k < mb; k++)
            r->errors += fm1__sdr_words(k << 20, 16384u, 0x01234567u + k);
        r->narrow = fm1__sdr_narrow(0x1000u);
        for (k = 0; k < 8u; k++)
            w[(k << 20) / 4u] = 0x5D000000u | k;
        for (k = 0; k < 8u; k++)
            r->alias[k] = w[(k << 20) / 4u];
        {
            uint32_t i, x = 0xA5A5F00Du, n = 65536u;     /* 256 KB */
            for (i = 0; i < n; i++)
                w[i] = x = fm1__sdr_rnd(x);
            fm1_delay_ms(300);                       /* the watchdog (8 s) outlasts the whole probe */
            for (i = 0, x = 0xA5A5F00Du; i < n; i++)
                r->retain += w[i] != (x = fm1__sdr_rnd(x));
        }
        r->found = r->errors || r->narrow || r->retain ? 2u : 1u;
    }
    r->dbg_msg = FM1_DBG_MSG;

    if (!(keep && r->found == 1u))
        fm1__sdr_restore(&s);
    fm1_irq_off();
    fm1__dbg_unlock();
    FM1_DBG_MSG_CLR = 0xFFFFFFFFu;
    FM1_DBG_EN = dbg_en;
    fm1__dbg_lock();
    fm1_irq_on();
    r->us = (fm1_ticks() - t0) / FM1_TICKS_PER_US;
}
