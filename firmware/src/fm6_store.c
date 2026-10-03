/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM6 voices outside the engine (eng_fm6.c): the user bank in flash (storage.c OBJ_FM6, the
 * DX7 bytes 7-bit packed), DX7 SysEx from USB-MIDI and back (fm6_service: main loop), and the
 * actions of the STORE page.
 *
 * DX7 SysEx accepted on any channel n:
 *   F0 43 0n 00 01 1B <155 bytes> <checksum> F7    a voice (VCED) -> the FM6 part's buffer
 *   F0 43 0n 09 20 00 <4096 bytes> <checksum> F7   32 voices (VMEM) -> the user bank, saved
 *   F0 43 1n gg pp dd F7                           a voice parameter pp + 128 gg (155: the six
 *                                                  operator switches, OP1 = bit 5) -> the part
 *   F0 43 2n 00 F7 / F0 43 2n 09 F7                dump requests: the part's voice / the bank
 * The FM6 part: the selected track when it plays FM6, else part n + 1, else the first FM6 part.
 * Dexed or any DX7 librarian can so edit a part live and keep the banks. */
#define FM6_MAGIC 0x42364D46u                            /* "FM6B" */
#define FM6_PACKED (FM6_NUSER * 128u * 7u / 8u)          /* 3584: the bank as 7-bit data */
static uint8_t fm6_buf[FM6_RX] __attribute__((section(".pool")));   /* main loop: flash image, dumps out */

/* eight 7-bit bytes <-> seven: the eighth rides in the top bits of the other seven */
static void fm6_pack7(uint8_t *d, const uint8_t *s, uint32_t n)
{
    uint32_t i, j;
    for (i = 0; i < n; i += 8u, d += 7, s += 8)
        for (j = 0; j < 7u; j++)
            d[j] = (uint8_t)((s[j] & 0x7Fu) | ((s[7] >> j) & 1u) << 7);
}

static void fm6_unpack7(uint8_t *d, const uint8_t *s, uint32_t n)
{
    uint32_t i, j;
    for (i = 0; i < n; i += 8u, d += 8, s += 7) {
        d[7] = 0;
        for (j = 0; j < 7u; j++) {
            d[j] = s[j] & 0x7Fu;
            d[7] |= (uint8_t)((s[j] >> 7) << j);
        }
    }
}

static void fm6_bank_init(void)                          /* every user slot: the init voice */
{
    static int16_t ed[FM6_NP];
    uint32_t k;
    fm6_from_rom(ed, &FM6_INIT);
    for (k = 0; k < FM6_NUSER; k++)
        fm6_pack(fm6_bank[k], ed);
}

static void fm6_boot(void)                               /* the user bank from flash (main.c, after persist_boot) */
{
#if FELUCCA_FLASH
    uint8_t *b = fm6_buf;
    if (flash_ok && st_load(OBJ_FM6, b, 4u + FM6_PACKED) == (int)(4u + FM6_PACKED) &&
        (b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24) == FM6_MAGIC) {
        fm6_unpack7(&fm6_bank[0][0], b + 4, FM6_NUSER * 128u);
        return;
    }
#endif
    fm6_bank_init();
}

static int fm6_bank_save(void)                           /* 0 = saved (RAM only without flash: -1) */
{
#if FELUCCA_FLASH
    uint8_t *b = fm6_buf;
    if (!flash_ok)
        return -1;
    b[0] = (uint8_t)FM6_MAGIC;
    b[1] = (uint8_t)(FM6_MAGIC >> 8);
    b[2] = (uint8_t)(FM6_MAGIC >> 16);
    b[3] = (uint8_t)(FM6_MAGIC >> 24);
    fm6_pack7(b + 4, &fm6_bank[0][0], FM6_NUSER * 128u);
    return st_save(OBJ_FM6, b, 4u + FM6_PACKED);
#else
    return -1;
#endif
}

static int fm6_is(uint32_t k) { return k < NPART && ENGINES[trk[k].eng_req % NENGINES] == &ENG_FM6; }

static int fm6_part(uint32_t ch)                         /* the FM6 part a DX7 message on channel ch is for, -1 */
{
    uint32_t k;
    if (fm6_is(song.sel))
        return (int)song.sel;
    if (fm6_is(ch))
        return (int)ch;
    for (k = 0; k < NPART; k++)
        if (fm6_is(k))
            return (int)k;
    return -1;
}

static uint8_t fm6_chk(const uint8_t *p, uint32_t n)    /* DX7 checksum: data + it = 0 mod 128 */
{
    uint32_t s = 0;
    while (n--)
        s += *p++;
    return (uint8_t)(-s & 0x7Fu);
}

/* ------------------------------------------------------------- out --- */
#if FELUCCA_OTA
#define fm6_tx fm6_buf

static void fm6_send_voice(uint32_t p)                   /* VCED: the part's voice */
{
    uint32_t i;
    static const uint8_t H[6] = {0xF0, 0x43, 0x00, 0x00, 0x01, 0x1B};
    memcpy(fm6_tx, H, 6);
    for (i = 0; i < 155u; i++)
        fm6_tx[6 + i] = (uint8_t)fm6_ed[p][i];
    fm6_tx[161] = fm6_chk(fm6_tx + 6, 155);
    fm6_tx[162] = 0xF7;
    ota_wire_send(fm6_tx, 163);
}

static void fm6_send_bank(void)                          /* VMEM: the user bank */
{
    static const uint8_t H[6] = {0xF0, 0x43, 0x00, 0x09, 0x20, 0x00};
    memcpy(fm6_tx, H, 6);
    memcpy(fm6_tx + 6, &fm6_bank[0][0], 4096);
    fm6_tx[4102] = fm6_chk(fm6_tx + 6, 4096);
    fm6_tx[4103] = 0xF7;
    ota_wire_send(fm6_tx, FM6_RX);
}
#endif

/* ------------------------------------------------------------- in --- */
static void fm6_sysex(const uint8_t *b, uint32_t n)
{
    uint32_t i, st = b[2] & 0xF0u, ch = b[2] & 15u;
    int p = fm6_part(ch);
    char nm[12];
    if (n == 163u && st == 0x00u && b[3] == 0x00 && b[4] == 0x01 && b[5] == 0x1B &&
        fm6_chk(b + 6, 155) == b[161]) {                  /* one voice */
        if (p < 0) {
            ui_message("FM6: NO FM6 TRACK");
            return;
        }
        fm1_irq_off();
        for (i = 0; i < 155u; i++)
            fm6_set(fm6_ed[p], i, b[6 + i]);
        for (i = 0; i < 6u; i++)
            fm6_ed[p][FV_ON + i] = 1;
        fm1_irq_on();
        fm6_name(nm, fm6_ed[p]);
        ui_say("FM6 VOICE ", nm);
        ui.force = 1;
    } else if (n == FM6_RX && st == 0x00u && b[3] == 0x09 && b[4] == 0x20 && b[5] == 0x00 &&
               fm6_chk(b + 6, 4096) == b[4102]) {         /* 32 voices */
        for (i = 0; i < 4096u; i++)
            (&fm6_bank[0][0])[i] = b[6 + i] & 0x7Fu;
        ui_message(fm6_bank_save() ? "FM6 BANK (RAM)" : "FM6 BANK SAVED");
        ui.force = 1;
    } else if (n == 7u && st == 0x10u && !(b[3] >> 2) && p >= 0) {   /* a voice parameter */
        uint32_t k = (uint32_t)(b[3] & 3u) << 7 | b[4];
        if (k < 155u) {
            fm6_set(fm6_ed[p], k, b[5]);
        } else if (k == 155u) {
            for (i = 0; i < 6u; i++)
                fm6_ed[p][FV_ON + i] = (int16_t)((b[5] >> (5u - i)) & 1u);
        }
        ui.force = 1;
    } else if (n == 5u && st == 0x20u) {                 /* dump requests */
#if FELUCCA_OTA
        if (b[3] == 0x00 && p >= 0)
            fm6_send_voice((uint32_t)p);
        else if (b[3] == 0x09)
            fm6_send_bank();
#endif
    }
}

static void fm6_service(void)                            /* main loop: a DX7 frame from USB-MIDI */
{
    if (!fm6_rx_ready)
        return;
    RING_PUBLISH();                                      /* read the frame only after the flag */
    fm6_sysex(fm6_rx, fm6_rx_n);
    RING_PUBLISH();
    fm6_rx_ready = 0;
}

/* --------------------------------------------------------- STORE page --- */
/* STORE: the selected part's voice -> user slot k, and VOICE follows it (edits are kept) */
static void fm6_store(uint32_t k)
{
    uint32_t p = song.sel;
    char nm[12];
    if (!fm6_is(p) || k >= FM6_NUSER)
        return;
    fm6_pack(fm6_bank[k], fm6_ed[p]);
    fm1_irq_off();
    trk[p].p[P_E0] = (int16_t)(FM6_NROM + k);
    fm6_cur[p] = (int16_t)(FM6_NROM + k + 1u);
    fm1_irq_on();
    fm6_name(nm, fm6_ed[p]);
    ui_say(fm6_bank_save() ? "STORED (RAM) " : "STORED ", nm);
}

static void fm6_init_voice(void)                         /* INIT: the selected part starts from the init voice */
{
    if (!fm6_is(song.sel))
        return;
    fm1_irq_off();
    fm6_from_rom(fm6_ed[song.sel], &FM6_INIT);
    fm1_irq_on();
    ui_message("INIT VOICE");
}

static void fm6_send(void)                               /* SEND: the selected part's voice as a DX7 dump */
{
#if FELUCCA_OTA
    if (fm6_is(song.sel)) {
        fm6_send_voice(song.sel);
        ui_message("VOICE SENT");
    }
#else
    ui_message("NO SYSEX OUT");
#endif
}
