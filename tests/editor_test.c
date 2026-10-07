/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The firmware editor handler, USB framing and UART recovery against RAM flash.
 * Build with the generated tables and the same flags as hostsim.c. */
static unsigned char host_samples[3][0x14000];
#define SMP_USER_XIP(k) host_samples[k]
#define MELODEE_OTA 1
#define MELODEE_FLASH 0
#define MELODEE_VERSION "TEST"
#define main hostsim_main
#include "hostsim.c"
#undef main

static uint32_t host_progress = 1, host_erases, host_writes;
static uint8_t host_wire[4096];
static uint32_t host_wire_n;
static void host_drain(void)
{
    while (so_r != so_w) {
        uint32_t p = sx_out_q[so_r++ % SXQ], cin = p & 15u, i;
        uint32_t n = cin == 4u || cin == 7u ? 3u : cin == 6u ? 2u : 1u;
        for (i = 0; i < n && host_wire_n < sizeof host_wire; i++)
            host_wire[host_wire_n++] = (uint8_t)(p >> (8u * (i + 1u)));
    }
}
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { host_drain(); fm1_ms++; }
static void fm1_wdt_feed(void)
{
    fm1_ms++;
    if (host_progress && transport_req == 2u) { seq_stop(); transport_req = 0; }
}
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static int32_t fm1_enc_take(uint32_t e) { (void)e; return 0; }
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; }
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
static void panel_setup(void) {}
#include "../firmware/src/upreset.c"
#include "../firmware/src/project.c"

static uint32_t flash_ok = 1;
static void audio_silence(void) {}
static void fl_inval(uint32_t off, uint32_t n) { (void)off; (void)n; }
static uint8_t *host_flash_ptr(uint32_t off) { return &host_samples[0][0] + off - 0xA0000u; }
static int fl_erase4k(uint32_t off, uint32_t *took)
{
    memset(host_flash_ptr(off), 0xFF, 4096); *took = 0; host_erases++; return 0;
}
static int fl_write(uint32_t off, const void *p, uint32_t n)
{
    memcpy(host_flash_ptr(off), p, n); host_writes++; return 0;
}
static int st_read(uint32_t off, void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static int st_prog(uint32_t off, const void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static int st_erase(uint32_t off) { (void)off; return -1; }
#include "../firmware/src/storage.c"
#include "../firmware/src/editor.c"
#include "../firmware/src/cz_store.c"

static int check(const char *what, int ok)
{
    printf("editor: %-70s %s\n", what, ok ? "ok" : "FAIL");
    return !ok;
}
static void reset(void)
{
    uint32_t t, i;
    memset(&song, 0, sizeof song); memset(trk, 0, sizeof trk);
    memset(&chain, 0, sizeof chain); chain_defaults(&chain_config);
    pattern_init();
    memset(&ed_w, 0, sizeof ed_w); memset(&ui, 0, sizeof ui);
    memset(&favorites, 0, sizeof favorites); memset(&settings, 0, sizeof settings); settings_init();
    memset(proj_slot, 0, sizeof proj_slot); memset(up_bank, 0, sizeof up_bank);
    memset(&um, 0, sizeof um);
    host_progress = 1; host_erases = host_writes = host_wire_n = 0;
    transport_req = panic_req = 0; sx_ready = sx_collect = sx_busy = 0;
    so_r = so_w = mi_r = mi_w = 0; midi_in_overflow = 0; usb.config = 1;
    for (i = 0; i < G_COUNT; i++) song.g[i] = GP[i].def;
    for (t = 0; t < NTRK; t++) {
        track_defaults(&trk[t]); set_engine_of(&trk[t], 0); apply_preset_to(&trk[t], 0);
        trk[t].engine = trk[t].eng_req; track_defaults_steps(&trk[t]);
    }
}
static uint32_t request(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    uint32_t i;
    host_wire_n = 0; ota_frame_done();
    sysex_byte(0xF0); sysex_byte(ED_HDR0); sysex_byte(ED_HDR1); sysex_byte(ED_HDR2); sysex_byte((uint8_t)cmd);
    for (i = 0; i < n; i++) sysex_byte(a[i]);
    sysex_byte(0xF7); ed_service(); host_drain();
    return host_wire_n;
}
static uint32_t pack7(const uint8_t *p, uint32_t n, uint8_t *a)
{
    uint32_t o = 0, i;
    while (n) {
        uint32_t k = n > 7u ? 7u : n, m = o++;
        a[m] = 0;
        for (i = 0; i < k; i++, n--) { a[m] |= (*p >> 7) << i; a[o++] = *p++ & 127u; }
    }
    return o;
}

static int preferences(void)
{
    int bad = 0;
    uint8_t a[4] = {0, 7, 0, 0};
    reset();
    uint32_t n = request(ED_INFO, a, 0);
    bad += check("INFO explicitly tags display capabilities after SONG without changing command 33",
        ED_SONG == 33 && ED_UI_STATE == 34 && ED_FAV_SET == 38 &&
        host_wire[n - 26] == 0 && host_wire[n - 25] == 0x55 &&
        host_wire[n - 24] == 1 && host_wire[n - 23] == 9 &&
        host_wire[n - 22] == 0x4d && host_wire[n - 21] == 1 &&
        host_wire[n - 20] == MOTION_MAX && host_wire[n - 19] == 1 &&
        host_wire[n - 18] == 0x42 && host_wire[n - 17] == 1 && host_wire[n - 16] == 3 &&
        host_wire[n - 15] == 0x50 && host_wire[n - 14] == 1 && host_wire[n - 13] == NPAT && host_wire[n - 12] == CHAIN_ROWS &&
        host_wire[n - 11] == 0x46 && host_wire[n - 10] == 1 && host_wire[n - 9] == FM6_NFAC &&
        host_wire[n - 8] == FM6_BANK_N && host_wire[n-7]==0x43 && host_wire[n-6]==1 && host_wire[n-5]==16 && host_wire[n-4]==1 && host_wire[n-3]==8 && host_wire[n-2]==16);
    request(ED_UI_SET, a, 2);
    bad += check("UI_SET updates the actual palette and reports RAM-only saving",
        host_wire[5] == 3 && settings.palette == 7 && T_BG == UI_PALETTES[7].bg);
    a[1] = NPALETTES; request(ED_UI_SET, a, 2);
    bad += check("out-of-range palette leaves the display unchanged", host_wire[5] == 1 && settings.palette == 7);
    a[0] = 1; a[1] = 1; request(ED_UI_SET, a, 2);
    bad += check("the retired font weight is not supported (rc 2), UI_STATE says 127",
        host_wire[5] == 2 && host_wire[8] == 9 && host_wire[10] == 127);
    a[0] = 2; request(ED_UI_SET, a, 2);
    bad += check("unsupported preference is reported without applying it", host_wire[5] == 2);
    a[0] = ENGI_DRUM; a[1] = 0; a[2] = 64; a[3] = 1;
    request(ED_FAV_SET, a, 4);
    bad += check("FAV_SET marks DRUM as a normal engine", host_wire[5] == 3 && favorite_has(ENGI_DRUM, 0));
    request(ED_FAV_GET, a, 4);
    bad += check("FAV_GET reads the bounded favorite range", host_wire[5] == 0 && host_wire[10] == 1);
    a[0] = NENGINES; a[1] = 31; request(ED_FAV_SET, a, 4);
    bad += check("empty user slot cannot be starred", host_wire[5] == 1 && !favorite_has(NENGINES, 31));
    up_store(31, "Saved"); request(ED_FAV_SET, a, 4);
    bad += check("saved user slot can be starred without changing its sound", host_wire[5] == 3 && favorite_has(NENGINES, 31));
    a[3] = 32; request(ED_FAV_GET, a, 4);
    bad += check("favorite range cannot cross the end of user slots", host_wire[5] == 1);
    request(ED_FAV_SET, a, 3);
    bad += check("short favorite writes return an error without reading absent bytes", host_wire[5] == 1);
    bad += check("UI_STATE rejects unexpected request bytes", request(ED_UI_STATE, a, 1) == 0);
    a[0] = 0; request(ED_SONG, a, 1);
    bad += check("SONG still responds through its original command", host_wire_n > 6 && host_wire[4] == 33 && host_wire[5] == 0);
    return bad;
}

static int framing(void)
{
    uint32_t i, bad = 0;
    static const uint8_t rt[] = {0xF0, 0x7D, 0xF8, 0x46, 0xFE, 0x4C, ED_PING, 0xFF, 0xF7};
    static const uint8_t aborted[] = {0xF0, 0x7D, 0x46, 0x4C, ED_SET, 0, P_LEVEL, 0x90, 0, 64, 0xF7};
    reset();
    for (i = 0; i < sizeof rt; i++) sysex_byte(rt[i]);
    ed_service(); host_drain();
    bad += check("realtime bytes interleaved in SysEx do not enter the editor frame",
                 host_wire_n == 7u && host_wire[4] == ED_PING && host_wire[5] == 0);
    ota_frame_done(); host_wire_n = 0;
    for (i = 0; i < sizeof aborted; i++) sysex_byte(aborted[i]);
    ed_service(); host_drain();
    bad += check("a non-realtime status aborts SysEx without changing parameters",
                 !sx_ready && !host_wire_n && TSEL->p[P_LEVEL] == TP[P_LEVEL].def);
    bad += check("a valid request works after an aborted frame", request(ED_PING, 0, 0) == 7u);
    ota_frame_done();
    midi_in_event(0x467DF004u);                          /* F0 7D 46 */
    midi_in_event(0x00004C04u);                          /* unfinished SysEx */
    midi_in_event(0x643C9009u);
    sysex_byte(0xF7);
    bad += check("a channel event in another USB packet aborts an unfinished SysEx",
                 !sx_ready && mi_w == 1u && midi_in_q[0] == 0x643C9009u);
    midi_in_event(0x643C8009u);                          /* wrong CIN */
    midi_in_event(0x643CFF09u);                          /* wrong status */
    midi_in_event(0xFF3C9009u);                          /* not a seven-bit velocity */
    midi_in_event(0x64FF9009u);                          /* not a seven-bit note */
    bad += check("USB channel packets reject mismatched CIN and high data bits", mi_w == 1u);
    midi_in_event(0xFF05C00Cu);                          /* one-byte status: padding is ignored */
    midi_in_event(0xFF07D00Du);
    bad += check("USB program and channel pressure keep their one-byte payload", mi_w == 3u);
    mi_w = UINT32_MAX - 5u; mi_r = mi_w;
    for (i = 0; i < MQ + 8u; i++) midi_in_event(0x643C9009u);
    bad += check("USB MIDI ring stops at capacity across counter wrap", mi_w - mi_r == MQ && midi_in_overflow);
    mi_r = mi_w; midi_in_event(0x643C9009u);
    bad += check("USB rejects new messages until the audio consumer clears overflow", mi_w == mi_r);
    midi_in_overflow = 0; midi_in_event(0x643C9009u);
    bad += check("USB resumes after the audio consumer clears overflow", mi_w - mi_r == 1u);
    return bad;
}

static int uart_recovery(void)
{
    uint32_t i;
    int bad = 0;
    reset();
    um_byte(0x90); um_byte(50);
    for (i = 0; i < UM_RING; i++) um_ring[i] = 0;
    um_ring[126] = 0x90; um_ring[127] = 72; um_ring[0] = 99;
    uart_midi_take(UM_RING + 1u);
    bad += check("UART DMA overrun never combines a stale partial note with new data",
                 !mi_w && midi_in_overflow && um.drops == 2u && um.bytes == UM_RING);
    midi_in_overflow = 0; um_byte(0x90); um_byte(72); um_byte(99);
    bad += check("UART receives a complete fresh note after DMA recovery",
                 mi_w == 1u && midi_in_q[0] == 0x63489009u);
    reset();
    um_byte(0x90); um_byte(50);
    for (i = 0; i < UM_RING; i++) um_ring[i] = 60;
    uart_midi_take(UM_RING + 2u);
    bad += check("UART discards running-status data after lost bytes until a new status",
                 !mi_w && midi_in_overflow && um.drops == 2u && !um.st && !um.got);
    midi_in_overflow = 0;
    um_byte(0x91); um_byte(70); um_byte(100);
    bad += check("UART recovers immediately on a complete channel message", mi_w == 1u && midi_in_q[0] == 0x64469109u);
    reset(); mi_w = MQ;
    um_byte(0x90); um_byte(60); um_byte(100);
    bad += check("UART queue overflow latches the same recovery flag as USB", midi_in_overflow && um.drops == 1u && mi_w == MQ);
    mi_r = mi_w; um_byte(61); um_byte(101);
    bad += check("UART rejects a new stream until audio clears overflow", mi_w == mi_r && um.drops == 2u);
    midi_in_overflow = 0; um_byte(62); um_byte(102);
    bad += check("UART resumes valid running status after audio clears overflow",
                 mi_w - mi_r == 1u && midi_in_q[mi_r % MQ] == 0x663E9009u);
    return bad;
}

static int steps(void)
{
    uint8_t a[80] = {0, 1, 60, 0, 0, 0, ST_NOTE, SF_ACCENT, 100, 0x12, 0x02, 1};
    step_t before;
    uint32_t n, ok = 1;
    int bad = 0;
    reset();
    TSEL->step[0].hit = TSEL->step[0].acc = 0x80;
    bad += check("legacy 8-byte step writes preserve lane data",
                 request(ED_STEP_SET, a, 9) == 19u && TSEL->step[0].note[0] == 60 &&
                 TSEL->step[0].hit == 0x80 && TSEL->step[0].acc == 0x80);
    bad += check("full grid step writes preserve high lane bits and constrain accents",
                 request(ED_STEP_SET, a, 12) == 19u && TSEL->step[0].hit == 0x92 && TSEL->step[0].acc == 2);
    before = TSEL->step[0]; a[2] = 71;
    for (n = 2; n <= sizeof a; n++) {
        if (n == 9u || n == 12u || n == 13u) continue;
        ok &= !request(ED_STEP_SET, a, n) && !memcmp(&before, &TSEL->step[0], sizeof before);
    }
    bad += check("partial or oversized step payloads never mutate a valid step", ok);
    a[0] = 1; a[1] = 0; memcpy(a + 2, (const uint8_t[]){1,64,0,0,0,ST_NOTE,0,99}, 8);
    bad += check("TRACK_STEP accepts its legacy payload on an unselected track",
                 request(ED_TRACK_STEP, a, 10) == 20u && trk[1].step[0].note[0] == 64 && song.sel == 0);
    before = trk[1].step[0]; a[3] = 65;
    bad += check("TRACK_STEP rejects an incomplete grid extension",
                 !request(ED_TRACK_STEP, a, 11) && !memcmp(&before, &trk[1].step[0], sizeof before));
    return bad;
}


static int song_protocol(void)
{
    uint8_t a[] = {1, 1, 0, 2};
    chain_config_t before;
    int bad = 0;
    reset();
    bad += check("SONG sets and queries the complete chain without changing selection",
                 request(ED_SONG, a, sizeof a) == 14u && !host_wire[6] && chain_config.count == 1 && song.sel == 0);
    before = chain_config; a[3] = 0;
    bad += check("SONG invalid repeats leave the chain unchanged",
                 request(ED_SONG, a, sizeof a) == 14u && host_wire[6] == 1 && !memcmp(&before, &chain_config, sizeof before));
    a[3] = 2; chain.armed = 1;
    bad += check("SONG cannot replace a chain while its start is pending",
                 request(ED_SONG, a, sizeof a) == 14u && host_wire[6] == 2 && !memcmp(&before, &chain_config, sizeof before));
    chain.armed = 0; a[0] = 2;
    bad += check("SONG PLAY refuses extra argument bytes", !request(ED_SONG, a, 2) && !transport_req);
    return bad;
}

static int malformed_saves(void)
{
    static const uint8_t slot[] = {1, 4}, name[] = {0, 'X', 0, 'Y'};
    const uint8_t save[] = {1, 0};
    project_store_t old;
    int bad = 0;
    reset();
    bad += check("PROJECT refuses an invalid slot instead of wrapping to slot 0",
                 !request(ED_PROJECT, slot, sizeof slot) && !project_used(0));
    bad += check("UP_STORE rejects bytes after its name terminator without saving",
                 request(ED_UP_STORE, name, sizeof name) == 8u && host_wire[6] == 1 && !up_used(0));
    bad += check("a successful PROJECT save confirms its slot", request(ED_PROJECT, save, sizeof save) == 9u && project_used(0));
    old = proj_slot[0];
    TSEL->p[P_LEVEL] = 21; song.playing = 1; host_progress = 0;
    bad += check("PROJECT never confirms an old used slot when STOP timed out",
                 !request(ED_PROJECT, save, sizeof save) && !memcmp(&old, &proj_slot[0], sizeof old));
    return bad;
}

/* FM6 patches (cmds 68..71): a track's own patch, the bank (RAM here), the factory patches, malformed frames */
static int fm6_patches(void)
{
    int bad = 0;
    uint8_t a[2 + FM6_PACKED], pk[FM6_PACKED];
    uint32_t n, i;
    reset();
    fm6_bank_check(-1);
    a[0] = ED_FM6_FACTORY; a[1] = 3;
    n = request(ED_FM6_GET, a, 2);
    bad += check("FM6_GET factory 4: the packed record", n == 5u + 3u + FM6_PACKED + 1u && host_wire[7] == 0 &&
                 !memcmp(host_wire + 8, FM6_FACTORY[3], FM6_PACKED));
    memcpy(pk, FM6_FACTORY[3], FM6_PACKED);
    memcpy(pk + 118, "MY PATCH  ", 10);
    a[0] = ED_FM6_TRACK; a[1] = 2; memcpy(a + 2, pk, FM6_PACKED);
    n = request(ED_FM6_PUT, a, sizeof a);
    bad += check("FM6_PUT track 3: rc 0, the track's patch", n == 9u && host_wire[7] == 0 &&
                 !memcmp(fm6_patch[2] + FP_NAME, "MY PATCH  ", 10));
    a[1] = 2;
    n = request(ED_FM6_GET, a, 2);
    bad += check("FM6_GET track 3: as sent", host_wire[7] == 0 && !memcmp(host_wire + 8, pk, FM6_PACKED));
    a[0] = ED_FM6_BANK; a[1] = 4;
    n = request(ED_FM6_GET, a, 2);
    bad += check("FM6_GET of an empty bank slot: rc 2, no record", n == 9u && host_wire[7] == 2);
    a[0] = ED_FM6_BANK; a[1] = 4; memcpy(a + 2, pk, FM6_PACKED);
    a[2 + 14] = 120;                                      /* OP6 output level 120: stored as 99 */
    n = request(ED_FM6_PUT, a, sizeof a);
    a[0] = ED_FM6_BANK; a[1] = 4;
    request(ED_FM6_GET, a, 2);
    bad += check("FM6_PUT bank B5, then GET: stored in range (a level of 120 -> 99)", host_wire[7] == 0 &&
                 host_wire[8 + 14] == 99 && !memcmp(host_wire + 8 + 118, "MY PATCH  ", 10) && fm6_bank_used(4));
    n = request(ED_FM6_LIST, a, 0);
    {   /* factory 24, bank 32, then used + name per slot */
        uint32_t p = 7, k, ok = host_wire[5] == FM6_NFAC && host_wire[6] == FM6_BANK_N, named = 0;
        for (k = 0; k < FM6_NFAC + FM6_BANK_N && p < n; k++) {
            uint32_t used = host_wire[p++];
            if (k == FM6_NFAC + 4u) named = used && !memcmp(host_wire + p, "MY PATCH", 9);
            if (k < FM6_NFAC) ok &= used == 1u;
            while (host_wire[p]) p++;
            p++;
        }
        bad += check("FM6_LIST: 24 factory names, the bank's used slots by name", ok && named && k == FM6_NSLOT);
    }
    trk[1].eng_req = ENGI_FM6;
    trk[1].p[P_E7] = FM6_NFAC + 4;
    fm6_poll();
    bad += check("PTCH B5 loads the bank patch into the track", !memcmp(fm6_patch[1] + FP_NAME, "MY PATCH  ", 10));
    a[0] = 4;
    n = request(ED_FM6_ERASE, a, 1);
    bad += check("FM6_ERASE B5: empty; PTCH B5 plays the init voice", host_wire[6] == 0 && !fm6_bank_used(4) &&
                 (fm6_poll(), !memcmp(fm6_patch[1] + FP_NAME, "INIT VOICE", 10)));
    a[0] = ED_FM6_TRACK; a[1] = 4;
    request(ED_FM6_PUT, a, sizeof a);
    i = host_wire[7];
    request(ED_FM6_PUT, a, 20);
    bad += check("FM6_PUT: a fifth track or a short record: rc 1", i == 1u && host_wire[7] == 1u);
    a[0] = ED_FM6_BANK; a[1] = FM6_BANK_N;
    request(ED_FM6_GET, a, 2);
    bad += check("FM6_GET past the bank: rc 1", host_wire[7] == 1u);
    return bad;
}

/* UP_PUT -> UP_GET: every value round-trips through the v4 record (a byte each), negative ones too */
static int user_preset_roundtrip(void)
{
    static uint8_t a[16 + 2u * P_COUNT + 32u];
    int16_t want[P_COUNT], got[P_COUNT];
    uint32_t k = 0, i, p, ok;
    int bad = 0;
    reset();
    a[k++] = 3; a[k++] = 0;                                /* slot U04, ANALOG */
    memcpy(a + k, "ROUND", 5); k += 5; a[k++] = 0;
    for (i = 0; i < P_COUNT; i++) {
        const param_desc_t *d = param_desc_of(0, i);
        int16_t v = d->def;
        uint32_t u;
        if (i == P_LEVEL) v = 90;
        if (i == P_PAN) v = -20;
        if (i == P_ED_FLT) v = -40;
        if (i == P_LD_FLT) v = d->min;
        if (i == P_E1) v = d->max;
        want[i] = v; u = (uint32_t)(v + 8192);
        a[k++] = u & 127u; a[k++] = (u >> 7) & 127u;
    }
    for (i = 0; i < 16u; i++) { a[k++] = i & 1u ? 0u : (uint8_t)(48u + i); a[k++] = 0; }
    request(ED_UP_PUT, a, k);
    bad += check("UP_PUT stores a v4 record (no flash here: kept in RAM)",
                 host_wire[6] != 1u && up_used(3) && up_rec(3)->ver == UP_VER);
    up_values(up_rec(3), got);
    for (i = 0, ok = 1; i < P_COUNT; i++) ok &= got[i] == want[i];
    bad += check("UP_PUT keeps every value (PAN -20, FLT -40, min and max)", ok);
    a[0] = 3;
    request(ED_UP_GET, a, 1);
    ok = host_wire[5] == 3 && host_wire[6] == 1 && host_wire[7] == 0 && !memcmp(host_wire + 8, "ROUND", 6);
    for (i = 0, p = 14; i < P_COUNT; i++, p += 2u)
        ok &= (int32_t)(host_wire[p] | host_wire[p + 1] << 7) - 8192 == want[i];
    for (i = 0; i < 16u; i++, p += 2u)
        ok &= host_wire[p] == (i & 1u ? 0u : 48u + i);
    bad += check("UP_GET returns the values and the pattern UP_PUT sent", ok);
    return bad;
}

static int bank_protocol(void)
{
    int bad=0; reset(); uint8_t a[12] = {2,1,7};
    bad += check("PATTERN selects a track's bank without changing track selection", request(ED_PATTERN,a,3)==12 && !host_wire[7] && trk[2].pattern==7 && !song.sel);
    a[2]=8;
    bad += check("PATTERN rejects out-of-range bank IDs", request(ED_PATTERN,a,3)==12 && host_wire[7] && trk[2].pattern==7);
    uint8_t row[] = {1,1,0,2,4,7,3};
    bad += check("bank SONG independently assigns four track banks", request(ED_BANK_SONG,row,sizeof row)==17 && !host_wire[6] && chain_patterns[0][3]==7 && chain_config.row[0].repeat==3);
    row[4]=8;
    bad += check("malformed bank SONG leaves the arrangement unchanged", request(ED_BANK_SONG,row,sizeof row)==17 && host_wire[6]==1 && chain_patterns[0][2]==4);
    a[0]=0; uint32_t writes=host_writes;
    bad += check("sample upload and erase commands cannot write into pattern storage", request(ED_SMP_BEGIN,a,1)==0 && request(ED_SMP_ERASE,a,1)==0 && host_writes==writes && !host_erases);
    request(ED_SMP_INFO,a,0);
    bad += check("sample inventory advertises zero user slots", host_wire[5]==0);
    return bad;
}
static int cz_native_protocol(void)
{
    int bad=0; uint8_t raw[CZ_BYTES],a[322]; reset();cz_init();
    cz_patch_init(raw);raw[20]=0x37;raw[16]=0x42;raw[17]=17;raw[14]=0xE0;raw[15]=0xDE;
    for(uint32_t k=0;k<8;k++){raw[21+2*k]=(uint8_t)(k*15);raw[22+2*k]=(uint8_t)(k*17);}raw[28]|=128;
    a[0]=0;a[1]=2;for(uint32_t i=0;i<CZ_BYTES;i++){a[2+2*i]=raw[i]&15;a[3+2*i]=raw[i]>>4;}
    request(ED_CZ_PUT,a,290);
    bad+=check("CZ PUT preserves all eight DCA points, velocity and hidden waves",host_wire[7]==0 && trk[2].eng_req==ENGI_CZ && trk[2].p[P_E7]==CZ_NATIVE && !memcmp(cz_patch[2].raw,raw,CZ_BYTES));
    request(ED_CZ_GET,a,2);int same=host_wire_n==297 && host_wire[7]==0;
    for(uint32_t i=0;i<CZ_BYTES;i++)same &= host_wire[8+2*i]==(raw[i]&15) && host_wire[9+2*i]==(raw[i]>>4);
    bad+=check("CZ GET returns exact low-first tone nibbles",same);
    a[40]=16;request(ED_CZ_PUT,a,290);bad+=check("invalid CZ upload leaves live patch intact",host_wire[7]==1 && !memcmp(cz_patch[2].raw,raw,CZ_BYTES));a[40]=raw[19]&15;
    a[0]=1;a[1]=8;request(ED_CZ_PUT,a,290);bad+=check("CZ tone uses atomic ordinary preset storage",host_wire[7]==0 && up_used(8) && up_rec(8)->ver==UP_VER_CZ && !memcmp(up_rec(8)->packed,raw,CZ_BYTES));
    song.sel=1;up_load(8);bad+=check("loading user CZ preset restores all 144 native bytes",TSEL->p[P_E7]==CZ_NATIVE && !memcmp(cz_patch[1].raw,raw,CZ_BYTES));
    project_t q,r;project_store_t packed;project_capture(&q);bad+=check("native tones survive project serialization",proj_pack(&packed,&q) && proj_unpack(&r,packed.raw,sizeof packed) && !memcmp(q.cz,r.cz,sizeof q.cz));
    static uint8_t banked[BANK_STORE_SIZE];
    bad+=check("native tones survive full pattern-bank serialization",bank_pack(banked,&q,1) && bank_valid(banked,sizeof banked) && !memcmp(q.cz,proj_scratch.cz,sizeof q.cz));
    template_save(); static tmpl_t saved; saved=tmpl;
    memset(cz_patch,0,sizeof cz_patch); memset(&tmpl,0,sizeof tmpl);
    int restored=tmpl_take((const uint8_t *)&saved,sizeof saved);template_load();
    bad+=check("native tone bytes and mode survive a saved template",restored && trk[1].p[P_E7]==CZ_NATIVE && !memcmp(cz_patch[1].raw,raw,CZ_BYTES));
    return bad;
}
static int cz_casio_sysex(void)
{
    int bad=0;reset();cz_init();trk[0].eng_req=ENGI_CZ;uint8_t raw[CZ_BYTES],frame[296],got[CZ_BYTES];cz_patch_init(raw);raw[16]=0x42;raw[17]=17;raw[18]=5;raw[19]=83;raw[128]='S';
    const uint8_t head[]={0xf0,0x44,0,0,0x70,0x21,0x60};memcpy(frame,head,7);for(uint32_t j=0;j<CZ_BYTES;j++){frame[7+2*j]=raw[j]&15;frame[8+2*j]=raw[j]>>4;}frame[295]=0xf7;
    for(uint32_t j=0;j<sizeof frame;j++)sysex_byte(frame[j]);cz_service();host_drain();
    bad+=check("direct Casio MIDI SysEx preserves complete native tone",!memcmp(cz_patch[0].raw,raw,CZ_BYTES));memcpy(got,cz_patch[0].raw,CZ_BYTES);
    frame[10]=16;for(uint32_t j=0;j<sizeof frame;j++)sysex_byte(frame[j]);cz_service();
    bad+=check("invalid direct Casio SysEx leaves the native tone intact",!memcmp(cz_patch[0].raw,got,CZ_BYTES));
    const uint8_t request[]={0xf0,0x44,0,0,0x70,0x11,0x60,0xf7};host_wire_n=0;for(uint32_t j=0;j<sizeof request;j++)sysex_byte(request[j]);cz_service();host_drain();
    int same=host_wire_n==295 && host_wire[5]==0x30;for(uint32_t j=0;same && j<CZ_BYTES;j++)same=host_wire[6+2*j]==(raw[j]&15) && host_wire[7+2*j]==(raw[j]>>4);
    bad+=check("direct Casio tone request exports exact native bytes",same);return bad;
}

static void legacy_cz_fixture(uint8_t *p)
{
    memset(p,0,LCZ_PACKED);memset(p+LCZ_NP,' ',16);memcpy(p+LCZ_NP,"MIGRATED",8);p[LCZ_OCT]=1;
    for(uint32_t l=0;l<2;l++){p[LCZ_LBASE(l)+LCZ_LEVEL]=15;p[LCZ_LBASE(l)+LCZ_KW]=5;
        for(uint32_t e=0;e<3;e++){uint32_t b=LCZ_EBASE(l,e);for(uint32_t j=0;j<8;j++)p[b+j]=99;p[b+8]=e?99:0;p[b+16]=0;p[b+17]=1;}}
}
static int cz_legacy_saved_sounds(void)
{
    int bad=0;reset();cz_init();uint8_t tone[LCZ_PACKED],native[CZ_BYTES];legacy_cz_fixture(tone);
    bad+=check("earlier next CZ tone becomes documented native wire bytes",cz_legacy_tone(native,tone,sizeof tone) && native[18]==5 && native[19]==83 && native[128]=='M');
    project_t q,r;project_capture(&q);project_store_t packed;proj_pack(&packed,&q);
    uint8_t legacy[PROJ_LEGACY_CZ];memset(legacy,0,sizeof legacy);memcpy(legacy,packed.raw,PROJ_CZ_OFF);
    uint32_t magic=0x46554E42u,size=sizeof legacy,sum;memcpy(legacy,&magic,4);memcpy(legacy+4,&size,4);
    for(uint32_t k=0;k<NTRK;k++){memcpy(legacy+PROJ_CZ_OFF+k*LCZ_PACKED,tone,LCZ_PACKED);legacy[68u+k*(P_COUNT+2u+NSTEP*9u)+P_COUNT]=14;}
    sum=proj_hash(legacy,sizeof legacy-4);memcpy(legacy+sizeof legacy-4,&sum,4);
    bad+=check("earlier next FUNB project migrates CZ engine and full envelopes",proj_import_any(&r,legacy,sizeof legacy) && r.t[0].engine==ENGI_CZ && r.t[0].p[P_E7]==CZ_NATIVE && !memcmp(r.cz[0].raw,native,CZ_BYTES));
    uint8_t full[BANK_STORE_SIZE];bank_pack(full,&q,0);uint32_t oldExtra=BANK_EXTRA_OFF;
    memmove(full+8u+sizeof legacy,full+8u+PROJ_STORE_SIZE,BANK_STORE_SIZE-8u-PROJ_STORE_SIZE-4u);
    memcpy(full+8u,legacy,sizeof legacy);magic=0x434B4246u;size=BANK_SIZE_CZ_NEXT;memcpy(full,&magic,4);memcpy(full+4,&size,4);sum=proj_hash(full,size-4u);memcpy(full+size-4u,&sum,4);
    bad+=check("earlier next FBKC pattern bank validates without losing timing",bank_valid(full,size));bank_upgrade(full);
    bad+=check("normalized CZ pattern bank upgrades to native project format",bank_valid(full,BANK_STORE_SIZE) && proj_scratch.t[0].engine==ENGI_CZ && !memcmp(proj_scratch.cz[0].raw,native,CZ_BYTES));
    uint8_t tpl[TMPL_CZ_NEXT];memset(tpl,0,sizeof tpl);memcpy(tpl,&tmpl,TMPL_SIZE6-8u);
    for(uint32_t k=0;k<NTRK;k++){memcpy(tpl+TMPL_SIZE6-8u+k*LCZ_PACKED,tone,LCZ_PACKED);tpl[(2u*G_COUNT+2u)+k*sizeof(tmpl.t[0])]=14;}
    size=sizeof tpl;magic=0x394C5054u;memcpy(tpl+size-8,&size,4);memcpy(tpl+size-4,&magic,4);
    bad+=check("earlier next TPL9 template migrates native CZ tone",tmpl_take(tpl,sizeof tpl) && tmpl.t[0].engine==ENGI_CZ && !memcmp(tmpl.cz[0].raw,native,CZ_BYTES));
    up_rec_t rec;memset(&rec,0,sizeof rec);rec.used=UP_USED;rec.engine=14;rec.ver=7;rec.np=P_COUNT;memcpy(rec.name,"OLD CZ",6);
    uint32_t pos=0;for(uint32_t k=0;k<LCZ_PACKED;k++){uint32_t w=up_legacy_width(k);for(uint32_t j=0;j<w;j++,pos++){uint32_t at=pos>>3;uint8_t *p=at<144?rec.packed+at:rec.cz_extra+at-144;*p|=((tone[k]>>j)&1u)<<(pos&7);}}
    for(uint32_t k=0;k<P_COUNT;k++){uint32_t val=(uint32_t)(TP[k].def-LCZ_PRESET_MIN[k]);if(k==P_REV)val=77;for(uint32_t j=0;j<LCZ_PRESET_WIDTH[k];j++,pos++){uint32_t at=pos>>3;uint8_t *p=at<144?rec.packed+at:rec.cz_extra+at-144;*p|=((val>>j)&1u)<<(pos&7);}}
    uint8_t converted[CZ_BYTES];int16_t values[P_COUNT];up_values(&rec,values);
    bad+=check("earlier next CZ preset retains native tone and effect settings",up_valid(&rec) && up_cz_raw(&rec,converted) && !memcmp(converted,native,CZ_BYTES) && values[P_REV]==77 && values[P_E7]==CZ_NATIVE);
    return bad;
}

static int cz_dedicated_banks(void)
{
    int bad=0;reset();cz_init();cz_bank_boot();cz_bank_t bank;
    for(uint32_t k=0;k<8;k++){
        cz_bank_empty(&bank,k);bank.used=0x8001u;cz_patch_init(bank.tone[0].raw);cz_patch_init(bank.tone[15].raw);
        bank.tone[0].raw[128]=(uint8_t)('A'+k);bank.tone[15].raw[128]=(uint8_t)('a'+k);
        memcpy(ED_BK_RAW,&bank,sizeof bank);ed_bk_id=(uint8_t)(9+k);ed_bk_len=ed_bk_pos=sizeof bank;ed_bk_crc=st_crc32(&bank,sizeof bank);
        bad+=check("CZ dedicated bank commits through bounded backup transfer",!ed_bk_commit());
    }
    for(uint32_t k=0;k<8;k++){
        uint8_t args[2]={2,(uint8_t)(16*k+15)};request(ED_CZ_GET,args,2);
        bad+=check("CZ banks remain independent with complete raw tones",!host_wire[7] && (host_wire[264]|host_wire[265]<<4)==('a'+k));
    }
    uint8_t a[1]={7};request(ED_CZ_BANK,a,1);
    bad+=check("CZ bank list names all 16 dedicated slots",host_wire[5]==7 && !host_wire[6] && !memcmp(host_wire+7,"BANK H",6));
    host_preset(&trk[0],ENGI_CZ,0);trk[0].p[P_E0]=7;trk[0].p[P_E1]=16;cz_bank_poll();
    bad+=check("device BANK and PTCH knobs load stored native tone",cz_patch[0].raw[128]=='h' && trk[0].eng_req==ENGI_CZ);
    uint8_t previous=cz_patch[0].raw[128];trk[0].p[P_E1]=2;cz_bank_poll();
    bad+=check("empty CZ bank slot leaves playing tone intact",cz_patch[0].raw[128]==previous);
    cz_bank_t keep=*cz_bank_load(7);bank=keep;bank.tone[0].raw[0]=255;
    memcpy(ED_BK_RAW,&bank,sizeof bank);ed_bk_id=16;ed_bk_len=ed_bk_pos=sizeof bank;ed_bk_crc=st_crc32(&bank,sizeof bank);
    bad+=check("invalid native bank rejected before replacing saved tones",ed_bk_commit()==2 && !memcmp(cz_bank_load(7),&keep,sizeof keep));
    request(ED_BACKUP_LIST,a,0);bad+=check("full backups include all eight CZ user banks",host_wire[7]==17);
    return bad;
}
static int names_whole(void)
{
    int bad = 0;
    reset();
    for (uint32_t e = 0; e < NENGINES; e++) {   /* 0.11 cut CZ-1's 65 names at a 600-byte reply */
        uint8_t a[1] = {(uint8_t)e};
        uint32_t n = request(ED_NAMES, a, 1), i = 7, k, want = ENGINES[e]->npresets + 2u;
        for (k = 0; k < want && i < n; k++)
            while (i < n && host_wire[i++]) ;
        if (n < 8u || host_wire[6] != ENGINES[e]->npresets || k != want || i != n - 1u || host_wire[n - 1u] != 0xF7) {
            printf("editor: NAMES of %s: %u bytes, cut short\n", ENGINES[e]->name, n);
            bad = 1;
        }
    }
    return check("NAMES of every engine carries all its presets and both titles", !bad);
}
int main(void)
{
    int bad = bank_protocol() + preferences() + framing() + uart_recovery() + steps() + song_protocol() + malformed_saves() + names_whole() +
              fm6_patches() + user_preset_roundtrip() + cz_native_protocol() + cz_dedicated_banks() + cz_legacy_saved_sounds() + cz_casio_sysex();
    printf("%s\n", bad ? "EDITOR TEST FAILED" : "editor test passed");
    return bad != 0;
}
