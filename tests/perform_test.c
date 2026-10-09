/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the FX hold layer's effects (firmware/src/perform.c), same sources as the firmware (through
 * hostsim.c).   build/host/perform_test [DEMO_DIR]          (run_tests.sh: build/perform_demo)
 * 1. timing: REPEAT and REVERSE start on the next 1/16 of the transport (sample exact) and end when let go
 *    (the live signal back, bit for bit, after the 2.9 ms ramp); the others start at once.
 * 2. stereo: REPEAT, REVERSE, TAPE STOP and FREEZE keep left and right apart (a silent right channel stays
 *    silent; each side follows its own input).
 * 3. a REPEAT too long for the loop (1/8 and REVERSE below 81 BPM) does nothing at all.
 * 4. the SLICER: its recordings are not used meanwhile and are dropped afterwards.
 * 5. the keys: a layer key plays nothing and sends no MIDI; a key held before FX stays a note; the white
 *    keys past the first 12 (D5 .. G5, none in the default map) are the layer's too but do nothing.
 * 6. idle: nothing held, nothing ramping, the mix is bit-identical (and the goldens of regress.c too).
 * 7. no clicks, no overflow; the filters, the CRUSH and THROW macros and the mutes do what they say.
 * 8. cost: instructions per sample of the song, idle and with every effect at once (proc_pid_rusage).
 * 9. OCT UP / OCT DN (the harmonizer): the pitch of the shifted part (the output less its 0.56 of the live
 *    input), each side its own: a sine whose period fits 1024 samples (the taps' distance) a whole number of
 *    times (215.33 / 344.53 Hz: both taps in phase, their jumps whole periods: no splice error) comes out at
 *    2x / 0.5x within 3 cents (a 2.5 s DFT, its peak refined); any other sine (220 / 330 Hz) has its energy in
 *    lines fs / 2048 = 21.5 Hz apart round the target (the rotating-delay splice, kept from the original: its "phasey"
 *    sound): its strongest line within one spacing of 2x / 0.5x. A silent side stays silent; no click in or out on a 110 Hz sine and the live signal back bit for bit
 *    after the ramp; the shimmer (KNOB 4 at 100, 0.82) on 4x full-scale square waves stays bounded and dies
 *    away after the input stops; KNOB 4 at 0 is plain (the delay holds only the input); the cost: over the
 *    idle song, and the harmonizer's own over REPEAT 1/16 alone (the layer's stage, which every effect of it
 *    pays): about 2 % at most (device estimate: 1.7 % per 100 instructions per sample).
 * 10. FLANGER, PHASER (1.2): silence stays silence, a silent side silent; no DC (a zero-mean noise keeps its mean, DC
 *    passes at unity) and about the input's level; bounded on 4x full-scale squares with LPF, HPF, REPEAT and CRUSH;
 *    the sweep follows the tempo (a 441 Hz saw comes out the same a bar / half a bar later at 120 and 90 BPM, not
 *    after 120's bar at 90); no click in or out, the live signal back bit for bit; stacked with REPEAT + LPF, let go:
 *    those as alone, bit for bit; L and R apart; KNOB 4 at 0 %: live; the cost.
 * 11. the key map (perform.c perf_map_of / perf_map_put, seq.c): 0 = the default; any effect or NONE on any key, the
 *    others kept; the keys of a remapped layer; a key remapped while held takes its new effect; two keys with one
 *    effect; FX LATCH follows a remap; the default map on the keys.
 * Demos (WAV) into DEMO_DIR; with a second directory, the harmonizer's (a melody with OCT UP, OCT DN, and
 * OCT UP with the shimmer) into it. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

static int check(const char *what, int ok)
{
    printf("perform: %-73s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

/* ------------------------------------------------------ a test signal --- */
#define NT (6u * 44100u)
static int32_t in_l[NT], in_r[NT], out_l[NT], out_r[NT];
static void test_signal(double fl, double al, double fr, double ar)
{
    uint32_t t;
    for (t = 0; t < NT; t++) {
        in_l[t] = (int32_t)lrint(al * sin(2 * M_PI * fl * t / FS));
        in_r[t] = (int32_t)lrint(ar * sin(2 * M_PI * fr * t / FS + 1.0));
    }
}
static void perf_reset(void)
{
    memset(&pf, 0, sizeof pf);
    pf.src = pf.next = PF_N;
    pf.lc = PF_TOP;
    pf.mg[0] = pf.mg[1] = pf.mg[2] = pf.mg[3] = 32768;
    perf_held = perf_act = 0;
    perf_latched = 0;
    perf_kill = 0;
    memcpy((void *)perf_map, PF_DEF, sizeof PF_DEF);
    memset((void *)perf_k, 0, sizeof perf_k);
    sl_lent = 0;
    memset(sl, 0, sizeof sl);
}
/* the stage alone on the test signal: samples [t0, t1); ev: press (+e + 1) / let go (-(e + 1)) at block bt */
typedef struct { uint32_t t; int e; } ev_t;
static int busy_seen;
static int8_t run_crush;                           /* KNOB 2 (CRUSH) during run(), 0 = untouched */
static int8_t run_k4;                              /* KNOB 4 (DEPTH; with OCT UP / DN the shimmer) during run() */
static void run(uint32_t bpm, int playing, const ev_t *ev, uint32_t nev, uint32_t t1)
{
    uint32_t t, k = 0;
    host_tracks_init();
    perf_reset();
    perf_k[1] = run_crush;
    perf_k[3] = run_k4;
    song.g[G_BPM] = (int16_t)bpm;
    song.playing = (uint8_t)playing;
    perf_start();
    busy_seen = 0;
    for (t = 0; t < t1; t += CTL) {
        while (k < nev && ev[k].t <= t) {
            perf_press((uint32_t)(ev[k].e > 0 ? ev[k].e - 1 : -ev[k].e - 1), ev[k].e > 0);
            k++;
        }
        memcpy(out_l + t, in_l + t, CTL * 4);
        memcpy(out_r + t, in_r + t, CTL * 4);
        if (perf_begin(CTL)) {
            busy_seen = 1;
            perf_block(out_l + t, out_r + t, CTL);
        }
    }
}
#define P16 5512u                                  /* a 1/16 at 120 BPM */
static int same(uint32_t a, uint32_t b)            /* out == in on [a, b), both sides */
{
    uint32_t t;
    for (t = a; t < b; t++)
        if (out_l[t] != in_l[t] || out_r[t] != in_r[t])
            return 0;
    return 1;
}
static int32_t err_vs(uint32_t a, uint32_t b, int32_t (*src)(const int32_t *, uint32_t), int side)
{
    uint32_t t;
    int32_t e = 0;
    for (t = a; t < b; t++) {
        int32_t d = abs((side ? out_r : out_l)[t] - src(side ? in_r : in_l, t));
        e = d > e ? d : e;
    }
    return e;
}
static uint32_t shift_d;                           /* the loop: in[t - shift_d] */
static int32_t delayed(const int32_t *x, uint32_t t) { return x[t - shift_d]; }
static uint32_t rev_a, rev_len, rev_t0;            /* REVERSE: in[rev_a + rev_len - 1 - (t - rev_t0) % rev_len] */
static int32_t reversed(const int32_t *x, uint32_t t) { return x[rev_a + rev_len - 1u - (t - rev_t0) % rev_len]; }
static int32_t peak(const int32_t *x, uint32_t a, uint32_t b)
{
    int32_t m = 0;
    for (; a < b; a++)
        m = abs(x[a]) > m ? abs(x[a]) : m;
    return m;
}

/* ------------------------------------------------------------ 1. timing --- */
static int test_timing(void)
{
    int bad = 0;
    char what[160];
    test_signal(440, 12000, 277, 9000);
    {   /* REPEAT 1/16: pressed mid-1/16, records the next 1/16, then loops it */
        ev_t ev[] = {{992, PF_R16 + 1}, {20000, -(PF_R16 + 1)}};
        int32_t el, er, el2;
        run(120, 1, ev, 2, 40000);
        shift_d = P16;
        el = err_vs(2 * P16 + 160, 3 * P16 - 160, delayed, 0);
        er = err_vs(2 * P16 + 160, 3 * P16 - 160, delayed, 1);
        shift_d = 2 * P16;
        el2 = err_vs(3 * P16 + 160, 20000 - 160, delayed, 0);
        snprintf(what, sizeof what, "REPEAT 1/16: live until the 1/16 after the press + one 1/16 (exact)");
        bad += check(what, same(0, 2 * P16));
        snprintf(what, sizeof what, "  then the 1/16 from that 1/16, looped (error L %d R %d, 2nd pass %d of 12000)", el, er, el2);
        bad += check(what, el < 100 && er < 100 && el2 < 100);
        bad += check("  let go: the live signal again after the ramp, bit for bit", same(20000 + 160, 40000));
        bad += check("  the buffer given back, nothing left running", !sl_lent && pf.mode == BM_NONE && !pf.busy);
    }
    {   /* REVERSE: the next 1/8 recorded, then played backwards */
        ev_t ev[] = {{992, PF_REV + 1}, {40000, -(PF_REV + 1)}};
        int32_t el, er;
        run(120, 1, ev, 2, 50000);
        rev_a = P16;
        rev_len = 11025;
        rev_t0 = P16 + rev_len;
        el = err_vs(rev_t0 + 160, rev_t0 + rev_len - 160, reversed, 0);
        er = err_vs(rev_t0 + 160, rev_t0 + rev_len - 160, reversed, 1);
        bad += check("REVERSE: live until the 1/16 after the press + a 1/8 (exact)", same(0, rev_t0));
        snprintf(what, sizeof what, "  then that 1/8 backwards (error L %d R %d of 12000)", el, er);
        bad += check(what, el < 100 && er < 100);
        bad += check("  let go: live again", same(40000 + 160, 50000));
    }
    {   /* stopped: at once */
        ev_t ev[] = {{992, PF_R16 + 1}};
        run(120, 0, ev, 1, 12000);
        bad += check("stopped: a 1/16 effect starts at once", (perf_act & PF_BIT(PF_R16)) && !same(992, 12000));
    }
    {   /* the immediate ones: LPF moves at once */
        ev_t ev[] = {{992, PF_LPF + 1}};
        run(120, 1, ev, 1, 3000);
        bad += check("LPF starts at once (not on the 1/16)", same(0, 992) && !same(992, 3000));
    }
    return bad;
}

/* ------------------------------------------------------------ 2. stereo --- */
static int test_stereo(void)
{
    static const struct { const char *name; int e; } C[] = {
        {"REPEAT 1/8", PF_R8}, {"REVERSE", PF_REV}, {"TAPE STOP", PF_TAPE}, {"FREEZE", PF_FRZ}};
    uint32_t c;
    int bad = 0;
    char what[160];
    for (c = 0; c < 4u; c++) {
        ev_t ev[] = {{992, C[c].e + 1}, {60000, -(C[c].e + 1)}};
        int32_t pl, pr;
        double cl = 0, cr = 0, nl = 0, nr = 0;
        uint32_t t;
        test_signal(440, 12000, 0, 0);                    /* the right side silent */
        run(120, 1, ev, 2, 70000);
        pl = C[c].e == PF_TAPE ? peak(out_l, 2000, 12000) : peak(out_l, 30000, 60000);
        pr = peak(out_r, 0, 70000);
        snprintf(what, sizeof what, "%s: a silent right side stays silent (L %d, R %d)", C[c].name, pl, pr);
        bad += check(what, pr == 0 && pl > 3000);
        test_signal(440, 12000, 3 * 440, 12000);          /* each side its own pitch */
        run(120, 1, ev, 2, 70000);
        for (t = C[c].e == PF_TAPE ? 1100u : 30000u; t < (C[c].e == PF_TAPE ? 2600u : 50000u); t++) {
            cl += (double)out_l[t] * out_l[t - 1];
            nl += (double)out_l[t] * out_l[t];
            cr += (double)out_r[t] * out_r[t - 1];
            nr += (double)out_r[t] * out_r[t];
        }
        cl = nl > 0 ? cl / nl : 1;
        cr = nr > 0 ? cr / nr : 1;
        snprintf(what, sizeof what, "  each side keeps its own pitch (lag-1 correlation L %.3f, R %.3f)", cl, cr);
        bad += check(what, cl > 0.99 && cr < 0.99 && cr > 0.9 && cl - cr > 0.008);
    }
    {   /* FREEZE holds: the input stops, the sound goes on */
        ev_t ev[] = {{992, PF_FRZ + 1}, {60000, -(PF_FRZ + 1)}};
        uint32_t t;
        test_signal(440, 12000, 330, 9000);
        for (t = 9000; t < NT; t++)
            in_l[t] = in_r[t] = 0;
        run(120, 1, ev, 2, 70000);
        snprintf(what, sizeof what, "FREEZE: the input gone, it holds what it caught (L %d R %d)", peak(out_l, 20000, 60000),
                 peak(out_r, 20000, 60000));
        bad += check(what, peak(out_l, 20000, 60000) > 6000 && peak(out_r, 20000, 60000) > 4000 && same(60000 + 160, 70000));
    }
    {   /* TAPE STOP: slower, then silent within its beat */
        ev_t ev[] = {{992, PF_TAPE + 1}, {50016, -(PF_TAPE + 1)}};
        test_signal(440, 12000, 330, 9000);
        run(120, 1, ev, 2, 60000);
        snprintf(what, sizeof what, "TAPE STOP: silent a beat after the press (peak %d), live again after", peak(out_l, 992 + 22050 + 64, 50016));
        bad += check(what, peak(out_l, 992 + 22050 + 64, 50016) == 0 && peak(out_r, 992 + 22050 + 64, 50016) == 0 &&
                           same(50016 + 160, 60000));
    }
    return bad;
}

/* -------------------------------------------------------- 3. too long --- */
static int test_too_long(void)
{
    int bad = 0;
    ev_t ev[] = {{992, PF_R8 + 1}, {40000, -(PF_R8 + 1)}};
    test_signal(440, 12000, 277, 9000);
    run(72, 1, ev, 2, 50000);
    bad += check("REPEAT 1/8 at 72 BPM (417 ms, the loop holds 371): nothing happens, bit for bit",
                 same(0, 50000) && !busy_seen && !sl_lent && !(perf_avail() & PF_BIT(PF_R8)));
    run(120, 1, ev, 2, 50000);
    bad += check("REPEAT 1/8 at 120 BPM (250 ms): it plays", !same(0, 40000) && (perf_avail() & PF_BIT(PF_R8)));
    song.g[G_BPM] = 81;
    bad += check("  REPEAT 1/8 and REVERSE (1/8) fit from 81 BPM, 1/16 from 41",
                 (perf_avail() & PF_BIT(PF_R8) && perf_avail() & PF_BIT(PF_REV)) &&
                 (song.g[G_BPM] = 80, !(perf_avail() & PF_BIT(PF_R8)) && !(perf_avail() & PF_BIT(PF_REV))) &&
                 (song.g[G_BPM] = 41, perf_avail() & PF_BIT(PF_R16)) &&
                 (song.g[G_BPM] = 40, !(perf_avail() & PF_BIT(PF_R16))));
    {   /* a REPEAT held while the tempo slows past its fit stops cleanly */
        uint32_t t;
        int32_t mx = 0;
        host_tracks_init();
        perf_reset();
        song.g[G_BPM] = 120;
        song.playing = 1;
        perf_start();
        test_signal(110, 12000, 110, 12000);
        perf_press(PF_R8, 1);
        for (t = 0; t < 120000u; t += CTL) {
            uint32_t i;
            if (t == 60000u)
                song.g[G_BPM] = 72;
            memcpy(out_l + t % NT, in_l + t % NT, CTL * 4);
            memcpy(out_r + t % NT, in_r + t % NT, CTL * 4);
            if (perf_begin(CTL))
                perf_block(out_l + t % NT, out_r + t % NT, CTL);
            for (i = 1; i < CTL && t > 1000u; i++) {
                int32_t d = abs(out_l[t % NT + i] - out_l[t % NT + i - 1]);
                mx = d > mx ? d : mx;
            }
        }
        perf_press(PF_R8, 0);
        bad += check("  a REPEAT 1/8 held while the tempo drops below its fit: it ends, no click",
                     pf.mode == BM_NONE && !sl_lent && mx < 4 * 190);
    }
    return bad;
}

/* ------------------------------------------------------------ the song --- */
static void song_setup(void)
{
    static const uint8_t ACID[16] = {45, 45, 57, 45, 0, 48, 45, 55, 45, 0, 57, 52, 45, 48, 0, 50};
    static const uint8_t AM[4] = {57, 60, 64, 67};
    track_t *t1 = &trk[0], *t2 = &trk[1], *td = &trk[3];
    uint32_t i;
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    perf_reset();
    song.g[G_BPM] = 120;
    host_preset(t1, 0, 4);
    host_preset(t2, 1, 5);
    host_808_kit(td);
    for (i = 0; i < 16u; i++) {
        uint8_t n = ACID[i], d[4], k = 0;
        put_step(t1, i, n ? 1u : 0u, &n, n ? ST_NOTE : ST_REST, 0);
        put_step(t2, i, i % 8u == 0u ? 4u : 0u, AM, i % 8u == 0u ? ST_NOTE : ST_TIE, 0);
        if (i % 4u == 0u) d[k++] = 36;
        if (i % 2u == 0u) d[k++] = 42;
        if (i == 4u || i == 12u) d[k++] = 38;
        put_step(td, i, k, d, k ? ST_NOTE : ST_REST, 0);
    }
    trk[0].p[P_PAN] = -40;                         /* the mix itself stereo */
    trk[1].p[P_PAN] = 40;
}
static int32_t song_l[NT], song_r[NT];
static void song_render(uint32_t frames, void (*at)(uint32_t t))
{
    uint32_t t, i;
    int32_t o[2 * CTL];
    transport_req = 1;
    for (t = 0; t < frames; t += CTL) {
        if (at)
            at(t);
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            song_l[t + i] = o[2 * i];
            song_r[t + i] = o[2 * i + 1];
        }
    }
    transport_req = 2;
    mix_block(o, CTL);
}

/* ------------------------------------------------------------ 4. SLICER --- */
static int slicer_ok;
static void stut_at(uint32_t t)
{
    uint32_t k;
    if (t == 44032u)
        perf_press(PF_R16, 1);
    if (t > 50000u && t < 80000u)
        for (k = 0; k < NTRK; k++)
            if (sl[k].rec_on || sl[k].loop)
                slicer_ok = 0;
    if (t > 50000u && t < 80000u && !sl_lent)
        slicer_ok = 0;
    if (t == 80000u)
        perf_press(PF_R16, 0);
    if (t == 80000u + 320u)
        for (k = 0; k < NTRK; k++)
            if (sl_lent || sl[k].rec || sl[k].loop)
                slicer_ok = 0;
}
static int test_slicer(void)
{
    uint32_t k;
    song_setup();
    for (k = 0; k < NTRK; k++) {
        trk[k].p[P_SLCR] = SL_STUT;
        trk[k].p[P_SLPAT] = 1;
        trk[k].p[P_SLRATE] = 1;
        trk[k].p[P_SLDEPTH] = 127;
    }
    slicer_ok = 1;
    song_render(120000, stut_at);
    return check("SLICER STUT: live (no recording, no repeat) while REPEAT has the buffer, dropped after", slicer_ok);
}

/* -------------------------------------------------------------- 5. keys --- */
static int test_keys(void)
{
    int bad = 0;
    uint32_t mo0, fx = 1u << 3, k = 9;              /* (a button bit for FX; key 9: a white key, D4) */
    song_setup();
    usb.config = 1;
    mo_r = mo_w = 0;
    kb_mask = perf_mask = fx;
    fm1_in.buttons = fx;
    fm1_in.notes = 1u << k;
    keyboard_block();
    mo0 = mo_w;
    events_block(CTL);
    bad += check("a key pressed while FX is held: no voice, no MIDI, the layer's, its effect held",
                 !busy_now() && mo0 == 0u && (kb_layer >> k) & 1u && (perf_held & PF_BIT(perf_key(k))));
    fm1_in.buttons = 0;                             /* FX let go first: the effect stays with the key */
    keyboard_block();
    bad += check("  FX let go first: the key still holds its effect", (perf_held & PF_BIT(perf_key(k))) != 0u);
    fm1_in.notes = 0;
    keyboard_block();
    bad += check("  let go: no note-off, the effect off", mo_w == 0u && !kb_layer && !perf_held);
    fm1_in.notes = 1u << 7;                         /* a key, then FX: a note, and its note-off later */
    keyboard_block();
    fm1_in.buttons = fx;
    keyboard_block();
    fm1_in.notes = 0;
    keyboard_block();
    bad += check("a key held before FX stays a note (note-on and note-off sent)", mo_w == 2u && !kb_layer);
    kb_mask = perf_mask = 0;                                /* no layer now (a menu): keys are notes */
    fm1_in.notes = 1u << 7;
    keyboard_block();
    bad += check("no layer (kb_mask 0): FX held, keys are notes", mo_w == 3u && !kb_layer);
    fm1_in.notes = 0;
    fm1_in.buttons = 0;
    keyboard_block();
    {   /* the white keys past the first 12 (D5 on), and black keys past the mutes: the layer's, nothing */
        uint32_t q, ok = 1, assigned = 0;
        for (q = 0; q < 27u; q++)
            if (perf_key(q) < PF_N)
                assigned |= 1u << q;
        kb_mask = perf_mask = fx;
        mo0 = mo_w;
        for (q = 0; q < 27u; q++) {
            if ((assigned >> q) & 1u)
                continue;
            fm1_in.buttons = fx;
            fm1_in.notes = 1u << q;
            keyboard_block();
            ok &= (kb_layer >> q) & 1u && kb_note[q] == KB_SILENT && !perf_held && mo_w == mo0;
            fm1_in.notes = 0;
            keyboard_block();
            ok &= !kb_layer && !perf_held && mo_w == mo0;
        }
        fm1_in.buttons = 0;
        bad += check("the keys without an effect (4 white from D5, 7 black from D#4): silent, nothing held",
                     ok && assigned == 0xD5BFFu);   /* F3 .. C5 but D#4 F#4 G#4 A#4 */
        kb_mask = perf_mask = 0;
    }
    usb.config = 0;
    return bad;
}

/* -------------------------------------------------------------- 6. idle --- */
static int test_idle(void)
{
    static int32_t a_l[NT], a_r[NT];
    uint32_t t, diff = 0;
    int bad = 0, fd[2];
    pid_t pid;
    song_setup();
    if (pipe(fd) || (pid = fork()) < 0)
        return check("idle: fork", 0);
    if (!pid) {                                     /* the same state, FX held the whole time, no key */
        kb_mask = perf_mask = 1u << 3;
        fm1_in.buttons = 1u << 3;
        song_render(3u * FS, 0);
        close(fd[0]);
        if (write(fd[1], song_l, 3u * FS * 4u) < 0 || write(fd[1], song_r, 3u * FS * 4u) < 0)
            _exit(1);
        _exit(0);
    }
    close(fd[1]);
    song_render(3u * FS, 0);
    {
        FILE *f = fdopen(fd[0], "rb");
        size_t got = fread(a_l, 4, 3u * FS, f) + fread(a_r, 4, 3u * FS, f);
        fclose(f);
        waitpid(pid, 0, 0);
        diff = got != 6u * FS;
    }
    for (t = 0; t < 3u * FS; t++)
        diff += a_l[t] != song_l[t] || a_r[t] != song_r[t];
    bad += check("idle (FX held, no key): the song bit for bit as without the layer", !diff);
    {   /* after an effect: once its ramps are over, the stage is skipped again */
        uint32_t f;
        int32_t o[2 * CTL];
        perf_press(PF_R16, 1);
        for (f = 0; f < FS; f += CTL)
            mix_block(o, CTL);
        perf_press(PF_R16, 0);
        for (f = 0; f < 512u; f += CTL)
            mix_block(o, CTL);
        bad += check("  after an effect is let go and its ramp is over: skipped again", !pf.busy && !perf_begin(CTL));
    }
    return bad;
}

/* ----------------------------------------------- 7. clicks, the rest --- */
static int test_misc(void)
{
    int bad = 0;
    char what[160];
    static const struct { const char *name; int e; uint32_t off; } C[] = {
        {"REPEAT 1/8", PF_R8, 30000}, {"REPEAT 1/16", PF_R16, 30000}, {"REPEAT 1/32", PF_R32, 30000},
        {"REVERSE", PF_REV, 50000}, {"TAPE STOP", PF_TAPE, 30016}, {"FREEZE", PF_FRZ, 30016}, {"LPF", PF_LPF, 60000},
        {"HPF", PF_HPF, 60000}};
    uint32_t c, t;
    int32_t own;
    test_signal(110, 16000, 110, 16000);
    for (own = 0, t = 1; t < NT; t++)
        own = abs(in_l[t] - in_l[t - 1]) > own ? abs(in_l[t] - in_l[t - 1]) : own;
    for (c = 0; c < sizeof C / sizeof C[0]; c++) {
        ev_t ev[] = {{992, C[c].e + 1}, {C[c].off, -(C[c].e + 1)}};
        int32_t mx = 0;
        run(133, 1, ev, 2, C[c].off + 9000);
        for (t = 1; t < C[c].off + 9000; t++) {
            int32_t d = abs(out_l[t] - out_l[t - 1]);
            mx = d > mx ? d : mx;
        }
        snprintf(what, sizeof what, "no clicks: %s on a 110 Hz sine, in and out: largest step %d (sine %d)", C[c].name, mx, own);
        bad += check(what, mx <= 4 * own);
        bad += check("  back to the live signal, bit for bit", same(C[c].off + 3000, C[c].off + 9000));
    }
    {   /* the filters at their ends; full scale in, no overflow */
        ev_t ev[] = {{0, PF_LPF + 1}};
        test_signal(5000, 30000, 5000, 30000);
        run(120, 1, ev, 1, 100000);
        snprintf(what, sizeof what, "LPF held a bar: a 5 kHz sine down to %d of 30000", peak(out_l, 92000, 100000));
        bad += check(what, peak(out_l, 92000, 100000) < 3000 && peak(out_r, 92000, 100000) < 3000);
        ev[0].e = PF_HPF + 1;
        test_signal(80, 30000, 80, 30000);
        run(120, 1, ev, 1, 100000);
        snprintf(what, sizeof what, "HPF held a bar: an 80 Hz sine down to %d of 30000", peak(out_l, 92000, 100000));
        bad += check(what, peak(out_l, 92000, 100000) < 3000);
        for (t = 0; t < NT; t++) {                 /* full scale square waves, past Q15 */
            in_l[t] = (t / 37u) & 1u ? 120000 : -120000;
            in_r[t] = (t / 53u) & 1u ? 120000 : -120000;
        }
        {
            ev_t e2[] = {{0, PF_LPF + 1}, {0, PF_HPF + 1}, {0, PF_R16 + 1}};
            run_crush = 100;
            run(120, 1, e2, 3, 100000);
            run_crush = 0;
            snprintf(what, sizeof what, "4x full scale through LPF + HPF + KNOB 2 CRUSH + REPEAT: peak %d (no wrap)",
                     peak(out_l, 0, 100000) > peak(out_r, 0, 100000) ? peak(out_l, 0, 100000) : peak(out_r, 0, 100000));
            bad += check(what, peak(out_l, 0, 100000) < 300000 && peak(out_r, 0, 100000) < 300000);
        }
    }
    {   /* KNOB 1 left: a low-pass at once; KNOB 4: the REPEAT's level */
        test_signal(5000, 30000, 5000, 30000);
        host_tracks_init();
        perf_reset();
        song.g[G_BPM] = 120;
        song.playing = 1;
        perf_start();
        perf_k[0] = -100;
        for (t = 0; t < 20000u; t += CTL) {
            memcpy(out_l + t, in_l + t, CTL * 4);
            memcpy(out_r + t, in_r + t, CTL * 4);
            if (perf_begin(CTL))
                perf_block(out_l + t, out_r + t, CTL);
        }
        perf_k[0] = 0;
        snprintf(what, sizeof what, "KNOB 1 FILTER all the way left: a 5 kHz sine down to %d", peak(out_l, 10000, 20000));
        bad += check(what, peak(out_l, 10000, 20000) < 3000);
    }
    {   /* THROW (KNOB 3): the dry mix into the reverb send; mutes: a track ramps out */
        static int32_t ml[CTL], mr[CTL], sr[CTL];
        uint32_t i;
        host_tracks_init();
        perf_reset();
        song.playing = 1;
        perf_k[2] = 100;
        for (t = 0; t < 1024u; t += CTL) {
            for (i = 0; i < CTL; i++)
                ml[i] = mr[i] = 8000, sr[i] = 0;
            if (perf_begin(CTL))
                perf_pre(ml, mr, sr, CTL);
        }
        bad += check("KNOB 3 THROW at 100: the dry mix into reverb, full, the dry mix untouched",
                     sr[CTL - 1] > 7900 && ml[CTL - 1] == 8000 && mr[CTL - 1] == 8000);
        perf_k[2] = 0;
    }
    {
        int32_t o[2 * CTL];
        uint32_t f;
        int32_t before = 0, during = 0;
        song_setup();
        trk[1].p[P_SLEN] = 1; trk[3].p[P_SLEN] = 1;
        trk[1].step[0].n = 0; trk[3].step[0].n = 0;          /* only track 1 plays, dry */
        trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
        transport_req = 1;
        for (f = 0; f < 2u * FS; f += CTL) {
            uint32_t i;
            if (f == 44096u)
                perf_press(PF_M1, 1);
            mix_block(o, CTL);
            for (i = 0; i < CTL; i++) {
                if (f > FS / 2u && f < 44096u) before = abs(o[2 * i]) > before ? abs(o[2 * i]) : before;
                if (f > FS + 512u) during = abs(o[2 * i]) > during ? abs(o[2 * i]) : during;
            }
        }
        perf_press(PF_M1, 0);
        transport_req = 2;
        mix_block(o, CTL);
        snprintf(what, sizeof what, "black key 1: track 1 muted while held (peak %d -> %d), P_MUTE untouched", before, during);
        bad += check(what, before > 1000 && during < 200 && !trk[0].p[P_MUTE]);
    }
    return bad;
}

/* -------------------------------------------------------- 9. harmonizer --- */
/* the strongest frequency of x[a .. b) near f0 (+-15 %): a coarse scan, then golden-section on the DFT power */
static double dft_pow(const int32_t *x, uint32_t a, uint32_t b, double f)
{
    double w = 2 * M_PI * f / FS, re = 0, im = 0, c = cos(w), sn = sin(w), cr = 1, ci = 0;
    uint32_t t;
    for (t = a; t < b; t++) {                      /* (a Hann window: leakage of the splices kept local) */
        double h = 0.5 - 0.5 * cos(2 * M_PI * (t - a) / (b - a)), v = x[t] * h, nr;
        re += v * cr;
        im -= v * ci;
        nr = cr * c - ci * sn;
        ci = cr * sn + ci * c;
        cr = nr;
    }
    return re * re + im * im;
}
static double peak_freq(const int32_t *x, uint32_t a, uint32_t b, double f0)
{
    double lo = f0 * 0.85, hi = f0 * 1.15, best = f0, bp = -1, f, g = 0.6180339887;
    int k;
    for (f = lo; f <= hi; f += 0.25) {
        double p = dft_pow(x, a, b, f);
        if (p > bp) {
            bp = p;
            best = f;
        }
    }
    lo = best - 0.25;
    hi = best + 0.25;
    for (k = 0; k < 40; k++) {
        double m1 = hi - g * (hi - lo), m2 = lo + g * (hi - lo);
        if (dft_pow(x, a, b, m1) > dft_pow(x, a, b, m2))
            hi = m2;
        else
            lo = m1;
    }
    return (lo + hi) / 2;
}
static int32_t res_l[NT], res_r[NT];
static int test_harm(void)
{
    int bad = 0;
    char what[200];
    uint32_t t, k;
    for (k = 0; k < 4u; k++) {   /* the pitch, each side its own: on the sweep's grid (k 0, 1), off it (2, 3) */
        int e = k & 1u ? PF_ODN : PF_OUP;
        double r = k & 1u ? 0.5 : 2.0, f0l = k < 2u ? 5.0 * FS / 1024 : 220, f0r = k < 2u ? 8.0 * FS / 1024 : 330;
        double fl, fr, cl, cr;
        ev_t ev[] = {{992, e + 1}};
        test_signal(f0l, 12000, f0r, 9000);
        run(120, 1, ev, 1, 3u * FS);
        for (t = 0; t < 3u * FS; t++) {                /* the shifted part: out less 0.56 of the input */
            res_l[t] = out_l[t] - ((in_l[t] >> 1) + ((in_l[t] >> 1) >> 3));
            res_r[t] = out_r[t] - ((in_r[t] >> 1) + ((in_r[t] >> 1) >> 3));
        }
        fl = peak_freq(res_l, FS / 2u, 3u * FS, f0l * r);
        fr = peak_freq(res_r, FS / 2u, 3u * FS, f0r * r);
        cl = 1200 * log2(fl / (f0l * r));
        cr = 1200 * log2(fr / (f0r * r));
        if (k < 2u) {
            snprintf(what, sizeof what, "%s: %.2f / %.2f Hz in, out at %.2f / %.2f Hz (%+.2f / %+.2f cents)",
                     k ? "OCT DN" : "OCT UP", f0l, f0r, fl, fr, cl, cr);
            bad += check(what, fabs(cl) <= 3 && fabs(cr) <= 3);
        } else {
            snprintf(what, sizeof what, "  220 / 330 Hz: the strongest splice line %+.1f / %+.1f Hz from %.0f / %.0f (within 21.5)",
                     fl - f0l * r, fr - f0r * r, f0l * r, f0r * r);
            bad += check(what, fabs(fl - f0l * r) <= FS / 2048.0 && fabs(fr - f0r * r) <= FS / 2048.0);
        }
    }
    {   /* a silent side stays silent */
        ev_t ev[] = {{992, PF_OUP + 1}, {60000, -(PF_OUP + 1)}};
        test_signal(440, 12000, 0, 0);
        run(120, 1, ev, 2, 70000);
        snprintf(what, sizeof what, "OCT UP: a silent right side stays silent (L %d, R %d)", peak(out_l, 20000, 60000),
                 peak(out_r, 0, 70000));
        bad += check(what, peak(out_r, 0, 70000) == 0 && peak(out_l, 20000, 60000) > 3000);
    }
    for (k = 0; k < 2u; k++) {   /* no clicks in or out; live again after the ramp */
        int e = k ? PF_ODN : PF_OUP;
        ev_t ev[] = {{992, e + 1}, {40000, -(e + 1)}};
        int32_t mx = 0, own = 0;
        test_signal(110, 16000, 110, 16000);
        for (t = 1; t < NT; t++)
            own = abs(in_l[t] - in_l[t - 1]) > own ? abs(in_l[t] - in_l[t - 1]) : own;
        run(133, 1, ev, 2, 49000);
        for (t = 1; t < 49000u; t++) {
            int32_t d = abs(out_l[t] - out_l[t - 1]);
            mx = d > mx ? d : mx;
        }
        snprintf(what, sizeof what, "no clicks: %s on a 110 Hz sine, in and out: largest step %d (sine %d)",
                 k ? "OCT DN" : "OCT UP", mx, own);
        bad += check(what, mx <= 4 * own);
        bad += check("  back to the live signal, bit for bit; the buffer given back",
                     same(40000 + 3000, 49000) && !sl_lent && pf.mode == BM_NONE);
    }
    {   /* the shimmer at its most: bounded, and it dies away */
        ev_t ev[] = {{0, PF_OUP + 1}};
        int32_t pk, tail;
        uint32_t e1 = NT / CTL * CTL, e0 = e1 - FS / 2u;   /* (whole blocks) the last half second */
        for (t = 0; t < NT; t++) {
            in_l[t] = t < 3u * FS ? ((t / 37u) & 1u ? 120000 : -120000) : 0;
            in_r[t] = t < 3u * FS ? ((t / 53u) & 1u ? 120000 : -120000) : 0;
        }
        run_k4 = 100;
        run(120, 1, ev, 1, e1);
        run_k4 = 0;
        pk = peak(out_l, 0, e1) > peak(out_r, 0, e1) ? peak(out_l, 0, e1) : peak(out_r, 0, e1);
        tail = peak(out_l, e0, e1) > peak(out_r, e0, e1) ? peak(out_l, e0, e1) : peak(out_r, e0, e1);
        snprintf(what, sizeof what, "OCT UP shimmer 0.82 on 4x full-scale squares: peak %d, 2.5 s after they stop %d", pk, tail);
        bad += check(what, pk < 300000 && tail <= 16 && pf.hfb == 100 * HB_FB);
    }
    {   /* KNOB 4 at 0: no feedback, the delay holds the input only (half level) */
        ev_t ev[] = {{0, PF_OUP + 1}};
        uint32_t f, diff = 0;
        test_signal(440, 12000, 330, 9000);
        run(120, 1, ev, 1, 20000);
        for (f = 1; f <= 2048u; f++) {
            uint32_t s = 20000u - f;
            const int16_t *q = perf_audio + 2u * ((pf.wr - f) & HB_MASK);
            diff += q[0] != (int16_t)(in_l[s] >> 1) || q[1] != (int16_t)(in_r[s] >> 1);
        }
        bad += check("OCT UP, KNOB 4 at 0: no shimmer (the delay holds the input alone)", !diff && !pf.hfb);
    }
    return bad;
}

/* ------------------------------------------------- 10. FLANGER, PHASER --- */
static double mean_of(const int32_t *x, uint32_t a, uint32_t b)
{
    double m = 0;
    uint32_t t;
    for (t = a; t < b; t++)
        m += x[t];
    return m / (b - a);
}
static double rms_of(const int32_t *x, uint32_t a, uint32_t b)
{
    double m = 0;
    uint32_t t;
    for (t = a; t < b; t++)
        m += (double)x[t] * x[t];
    return sqrt(m / (b - a));
}
/* the largest |out[t] - out[t + d]| over [a, b), both sides: the output repeats after d samples */
static int32_t period_err(uint32_t a, uint32_t b, uint32_t d)
{
    uint32_t t;
    int32_t e = 0;
    for (t = a; t < b; t++) {
        int32_t x = abs(out_l[t] - out_l[t + d]), y = abs(out_r[t] - out_r[t + d]);
        e = x > e ? x : e;
        e = y > e ? y : e;
    }
    return e;
}
static int test_mod(void)
{
    static const struct { const char *name; int e; uint32_t s16; double dc; } M[] = {{"FLANGER", PF_FLG, 16, 1.275},
                                                                                     {"PHASER", PF_PHS, 8, 1.2}};
    static int32_t ref_l[NT], ref_r[NT];
    int bad = 0;
    char what[200];
    uint32_t m, t;
    int32_t own;
    for (m = 0; m < 2u; m++) {
        int e = M[m].e;
        {   /* silence in: silence out; a silent side stays silent */
            ev_t ev[] = {{992, e + 1}, {60000, -(e + 1)}};
            test_signal(440, 0, 0, 0);
            run(120, 1, ev, 2, 70000);
            snprintf(what, sizeof what, "%s: silence in, silence out (exact)", M[m].name);
            bad += check(what,
                         !peak(out_l, 0, 70000) && !peak(out_r, 0, 70000) && busy_seen);
            test_signal(440, 12000, 0, 0);
            run(120, 1, ev, 2, 70000);
            snprintf(what, sizeof what, "  a silent right side stays silent (L %d, R %d)", peak(out_l, 20000, 60000),
                     peak(out_r, 0, 70000));
            bad += check(what, !peak(out_r, 0, 70000) && peak(out_l, 20000, 60000) > 6000 && !same(2000, 60000));
        }
        {   /* no DC: a zero-mean mix stays zero-mean; its level about the input's (noise: the comb / notches average out) */
            ev_t ev[] = {{0, e + 1}};
            double dm, lv;
            uint32_t r = 12345;
            for (t = 0; t < NT; t++) {
                r = r * 1103515245u + 12345u;
                in_l[t] = (int32_t)((r >> 16) & 0x3FFF) - 8192;
                r = r * 1103515245u + 12345u;
                in_r[t] = (int32_t)((r >> 16) & 0x3FFF) - 8192;
            }
            {   /* (less its own mean over the part measured: |mean| < 0.5) */
                int32_t ml = (int32_t)lrint(mean_of(in_l, FS, 4u * FS)), mr = (int32_t)lrint(mean_of(in_r, FS, 4u * FS));
                for (t = 0; t < 4u * FS; t++) {
                    in_l[t] -= ml;
                    in_r[t] -= mr;
                }
            }
            run(120, 1, ev, 1, 4u * FS);
            dm = fmax(fabs(mean_of(out_l, FS, 4u * FS)), fabs(mean_of(out_r, FS, 4u * FS)));
            lv = 20 * log10(rms_of(out_l, FS, 4u * FS) / rms_of(in_l, FS, 4u * FS));
            snprintf(what, sizeof what, "  no DC: zero-mean noise (%.0f rms) in, the mean out %.2f; its level %+.1f dB",
                     rms_of(in_l, FS, 4u * FS), dm, lv);
            bad += check(what, dm < 4.0 && lv > -6 && lv < 1.5);
            for (t = 0; t < NT; t++)
                in_l[t] = in_r[t] = 8000;
            run(120, 1, ev, 1, 2u * FS);
            {
                int32_t ml = (int32_t)lrint(mean_of(out_l, FS, 2u * FS)), mr = (int32_t)lrint(mean_of(out_r, FS, 2u * FS));
                snprintf(what, sizeof what, "  DC through, steady, at the peaks' gain (%.3f): 8000 in, L %d R %d out", M[m].dc,
                         ml, mr);
                bad += check(what, fabs(ml - 8000 * M[m].dc) <= 80 && fabs(mr - 8000 * M[m].dc) <= 80 &&
                                   peak(out_l, FS, 2u * FS) - abs(ml) <= 8);
            }
        }
        {   /* bounded: 4x full-scale squares through it with LPF, HPF, REPEAT and CRUSH */
            ev_t ev[] = {{0, PF_LPF + 1}, {0, PF_HPF + 1}, {0, PF_R16 + 1}, {0, e + 1}};
            int32_t pk;
            for (t = 0; t < NT; t++) {
                in_l[t] = (t / 37u) & 1u ? 120000 : -120000;
                in_r[t] = (t / 53u) & 1u ? 120000 : -120000;
            }
            run_crush = 100;
            run(120, 1, ev, 4, 100000);
            run_crush = 0;
            pk = peak(out_l, 0, 100000) > peak(out_r, 0, 100000) ? peak(out_l, 0, 100000) : peak(out_r, 0, 100000);
            snprintf(what, sizeof what, "  4x full scale with LPF + HPF + REPEAT + CRUSH: peak %d (no wrap)", pk);
            bad += check(what, pk < 300000);
        }
        {   /* the sweep in time: a 16-sample pattern (a period that fits the sweep at both tempi) comes out the same a sweep
             * later, at 120 and at 90 BPM; at 90 BPM not after 120's sweep, at 120 not after half of it */
            static const uint32_t BPM[2] = {120, 90};
            static const int16_t PAT[16] = {9000, -3000, 4000, 12000, -8000, 2000, -11000, 6000, 0, 7000, -5000, -12000,
                                            3000, 10000, -6000, -8000};
            uint32_t b;
            ev_t ev[] = {{0, e + 1}};
            for (t = 0; t < NT; t++)
                in_l[t] = in_r[t] = PAT[t % 16u];
            for (b = 0; b < 2u; b++) {
                uint32_t per = M[m].s16 * (FS * 60u / BPM[b] / 4u), wrong = M[m].s16 * (FS * 60u / 120u / 4u);
                int32_t ok_e, bad_e;
                run(BPM[b], 1, ev, 1, 8192u + 2u * per);   /* (a sweep from 8192 on, against the next: within NT) */
                ok_e = period_err(8192u, 8192u + per, per);
                bad_e = period_err(8192u, 8192u + per, b ? wrong : per / 2u);
                snprintf(what, sizeof what, "  %u BPM: the output repeats after %s (%u samples): error %d; after %s %d",
                         BPM[b], m ? "1/2 bar" : "a bar", per, ok_e, b ? "120 BPM's" : "half of it", bad_e);
                bad += check(what, ok_e <= 160 && bad_e > 1000);   /* (PHASER: its coefficients a block at a time) */
            }
        }
        {   /* no click in or out (a 110 Hz sine); live again after the ramp, bit for bit */
            ev_t ev[] = {{992, e + 1}, {40000, -(e + 1)}};
            int32_t mx = 0;
            test_signal(110, 16000, 110, 16000);
            for (own = 0, t = 1; t < NT; t++)
                own = abs(in_l[t] - in_l[t - 1]) > own ? abs(in_l[t] - in_l[t - 1]) : own;
            run(133, 1, ev, 2, 49000);
            for (t = 1; t < 49000u; t++) {
                int32_t d = abs(out_l[t] - out_l[t - 1]);
                d = abs(out_r[t] - out_r[t - 1]) > d ? abs(out_r[t] - out_r[t - 1]) : d;
                mx = d > mx ? d : mx;
            }
            snprintf(what, sizeof what, "  no clicks on a 110 Hz sine, in and out: largest step %d (sine %d)", mx, own);
            bad += check(what, mx <= 4 * own);
            bad += check("  let go: the live signal again after the ramp, bit for bit; the stage off",
                         same(40000 + 640, 49000) && !pf.busy && !pf.fw && !pf.pw && !pf.fn);
        }
        {   /* stacking: with REPEAT 1/16 and LPF held, let go of it: the rest as without it, bit for bit (after the ramp) */
            ev_t ev[] = {{0, PF_R16 + 1}, {0, PF_LPF + 1}, {992, e + 1}, {30000, -(e + 1)}};
            uint32_t diff = 0, during = 0;
            test_signal(330, 12000, 220, 12000);
            run(120, 1, ev, 2, 50000);                 /* REPEAT and LPF alone */
            memcpy(ref_l, out_l, 50000u * 4u);
            memcpy(ref_r, out_r, 50000u * 4u);
            run(120, 1, ev, 4, 50000);
            for (t = 0; t < 50000u; t++) {
                if (t >= 30000u + 640u)
                    diff += out_l[t] != ref_l[t] || out_r[t] != ref_r[t];
                if (t >= 12000u && t < 30000u)
                    during += out_l[t] != ref_l[t];
            }
            snprintf(what, sizeof what, "  with REPEAT 1/16 + LPF: it changes them (%u samples), let go they are as alone",
                     during);
            bad += check(what, during > 10000u && !diff && (perf_act & PF_BIT(PF_R16)));
        }
    }
    {   /* both at once with HPF: bounded and both running; the right side a quarter sweep behind (L != R) */
        ev_t ev[] = {{0, PF_FLG + 1}, {0, PF_PHS + 1}, {0, PF_HPF + 1}};
        uint32_t lr = 0;
        test_signal(330, 12000, 330, 12000);
        for (t = 0; t < NT; t++)
            in_r[t] = in_l[t];
        run(120, 1, ev, 3, 80000);
        for (t = 20000; t < 80000u; t++)
            lr += out_l[t] != out_r[t];
        snprintf(what, sizeof what, "FLANGER + PHASER + HPF: the same input both sides, L and R apart (%u samples), peak %d", lr,
                 peak(out_l, 0, 80000));
        bad += check(what, lr > 50000u && peak(out_l, 0, 80000) < 40000 && pf.fw == 32768 && pf.pw == 32768);
    }
    {   /* KNOB 4 DEPTH: their level (0 % = the live signal) */
        ev_t ev[] = {{0, PF_FLG + 1}, {0, PF_PHS + 1}};
        test_signal(330, 12000, 220, 12000);
        run_k4 = 100;
        run(120, 1, ev, 2, 20000);
        run_k4 = 0;
        {
            int32_t d = 0;
            for (t = 0; t < 20000u; t++)
                d = abs(out_l[t] - in_l[t]) > d ? abs(out_l[t] - in_l[t]) : d;
            snprintf(what, sizeof what, "KNOB 4 DEPTH 0 %%: FLANGER and PHASER held, the live signal (within %d of 12000)", d);
            bad += check(what, d <= 120);                /* (as the buffer effects: 0 % is 68 / 32768 of the effect) */
        }
    }
    return bad;
}

/* ------------------------------------------------- 11. the key map --- */
static int test_map(void)
{
    int bad = 0, ok = 1;
    uint8_t b[16];
    uint32_t p, e, q;
    char what[160];
    memset(b, 0, sizeof b);
    for (p = 0; p < PF_KEYS; p++)                       /* all 0 (every setting before 1.2): the default */
        ok &= perf_map_of(b, p) == PF_DEF[p];
    bad += check("map: settings of 0 (before 1.2) read as the default (1.1.5's ten, FLANGER, PHASER, 4 none)",
                 ok && PF_DEF[10] == PF_FLG && PF_DEF[11] == PF_PHS && PF_DEF[12] == PF_N && PF_DEF[0] == PF_R8);
    ok = 1;
    for (p = 0; p < PF_KEYS; p++)                       /* any effect (or none) on any key, the others untouched */
        for (e = 0; e <= PF_NFX; e++) {
            uint32_t ee = e < PF_NFX ? e : PF_N;
            memset(b, 0, sizeof b);
            for (q = 0; q < PF_KEYS; q++)
                perf_map_put(b, q, (q * 5u + 3u) % (PF_NFX + 1u) < PF_NFX ? (q * 5u + 3u) % (PF_NFX + 1u) : PF_N);
            perf_map_put(b, p, ee);
            ok &= perf_map_of(b, p) == ee && !b[10] && !b[11];
            for (q = 0; q < PF_KEYS; q++)
                if (q != p)
                    ok &= perf_map_of(b, q) == ((q * 5u + 3u) % (PF_NFX + 1u) < PF_NFX ? (q * 5u + 3u) % (PF_NFX + 1u) : PF_N);
        }
    memset(b, 0, sizeof b);
    for (p = 0; p < PF_KEYS; p++) {                     /* each key's default back: 0 again */
        perf_map_put(b, p, PF_FLG);
        perf_map_put(b, p, PF_DEF[p]);
    }
    for (p = 0; p < 10u; p++)
        ok &= !b[p];
    b[0] = 31;                                          /* an unknown code (a later firmware's effect): the default */
    ok &= perf_map_of(b, 0) == PF_DEF[0];
    bad += check("map: any effect or NONE on any of the 16 keys, the others kept, 10 bytes; the default stores 0", ok);
    {   /* the keys: a rotated map, each white key holds its effect; NONE holds nothing */
        uint32_t fx = 1u << 3, k, white = 0;
        song_setup();
        kb_mask = perf_mask = fx;
        ok = 1;
        for (p = 0; p < PF_KEYS; p++)
            perf_map[p] = (uint8_t)(p < PF_NFX ? PF_NFX - 1u - p : PF_N);
        for (k = 0; k < 27u; k++) {
            if (key_black(k))
                continue;
            p = key_place(k);
            fm1_in.buttons = fx;
            fm1_in.notes = 1u << k;
            keyboard_block();
            ok &= p < PF_NFX ? perf_held == PF_BIT(PF_NFX - 1u - p) : !perf_held;
            fm1_in.notes = 0;
            keyboard_block();
            ok &= !perf_held && !kb_layer;
            white++;
        }
        bad += check("keys: a remapped layer (white key p holds effect 11 - p, the last 4 none), all 16 white keys", ok && white == 16u);
        /* remapped while held: the key takes the new effect at once, the old one let go */
        fm1_in.notes = 1u << 0;                         /* F3 */
        keyboard_block();
        ok = perf_held == PF_BIT(perf_map[0]);
        perf_map[0] = PF_PHS;
        perf_remap = 1;
        keyboard_block();
        ok &= perf_held == PF_BIT(PF_PHS) && !perf_remap;
        perf_map[0] = PF_N;                             /* .. to NONE: nothing held, the key still the layer's */
        perf_remap = 1;
        keyboard_block();
        ok &= !perf_held && (kb_layer & 1u);
        fm1_in.notes = 0;
        keyboard_block();
        ok &= !perf_held && !kb_layer;
        bad += check("  a key remapped while held: its new effect at once (PHASER), then NONE: nothing; let go: clean", ok);
        /* two keys the same effect: it ends with the last let go */
        perf_map[0] = perf_map[1] = PF_LPF;
        fm1_in.notes = 1u << 0 | 1u << 2;               /* F3, G3 */
        keyboard_block();
        ok = perf_held == PF_BIT(PF_LPF);
        fm1_in.notes = 1u << 2;
        keyboard_block();
        ok &= perf_held == PF_BIT(PF_LPF);
        fm1_in.notes = 0;
        keyboard_block();
        ok &= !perf_held;
        bad += check("  two keys with LPF: it holds until the last of them is let go", ok);
        /* FX LATCH: a latched effect follows its key's new mapping */
        perf_latch_on = 1;
        perf_map[0] = PF_LPF;
        fm1_in.notes = 1u << 0;
        keyboard_block();
        ok = perf_latched == PF_BIT(PF_LPF);
        perf_map[0] = PF_FLG;
        perf_remap = 1;
        keyboard_block();
        ok &= perf_latched == PF_BIT(PF_FLG) && !perf_held;
        fm1_in.notes = 0;
        keyboard_block();
        ok &= perf_latched == PF_BIT(PF_FLG);
        fm1_in.notes = 1u << 0;
        keyboard_block();
        ok &= !perf_latched;
        fm1_in.notes = 0;
        keyboard_block();
        perf_latch_on = 0;
        bad += check("  FX LATCH: the key's latched LPF becomes FLANGER as it is remapped; pressed again: off", ok);
        for (p = 0; p < PF_KEYS; p++)
            perf_map[p] = PF_DEF[p];
        fm1_in.buttons = 0;
        kb_mask = perf_mask = 0;
    }
    snprintf(what, sizeof what, "map: the default map on the keys: F3 .. A4 as 1.1.5, B4 FLANGER, C5 PHASER");
    {
        uint32_t k;
        ok = 1;
        for (k = 0; k < 27u; k++)
            if (!key_black(k))
                ok &= perf_key(k) == (key_place(k) < PF_KEYS ? PF_DEF[key_place(k)] : PF_N);
        ok &= perf_key(18) == PF_FLG && perf_key(19) == PF_PHS && perf_key(16) == PF_ODN;
    }
    bad += check(what, ok);
    return bad;
}

/* -------------------------------------------------------------- 8. cost --- */
static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}
static double cost_run(int on)
{
    static const int ALL[] = {PF_R16, PF_LPF, PF_HPF, PF_M1 + 2};
    static const int HARM[] = {PF_OUP};
    uint32_t f, k;
    uint64_t i0;
    int32_t o[2 * CTL];
    song_setup();
    transport_req = 1;
    for (f = 0; f < FS; f += CTL)
        mix_block(o, CTL);
    if (on == 1) {
        for (k = 0; k < sizeof ALL / sizeof ALL[0]; k++)
            perf_press((uint32_t)ALL[k], 1);
        perf_k[1] = perf_k[2] = 100;                /* the CRUSH and THROW macros */
    } else if (on == 2 || on == 4) {                /* OCT UP, with the shimmer (2) or plain (4) */
        perf_press((uint32_t)HARM[0], 1);
        perf_k[3] = on == 2 ? 60 : 0;
    } else if (on == 3) {                           /* REPEAT 1/16 alone: the layer's own cost, for scale */
        perf_press(PF_R16, 1);
    }
    i0 = instr_now();
    for (f = 0; f < 2u * FS; f += CTL)
        mix_block(o, CTL);
    for (k = 0; k < PF_N; k++)
        perf_press(k, 0);
    perf_k[1] = perf_k[2] = perf_k[3] = 0;
    transport_req = 2;
    return i0 ? (double)(instr_now() - i0) / (2.0 * FS) : 0;
}
static int test_cost(void)
{
    double idle = cost_run(0), on = cost_run(1), harm = cost_run(2), rep = cost_run(3), plain = cost_run(4);
    char what[200];
    int bad;
    if (!idle) {
        printf("perform: cost: no instruction counter on this host\n");
        return 0;
    }
    snprintf(what, sizeof what, "cost: the song %.0f instructions / sample idle, %.0f with REPEAT+LPF+HPF+MUTE, K2 CRUSH, K3 THROW: +%.0f",
             idle, on, on - idle);
    bad = check(what, on - idle < 400);
    printf("perform: cost: over the idle song: REPEAT 1/16 alone +%.0f (the layer's own stage), OCT UP +%.0f, with the shimmer +%.0f\n",
           rep - idle, plain - idle, harm - idle);
    snprintf(what, sizeof what, "  OCT UP over REPEAT 1/16 (the harmonizer itself): +%.0f, with the shimmer +%.0f (device ~%.1f / %.1f %%, about 2 %%)",
             plain - rep, harm - rep, (plain - rep) * 0.017, (harm - rep) * 0.017);
    return bad + check(what, (harm - rep) * 0.017 <= 2.2);
}

/* ------------------------------------------------------------ demos --- */
static void demo_at(uint32_t t)
{
    static const struct { uint32_t t; int e; } D[] = {     /* press +(e + 1), let go -(e + 1) */
        {88192, PF_R8 + 1}, {110080, -(PF_R8 + 1)}, {110080, PF_R16 + 1}, {121088, PF_R32 + 1},
        {126592, -(PF_R32 + 1)}, {132096, -(PF_R16 + 1)}, {176384, PF_LPF + 1}, {264576, -(PF_LPF + 1)},
        {264576, PF_HPF + 1}, {308672, -(PF_HPF + 1)}, {308672, PF_REV + 1}, {352768, -(PF_REV + 1)},
        {352768, PF_TAPE + 1}, {385024, -(PF_TAPE + 1)}, {396800, PF_FRZ + 1}, {440896, -(PF_FRZ + 1)},
        {440896, PF_M1 + 1}, {462848, -(PF_M1 + 1)}};
    uint32_t i;
    if (t == 462848u)
        perf_k[2] = 100;                            /* KNOB 3 THROW for a moment */
    if (t == 474880u)
        perf_k[2] = 0;
    for (i = 0; i < sizeof D / sizeof D[0]; i++)
        if (D[i].t == t)
            perf_press((uint32_t)abs(D[i].e) - 1u, D[i].e > 0);
}
static void demos(const char *dir)
{
    char path[512];
    FILE *f;
    uint32_t t;
    song_setup();
    song_render(NT - 4096u, demo_at);
    snprintf(path, sizeof path, "%s/perform_tour.wav", dir);
    if (!(f = fopen(path, "wb")))
        return;
    wav_hdr(f, NT - 4096u);
    for (t = 0; t < NT - 4096u; t++)
        wav_put(f, song_l[t], song_r[t]);
    fclose(f);
    printf("perform: demo %s (REPEAT 1/8, a 1/16 -> 1/32 roll, LPF, HPF, REVERSE, TAPE STOP, FREEZE, MUTE 1, KNOB 3 THROW)\n", path);
}

/* the harmonizer on a melody: plain, OCT UP (a bar), OCT DN (a bar), OCT UP with the shimmer (a bar) */
static uint32_t hd_mode;
static void harm_at(uint32_t t)
{
    uint32_t bar = 4u * FS * 60u / 110u, e = hd_mode == 1u ? PF_ODN : PF_OUP;
    if (!hd_mode)
        return;
    if (t == ((bar + CTL - 1u) / CTL) * CTL) {
        perf_k[3] = hd_mode == 2u ? 70 : 0;
        perf_press(e, 1);
    }
    if (t == ((3u * bar + CTL - 1u) / CTL) * CTL)
        perf_press(e, 0);
}
static void harm_demos(const char *dir)
{
    static const uint8_t MEL[16] = {69, 0, 72, 76, 0, 74, 72, 0, 71, 0, 67, 69, 0, 0, 64, 0};
    static const char *const NAME[3] = {"harmonizer_up", "harmonizer_down", "harmonizer_shimmer"};
    char path[512];
    FILE *f;
    uint32_t t, i, m, frames = 4u * 4u * FS * 60u / 110u + FS;
    for (m = 0; m < 3u; m++) {
        memset(trk, 0, sizeof trk);
        host_tracks_init();
        perf_reset();
        song.g[G_BPM] = 110;
        host_preset(&trk[0], 0, 0);                       /* ANALOG SAW LEAD */
        for (i = 0; i < 16u; i++)
            put_step(&trk[0], i, MEL[i] ? 1u : 0u, &MEL[i], MEL[i] ? ST_NOTE : ST_REST, 0);
        hd_mode = m + 1u;
        song_render(frames, harm_at);
        hd_mode = 0;
        perf_k[3] = 0;
        snprintf(path, sizeof path, "%s/%s.wav", dir, NAME[m]);
        if (!(f = fopen(path, "wb")))
            return;
        wav_hdr(f, frames);
        for (t = 0; t < frames; t++)
            wav_put(f, song_l[t], song_r[t]);
        fclose(f);
        printf("perform: demo %s (a bar plain, two with %s, one plain)\n", path,
               m == 0u ? "OCT UP" : m == 1u ? "OCT DN" : "OCT UP and KNOB 4 SHIMMER 70");
    }
}

int main(int argc, char **argv)
{
    int bad = 0;
    bad += test_timing();
    bad += test_stereo();
    bad += test_too_long();
    bad += test_slicer();
    bad += test_keys();
    bad += test_idle();
    bad += test_misc();
    bad += test_harm();
    bad += test_mod();
    bad += test_map();
    bad += test_cost();
    if (argc > 1)
        demos(argv[1]);
    if (argc > 2)
        harm_demos(argv[2]);
    printf("%s\n", bad ? "PERFORM TEST FAILED" : "perform test passed");
    return bad != 0;
}
