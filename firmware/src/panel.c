/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Physical panel: which matrix button / encoder carries which printed label.
 * The default table can be overridden by HARDWARE CALIBRATION (hold OCT- and
 * OCT+ while powering on), which asks for each label in turn. The learned
 * table lives in .noinit and, with MELODEE_FLASH, in flash with the settings
 * (project.c). */
enum { B_FX, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC,
       B_OCTDN, B_OCTUP, NB };
enum { EN_SELECT, EN_ALGO, EN_PRESET, EN_K1, EN_K2, EN_K3, EN_K4, NE };
static const char *const B_NAME[NB] = {"FX", "SCL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE",
                                        "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"};
static const char *const E_NAME[NE] = {"SELECT", "ALGORITHM", "PRESETS", "KNOB 1", "KNOB 2",
                                        "KNOB 3", "KNOB 4"};
#define PANEL_MAGIC 0x50414E35u          /* "PAN5": bump when PANEL_DEFAULT changes */

typedef struct {
    uint32_t magic;
    uint8_t btn[NB];             /* matrix button id (0..13) per label */
    uint8_t enc[NE];             /* matrix encoder (0..6) per role */
    int8_t dir[NE];              /* +1 / -1 so that clockwise is + */
} panel_t;
panel_t panel __attribute__((section(".noinit")));

static const panel_t PANEL_DEFAULT = {
    PANEL_MAGIC,
    {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 1},   /* PLAY = 12, REC = 13 */
    {0, 1, 6, 2, 3, 4, 5},                           /* SELECT = enc 0, ALGORITHM = enc 1, PRESETS = enc 6 */
    {1, 1, 1, 1, 1, 1, 1},
};

static void panel_init(void)                     /* also after a flash load: ids are used as array indexes and shifts */
{
    uint32_t i, ok = panel.magic == PANEL_MAGIC;
    for (i = 0; ok && i < NB; i++)
        ok = panel.btn[i] < 14u;
    for (i = 0; ok && i < NE; i++)
        ok = panel.enc[i] < 7u && (panel.dir[i] == 1 || panel.dir[i] == -1);
    if (!ok)
        panel = PANEL_DEFAULT;
}

static uint32_t panel_btn_of(uint32_t matrix_id)        /* label of a matrix button, NB if none */
{
    uint32_t b;
    for (b = 0; b < NB; b++)
        if (panel.btn[b] == matrix_id)
            return b;
    return NB;
}

static void panel_led(uint32_t label, int on) { fm1_led_key(panel.btn[label], on); }

/* steps of a role, + = clockwise */
static int32_t panel_enc(uint32_t role)
{
    return fm1_enc_take(panel.enc[role]) * panel.dir[role];
}

/* user settings that survive a reset */
#define SETTINGS_MAGIC 0x53455434u              /* "SET4" */
enum { KEYS_OFF, KEYS_LOW, KEYS_MID, KEYS_HIGH, KEYS_FULL, KEYS_N };   /* idle key LEDs (menu KEYS) */
static const char *const KEYS_NAME[KEYS_N] = {"OFF", "LOW", "MID", "HIGH", "FULL"};
struct {
    uint32_t magic, palette, lowcut, zoom;
    uint32_t usb_off;                          /* USB audio devices switched off: UA_OFF_OUT | UA_OFF_IN */
    uint32_t keys;                             /* KEYS_*: playable keys not sounding */
} settings __attribute__((section(".noinit")));

static void settings_save(void);              /* project.c: flash copy (MELODEE_FLASH) */

static void settings_init(void)
{
    if (settings.magic != SETTINGS_MAGIC || settings.palette >= NPALETTES) {
        settings.magic = SETTINGS_MAGIC;
        settings.palette = PAL_STUDIO;
        settings.lowcut = 0;
        settings.zoom = 1;                     /* show the touched value on new installations */
        settings.usb_off = 0;                  /* both USB audio devices on */
        settings.keys = KEYS_MID;
    }
    if (settings.keys >= KEYS_N)               /* a .noinit copy from before KEYS */
        settings.keys = KEYS_MID;
    palette_set(settings.palette);
    fx_lowcut = (uint8_t)(settings.lowcut != 0);
#if MELODEE_USB_AUDIO
    settings.usb_off &= UA_OFF_OUT | UA_OFF_IN;
    ua_off = ua_off_want = (uint8_t)settings.usb_off;   /* before usb_start: the host sees only these */
#endif
}
