/* SPDX-License-Identifier: GPL-3.0-only */
/* Shared PER5 layout: each feature updates only its own fields, preserving
 * the other feature's saved preferences when either is built independently.
 * PER1/PER2 are upstream; PER3 added bold; PER4 added favorites (Felucca 1.0); PER5 added Melodee's ext block
 * (usb_off: the USB audio devices left out, USB AUDIO in the menu; spare words read 0 = the default).
 * palette: UI_PAL_TAG + index; an old id (below 20, earlier firmware) is migrated on import.
 * bold: no longer used (one font weight); kept as it was saved, unless it holds the HOLD setting (panel.c). */
typedef struct {
    uint32_t magic, palette, lowcut, zoom;
    panel_t panel;
    uint32_t bold;
    struct { uint8_t factory[16][32]; uint32_t user, filter; } favorites;
    struct { uint32_t usb_off, spare[7]; } ext;
} persist_t;
#define PERSIST_MAGIC 0x50455235u
#define PERSIST_MAGIC4 0x50455234u                  /* Felucca 1.0: no ext */
#define PERSIST_LEN4 (sizeof(persist_t) - sizeof(((persist_t *)0)->ext))

/* Normalize in place; 1 = current, 2 = migrated, 0 = invalid. */
static int settings_import(persist_t *p, int n)
{
    int current = n == (int)sizeof *p && p->magic == PERSIST_MAGIC;
    int old4 = n == (int)PERSIST_LEN4 && p->magic == PERSIST_MAGIC4;
    int old3 = n == (int)(PERSIST_LEN4 - sizeof p->favorites) && p->magic == 0x50455233u;
    int old2 = n == (int)(PERSIST_LEN4 - sizeof p->favorites - sizeof p->bold) && p->magic == 0x50455232u;
    int old1 = n == (int)(8u + sizeof(panel_t)) && p->magic == 0x50455231u;
    if (!(current || old4 || old3 || old2 || old1)) return 0;
    if (old1) {
        panel_t old;
        memcpy(&old, (uint8_t *)p + 8, sizeof old);
        p->panel = old;
        p->lowcut = p->zoom = 0;
    }
    if (old1 || old2) p->bold = 0;
    if (!current && !old4) memset(&p->favorites, 0, sizeof p->favorites);
    if (!current) memset(&p->ext, 0, sizeof p->ext);
    p->ext.usb_off &= 3u;
#if MELODEE_USB_AUDIO
    ua_off = ua_off_want = (uint8_t)p->ext.usb_off;    /* (usb_start, after this, builds the configuration) */
#endif
    p->magic = PERSIST_MAGIC;
    p->palette = palette_to_stored(palette_from_stored(p->palette));
    settings.magic = SETTINGS_MAGIC;
    settings.palette = palette_from_stored(p->palette);
    settings.lowcut = p->lowcut;
    settings.zoom = p->zoom;
    settings_hold = (uint8_t)hold_from_stored(p->bold);
#ifdef MELODEE_FAVORITES
    memcpy(&favorites, &p->favorites, sizeof favorites);
    favorites.filter = favorites.filter == 1u;
#if defined(FM4_NPRESETS) && !MELODEE_FM4
    {   /* DIGITAL's starred presets (engine 1, retired) -> the FM6 presets that cover them (fm4_convert.c) */
        uint32_t k;
        for (k = 0; k < FM4_NPRESETS; k++)
            if ((favorites.factory[ENGI_DIGITAL][0] >> k) & 1u)
                favorites.factory[ENGI_FM6][FM4_TO_FM6[k] / 8u] |= (uint8_t)(1u << (FM4_TO_FM6[k] % 8u));
        memset(favorites.factory[ENGI_DIGITAL], 0, sizeof favorites.factory[ENGI_DIGITAL]);
    }
#endif
#endif
    if (p->panel.magic == PANEL_MAGIC) panel = p->panel;
    return current ? 1 : 2;
}

/* Start with the last imported/saved object, including fields owned by a
 * feature absent from this build. No save-on-boot or extra flash writes. */
static void settings_export(persist_t *p)
{
    p->magic = PERSIST_MAGIC;
    p->palette = palette_to_stored(settings.palette);
    p->lowcut = settings.lowcut;
    p->zoom = settings.zoom;
    p->panel = panel;
    p->bold = hold_to_stored(p->bold, settings_hold);
#ifdef MELODEE_FAVORITES
    memcpy(&p->favorites, &favorites, sizeof favorites);
#endif
#if MELODEE_USB_AUDIO
    p->ext.usb_off = ua_off;
#endif
}
