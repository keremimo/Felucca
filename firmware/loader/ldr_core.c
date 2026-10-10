/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Melodee update loader, the portable part (step 2 of the M-UPGRADE update
 * protocol).
 *
 * The host serves the package ("logical image") with cmd 0x30 reads (ota.c).
 * The loader:
 *   1. reads the UFW header and finds flash.bin;
 *   2. checks that the package's app area decrypts with THIS chip's key
 *      (taken from isd_config.ini in the device's own flash head), so a
 *      package for another key is refused before anything is erased;
 *   3. moves the Prophet user banks from 0x89000 (below an older, smaller app area's end) to 0x93000, then
 *      writes ONLY the app area [0x4000, 0x93000), sector by sector, skipping
 *      sectors that are already equal, and verifies each one; the flash head
 *      (SPL, isd_config) is never written;
 *   4. asks 0xF0000000 ("success"), then invalidates the update record (RAM
 *      and flash) so the SPL boots the new app, and resets.
 * Power loss during 3: the record is still there, the SPL runs the loader
 * again on the next power-on and the host can resume.
 *
 * Hooks from the platform (loader.c, ldr_test.c):
 *   ldr_fread(off, p, n)  ldr_erase(off)  ldr_prog(off, p, n)   flash, 0 = ok
 *   ldr_record_clear()    forget the RAM update record
 *   ldr_progress(done, total)
 * plus the ota.c hooks (frames, time, idle). */
#define LDR_APP_LO 0x4000u
#define LDR_APP_HI 0x93000u
#define LDR_P5_OLD 0x89000u                     /* the Prophet user banks (storage.c OBJ_P5BANK0.., ten sectors: five */
#define LDR_P5_NEW 0x93000u                     /* banks, copies A B) before and since the app area grew to 0x93000 */
#define LDR_P5_SECTORS 10u
#define LDR_P5_OBJ0 25u                         /* storage.c OBJ_P5BANK0 (prophet_integration_test checks it) */
#define LDR_REC_LO 0x93000u                     /* update records live above the app ... */
#define LDR_REC_HI 0xFC000u                     /* ... and below Melodee's globals */

static int ldr_fread(uint32_t off, void *p, uint32_t n);
static int ldr_erase(uint32_t off);
static int ldr_prog(uint32_t off, const void *p, uint32_t n);
static void ldr_record_clear(void);
static void ldr_progress(uint32_t done, uint32_t total);

/* SFC cipher: per 32-byte block, key ^ (block offset >> 2) */
static void ldr_sfc(uint8_t *p, uint32_t n, uint32_t base_off, uint32_t key)
{
    uint32_t i, j;
    for (i = 0; i < n; i += 32u) {
        uint32_t k = key ^ ((base_off + i) >> 2);
        for (j = 0; j < 32u && i + j < n; j++) {
            p[i + j] ^= (uint8_t)k;
            k = ((k << 1) ^ (k & 0x8000u ? 0x1021u : 0u)) & 0xFFFFu;
        }
    }
}

/* chip key from the device's flash head: JLFS top entries (ENC 0xFFFF) -> isd_config.ini blob */
static int ldr_chip_key(uint32_t *key)
{
    uint8_t e[32], blob[34];
    uint32_t off, i, sum, k;
    for (off = 32; off < 0x400u; off += 32u) {
        if (ldr_fread(off, e, 32))
            return -1;
        ota_jl_enc(e, 32);
        if (ota_crc16(e + 2, 30, 0) != ota_rd16(e))
            return -2;
        if (!ota_memeq(e + 16, (const uint8_t *)"isd_config.ini", 15)) {
            if (ota_rd16(e + 14))
                return -3;                               /* last entry, not found */
            continue;
        }
        if (ldr_fread(ota_rd32(e + 4), blob, 34) || ota_crc16(blob, 32, 0) != ota_rd16(blob + 32))
            return -4;
        for (i = 0, sum = 0; i < 16u; i++)
            sum += blob[i];
        sum &= 0xFFu;
        sum = sum >= 0xE0u ? 0xAAu : sum <= 0x10u ? 0x55u : sum;
        for (i = 0, k = 0; i < 16u; i++)
            if ((uint32_t)(blob[16 + i] ^ blob[15 - i]) < sum)
                k |= 1u << i;
        *key = k;
        return 0;
    }
    return -3;
}

/* storage.c's commit record (magic, type, copy, .., header CRC-32) of Prophet bank s / 2, copy s % 2 */
static int ldr_p5_rec(const uint8_t *h, uint32_t s)
{
    uint32_t c = 0xFFFFFFFFu, i, j;
    for (i = 0; i < 28u; i++)                           /* (zlib CRC-32, as st_crc32) */
        for (c ^= h[i], j = 0; j < 8u; j++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    return ota_rd32(h) == 0x554C4546u && ota_rd16(h + 4) == LDR_P5_OBJ0 + s / 2u && ota_rd16(h + 6) == (s & 1u) &&
           ota_rd32(h + 28) == ~c;
}
/* the Prophet user banks to their place above the app area, before the app is written over the old one: a sector
 * whose new place holds its commit record is done (the record goes in last, so a copy cut short never has one); an
 * old one without a record has nothing to move (never saved, or written over by an earlier session's app) */
static int ldr_p5_move(uint8_t *sec, uint8_t *cur)
{
    uint32_t s, k, tries;
    for (s = 0; s < LDR_P5_SECTORS; s++) {
        uint32_t from = LDR_P5_OLD + s * 0x1000u, to = LDR_P5_NEW + s * 0x1000u;
        if (ldr_fread(to, cur, 32))
            return -8;
        if (ldr_p5_rec(cur, s))
            continue;
        if (ldr_fread(from, sec, 0x1000u))
            return -8;
        if (!ldr_p5_rec(sec, s))
            continue;
        for (tries = 0; tries < 2u; tries++) {
            if (ldr_erase(to))
                return -9;
            for (k = 256u; k < 0x1000u; k += 256u)      /* the body, then the page with the record */
                if (ldr_prog(to + k, sec + k, 256))
                    return -10;
            if (ldr_prog(to, sec, 256))
                return -10;
            if (ldr_fread(to, cur, 0x1000u))
                return -8;
            if (ota_memeq(sec, cur, 0x1000u))
                break;
        }
        if (tries == 2u)
            return -11;
    }
    return 0;
}

static int ldr_session(void)
{
    static uint8_t hdr[0x400], sec[0x1000], cur[0x1000];
    uint32_t i, k, fl_off, fl_size, ota_off, ota_len, key, s, done = 0;
    int rc;
    /* 1. UFW header + entry list (ota.c) */
    if ((rc = ota_ufw(hdr, &fl_off, &fl_size, &ota_off, &ota_len)) != 0)
        return rc;
    if (!fl_off || fl_size != LDR_APP_HI)
        return -3;
    /* 2. the package's app area must decrypt with this chip's key */
    if ((rc = ldr_chip_key(&key)) != 0)
        return -40 + rc;
    if (ota_read(fl_off + LDR_APP_LO, sec, 32))
        return -5;
    ldr_sfc(sec, 32, 0, key);
    if (ota_crc16(sec + 2, 30, 0) != ota_rd16(sec))
        return -6;                                       /* package for another chip key */
    /* 3. the Prophet banks up, then the app area, sector by sector */
    if ((rc = ldr_p5_move(sec, cur)) != 0)
        return rc;
    for (s = LDR_APP_LO; s < LDR_APP_HI; s += 0x1000u) {
        for (k = 0; k < 0x1000u; k += 512u)
            if (ota_read(fl_off + s + k, sec + k, 512))
                return -7;
        if (ldr_fread(s, cur, 0x1000u))
            return -8;
        if (!ota_memeq(sec, cur, 0x1000u)) {
            uint32_t tries;
            for (tries = 0; tries < 2u; tries++) {
                if (ldr_erase(s))
                    return -9;
                for (k = 0; k < 0x1000u; k += 256u)
                    if (ldr_prog(s + k, sec + k, 256))
                        return -10;
                if (ldr_fread(s, cur, 0x1000u))
                    return -8;
                if (ota_memeq(sec, cur, 0x1000u))
                    break;
            }
            if (tries == 2u)
                return -11;                              /* verify failed twice */
        }
        done += 0x1000u;
        ldr_progress(done, LDR_APP_HI - LDR_APP_LO);
    }
    /* 4. finish: the host confirms, the record goes, the SPL boots the app */
    for (i = 0; i < 4u; i++)
        if (!ota_read(0xF0000000u, sec, 8))
            break;
    ldr_record_clear();
    for (s = LDR_REC_HI; s > LDR_REC_LO; s -= 0x1000u) {   /* flash records: 4K boundary - 256 */
        uint8_t r[80];
        if (ldr_fread(s - 0x100u, r, 80))
            break;
        if (ota_rd16(r + 6) == 0x5441u && ota_rd16(r) && ota_rd16(r) == ota_crc16(r + 2, 78, 0))
            ldr_erase(s - 0x1000u);
    }
    return 0;
}
