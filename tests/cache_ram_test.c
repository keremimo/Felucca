/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the actual boot routine against cache registers and faulty banks. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static uint32_t memory[0x30000 / 4], con, dc, ic, ticks, external;
static uint32_t writes, configured, fault_mode;
static uint32_t read_word(uint32_t a)
{
    if (a == 0x01EEE008u) return con;
    if (a == 0x01EEE00Cu) return dc ^ (fault_mode == 1u && configured ? 1u : 0u);
    if (a == 0x01EEE010u) return ic;
    if (a == 0x00010804u) return ticks++;
    if (a == 0x00040400u) return external;
    if (a == 0x00040500u) return 0;
    assert(a >= 0x01F00000u && a < 0x01F30000u && !(a & 3u));
    if (fault_mode == 2u && a == 0x01F28000u) return 0;
    if (fault_mode == 3u && a >= 0x01F28000u) a = 0x01F28000u + (a & 0xFFFu);
    return memory[(a - 0x01F00000u) / 4u];
}
static void write_word(uint32_t a, uint32_t v)
{
    writes++;
    if (a == 0x01EEE008u) { con = v; return; }
    if (a == 0x01EEE00Cu) { dc = v; configured = v == 0xAB808080u; return; }
    if (a == 0x01EEE010u) { ic = v; return; }
    assert(a >= 0x01F00000u && a < 0x01F30000u && !(a & 3u));
    if (fault_mode == 3u && a >= 0x01F28000u) a = 0x01F28000u + (a & 0xFFFu);
    memory[(a - 0x01F00000u) / 4u] = v;
}
#define MELODEE_CACHE_RAM 1
#define FM1_CACHE_HOST_TEST 1
#define FC_READ(a) read_word(a)
#define FC_WRITE(a,v) write_word(a,v)
#define FC_SYNC() ((void)0)
#include "../firmware/hal/fm1_cache.h"
static void reset(uint32_t mode)
{
    memset(memory, 0xA5, sizeof memory);
    memset(&fm1_cache, 0, sizeof fm1_cache);
    con = 0x4300u; dc = 0xABFFFFFFu; ic = 0xCDEF1234u;
    ticks = UINT32_MAX - 8u; external = writes = configured = 0; fault_mode = mode;
}
int main(void)
{
    reset(0); fm1_cache_init(1);
    assert(fm1_cache.status == FM1_CACHE_RETRY && !writes);
    reset(0); external = 0x800u; fm1_cache_init(0);
    assert(fm1_cache.status == FM1_CACHE_EXTERNAL_MEMORY && !writes);
    reset(0); con &= ~0x4000u; fm1_cache_init(0);
    assert(fm1_cache.status == FM1_CACHE_TIMEOUT && !writes);
    reset(0); fm1_cache_init(0);
    assert(fm1_cache.status == FM1_CACHE_READY && fm1_cache.test_ticks < 20u);
    assert(dc == 0xAB808080u && ic == 0xCDEFFFFFu && con == 0x4100u);
    for (uint32_t a = 0x01F28000u; a < 0x01F2F000u; a += 4u) assert(read_word(a) == 0);
    assert(read_word(0x01F27FFCu) == 0xA5A5A5A5u && read_word(0x01F2F000u) == 0xA5A5A5A5u);
    for (uint32_t mode = 1; mode <= 3; mode++) {
        reset(mode); fm1_cache_init(0);
        assert(fm1_cache.status == (mode == 1 ? FM1_CACHE_WAY_FAILED : FM1_CACHE_MEMORY_FAILED));
        assert(con == 0x4300u && dc == 0xABFFFFFFu && ic == 0xCDEF1234u);
        if (mode != 1) assert(fm1_cache.failure_address >= 0x01F28000u);
    }
    puts("cache RAM: retry, external-memory refusal, timeout, wrapping clock, full-bank test, stuck/aliased banks and rollback passed");
}
