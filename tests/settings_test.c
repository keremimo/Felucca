/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../firmware/src/tuning.h"
#define __attribute__(x)
#define NENGINES 20u
#define USER_GENERAL 16u
#define USER_NATIVE_P5 20u
#define ENGI_PROPHET 19u
static int p5_favorite_has(uint32_t slot,int factory){(void)slot;(void)factory;return 0;}
static int p5_favorite_set(uint32_t slot,int on,int factory){(void)slot;(void)on;(void)factory;return 0;}
#define UP_SLOTS 64u
#define USER_NATIVE_FM 17u
#define USER_NATIVE_CZ 18u
#define ENGI_FM6 12u
#define ENGI_CZ 15u
static uint8_t fx_lowcut;
static void fm1_led_key(unsigned k, int on) { (void)k; (void)on; }
static int fm1_enc_take(unsigned k) { (void)k; return 0; }
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; }
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
static void settings_save(void) {}
static uint8_t settings_boot, settings_drumch = 10;  /* (params.c) */
#define PF_KEYS 16u                                       /* (perform.c: the FX key map's keys and effects) */
#define PF_NFX 12u
static int native_used(uint32_t e, uint32_t k) { (void)e; (void)k; return 0; }
#if __has_include("../firmware/src/favorites.c")
#include "../firmware/src/favorites.c"
#endif
#include "../firmware/src/settings_persist.c"
int main(void)
{
    persist_t original = {0}, p;
    original.magic = PERSIST_MAGIC;
    original.palette = 4; original.bold = original.lowcut = original.zoom = 1;   /* old id 4: MONO */
    original.panel = PANEL_DEFAULT; original.panel.enc[0] = 3;
    original.favorites.factory[8][0] = 1;
    original.favorites.user = 1u << 31; original.favorites.filter = 1;
    p = original;
    assert(settings_import(&p, sizeof p) == 1); settings_init();
    assert(settings.palette == UI_MONO_INDEX && fx_lowcut && settings.zoom && panel.enc[0] == 3);
    assert(p.palette == palette_to_stored(UI_MONO_INDEX));        /* migrated in place */
    {   /* every old id maps to a palette; tagged ids round trip; anything else is MONO */
        persist_t q = original;
        for (uint32_t i = 0; i < 20u; i++) assert(palette_from_stored(i) < NPALETTES);
        assert(palette_from_stored(13) == 6u && palette_from_stored(11) == 7u && palette_from_stored(19) == 7u);
        for (uint32_t i = 0; i < NPALETTES; i++) assert(palette_from_stored(palette_to_stored(i)) == i);
        assert(palette_from_stored(40) == UI_MONO_INDEX && !palette_stored_ok(40) && palette_stored_ok(3));
        q.palette = palette_to_stored(5);
        assert(settings_import(&q, sizeof q) == 1 && settings.palette == 5u);
        settings.magic = SETTINGS_MAGIC_OLD; settings.palette = 13; settings_init();   /* retained SET3 */
        assert(settings.magic == SETTINGS_MAGIC && settings.palette == 6u);
        p = original; assert(settings_import(&p, sizeof p) == 1); settings_init();
    }
#ifdef MELODEE_FAVORITES
    assert(favorite_has(8, 0) && favorite_has(USER_GENERAL, 31) && favorites.filter);
#endif
    favorite_set(USER_NATIVE_FM,63,1);favorite_set(USER_NATIVE_CZ,127,1);favorite_set(USER_GENERAL,63,1);
    settings_export(&p);memset(&favorites,0,sizeof favorites);favorites_user_hi=0;settings_import(&p,sizeof p);
    assert(favorite_has(USER_NATIVE_FM,63) && favorite_has(USER_NATIVE_CZ,127) && favorite_has(USER_GENERAL,63));
    assert(!favorite_has(USER_NATIVE_FM,64) && !favorite_has(USER_NATIVE_CZ,128));
    p=original;settings_import(&p,sizeof p);
    {
        uint8_t legacy[16]; memset(favorites.factory[14], 0xA5, 32);
        memcpy(legacy, favorites.factory[14], sizeof legacy);
        assert(!scale_favorite(0) && !scale_favorite(69));
        assert(scale_favorite_set(0, 1) && scale_favorite_set(69, 1) && scale_favorite_set(95, 1));
        assert(!scale_favorite_set(96, 1) && !scale_favorite_set(69, 1));
        assert(!memcmp(legacy, favorites.factory[14], sizeof legacy));
        settings_export(&p); memset(&favorites, 0, sizeof favorites); settings_import(&p, sizeof p);
        assert(scale_favorite(0) && scale_favorite(69) && scale_favorite(95));
        assert(scale_favorite_set(69, 0) && !scale_favorite(69) && scale_favorite(95));
        p=original;settings_import(&p,sizeof p);
    }
    settings.lowcut = 0;
    settings_export(&p);
    assert(!p.lowcut && p.bold == 1 && p.favorites.user == (1u << 31));   /* bold: kept as saved */
    assert(p.favorites.factory[8][0] == 1 && p.favorites.filter == 1);
    assert(p.panel.enc[0] == 3); /* saving one feature preserves the other */
    {   /* A4 shares PER5's last spare word; old and malformed records use 440 Hz. */
        persist_t q = original;
        assert(tuning_a4 == 440);
        tuning_a4 = 432;
        settings_export(&q);
        assert(q.ext.spare[1] == (A4_TAG | 432u));
        tuning_a4 = 480;
        assert(settings_import(&q, sizeof q) == 1 && tuning_a4 == 432);
        for (uint32_t hz = A4_MIN; hz <= A4_MAX; hz++) {
            tuning_a4 = (int16_t)hz; settings_export(&q);
            tuning_a4 = 0;
            assert(settings_import(&q, sizeof q) == 1 && tuning_a4 == (int16_t)hz);
        }
        const uint32_t invalid[] = {0u, 432u, 0xFFFFFFFFu, A4_TAG | 399u, A4_TAG | 481u};
        for (uint32_t k = 0; k < sizeof invalid / sizeof invalid[0]; k++) {
            q.ext.spare[1] = invalid[k]; tuning_a4 = 432;
            assert(settings_import(&q, sizeof q) == 1 && tuning_a4 == 440);
        }
        q = original; q.magic = PERSIST_MAGIC4; tuning_a4 = 432;
        assert(settings_import(&q, PERSIST_LEN4) == 2 && tuning_a4 == 440);
        p = original; settings_import(&p, sizeof p);
    }
    p = original; p.magic = PERSIST_MAGIC4; p.ext.usb_off = 3;   /* PER4 (Felucca 1.0): favorites, no ext */
    assert(settings_import(&p, PERSIST_LEN4) == 2 && p.magic == PERSIST_MAGIC);
    assert(p.favorites.user == (1u << 31) && p.favorites.factory[8][0] == 1 && !p.ext.usb_off);
    settings_drumch = 0; settings_export(&p);        /* DRUM OFF: 17 in the record; 10 (the default): 0 */
    assert(p.ext.drumch == 17u && settings_import(&p, sizeof p) == 1 && !settings_drumch);
    settings_drumch = 10; settings_export(&p); assert(!p.ext.drumch);
    settings_drumch = 3; assert(settings_import(&p, sizeof p) == 1 && settings_drumch == 10u);
    p = original; p.ext.usb_off = 2;                /* PER5: ext kept by a build without USB audio */
    assert(settings_import(&p, sizeof p) == 1 && p.ext.usb_off == 2u);
    settings_export(&p); assert(p.ext.usb_off == 2u);
    p = original; p.magic = 0x50455233u;
    assert(settings_import(&p, PERSIST_LEN4 - sizeof p.favorites) == 2);
    assert(p.bold == 1 && !p.favorites.user && !p.favorites.filter);
    settings_export(&p); assert(p.bold == 1); /* favorites-only preserves PER3 font */
    p = original; p.magic = 0x50455232u;
    assert(settings_import(&p, PERSIST_LEN4 - sizeof p.favorites - sizeof p.bold) == 2);
    assert(!p.bold && !p.favorites.user && settings.zoom && panel.enc[0] == 3);
    p = original; p.magic = 0x50455231u;
    memcpy((uint8_t *)&p + 8, &PANEL_DEFAULT, sizeof(panel_t));
    assert(settings_import(&p, 8 + sizeof(panel_t)) == 2);
    assert(!p.bold && !p.zoom && !p.lowcut && !p.favorites.user && panel.magic == PANEL_MAGIC);
    {   /* HOLD: in the retired bold field; older records (bold 0 / 1) are 0.4 s, and stay as saved */
        p = original; p.bold = 0;
        assert(settings_import(&p, sizeof p) == 1 && settings_hold == HOLD_DEF && HOLD_MS[settings_hold] == 400u);
        p = original;
        assert(settings_import(&p, sizeof p) == 1 && settings_hold == HOLD_DEF);
        settings_hold = 3;
        settings_export(&p);
        assert(hold_stored_ok(p.bold) && p.bold != 1u);
        settings_hold = 0;
        assert(settings_import(&p, sizeof p) == 1 && settings_hold == 3u && HOLD_MS[settings_hold] == 600u);
        settings_hold = HOLD_DEF;
        settings_export(&p);
        assert(p.bold == 0u && settings_import(&p, sizeof p) == 1 && settings_hold == HOLD_DEF);
        assert(!hold_stored_ok(2u) && !hold_stored_ok(HOLD_TAG + 4u) && hold_stored_ok(HOLD_TAG | 2u));
        p = original; p.magic = 0x50455231u;
        memcpy((uint8_t *)&p + 8, &PANEL_DEFAULT, sizeof(panel_t));
        settings_hold = 2;
        assert(settings_import(&p, 8 + sizeof(panel_t)) == 2 && settings_hold == HOLD_DEF);
    }
    assert(settings_import(&p, 3) == 0 && settings_import(&p, -1) == 0);
    assert(settings_import(&p, sizeof p - 1) == 0);
    puts("Settings: PER1..PER4 migration, palette ids, calibration, HOLD, ext and independent feature preservation passed.");
}
