/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of Felucca 1.2's panel controls as Melodee has them, through the UI test's stubs (ui_test.c):
 * double-tapped quick layers stay open and own the keys silently, FX LATCH, the FX key map (PRESETS assigns, EDIT
 * restores the default), MENU > SCREEN OFF (the panel dark while the transport runs, the wake press swallowed, NEVER),
 * and LFO 2 (SYNC's increments at every division and tempo, TRIG NOTE / FREE, POL UNI, the transport's reset). */
#include <stdint.h>
static uint32_t lcd_commands[8], lcd_count;
#define UI_LCD_POWER(s) (lcd_commands[lcd_count++ % 8u] = (s))
#define UI_LCD_WAKE() ((void)0)
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include <assert.h>

static void reset_controls(void)
{
    seq_stop(); ui_power_on();
    memset(&scrn, 0, sizeof scrn); lcd_count = 0;
    settings_screen = settings_latch = kb_asleep = kb_lock = 0;
    perf_latched = perf_latch_on = 0;
    memset(kb_fx, 0, sizeof kb_fx); fx_map_sync();
}
static void tap_fast(uint32_t b) { btn_down(b); frame(); btn_up(b); frame(); }
static void test_layers(void)
{
    for (uint32_t l=LAYER_FX;l<LAYER_N;l++) {
        reset_controls(); uint32_t b=LAYERS[l].btn;
        tap_fast(b); tap_fast(b);
        assert(ui.lock==l && ui.layer==l && ui.home);
        key_down(white(0)); frame();
        assert(kb_layer & (1u<<white(0))); assert(!gates());
        key_up(white(0)); frame(); tap_fast(b);
        assert(!ui.lock && !ui.layer);
    }
    reset_controls(); settings_latch=1;
    tap_fast(B_FX); tap_fast(B_FX);
    key_down(white(10)); frame(); key_up(white(10)); frame();
    assert(perf_latched & PF_BIT(PF_FLG));
    key_down(white(10)); frame(); key_up(white(10)); frame();
    assert(!(perf_latched & PF_BIT(PF_FLG)));
    key_down(white(11)); frame(); key_up(white(11)); frame();
    assert(perf_latched & PF_BIT(PF_PHS));
    press(B_OCTDN); assert(!perf_latched);
    key_down(white(12)); frame(); turn(EN_PRESET,1);
    assert(perf_map[12]==PF_R8);
    press(B_EDIT); assert(perf_map[12]==PF_N);
    key_up(white(12)); frame(); tap_fast(B_FX);
    assert(!ui.lock);
    puts("layers/screen: double-tap layers, silent keys, FX latch, clear and key assignment passed");
}
static void test_screen_sleep(void)
{
    reset_controls(); scr_put(1); song.playing=1;
    fm1_ms=scrn.idle+300000u; ui_draw();
    assert(scrn.st==SCR_OFF && kb_asleep && song.playing && lcd_commands[0]==0);
    key_down(white(0)); frame(); assert(!gates() && scrn.st==SCR_WAKE);
    fm1_ms+=120; ui_draw(); assert(scrn.st==SCR_SLPOUT && lcd_commands[1]==1);
    fm1_ms+=120; ui_draw(); assert(scrn.st==SCR_ON && lcd_commands[2]==2);
    key_up(white(0)); fm1_ms+=320; frame(); assert(!kb_asleep);
    key_down(white(0)); frame(); assert(gates()); key_up(white(0)); frame();
    song.playing=0;
    for(uint32_t v=0;v<5;v++){scr_put(v);assert(scr_get()==v);}
    scr_put(0);fm1_ms+=3600001u;ui_draw();assert(scrn.st==SCR_ON);
    scr_put(1);ui.confirm=1;fm1_ms+=300001;ui_draw();assert(scrn.st==SCR_ON);
    ui.confirm=0;scr_put(1);fm1_ms+=300001;ui_draw();assert(scrn.st==SCR_OFF);
    int16_t value=TSEL->p[P_LEVEL];host_enc[panel.enc[EN_K1]]=2;frame();assert(TSEL->p[P_LEVEL]==value);
    scr_wake_now();fm1_ms+=320;frame();assert(!kb_asleep);
    puts("layers/screen: screen timeout, continuing transport, silent wake key/knob and NEVER passed");
}
static void test_lfo(void)
{
    reset_controls(); track_t *t=TSEL;
    for(uint32_t s=1;s<NELEM(N_LSYNC);s++){
        t->p[P_LSYNC]=s;
        for(uint32_t bpm=60;bpm<=240;bpm+=60){
            song.g[G_BPM]=bpm;uint32_t cycle=div_samples(LSYNC_DIV[s]);
            double expected=4294967296.0*CTL/cycle;
            assert(fabs((double)lfo_sync_inc(t)-expected)<1.01);
        }
    }
    t->p[P_LSYNC]=0;t->p[P_LTRIG]=1;t->lfo_ph=0x34567890;
    trk_note_on(t,60,100);assert(t->lfo_ph==0x34567890);trk_note_off(t,60);
    t->p[P_LTRIG]=0;trk_note_on(t,64,100);assert(!t->lfo_ph);
    t->p[P_LPOL]=1;t->p[P_LWAVE]=0;
    int32_t out[CTL];for(uint32_t i=0;i<2000;i++){track_render(t,out,CTL);assert(t->lfo_val>=0 && t->lfo_val<=32767);}
    t->p[P_LTRIG]=1;t->p[P_LSYNC]=5;t->lfo_ph=1;seq_start();assert(!t->lfo_ph);seq_stop();
    puts("layers/screen: all synced LFO divisions, NOTE/FREE, UNI and transport reset passed");
}
/* the FX key map and the native CZ-1 favorites share no bits (the map sat in CZ's row, factory[15][14..23], over
 * native slots 47..126's stars), and a settings record from before the move keeps its map */
static uint32_t cz_stars(void)
{
    uint32_t k, n = 0;
    for (k = 0; k < NATIVE_CZ_SLOTS; k++)
        n += (uint32_t)favorite_has(USER_NATIVE_CZ, k);
    return n;
}
static int map_is(const uint8_t *m)
{
    uint32_t q;
    for (q = 0; q < PF_KEYS && perf_map[q] == m[q]; q++)
        ;
    return q == PF_KEYS;
}
static void test_fx_map_storage(void)
{
    static const uint8_t MAP[PF_KEYS] = {PF_PHS, PF_N, PF_FLG, PF_TAPE, PF_R32, PF_N, PF_REV, PF_LPF,
                                         PF_ODN, PF_OUP, PF_FRZ, PF_HPF, PF_R8, PF_R16, PF_PHS, PF_N};
    static const uint8_t STARS[] = {0, 46, 47, 60, 126, 127};
    uint8_t def[PF_KEYS];
    persist_t p;
    uint32_t k, q;
    reset_controls(); memset(&favorites, 0, sizeof favorites); fx_map_sync();
    for (q = 0; q < PF_KEYS; q++)
        def[q] = perf_map[q];
    for (k = 0; k < NATIVE_CZ_SLOTS; k++) {                     /* (one by one: all at once, 31s, read as defaults) */
        memset(&favorites, 0, sizeof favorites);
        favorite_set(USER_NATIVE_CZ, k, 1); fx_map_sync();
        assert(cz_stars() == 1u && map_is(def));                 /* a CZ slot starred: the keys unchanged */
    }
    memset(&favorites, 0, sizeof favorites);                     /* (a device with no settings saved yet) */
    for (q = 0; q < PF_KEYS; q++)
        perf_map_put(fx_keys, q, MAP[q]);
    fx_map_sync();
    assert(map_is(MAP) && !cz_stars());                          /* a key map stars no CZ slot */
    for (k = 0; k < sizeof STARS; k++)
        favorite_set(USER_NATIVE_CZ, STARS[k], 1);
    settings_export(&p); memset(&favorites, 0, sizeof favorites);
    assert(settings_import(&p, sizeof p) == 1); fx_map_sync();
    assert(map_is(MAP) && cz_stars() == sizeof STARS);
    for (k = 0; k < sizeof STARS; k++)
        assert(favorite_has(USER_NATIVE_CZ, STARS[k]));

    memset(&p, 0, sizeof p); p.magic = PERSIST_MAGIC;            /* a record from before the move */
    for (q = 0; q < PF_KEYS; q++)
        perf_map_put(&p.favorites.factory[15][14], q, MAP[q]);
    p.favorites.factory[15][0] = 0x81;                           /* CZ factory tones 0 and 7 */
    p.favorites.factory[15][8] |= 1u << 1;                       /* native CZ slot 0 (bit 65) */
    p.favorites.factory[15][13] |= 1u << 7;                      /* .. 46 (bit 111), the last below the map */
    p.favorites.factory[15][24] |= 1u;                           /* .. 127 (bit 192), above it */
    p.favorites.factory[15][27] = 2u; p.favorites.factory[15][30] = 1u; p.favorites.factory[15][31] = 0x1Cu;
    p.favorites.factory[8][0] = 0x0Cu;                           /* Felucca 1.0's GRAIN stars: left as they are */
    memset(&favorites, 0, sizeof favorites);
    assert(settings_import(&p, sizeof p) == 1); fx_map_sync();
    assert(map_is(MAP));
    assert(cz_stars() == 3u && favorite_has(USER_NATIVE_CZ, 0) && favorite_has(USER_NATIVE_CZ, 46) &&
           favorite_has(USER_NATIVE_CZ, 127));
    assert(favorite_has(ENGI_CZ, 0) && favorite_has(ENGI_CZ, 7) && favorites.factory[8][0] == 0x0Cu);
    assert(settings_screen == 2u && settings_latch == 1u && layer_seen == 0x1Cu);
    favorite_set(USER_NATIVE_CZ, 60, 1);                         /* saved and loaded again: moved once only */
    settings_export(&p); memset(&favorites, 0, sizeof favorites);
    assert(settings_import(&p, sizeof p) == 1); fx_map_sync();
    assert(map_is(MAP) && cz_stars() == 4u && favorite_has(USER_NATIVE_CZ, 60));
    memset(&favorites, 0, sizeof favorites); reset_controls();
    puts("layers/screen: FX key map apart from the native CZ-1 favorites, an old record's map moved once passed");
}
int main(void) { test_layers(); test_screen_sleep(); test_lfo(); test_fx_map_storage(); return 0; }
