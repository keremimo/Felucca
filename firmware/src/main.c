/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* MELODEE boot and main loop. Boot order: WDT first, boot-loop guard, fatal
 * vectors, guards; then LCD, input (TIMER5 IRQ, 10 kHz), audio (ALNK0 IRQ). */
extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _pool_start[], _pool_end[], _rt_start[], _rt_end[], _rt_load[];


void fm1_timer5_irq(void)
{
    static uint32_t sub;
    fm1_timer5_ack();
    melodee_dbg.timer_irqs++;
    if (melodee_dbg.in_audio)
        melodee_dbg.nested++;                      /* only possible if this IRQ outranks ALNK0 */
    fm1_input_tick();
#if MELODEE_USB_AUDIO
    {
        static uint32_t last_poll;
        uint32_t start = fm1_ticks(), gap = start - last_poll, elapsed;
        /* USB work can span several 100 us timer ticks. Count elapsed time, not
         * serviced interrupts, or USB work itself stretches the next deadline. */
        if (!last_poll || gap >= 250u * FM1_TICKS_PER_US) {
            if (last_poll && gap > ua.poll_max_ticks)
                ua.poll_max_ticks = gap;
            last_poll = start;
            usb_poll();                         /* at most 4 kHz, independent of coalesced ticks */
            elapsed = fm1_ticks() - start;
            if (elapsed > ua.service_max_ticks)
                ua.service_max_ticks = elapsed;
        }
    }
#else
    if (sub % 5u == 0u)
        usb_poll();                             /* 2 kHz: all USB SIE traffic lives here */
#endif
#if MELODEE_UART
    if (sub % 5u == 2u)
        uart_midi_poll();                       /* 2 kHz: <= ~7 bytes per call at 31250 baud */
#endif
    if (++sub == 10u)
        sub = 0;
    {   /* milliseconds from the 24 MHz TIMER4: TIMER5 ticks coalesce while ALNK0 renders */
        static uint32_t last, acc;
        uint32_t now = fm1_ticks();
        acc += now - last;
        last = now;
        while (acc >= 1000u * FM1_TICKS_PER_US) {
            acc -= 1000u * FM1_TICKS_PER_US;
            fm1_ms++;
        }
    }
}
extern void isr_timer5(void);

static void timer5_start(void)                 /* OSC /4 = 6 MHz, PRD 600 -> 10 kHz */
{
#if MELODEE_USB_AUDIO
    fm1_timer5_start(isr_timer5, 4);   /* isochronous service cannot wait for a 5.8 ms render */
#else
    fm1_timer5_start(isr_timer5, 1);   /* below ALNK0 (3): no nesting into audio */
#endif
}

static void hexs(char *b, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        b[i] = "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u];
    b[8] = 0;
}

static void fm1_fault(const fm1_crash_t *c)
{
    char b[12];
    uint32_t t0;
    fm1_audio_stop();
    lcd_fill(0, 0, 240, 240, RGB(160, 0, 0));
    draw_text_box(0, 8, 240, &FONT_S, "MELODEE CRASH", C_WHITE, 1);
    hexs(b, c->vec);
    draw_text_box(10, 40, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->pc);
    draw_text_box(10, 60, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->emu);
    draw_text_box(10, 84, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->dbg);
    draw_text_box(10, 102, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->rets);
    draw_text_box(10, 120, 220, &FONT_S, b, C_WHITE, 0);
    t0 = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t0) < 4000u * 1000u * FM1_TICKS_PER_US)
        ;
    fm1_reboot();
}

/* power-on: three parts with their default sounds (TRK_DEF), the drum track, empty patterns
 * (a BOOT project replaces them: project_boot) */
static void melodee_init(void)
{
    uint32_t i;
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults(t);
        if (i < NPART) {
            set_engine_of(t, TRK_DEF[i][0]);
            apply_preset_to(t, TRK_DEF[i][1]);   /* with its sends */
            t->engine = t->eng_req;
        }
        track_defaults_steps(t);              /* the sequencers start empty */
        pat_clear_bank(t);
    }
    song.sel = 0;
    song.master_q12 = 2048;
    ui.home = 1;
    ui.force = 1;
}

static void fm1_main(void)
{
    int32_t knob = 512 * 16;
    persist_boot();
    fm6_boot();                                         /* the FM6 user bank */
#if MELODEE_OTA
    if (flash_ok)
        ota_boot_cleanup();                             /* staging area left by an update */
#endif
    settings_init();
    lcd_init();
    draw_text_box(0, 100, 240, &FONT_L, "MELODEE", C_HI, 1);
    draw_text_box(0, 130, 240, &FONT_S, "MULTI-ENGINE SYNTH", C_GRAY, 1);
    if (melodee_dbg.magic != DBG_MAGIC) {
        memset(&melodee_dbg, 0, sizeof melodee_dbg);
        melodee_dbg.magic = DBG_MAGIC;
    }
    melodee_dbg.boots++;
    melodee_dbg.max_us = 0;
    melodee_dbg.prev_stage = melodee_dbg.stage;     /* a WDT reset leaves the last breadcrumb here */
    melodee_dbg.prev_page = melodee_dbg.page;
    melodee_dbg.prev_home = melodee_dbg.home;
    melodee_dbg.prev_frames = melodee_dbg.ui_frames;
    melodee_dbg.prev_rst = fm1_boot.p3_rst;
    fm1_input_init();
    fm1_adc_init();
    panel_init();
    melodee_init();
    audio_init();
    usb_start();
#if MELODEE_UART
    uart_midi_init();
#endif
    timer5_start();
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    fm1_delay_ms(30);
    if ((fm1_in.buttons & 3u) == 3u) {
        panel_setup();                        /* OCT- + OCT+ held at power-on */
        settings_save();
    }
    project_boot();                           /* SAVE > PROJECT BOOT: that project instead of TRK_DEF */
    fm1_delay_ms(400);
    lcd_fill(0, 0, 240, 240, C_BLACK);

    for (;;) {
        uint32_t m = fm1_ms;
        fm1_wdt_feed();
        usb_retry(fm1_ms);
#if MELODEE_USB_AUDIO
        if (ua_off_apply(fm1_ms)) {                     /* GLO > SYSTEM switched Melodee Out / In */
            settings.usb_off = ua_off;
            settings_save();                            /* while off the bus: no USB deadline missed */
            ui_say("USB ", "RECONNECTING");
        }
#endif
        if (fm1_ms > 30000u && bootguard.pending) {     /* a crash or hang in the first 30 s counts */
            bootguard.pending = 0;
            bootguard.failed = 0;
        }
        {
            int32_t b = fm1_adc_read(FM1_ADC_BATT);     /* battery: slow IIR */
            if (b > 0)
                song.batt_raw = song.batt_raw ? song.batt_raw + (b - song.batt_raw) / 32 : b;
        }
        {
            int32_t a = fm1_adc_read(FM1_ADC_MASTER);
            if (a >= 0) {
                uint32_t k10;
                knob += (a * 16 - knob) / 8;
                k10 = (uint32_t)(knob / 16);
                song.master_q12 = (k10 * k10) >> 8;            /* 0 .. ~4096 */
            }
        }
        {   /* OCT- + OCT+ held 5 s: enter UBOOT with RAM intact (debug / update); a countdown
             * shows from 2 s, letting go cancels it */
            static uint32_t t0, shown;
            uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
            if ((fm1_in.buttons & both) != both) {
                if (shown)
                    ui_say("UPDATE MODE ", "CANCELLED");
                shown = 0;
                t0 = fm1_ms;
            } else if (fm1_ms - t0 > 2000u && fm1_ms - t0 <= 5000u) {
                uint32_t left = (5000u - (fm1_ms - t0) + 999u) / 1000u;
                if (left != shown) {
                    char d[4] = {(char)('0' + left), '.', '.', 0};
                    ui_say("UPDATE MODE IN ", d);
                    shown = left;
                }
            } else if (fm1_ms - t0 > 5000u) {
                fm1_audio_stop();
                lcd_fill(0, 0, 240, 240, C_BLACK);
                draw_text_box(0, 110, 240, &FONT_S, "UBOOT", RGB(80, 120, 255), 1);
                usb_detach();
                fm1_delay_ms(30);
                bootguard.pending = 0;                  /* intentional reset: not a failed boot */
                fm1_enter_uboot();
            }
        }
        fm6_service();                                  /* DX7 SysEx for FM6 */
#if MELODEE_OTA
        ed_service();                                   /* web editor SysEx */
        ota_service();                                  /* M-UPGRADE handshake */
        if (usb.ota_req) {                              /* M-UPGRADE upgrade command */
            usb.ota_req = 0;
            panic_req = (1u << NTRK) - 1u;               /* every track (bit per track) */
            if (flash_ok)
                ota_session();                          /* returns only if nothing was committed */
            lcd_fill(0, 0, 240, 240, C_BLACK);
            ui.force = 1;
        }
#endif
        if (usb.uboot_req) {                            /* SysEx F0 22 24 35 7D F7 from the host */
            fm1_audio_stop();
            lcd_fill(0, 0, 240, 240, C_BLACK);
            draw_text_box(0, 110, 240, &FONT_S, "UBOOT (USB)", C_WHITE, 1);
            fm1_delay_ms(20);
            usb_detach();
            fm1_delay_ms(30);
            bootguard.pending = 0;
            fm1_enter_uboot();
        }
#if MELODEE_CDC
        cdc_task();
#endif
        melodee_dbg.ui_frames++;
        melodee_dbg.page = ui.page;
        melodee_dbg.home = ui.home;
        melodee_dbg.stage = 1;
        ui_input();
        melodee_dbg.stage = 2;
        ui_leds();
        ui_draw();
        melodee_dbg.stage = 9;
        while (fm1_ms - m < 15u) {                               /* ~60 UI frames/s at most */
            ui_input();
#if MELODEE_OTA
            ed_service();                       /* editor replies without waiting for the next frame */
#endif
        }
    }
}

void fm1_cstart(void)
{
    uint32_t *s, *d, p3, src, wdt;
    fm1_time_init();
    fm1_reset_reason();
    p3 = fm1_boot.p3_rst;
    src = fm1_boot.rst_src;
    wdt = fm1_boot.wdt_con;
    fm1_wdt_arm(0x0D);
    if (bootguard.magic != BOOTGUARD_MAGIC) {
        bootguard.magic = BOOTGUARD_MAGIC;
        bootguard.failed = 0;
        bootguard.pending = 0;
    }
    if (bootguard.pending)
        bootguard.failed++;
    bootguard.pending = 1;
    if (bootguard.failed >= 2u) {
        bootguard.failed = 0;
        bootguard.pending = 0;
        fm1_enter_uboot();
    }
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (d = _pool_start; d < _pool_end; d++)
        *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++)
        *d = *s;                                /* flash driver code that must run from RAM */
    fm1_mailbox_clear();
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    fm1_boot.p3_rst = (uint8_t)p3;
    fm1_boot.rst_src = src;
    fm1_boot.wdt_con = (uint8_t)wdt;
    fm1_main();
    for (;;)
        ;
}
