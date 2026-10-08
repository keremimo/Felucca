/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The editor's full backup (editor_backup.c: LIST / GET / PUT) against simulated NOR flash:
 * CRC before any write, stale runtime copies, USB resets and timeouts, malformed objects, older
 * project formats, settings values and user preset banks kept byte for byte. */
static unsigned char host_samples[3][0x14000];
#define SMP_USER_XIP(k) host_samples[k]
#define main hostsim_main
#include "hostsim.c"
#undef main

static int32_t fm1_enc_take(uint32_t e) { (void)e; return 0; }
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; }
#define MELODEE_FLASH 1
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
static void panel_setup(void) {}

static uint8_t nor[0x100000], flash_ok = 1;
static int erase_error;
static uint32_t erases;
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { if (erase_error) return -8; memset(nor + off, 0xFF, 4096); erases++; return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n) { memcpy(nor + off, src, n); return 0; }
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static uint32_t fl_jedec_ram(void) { return 0; }
static void fl_plain_window_init(void) {}
#define FL_FAR(fn) (fn)
#include "../firmware/src/storage.c"
#include "../firmware/src/upreset.c"
#include "../firmware/src/project.c"

#define ED_BK_FLASH_PTR(off) (nor + (off))
enum { ED_BACKUP_LIST = 65, ED_BACKUP_GET, ED_BACKUP_PUT };
static uint8_t rep[4096];
static uint32_t rep_n;
static void ed_b(uint32_t v) { if (rep_n < sizeof rep) rep[rep_n++] = (uint8_t)(v & 127u); }
static uint32_t ed_unpack7(const uint8_t *a, uint32_t na, uint8_t *out, uint32_t max)
{
    uint32_t n = 0;
    while (na && n < max) {
        uint32_t m = *a++, j;
        na--;
        uint32_t k = na > 7u ? 7u : na;
        if (!k || n + k > max || (m >> k))
            return 0;
        for (j = 0; j < k; j++, na--)
            out[n++] = (uint8_t)(*a++ | ((m >> j) & 1u) << 7);
    }
    return na ? 0u : n;
}
static uint8_t ed_smp_buf[512] __attribute__((aligned(4)));
static void fm1_wdt_feed(void) {}
static int ed_flash_stop(void) { return transport_busy(); }
#include "../firmware/src/editor_backup.c"

static int check(const char *what, int ok)
{
    printf("backup: %-72s %s\n", what, ok ? "ok" : "FAIL");
    return !ok;
}

static void reset(void)
{
    memset(nor, 0xFF, sizeof nor);
    memset(&song, 0, sizeof song);
    memset(trk, 0, sizeof trk);
    memset(&chain, 0, sizeof chain);
    chain_defaults(&chain_config);
    pattern_init();
    memset(proj_slot, 0, sizeof proj_slot);
    up_cache_reset(); native_cache_reset();
    memset(&persist_saved, 0, sizeof persist_saved);
    memset(&settings, 0, sizeof settings);
    memset(&ui, 0, sizeof ui);
    panel = PANEL_DEFAULT;
    settings_init();
    host_tracks_init();
    fm1_ms = 0;
    transport_req = 0;
    usb.resets = 0;
    erase_error = 0;
    erases = 0;
    ed_bk_valid = ed_bk_put = 0;
}

static uint32_t call(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    rep_n = 0;
    ed_backup_handle(cmd, a, n);
    return rep_n;
}
static void put32(uint8_t *a, uint32_t v) { for (uint32_t i = 0; i < 5u; i++) a[i] = (uint8_t)((v >> (7u * i)) & 127u); }

/* PUT begin: rc */
static uint32_t put_begin(uint32_t id, uint32_t len, uint32_t crc)
{
    uint8_t a[12] = {0, (uint8_t)id};
    put32(a + 2, len);
    put32(a + 7, crc);
    call(ED_BACKUP_PUT, a, sizeof a);
    return rep[2];
}
/* PUT one chunk of data at off: rc */
static uint32_t put_chunk(uint32_t id, uint32_t off, const uint8_t *p, uint32_t n)
{
    uint8_t a[16 + 300];
    uint32_t k = 7;
    a[0] = 1;
    a[1] = (uint8_t)id;
    put32(a + 2, off);
    while (n) {
        uint32_t g = n > 7u ? 7u : n, m = 0, i;
        for (i = 0; i < g; i++) m |= (uint32_t)(p[i] >> 7) << i;
        a[k++] = (uint8_t)m;
        for (i = 0; i < g; i++) a[k++] = p[i] & 127u;
        p += g;
        n -= g;
    }
    call(ED_BACKUP_PUT, a, k);
    return rep[2];
}
static uint32_t put_end(uint32_t id, uint32_t op)   /* op 2 commit, 3 abort */
{
    uint8_t a[2] = {(uint8_t)op, (uint8_t)id};
    call(ED_BACKUP_PUT, a, 2);
    return rep[2];
}
/* a whole object: begin, 256-byte chunks, commit; the first failing rc or the commit's */
static uint32_t put_all(uint32_t id, const void *v, uint32_t len, uint32_t crc)
{
    const uint8_t *p = v;
    uint32_t off, rc = put_begin(id, len, crc);
    for (off = 0; !rc && off < len; off += 256u)
        rc = put_chunk(id, off, p + off, len - off > 256u ? 256u : len - off);
    return rc ? rc : put_end(id, 2);
}
/* LIST: rc; the length and CRC of object id */
static uint32_t list(uint32_t id, uint32_t *len, uint32_t *crc)
{
    uint32_t i;
    call(ED_BACKUP_LIST, 0, 0);
    if (rep[1])
        return rep[1];
    for (i = 0; i < rep[2]; i++) {
        const uint8_t *o = rep + 3 + i * 11u;
        if (o[0] == id) {
            *len = ed_bk_r32(o + 1);
            *crc = ed_bk_r32(o + 6);
        }
    }
    return 0;
}
static uint32_t get(uint32_t id, uint32_t off, uint32_t count)
{
    uint8_t a[8] = {(uint8_t)id};
    put32(a + 1, off);
    a[6] = (uint8_t)(count & 127u);
    a[7] = (uint8_t)(count >> 7);
    call(ED_BACKUP_GET, a, sizeof a);
    return rep[1];
}

static int full_pattern_archive(void)
{
    int bad=0, ok=1; reset(); static uint8_t archive[BANK_STORE_SIZE], saved[BANK_STORE_SIZE];
    for (uint32_t k=0; k<NTRK; k++) for (uint32_t b=0; b<NPAT; b++) {
        track_t *t=&trk[k]; pattern_request(t,b); t->p[P_SLEN]=(int16_t)(16+b);
        t->step[3]=(step_t){{(uint8_t)(40+k*8+b),60,64,67},4,ST_NOTE,SF_ACCENT,100,0x81,0x80,(uint8_t)(20+b)};
        t->step[4]=(step_t){{0},0,ST_TIE}; motion_set_event(t,3,P_REV,(int16_t)(k*8+b));
    }
    chain_config.count=1; chain_config.row[0]=(chain_row_t){0,2};
    for (uint32_t k=0; k<NTRK; k++) chain_patterns[0][k]=(uint8_t)(k+1);
    project_save_as(0,"CHORD BANKS");
    uint32_t len,crc; list(0,&len,&crc); memcpy(archive,ED_BK_RAW,sizeof archive);
    bad+=check("full bank archive contains all runtime banks with its verified CRC",len==sizeof archive && crc==st_crc32(archive,sizeof archive));
    for (uint32_t off=0; off<sizeof saved; off+=256u) {
        uint32_t n=sizeof saved-off>256u?256u:sizeof saved-off;
        ok &= get(2,off,n)==0;
        ok &= ed_unpack7(rep+9u,rep_n-9u,saved+off,n)==n;
    }
    /* GET emits pack7; the XIP payload itself remains byte-identical after other object reads. */
    ok &= !memcmp(saved,archive,sizeof saved) && (st_load(OBJ_BANK0,saved,sizeof saved)==sizeof saved && !memcmp(saved,archive,sizeof saved)) && get(0,0,64)==0;
    bad+=check("saved-bank GET uses flash without overwriting the frozen runtime snapshot",ok);
    bad+=check("full archive restores a saved slot through the A/B path",put_all(3,archive,sizeof archive,st_crc32(archive,sizeof archive))==0);
    pattern_init(); memset(&motion,0,sizeof motion); memset(proj_meta,0,sizeof proj_meta); memset(proj_slot,0,sizeof proj_slot);
    proj_fetch(1); project_load(1); ok=1;
    for (uint32_t k=0; k<NTRK; k++) for (uint32_t b=0; b<NPAT; b++) {
        track_t *t=&trk[k]; pattern_request(t,b);
        ok &= t->step[3].n==4 && t->step[3].note[0]==40+k*8+b && t->step[3].note[3]==67 &&
            t->step[4].time==ST_TIE && t->p[P_SLEN]==16+(int16_t)b && step_chance(&t->step[3])==20+b && motion_count(t)==1;
    }
    bad+=check("cold flash load restores all 32 chords, ties, timing, chance and motion banks",ok && chain_patterns[0][3]==4);
    bad+=check("full archive restores the runtime with its original active banks",put_all(0,archive,sizeof archive,st_crc32(archive,sizeof archive))==0 && trk[0].pattern==7 && trk[3].pattern==7);
    return bad;
}
static int native_archive(void)
{
    reset();up_boot();int ok=1;uint8_t fm[FM6_PACKED],cz[CZ_BYTES];
    const uint32_t slots[4]={0,16,32,63},ids[4]={21,22,19,20};static native_fm_t saved[4];static cz_bank_t savedcz;
    for(uint32_t b=0;b<4;b++){fm6_factory(b,fm);ok &= !native_put(ENGI_FM6,slots[b],fm);saved[b]=(*native_fm_bank(b));}
    cz_patch_init(cz);cz[128]='Z';ok &= !native_put(ENGI_CZ,127,cz);savedcz=(*native_cz_bank(7));
    for(uint32_t b=0;b<4;b++){uint32_t len;const uint8_t *raw=ed_bk_object(ids[b],&len);ok &= len==sizeof saved[b] && !memcmp(raw,&saved[b],len);ok &= !native_put(ENGI_FM6,slots[b],0);}
    ok &= !native_put(ENGI_CZ,127,0);
    for(uint32_t b=0;b<4;b++)ok &= !put_all(ids[b],(uint8_t *)&saved[b],sizeof saved[b],st_crc32(&saved[b],sizeof saved[b]));
    ok &= !put_all(16,(uint8_t *)&savedcz,sizeof savedcz,st_crc32(&savedcz,sizeof savedcz));
    native_cache_reset();cz_bank_cached=255;up_boot();
    for(uint32_t b=0;b<4;b++)ok &= !memcmp(native_fm_bank(b),&saved[b],sizeof saved[b]);
    ok &= native_used(ENGI_CZ,127) && !memcmp(native_raw(ENGI_CZ,127),cz,sizeof cz);
    return check("all four native FM6 objects and Z128 restore exactly after reboot",ok);
}
static int expanded_recording_archive(void)
{
    reset();
    for(uint32_t k=0;k<NTRK;k++)for(uint32_t b=0;b<NPAT;b++) {
        pattern_request(&trk[k],b);
        trk[k].step[3]=(step_t){{60},1,ST_NOTE,SF_RECORDED,100,0,0,0};
    }
    for(uint32_t i=0;i<RECORD_MAX;i++)recording[i]=(recorded_note_t){(uint16_t)(i*61u),1234,60,100,(uint8_t)(i%32u),3};
    recording_reindex();
    static recorded_note_t saved[RECORD_MAX];memcpy(saved,recording,sizeof saved);
    int ok=!project_save_as(0,"FULL TAKE");
    uint32_t len,crc;ok &= !list(0,&len,&crc);
    static uint8_t archive[BANK_STORE_SIZE], readback[BANK_STORE_SIZE];memcpy(archive,ED_BK_RAW,sizeof archive);
    for(uint32_t off=0;off<sizeof readback;off+=256u){uint32_t n=sizeof readback-off>256u?256u:sizeof readback-off;ok &= !get(2,off,n) && ed_unpack7(rep+9u,rep_n-9u,readback+off,n)==n;}
    ok &= !memcmp(archive,readback,sizeof archive);
    ok &= !put_all(3,archive,sizeof archive,st_crc32(archive,sizeof archive));
    pattern_init();memset(proj_meta,0,sizeof proj_meta);project_load(1);
    ok &= !memcmp(saved,recording,sizeof saved);
    int bad=check("all 1024 timed notes across 32 banks survive flash, backup GET and saved-slot restore",ok);
    ok=!put_all(0,archive,sizeof archive,st_crc32(archive,sizeof archive)) && !memcmp(saved,recording,sizeof saved);
    bad+=check("expanded runtime archive restores every original timed note",ok);
    return bad;
}
int main(void)
{
    int expanded_bad = expanded_recording_archive();

    int bad = 0;
    uint32_t len = 0, crc = 0, before;
    static project_store_t st;
    static project_v6_t v6;
    persist_t ps;

    reset();
    trk[0].step[0] = (step_t){{60}, 1, ST_NOTE, 0, 96, 0, 0};
    bad += check("LIST captures runtime and all 23 archive objects with CRC",
                 list(0, &len, &crc) == 0 && rep[2] == 23u && len == BANK_STORE_SIZE &&
                 crc == st_crc32(ED_BK_RAW, len));
    bad += check("an empty project slot lists as length 0", list(2, &len, &crc) == 0 && len == 0);
    bad += check("GET of the runtime copy", get(0, 0, 64) == 0);
    project_save(0);
    bad += check("a project save after LIST reused the staging RAM: GET of the runtime is stale (5)",
                 get(0, 0, 64) == 5u);
    bad += check("a new LIST makes it readable again", list(0, &len, &crc) == 0 && get(0, 0, 64) == 0);
    usb.resets++;
    bad += check("a USB reset after LIST: GET is stale (5)", get(2, 0, 16) == 5u);

    /* settings */
    reset();
    memset(&ps, 0, sizeof ps);
    settings_export(&ps);
    ps.lowcut = 2;                                       /* SPEAKER BASS+ */
    ps.palette = palette_to_stored(6);
    bad += check("settings with SPEAKER BASS+ restore", put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 0 &&
                 settings.lowcut == 2u && fx_lowcut == 2u && settings.palette == 6u);
    ps.palette = 13;                                     /* an older backup: its PAPER (old id 13) */
    bad += check("settings of an older backup restore with the palette migrated",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 0 && settings.palette == 6u);
    ps.palette = palette_to_stored(6);
    before = erases;
    ps.lowcut = 3;
    bad += check("settings with an unknown SPEAKER value are refused, nothing written",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 2u && erases == before && settings.lowcut == 2u);
    ps.lowcut = 0;
    ps.bold = hold_to_stored(0, 3);                      /* HOLD 0.6 s */
    bad += check("settings with HOLD 0.6 s restore",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 0 && HOLD_MS[settings_hold] == 600u);
    ps.bold = 1;                                         /* an older backup (its font weight): HOLD 0.4 s */
    bad += check("settings of an older backup restore with HOLD 0.4 s",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 0 && HOLD_MS[settings_hold] == 400u);
    before = erases;
    ps.bold = HOLD_TAG + 7u;
    bad += check("settings with an unknown HOLD value are refused, nothing written",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 2u && erases == before);
    ps.bold = 0;
    bad += check("a wrong CRC is refused before any write",
                 put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps) ^ 1u) == 2u && erases == before);
    {   /* the settings record with the template after it (SLOT TMPL): both back; without one: no template */
        static struct { persist_t p; tmpl_t t; } rec;
        rec.p = ps;
        rec.p.ext.boot = 2;
        memset(&tmpl, 0, sizeof tmpl);
        trk[1].p[P_LEVEL] = 66;
        template_save();
        rec.t = tmpl;
        memset(&tmpl, 0, sizeof tmpl);
        settings_boot = 0;
        bad += check("settings + template restore: BOOT B, the template back",
                     put_all(1, &rec, sizeof rec, st_crc32(&rec, sizeof rec)) == 0 && settings_boot == 2u &&
                     template_used() && tmpl.t[1].p[P_LEVEL] == 66);
        bad += check("  settings without one: no template", put_all(1, &ps, sizeof ps, st_crc32(&ps, sizeof ps)) == 0 &&
                     !template_used() && settings_boot == 0u);
        rec.t.magic ^= 1u;
        before = erases;
        bad += check("  a broken template is refused, nothing written",
                     put_all(1, &rec, sizeof rec, st_crc32(&rec, sizeof rec)) == 2u && erases == before);
    }

    /* malformed requests */
    bad += check("an unknown object id is refused", put_begin(8, sizeof ps, 0) == 1u);
    bad += check("a project of the wrong length is refused", put_begin(2, sizeof(project_store_t) - 4u, 0) == 1u);
    bad += check("settings of the wrong length are refused", put_begin(1, sizeof ps + 4u, 0) == 1u);
    bad += check("a chunk without a begin is refused (5)", (ed_bk_put = 0, put_chunk(1, 0, (const uint8_t *)&ps, 16)) == 5u);
    put_begin(1, sizeof ps, st_crc32(&ps, sizeof ps));
    bad += check("a chunk at the wrong offset is refused", put_chunk(1, 16, (const uint8_t *)&ps, 16) == 1u);
    bad += check("a chunk for another object is refused (5)", put_chunk(2, 0, (const uint8_t *)&ps, 16) == 5u);
    {
        uint32_t off = 0, step = sizeof ps - 8u;
        put_begin(1, sizeof ps, st_crc32(&ps, sizeof ps));
        for (; off + 256u <= step; off += 256u)
            put_chunk(1, off, (const uint8_t *)&ps + off, 256);
        if (off < step) put_chunk(1, off, (const uint8_t *)&ps + off, step - off);
        bad += check("a chunk past the length is refused", put_chunk(1, step, (const uint8_t *)&ps, 16) == 1u);
    }
    bad += check("commit before all bytes arrived is refused", put_end(1, 2) == 2u && erases == before);
    put_begin(1, sizeof ps, st_crc32(&ps, sizeof ps));
    put_chunk(1, 0, (const uint8_t *)&ps, 64);
    usb.resets++;
    bad += check("a USB reset during PUT ends it (5)", put_chunk(1, 64, (const uint8_t *)&ps + 64, 64) == 5u);
    put_begin(1, sizeof ps, st_crc32(&ps, sizeof ps));
    fm1_ms += 16000u;
    bad += check("16 s without a chunk ends the PUT (5)", put_chunk(1, 0, (const uint8_t *)&ps, 64) == 5u);
    put_begin(2, sizeof st, 0);
    put_chunk(2, 0, (const uint8_t *)&ps, 64);
    project_save(1);
    bad += check("a project save during PUT (the staging RAM) ends it (5)",
                 put_chunk(2, 64, (const uint8_t *)&ps, 64) == 5u);
    transport_req = 1;
    bad += check("PUT while starting PLAY is refused (3)", put_begin(1, sizeof ps, 0) == 3u);
    transport_req = 0;

    /* projects */
    reset();
    trk[1].step[3] = (step_t){{64}, 1, ST_NOTE, 0, 96, 0, 0};
    project_capture(&proj_scratch);
    proj_pack(&st, &proj_scratch);
    bad += check("a FUN7 project restores into slot 3 (flash and RAM)",
                 put_all(4, &st, sizeof st, st_crc32(&st, sizeof st)) == 0 && project_used(2) &&
                 !memcmp(&proj_slot[2], &st, sizeof st) && st_load(OBJ_BANK0 + 2, proj_wire_u.raw, sizeof proj_wire_u.raw) == BANK_STORE_SIZE);
    memset(&v6, 0, sizeof v6);                           /* FUN6: 69 parameters, steps out of range */
    v6.magic = PROJ_MAGIC_V6;
    v6.size = sizeof v6;
    for (uint32_t i = 0; i < G_COUNT; i++)
        v6.g[i] = GP[i].def;
    for (uint32_t i = 0; i < NTRK; i++)
        v6.t[i].engine = trk[i].engine;
    v6.t[0].step[0] = (step10_t){{255, 72}, 9, 7, 0, 96, 0, 0};
    v6.t[0].p[61] = 3;                                   /* old E0 */
    chain_defaults(&v6.chain);
    v6.sum = proj_hash(&v6, sizeof v6 - 4u);
    bad += check("a FUN6 project restores as FUN7, bounded, its E0 at P_E0",
                 put_all(5, &v6, sizeof v6, st_crc32(&v6, sizeof v6)) == 0 && ((uint32_t *)proj_slot[3].raw)[0] == PROJ_MAGIC &&
                 proj_import(&proj_scratch, &proj_slot[3], sizeof st) && proj_scratch.t[0].step[0].n == 4u &&
                 proj_scratch.t[0].step[0].note[0] == 127u && proj_scratch.t[0].p[P_E0] ==
                 clamp(3, param_desc_of(trk[0].engine, P_E0)->min, param_desc_of(trk[0].engine, P_E0)->max));
    {   /* Selector-based archives are retired; owned-voice backups still restore. */
        static fm6_bank_t bk;
        static upf_t got;
        static up_bank_t records;
        uint8_t pk[FM6_PACKED], rec[FM6_PACKED];
        upf_empty();
        set_engine_of(TSEL, ENGI_FM6);
        up_store(2, "OLD FM6");
        up_set_value(up_rec(2), P_E7, FM6_NFAC + 2u);up_save_bank(0);
        memset(&bk, 0, sizeof bk);
        bk.magic = FM6_BANK_MAGIC; bk.ver = FM6_BANK_VER; bk.nslot = FM6_BANK_N; bk.used = 1u << 2;
        memcpy(bk.fn, FM6_FNDEF, FM6_NFN);
        fm6_factory(5, pk); fm6_pack7(bk.pk[2], pk, FM6_PACKED);
        bad += check("retired selector bank backup is refused without changing native voices",
                     put_all(8, &bk, sizeof bk, st_crc32(&bk, sizeof bk)) == 2 && !native_used(ENGI_FM6,2));
        bad += check("retired bank id 8 is listed empty", list(8, &len, &crc) == 0 && !len);
        bad += check("refusing the retired bank leaves native object 21 empty", list(21, &len, &crc) == 0 && !len);
        upf_set(2,pk); /* construct a historical owned-voice archive */
        memcpy(&got, upf_bank((0) * UPF_SLOTS), sizeof got); upf_empty();
        bad += check("owned voices restore through id 19", put_all(19, &got, sizeof got, st_crc32(&got, sizeof got)) == 0 && native_used(ENGI_FM6,2) && !memcmp(native_raw(ENGI_FM6,2), pk, FM6_PACKED));
        bad += check("native voice id 21 is listed with its CRC", list(21, &len, &crc) == 0 && len == sizeof(native_fm_t) && crc == st_crc32(native_fm_bank(0), len));
        bk.fn[FN_ENGINE] = 9;
        bad += check("damaged historical bank is refused", put_all(8, &bk, sizeof bk, st_crc32(&bk, sizeof bk)) == 2u);
        up_store(63, "LAST FM6");
        bad += check("additional user record bank id 18 retains U64", list(18, &len, &crc) == 0 && len == sizeof(up_bank_t));
        memcpy(&records, up_cache_bank(3), sizeof records); memset(up_cache_bank(3), 0, sizeof (*up_cache_bank(3)));
        bad += check("additional user record bank id 18 restores U64", put_all(18, &records, sizeof records, st_crc32(&records, sizeof records)) == 0 && up_used(63));
        fm6_pack(fm6_patch[0],pk);upf_set(63,pk);
        memcpy(&got, upf_bank((1) * UPF_SLOTS), sizeof got); upf_empty_bank(1);
        bad += check("additional voice bank id 20 restores U64", put_all(20, &got, sizeof got, st_crc32(&got, sizeof got)) == 0 && native_used(ENGI_FM6,63) && !memcmp(native_raw(ENGI_FM6,63),pk,FM6_PACKED));
    }
    memcpy(&st, &proj_slot[2], sizeof st);
    erase_error = 1;
    proj_slot[2].raw[100] ^= 1;                          /* (marks the RAM copy, to see it is kept) */
    memcpy(&v6, &proj_slot[2], sizeof v6);
    bad += check("a flash error on commit (4) keeps the slot's RAM copy",
                 put_all(4, &st, sizeof st, st_crc32(&st, sizeof st)) == 4u && !memcmp(&proj_slot[2], &v6, sizeof v6));
    erase_error = 0;
    bad += check("an empty project object clears the slot", put_all(4, 0, 0, st_crc32(0, 0)) == 0 && !project_used(2));
    st.raw[200] ^= 1;                                    /* hash no longer matches */
    bad += check("a project whose own hash fails is refused", put_all(4, &st, sizeof st, st_crc32(&st, sizeof st)) == 2u);

    /* the runtime */
    reset();
    trk[2].step[5] = (step_t){{67}, 1, ST_NOTE, 0, 96, 0, 0};
    trk[2].p[P_LEVEL] = 77;
    project_capture(&proj_scratch);
    proj_pack(&st, &proj_scratch);
    host_tracks_init();
    bad += check("the runtime object restores the tracks",
                 put_all(0, &st, sizeof st, st_crc32(&st, sizeof st)) == 0 && trk[2].step[5].note[0] == 67u &&
                 trk[2].p[P_LEVEL] == 77);

    /* user preset banks: unknown record versions are kept as bytes */
    reset();
    {
        static up_bank_t b;
        memset(&b, 0, sizeof b);
        b.magic = UP_BANK_MAGIC;
        b.rsize = sizeof(up_rec_t);
        b.nslot = UP_PER_BANK;
        b.r[4].used = UP_USED;
        b.r[4].ver = 99;                                 /* a future record */
        memcpy(b.r[4].name, "Future", 7);
        b.r[4].p[0] = 55;
        bad += check("a bank with a future record version restores byte for byte",
                     put_all(6, &b, sizeof b, st_crc32(&b, sizeof b)) == 0 &&
                     st_load(OBJ_UPRESET0, &proj_wire, sizeof proj_wire) == (int)sizeof b &&
                     !memcmp(&proj_wire, &b, sizeof b));
        b.nslot = 3;
        bad += check("a bank with the wrong slot count is refused", put_all(7, &b, sizeof b, st_crc32(&b, sizeof b)) == 2u);
    }
    {   /* CZ-1 banks: a never-saved bank (BANK A..D: Casio's factory tones) is no object of the backup, so a
         * restore keeps it the default; a saved one is carried byte for byte */
        static cz_bank_t cb;
        reset();
        cz_bank_boot();
        bad += check("CZ-1: a never-saved BANK A (its factory default) lists as length 0",
                     list(9, &len, &crc) == 0 && len == 0 && cz_bank_load(0)->used == 0xFFFFu);
        cz_bank_empty(&cb, 0);
        memcpy(cb.tone[3].raw, CZ_FACTORY[7], CZ_BYTES);
        cb.used = 1u << 3;
        bad += check("CZ-1: a saved BANK A restores and lists with its 2332 B",
                     put_all(9, &cb, sizeof cb, st_crc32(&cb, sizeof cb)) == 0 && list(9, &len, &crc) == 0 &&
                     len == sizeof cb && cz_bank_load(0)->used == 1u << 3);
        bad += check("CZ-1: length 0 clears native presets while keeping factory tones separate",
                     put_all(9, &cb, 0, st_crc32(&cb, 0)) == 0 && !native_cz_bank(0)->used &&
                     list(9, &len, &crc) == 0 && len == sizeof(cz_bank_t));
    }
    bad += native_archive();
    bad += full_pattern_archive();
    printf("backup test %s\n", bad ? "FAILED" : "passed");
    return (bad + expanded_bad) != 0;
}
