/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Keyboard, scale, arpeggiator, sequencer and transport. Runs in the audio
 * ISR, once per CTL-sample block, and ends in trk_note_on / trk_note_off:
 * engines never see where a note came from.
 * Four tracks, one transport: every track's pattern loops on its own LEN / DIV /
 * SWING / GATE (polymeter); each track has NPAT patterns (pat_*), switched at its loop
 * end. The keys play the selected track; MIDI channels 1..3 play parts 1..3, the
 * DRUMS channel (GLO > DRUMS, default 10) the drum track, any other channel the
 * selected track. A note into an armed track (song.rec) while
 * the transport runs is recorded into its pattern, quantised to its (swung) steps, with its
 * held length as TIE steps (rec_note, rec_hold, rec_release). */
static const uint16_t SCALE_MASK[] = {
    0xFFF,                                   /* CHR */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),   /* MAJ */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),   /* MIN */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),   /* DOR */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),   /* MIX */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 7) | (1 << 9),                          /* PEN */
    (1 << 0) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 10),                         /* MPEN */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 11),   /* HARM */
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),   /* PHRY */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 7) | (1 << 9) | (1 << 11),   /* LYD */
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 8) | (1 << 10),   /* LOC */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),   /* MEL (ascending) */
    (1 << 0) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 10),              /* BLUES (minor) */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 8) | (1 << 10),              /* WHOLE */
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 4) | (1 << 6) | (1 << 7) | (1 << 9) | (1 << 10), /* DIMHW */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 8) | (1 << 9) | (1 << 11), /* DIMWH */
};

#define KB_SILENT 255u
static uint32_t kb_prev;
static uint8_t kb_note[27], kb_trk[27];  /* per key: the note it started and on which track */
static volatile uint32_t kb_nav_btn;      /* ui_input.c: EDIT's button bit on an EDIT page; while it is held keys navigate, silent */
/* MIDI releases use the original destination, even after root/scale/track changes.
 * Zero means no sounding note; high byte = track + 1, low byte = mapped note. */
static uint16_t live_refs[NTRK][128];     /* overlapping local/MIDI keys sharing a pitch */
static uint8_t last_note = 60;
/* HOME's note readout (ui_draw.c): one bit per pitch, as played (after the scale).
 * live_held: what a synth part holds now; live_last: what was held at the last
 * synth note-on, kept after the release (a quick tap between two frames shows too). */
static uint32_t live_held[4], live_last[4];

static void live_held_update(uint32_t note)      /* bit note of live_held from the parts' live_refs */
{
    uint32_t p, on = 0, bit = 1u << (note & 31u);
    for (p = 0; p < NPART; p++)
        on |= live_refs[p][note];
    live_held[note >> 5] = on ? live_held[note >> 5] | bit : live_held[note >> 5] & ~bit;
}
/* Audio ISR -> UI step-entry edges. Carry the mapped pitch and destination so
 * the UI never has to guess after a channel, scale or track change. */
#define STEP_MIDI_Q 64u
static uint32_t step_midi_q[STEP_MIDI_Q];
static volatile uint32_t step_midi_w, step_midi_r;
static volatile uint8_t step_midi_overflow;

static void step_midi_edge(uint32_t track, uint32_t pitch, uint32_t on)
{
    uint32_t w = step_midi_w;
    if (!song.seq_mode)
        return;
    if (w - step_midi_r >= STEP_MIDI_Q) {
        step_midi_overflow = 1;
        return;
    }
    step_midi_q[w % STEP_MIDI_Q] = (track << 8) | pitch | (on << 10); /* on: 0 off, 1 on, 2 all off */
    step_midi_w = w + 1u;
}
static volatile uint8_t transport_req;   /* 1 start, 2 stop (from the UI) */
static volatile uint8_t panic_req;       /* bit per track: release every sounding note (preset / engine change) */
/* The input ISR timestamps clock packets; this state is owned by the audio ISR.
 * The external sample timeline is corrected at each MIDI pulse and interpolated
 * between pulses, keeping 1/32 and triplet steps finer than the 24 PPQN grid. */
static struct {
    uint32_t pos, rendered, last_ms, start_ms, rem, interval_ms;
    uint32_t pulse_samples, interp_q8;
    uint32_t tempo_ms;
    uint8_t mode, have_pulse, tempo_valid, tempo_n;
} midi_clock;

/* the drum track on the keys: 27 useful GM notes, lowest key first */
static const uint8_t DRUM_KEYS[27] = {
    36, 35, 38, 40, 37, 39, 42, 44, 46,      /* kicks, snares, rim, clap, hi-hats */
    41, 43, 45, 47, 48, 50,                  /* toms, low to high */
    49, 57, 51, 53, 54, 56,                  /* crashes, ride, ride bell, tambourine, cowbell */
    62, 63, 64, 70, 75, 76,                  /* congas, maracas, claves, wood block */
};

static uint32_t trk_index(const track_t *t) { return (uint32_t)(t - trk); }

static uint32_t trk_midi_ch(uint32_t i)    /* MIDI channel 0..15 of track i (keys -> MIDI out) */
{
    if (i < NPART)
        return i;
    return song.g[G_DRCH] ? (uint32_t)song.g[G_DRCH] - 1u : 9u;
}

static uint32_t scale_mask(const track_t *t)
{
    return SCALE_MASK[clamp(t->p[P_SCALE], 0, sizeof SCALE_MASK / sizeof SCALE_MASK[0] - 1)];
}

static uint32_t scale_count(const track_t *t)
{
    uint32_t mask = scale_mask(t), count = 0;
    while (mask) {
        count += mask & 1u;
        mask >>= 1;
    }
    return count;
}

static int32_t mpc_degree(const track_t *t)
{
    return clamp(t->p[P_MPCDEG], 1, (int32_t)scale_count(t));
}

static int is_gm_sample(const track_t *t)          /* (the engine it switches to) */
{
    return !is_drum(t) && ENGINES[t->eng_req % NENGINES] == &ENG_SAMPLE && drum_set() >= 0 &&
           (uint32_t)t->p[P_E0] % SMP_NSETS == (uint32_t)drum_set();
}

static int is_slice(const track_t *t)
{
#if FELUCCA_SLICE
    return ENGINES[t->eng_req % NENGINES] == &ENG_SLICE;
#else
    (void)t;
    return 0;
#endif
}

/* Resolve a scale degree relative to C4 into the selected scale and root. */
static uint32_t scale_degree_map(const track_t *t, int32_t degree, int32_t offset, int strict)
{
    uint32_t mask = scale_mask(t), i;
    int32_t count = 0, oct, n;
    for (i = 0; i < 12u; i++)
        count += (mask >> i) & 1u;
    oct = degree / count;
    degree %= count;
    if (degree < 0) {
        degree += count;
        oct--;
    }
    for (i = 0; i < 12u; i++)
        if ((mask >> i) & 1u) {
            if (!degree)
                break;
            degree--;
        }
    n = 60 + t->p[P_ROOT] + 12 * oct + (int32_t)i;
    n += offset + t->p[P_TRANS];
    if (strict && (n < 0 || n > 127))
        return KB_SILENT;
    return (uint32_t)clamp(n, 0, 127);
}

/* Shared WHITE/ALL scale layouts. MPC keeps WHITE on the panel keyboard. */
static uint32_t scale_map(const track_t *t, int32_t n, int32_t offset)
{
    static const int8_t DEGREE[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};
    int32_t degree;
    if (t->p[P_QUANT] == Q_ALL)
        return scale_degree_map(t, n - 60, offset, 1);
    if (t->p[P_QUANT] == Q_WHITE || t->p[P_QUANT] == Q_MPC) {
        degree = DEGREE[n % 12];
        if (degree < 0)
            return KB_SILENT;
        degree += (n / 12 - 5) * 7;
        return scale_degree_map(t, degree, offset, 0);
    }
    return (uint32_t)clamp(n + offset + t->p[P_TRANS], 0, 127);
}

static uint32_t kb_map(const track_t *t, uint32_t k)
{
    int32_t n = 53 + (int32_t)k;
    if (is_drum(t))
        return DRUM_KEYS[k % 27u];
    if (is_gm_sample(t))                              /* GM KIT: lowest key = kick (C2), no scale */
        return (uint32_t)clamp(36 + 12 * song.octave + (int32_t)k, 0, 127);
#if FELUCCA_SLICE
    if (is_slice(t))                                  /* SLICE: lowest key = slice 0 (C4 + ROOT), no scale */
        return (uint32_t)clamp(SLC_BASE + t->p[P_ROOT] + 12 * song.octave + (int32_t)k, 0, 127);
#endif
    if (t->p[P_QUANT] == Q_SNAP) {                    /* SNAP: every key, rounded down to the scale (the old ON) */
        uint32_t mask = scale_mask(t), guard = 12;
        n += 12 * song.octave + t->p[P_TRANS];
        while (guard-- && !((mask >> (uint32_t)((n - t->p[P_ROOT] + 120) % 12)) & 1u))
            n--;
        return (uint32_t)clamp(n, 0, 127);
    }
    return scale_map(t, n, 12 * song.octave);
}

/* MPC Sample's default pad map wraps after F12: H01..H16 are MIDI 20..35.
 * Filter every destination, including drums, using the shared synth QNT mode. */
static uint32_t midi_map(const track_t *t, uint32_t note)
{
    int quant = is_drum(t) ? trk[0].p[P_QUANT] : t->p[P_QUANT];
    if (quant == Q_MPC && (note < 20u || note > 35u))
        return KB_SILENT;
    if (is_drum(t) || is_gm_sample(t) || is_slice(t))
        return note;
    if (t->p[P_QUANT] == Q_MPC)
        return scale_degree_map(t, (int32_t)note - 21 + mpc_degree(t) - 1, 12 * song.octave, 1);
    if (t->p[P_QUANT] == Q_WHITE || t->p[P_QUANT] == Q_ALL)
        return scale_map(t, (int32_t)note, 0);
    return note;
}

/* ------------------------------------------------------------- arp --- */
static void arp_add(track_t *t, uint32_t note)
{
    uint32_t i;
    if (t->p[P_AHOLD] && t->arp_phys == 0u)
        t->nheld = 0;                               /* new chord replaces the latched one */
    t->arp_phys++;                                  /* every key-down: arp_remove counts every key-up */
    for (i = 0; i < t->nheld; i++)
        if (t->held[i] == note)
            return;                                 /* repeated note-on: not a new note */
    if (t->nheld < 16u)
        t->held[t->nheld++] = (uint8_t)note;
    if (t->nheld == 1u) {
        t->arp_pos = 0xFFFFFFF;                     /* fire on this block */
        t->arp_idx = 0xFFFFFFFFu;
    }
}

static void arp_remove(track_t *t, uint32_t note)
{
    uint32_t i, k = 0;
    if (t->arp_phys)
        t->arp_phys--;
    if (t->p[P_AHOLD])
        return;
    for (i = 0; i < t->nheld; i++)
        if (t->held[i] != note)
            t->held[k++] = t->held[i];
    t->nheld = (uint8_t)k;
}

static void arp_tick(track_t *t, uint32_t n)
{
    uint32_t period = div_samples((uint32_t)t->p[P_ARATE]), cnt, list[64], len = 0, i, j, o;
    int32_t sw = t->p[P_ASWING] * (int32_t)period / 250;
    if (t->arp_note) {
        if (t->arp_off <= n) {
            trk_note_off(t, t->arp_note);
            t->arp_note = 0;
        } else {
            t->arp_off -= n;
        }
    }
    if (!t->p[P_AMODE] || !t->nheld) {
        if (!t->nheld && t->arp_note) {
            trk_note_off(t, t->arp_note);
            t->arp_note = 0;
        }
        return;
    }
    t->arp_pos += n;
    if (t->arp_pos < period + (uint32_t)((t->arp_idx & 1u) ? sw : -sw) && t->arp_pos != 0xFFFFFFF + n)
        return;
    t->arp_pos = 0;
    /* build the note list: held notes (sorted or as played) over OCT octaves */
    for (i = 0; i < t->nheld; i++)
        list[i] = t->held[i];
    cnt = t->nheld;
    if (!t->p[P_AORDER])
        for (i = 1; i < cnt; i++)
            for (j = i; j > 0 && list[j - 1] > list[j]; j--) {
                uint32_t x = list[j];
                list[j] = list[j - 1];
                list[j - 1] = x;
            }
    for (o = 0; o < (uint32_t)t->p[P_AOCT]; o++)
        for (i = 0; i < cnt && len < 64u; i++)
            list[len++] = clamp((int32_t)list[i] + 12 * (int32_t)o, 0, 127);
    t->arp_idx++;
    switch (t->p[P_AMODE]) {
    case 2:
        j = len - 1u - t->arp_idx % len;
        break;
    case 3: {
        uint32_t cyc = len > 1u ? 2u * len - 2u : 1u, k = t->arp_idx % cyc;
        j = k < len ? k : cyc - k;
        break;
    }
    case 4:
        j = rng() % len;
        break;
    default:
        j = t->arp_idx % len;
        break;
    }
    if (t->arp_note)
        trk_note_off(t, t->arp_note);
    t->arp_note = 0;
    if ((uint32_t)(rng() & 127u) <= (uint32_t)t->p[P_APROB]) {
        t->arp_note = (uint8_t)list[j];
        t->arp_off = period * (uint32_t)t->p[P_AGATE] / 128u;
        trk_note_on(t, t->arp_note, 100);
    }
}

/* -------------------------------------------------------- note input --- */
/* the length of step idx in samples: SWING (the track's + the global) makes the even steps longer
 * and the odd ones shorter, so every odd step starts late */
static uint32_t step_samples(const track_t *t, uint32_t period, uint32_t idx)
{
    int32_t sw = (t->p[P_SSWING] + song.g[G_SWING]) * (int32_t)period / 250;
    return period + (uint32_t)((idx & 1u) ? -sw : sw);
}

/* live recording: the note goes into the step currently playing. Overdub: a step that
 * holds notes gets this one added (a chord of up to 4; when full, the last note is
 * replaced); MONO / LEGATO / UNISON parts keep one note per step, as step entry does.
 * Held on (synth parts): each further step the sequencer enters while the note is
 * held becomes a TIE (rec_hold), up to the pattern length; a release before the middle
 * of the last one puts that step back (rec_release), so a short note stays one step.
 * A note recorded into another step ends the hold before (the step model ties the
 * notes of one step only). */
static void rec_note(track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u;
    uint32_t idx = t->seq_pos == 0x7FFFFFFFu ? 0u : t->seq_idx % len, k;
    step_t *s;
    s = &t->step[idx];
    if (s->time != ST_NOTE || !s->n || (!is_drum(t) && t->p[P_VOICE] != V_POLY)) {
        s->n = 0;                                   /* a fresh step */
        s->flags = 0;
        s->vel = 0;
    }
    for (k = 0; k < s->n && s->note[k] != note; k++)
        ;
    if (k == s->n) {
        if (s->n < 4u)
            s->n++;
        s->note[s->n - 1u] = (uint8_t)note;
    }
    s->time = ST_NOTE;
    if (vel > 110)
        s->flags |= SF_ACCENT;
    if (vel > s->vel)
        s->vel = (uint8_t)vel;
    t->seq_active = 1;
    if (t->seq_pos == 0x7FFFFFFFu) {          /* Start and note in one block: avoid a second trigger */
        if (t->rskip_idx != idx)
            t->rskip_n = 0;
        t->rskip_idx = (uint8_t)idx;
        if (t->rskip_n < 4u)
            t->rskip[t->rskip_n++] = (uint8_t)note;
    }
    if (is_drum(t))
        return;                                     /* hits: no length */
    if (!t->rh_n || t->rh_start != idx) {           /* a new hold (one in another step ends) */
        t->rh_n = 0;
        t->rh_start = (uint8_t)idx;
        t->rh_ties = 0;
    }
    for (k = 0; k < t->rh_n && t->rh_note[k] != note; k++)
        ;
    if (k == t->rh_n && t->rh_n < 4u)
        t->rh_note[t->rh_n++] = (uint8_t)note;      /* a chord: held until its last key is up */
}

/* the sequencer enters step idx (before playing it): a recorded note still held ties into it */
static void rec_hold(track_t *t, uint32_t idx, uint32_t len)
{
    step_t *s;
    uint32_t k;
    if (!t->rh_n)
        return;
    if (!((song.rec >> trk_index(t)) & 1u) || t->rh_ties + 1u >= len) {
        t->rh_n = 0;                                /* disarmed, or the whole pattern is this note */
        return;
    }
    if (idx == t->rh_start)
        return;                                     /* never replace the note's onset */
    s = &t->step[idx];
    t->rh_bak = *s;
    t->rh_last = (uint8_t)idx;
    t->rh_ties++;
    for (k = 0; k < 4u; k++)
        s->note[k] = 0;
    s->n = 0;
    s->time = ST_TIE;
    s->flags = 0;
    s->vel = 0;
}

/* a key of a recorded note is up: the hold ends with the last one */
static void rec_release(track_t *t, uint32_t note)
{
    uint32_t i, k = 0;
    for (i = 0; i < t->rh_n; i++)
        if (t->rh_note[i] != note)
            t->rh_note[k++] = t->rh_note[i];
    if (k == t->rh_n || (t->rh_n = (uint8_t)k))
        return;                                     /* not one of them, or others still held */
    if (t->rh_ties && t->seq_idx == t->rh_last &&
        t->seq_pos < step_samples(t, div_samples((uint32_t)t->p[P_SDIV]), t->seq_idx) / 2u)
        t->step[t->rh_last] = t->rh_bak;            /* released early in it: not held into this step */
}

static void input_on(track_t *t, uint32_t note, uint32_t vel)
{
    live_refs[trk_index(t)][note]++;
    last_note = (uint8_t)note;
    if (!is_drum(t)) {                              /* the drum kit's notes are no chord */
        uint32_t i;
        live_held[note >> 5] |= 1u << (note & 31u);
        for (i = 0; i < 4u; i++)
            live_last[i] = live_held[i];
    }
    if (((song.rec >> trk_index(t)) & 1u) && song.playing)
        rec_note(t, note, vel);
    if (t->p[P_AMODE] && !is_drum(t))
        arp_add(t, note);
    else
        trk_note_on(t, note, vel);
}

static void input_off(track_t *t, uint32_t note)
{
    uint16_t *refs = &live_refs[trk_index(t)][note];
    if (!*refs)
        return;
    if (--*refs) {                                  /* another live key still owns this pitch */
        if (t->arp_phys)
            t->arp_phys--;
        return;
    }
    live_held_update(note);
    rec_release(t, note);
    arp_remove(t, note);                            /* both: the note may have started in the */
    trk_note_off(t, note);                          /* other mode (ARP switched while held) */
}

static void keyboard_block(void)
{
    uint32_t cur = fm1_in.notes, ch, k;
    ch = cur ^ kb_prev;                           /* keys also sound while entering steps */
    if (!ch)
        return;
    for (k = 0; k < 27u; k++) {
        uint32_t mc;
        if (!((ch >> k) & 1u))
            continue;
        if ((cur >> k) & 1u) {                    /* the selected track; the key-up goes to the same one */
            kb_trk[k] = song.sel;
            kb_note[k] = (uint8_t)(kb_nav_btn && (fm1_in.buttons & kb_nav_btn) ? KB_SILENT : kb_map(&trk[kb_trk[k]], k));
            if (kb_note[k] == KB_SILENT)
                continue;
            input_on(&trk[kb_trk[k]], kb_note[k], 100);
            mc = trk_midi_ch(kb_trk[k]);
            midi_out_event(0x09u | (0x90u | mc) << 8 | (uint32_t)kb_note[k] << 16 | 100u << 24);
        } else {
            if (kb_note[k] == KB_SILENT)
                continue;
            input_off(&trk[kb_trk[k] % NTRK], kb_note[k]);
            mc = trk_midi_ch(kb_trk[k] % NTRK);
            midi_out_event(0x08u | (0x80u | mc) << 8 | (uint32_t)kb_note[k] << 16);
        }
    }
    kb_prev = cur;
}

/* --------------------------------------------------------- patterns --- */
/* NPAT patterns per track. The one playing lives in t->step and t->p[P_SLEN..P_SGATE], where
 * everything reads it; pat_bank holds the others. A switch swaps them (seq_tick): at once while
 * the transport is stopped, else when the track's loop ends, so each track changes on its own
 * length. The UI only queues it (t->pat_q). */
static step_t *pat_steps(track_t *t, uint32_t k)        /* the steps of pattern k, wherever they are */
{
    return k % NPAT == t->pat ? t->step : pat_bank[trk_index(t)][k % NPAT].step;
}

static int pat_used(track_t *t, uint32_t k)             /* pattern k holds a note */
{
    const step_t *s = pat_steps(t, k);
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        if (s[i].n)
            return 1;
    return 0;
}

static void pat_switch(track_t *t, uint32_t k)          /* audio ISR (or with it off) */
{
    pattern_t *o = &pat_bank[trk_index(t)][t->pat], *q = &pat_bank[trk_index(t)][k % NPAT];
    uint32_t i;
    if (k % NPAT == t->pat)
        return;
    memcpy(o->step, t->step, sizeof t->step);
    for (i = 0; i < 4u; i++)
        o->set[i] = t->p[P_SLEN + i];
    memcpy(t->step, q->step, sizeof t->step);
    if (q->set[0])                                      /* never played: it keeps the track's LEN etc. */
        for (i = 0; i < 4u; i++)
            t->p[P_SLEN + i] = q->set[i];
    t->pat = (uint8_t)(k % NPAT);
    t->rh_n = 0;                                        /* a recorded hold ends with its pattern */
}

static void pat_clear_bank(track_t *t)                  /* the other patterns empty, pattern 1 playing */
{
    pattern_t *b = pat_bank[trk_index(t)];
    uint32_t k, i;
    for (k = 0; k < NPAT; k++) {
        memset(&b[k], 0, sizeof b[k]);
        for (i = 0; i < NSTEP; i++)
            b[k].step[i].time = ST_REST;
    }
    t->pat = 0;
    t->pat_q = 0;
}

/* -------------------------------------------------------- sequencer --- */
static void seq_start(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++) {                   /* every track from its step 0, together */
        track_t *t = &trk[i];
        t->seq_idx = (uint16_t)(t->p[P_SLEN] - 1);
        t->seq_pos = 0x7FFFFFFF;                   /* step 0 fires on the first block */
        t->rskip_n = 0;
        t->rh_n = 0;
    }
    song.tick = 0;
    song.playing = 1;
    slicer_start();                                /* slicer.c: its step 0 with the sequencer's */
}

static void seq_release(track_t *t)
{
    uint32_t i;
    for (i = 0; i < t->seq_n; i++)
        trk_note_off(t, t->seq_notes[i]);
    t->seq_n = 0;
    t->seq_hold = 0;
    t->slide_glide = 0;                             /* live MONO / LEG keys must not glide after it */
}

static void seq_stop(void)
{
    uint32_t i;
    song.playing = 0;
    for (i = 0; i < NTRK; i++) {
        seq_release(&trk[i]);
        trk[i].rh_n = 0;                           /* a recorded note held over the stop: as far as it got */
    }
}

static __attribute__((noinline)) void midi_clock_transport(uint32_t status, uint32_t ms)
{
    if (status == 0xFAu) {                         /* Start: step zero */
        midi_clock.pos = midi_clock.rendered = midi_clock.rem = 0;
        midi_clock.have_pulse = midi_clock.tempo_valid = 0;
        midi_clock.interval_ms = 0;
        midi_clock.start_ms = ms;
        seq_start();
    } else if (status == 0xFBu) {                  /* Continue: preserve the step */
        midi_clock.pos = midi_clock.rendered;
        midi_clock.rem = 0;
        midi_clock.have_pulse = midi_clock.tempo_valid = 0;
        midi_clock.interval_ms = 0;
        midi_clock.start_ms = ms;
        song.playing = 1;
    } else if (status == 0xFCu) {
        seq_stop();
    }
}

static __attribute__((noinline)) void midi_clock_pulse(uint32_t ms)
{
    uint32_t q;
    if (!midi_clock.tempo_valid) {
        midi_clock.tempo_valid = 1;
        midi_clock.tempo_ms = ms;
        midi_clock.tempo_n = 0;
    } else if (++midi_clock.tempo_n == 6u) {
        uint32_t dt = ms - midi_clock.tempo_ms;
        midi_clock.tempo_ms = ms;
        midi_clock.tempo_n = 0;
        /* Six clocks are a quarter of a beat. Reject gaps and corrupt bursts. */
        if (dt >= 62u && dt <= 375u) {
            uint32_t old_beat = beat_samples(), new_beat = (uint32_t)FS * dt / 250u;
            if (song.playing && new_beat != old_beat) {
                uint32_t i, ratio = (new_beat << 12) / old_beat;
                /* Preserve each track's fractional step phase when the master
                 * changes tempo. The next pulse then lands on its new boundary. */
                for (i = 0; i < NTRK; i++) {
                    if (trk[i].seq_pos < (uint32_t)FS * 2u)
                        trk[i].seq_pos = (uint32_t)(((uint64_t)trk[i].seq_pos * ratio + 2048u) >> 12);
                    if (trk[i].seq_off)
                        trk[i].seq_off = (uint32_t)(((uint64_t)trk[i].seq_off * ratio + 2048u) >> 12);
                }
            }
            midi_beat_samples = new_beat;
            song.g[G_BPM] = (int16_t)clamp((int32_t)((15000u + dt / 2u) / dt), 40, 240);
        }
    }
    if (song.playing) {
        if (midi_clock.have_pulse) {
            uint32_t interval = ms - midi_clock.last_ms;
            if (interval >= 8u && interval <= 80u)
                midi_clock.interval_ms = interval;
            q = beat_samples() + midi_clock.rem;
            midi_clock.pos += q / 24u;
            midi_clock.rem = q % 24u;
        }
        midi_clock.have_pulse = 1;
    }
    midi_clock.pulse_samples = beat_samples() / 24u;
    if (!midi_clock.interval_ms)
        midi_clock.interval_ms = beat_samples() * 1000u / ((uint32_t)FS * 24u);
    midi_clock.interp_q8 = midi_clock.pulse_samples * 256u / midi_clock.interval_ms;
    midi_clock.last_ms = ms;
}

static uint32_t midi_clock_advance(uint32_t now)
{
    uint32_t target, elapsed, offset, n;
    if (!midi_clock.have_pulse)
        return 0;
    elapsed = now - midi_clock.last_ms;
    /* Interpolate to the next pulse, never across it before it arrives. */
    if (elapsed > midi_clock.interval_ms)
        elapsed = midi_clock.interval_ms;
    offset = elapsed * midi_clock.interp_q8 >> 8;
    if (offset >= midi_clock.pulse_samples)
        offset = midi_clock.pulse_samples - 1u;
    target = midi_clock.pos + offset;
    n = (int32_t)(target - midi_clock.rendered) > 0 ? target - midi_clock.rendered : 0u;
    if (n > (uint32_t)FS / 8u)
        n = (uint32_t)FS / 8u;
    midi_clock.rendered += n;
    return n;
}

/* play one step: TIE extends, REST releases, NOTE (re)triggers; a SLIDE on
 * the previous step makes this one legato with a glide (acid style). skip: bit k =
 * note k already sounds from a note received alongside Start.
 * The drum track: each note is a hit (drum_on through trk_note_on), nothing else. */
static void seq_step(track_t *t, const step_t *s, uint32_t period, uint32_t skip)
{
    uint32_t i, j, gate = period * (uint32_t)t->p[P_SGATE] / 128u;
    uint32_t vel = (s->flags & SF_ACCENT) ? 127u : (s->vel ? s->vel : 96u);
    uint32_t slide_in = t->seq_hold && t->seq_n;
    uint32_t len = t->p[P_SLEN] ? (uint32_t)t->p[P_SLEN] : 1u;
    uint32_t next_tie = (t->pat_q && t->seq_idx + 1u >= len ? pat_steps(t, t->pat_q - 1u)[0]   /* the queued one's */
                                                            : t->step[(t->seq_idx + 1u) % len]).time == ST_TIE;
    if (s->time == ST_TIE) {
        if (t->seq_n) {
            t->seq_off = gate + period / 2u;
            t->seq_hold = (s->flags & SF_SLIDE) != 0 || next_tie;   /* chains hold at any GATE / swing */
        }
        return;
    }
    if (is_drum(t)) {
        if (s->time == ST_NOTE)
            for (i = 0; i < s->n; i++)
                if (!((skip >> i) & 1u))
                    trk_note_on(t, s->note[i], vel);
        return;
    }
    if (s->time == ST_REST || !s->n) {
        seq_release(t);
        return;
    }
    t->slide_glide = (uint8_t)slide_in;
    if (!slide_in)
        seq_release(t);
    for (i = 0; i < s->n; i++)
        if (!((skip >> i) & 1u))
            trk_note_on(t, s->note[i], vel);
    if (slide_in)                                   /* release what is not held over */
        for (i = 0; i < t->seq_n; i++) {
            for (j = 0; j < s->n && s->note[j] != t->seq_notes[i]; j++)
                ;
            if (j == s->n)
                trk_note_off(t, t->seq_notes[i]);
        }
    t->seq_n = 0;
    for (i = 0; i < s->n; i++)
        if (!((skip >> i) & 1u))
            t->seq_notes[t->seq_n++] = s->note[i];
    t->seq_off = gate;
    t->seq_hold = (s->flags & SF_SLIDE) != 0 || next_tie;   /* next step a TIE: keep the notes to it */
}

static void seq_tick(track_t *t, uint32_t n)
{
    uint32_t period, len;
    if (t->pat_q && !song.playing) {                    /* stopped: a picked pattern comes at once */
        pat_switch(t, t->pat_q - 1u);
        t->pat_q = 0;
    }
    period = div_samples((uint32_t)t->p[P_SDIV]);
    len = (uint32_t)t->p[P_SLEN];
    if (t->seq_n && !t->seq_hold) {
        if (t->seq_off <= n)
            seq_release(t);
        else
            t->seq_off -= n;
    }
    if (!song.playing)
        return;
    t->seq_pos += n;
    for (;;) {
        uint32_t cur_len = step_samples(t, period, t->seq_idx);
        if (t->seq_pos < cur_len && t->seq_pos != 0x7FFFFFFFu + n)
            break;
        t->seq_pos = t->seq_pos >= 0x7FFFFFFFu ? 0 : t->seq_pos - cur_len;
        t->seq_idx = (uint16_t)((t->seq_idx + 1u) % (len ? len : 1u));
        if (!t->seq_idx && t->pat_q) {                  /* the loop ends: the queued pattern starts */
            pat_switch(t, t->pat_q - 1u);
            t->pat_q = 0;
            period = div_samples((uint32_t)t->p[P_SDIV]);
            len = (uint32_t)t->p[P_SLEN];
        }
        rec_hold(t, t->seq_idx, len ? len : 1u);
        {
            const step_t *s = &t->step[t->seq_idx];
            uint32_t skip = 0, i, k;
            if (t->rskip_n && t->rskip_idx == t->seq_idx) {
                for (i = 0; i < s->n; i++)
                    for (k = 0; k < t->rskip_n; k++)
                        if (s->note[i] == t->rskip[k])
                            skip |= 1u << i;
                t->rskip_n = 0;
            }
            seq_step(t, s, period, skip);
        }
    }
}

/* MIDI in: the track a channel plays (0..15) */
static track_t *midi_track(uint32_t ch)
{
    if (song.g[G_DRCH] && ch + 1u == (uint32_t)song.g[G_DRCH])
        return TDRUM;
    return ch < NPART ? &trk[ch] : TSEL;
}

static void live_forget(uint32_t track)
{
    uint32_t note;
    for (note = 0; note < 128u; note++) {
        live_refs[track][note] = 0;
        live_held_update(note);
    }
    for (note = 0; note < 27u; note++)
        if (kb_trk[note] == track)
            kb_note[note] = KB_SILENT;
}
#include "midi_control.c"

/* everything that happens between two rendered blocks */
static void events_block(uint32_t n)
{
    uint32_t i, pr, seq_n = n;
    if (midi_clock.mode != (uint8_t)song.g[G_CLOCK]) {
        midi_clock.mode = (uint8_t)song.g[G_CLOCK];
        midi_clock.pos = midi_clock.rendered = midi_clock.rem = 0;
        midi_clock.interval_ms = 0;
        midi_clock.have_pulse = midi_clock.tempo_valid = midi_clock.tempo_n = 0;
        midi_beat_samples = 0;
        seq_stop();
    }
    if (transport_req == 1u) {
        if (song.g[G_CLOCK]) {
            midi_clock.pos = midi_clock.rendered = midi_clock.rem = 0;
            midi_clock.have_pulse = 0;
            midi_clock.interval_ms = 0;
            midi_clock.start_ms = fm1_ms;
        }
        seq_start();
        transport_req = 0;
    } else if (transport_req == 2u) {
        seq_stop();
        transport_req = 0;
    }
    pr = panic_req;
    panic_req = 0;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        if ((pr >> i) & 1u) {
            live_forget(i);
            midi_forget_track(i);
            trk_all_off(t);
            t->bend_target = t->bend_q8 = t->wheel_target = t->wheel_q8 = 0;
            t->nheld = 0;
            t->arp_phys = 0;
            t->arp_note = 0;
        }
        if (i < NPART)
            engine_block(t);                          /* engine switch: fade, then switch (voice.c) */
        /* ARP turned off, or HOLD released with no key down: drop the latched chord */
        if ((t->armp && !t->p[P_AMODE]) || (t->aholdp && !t->p[P_AHOLD] && !t->arp_phys)) {
            t->nheld = 0;
            if (!t->p[P_AMODE])
                t->arp_phys = 0;
            if (t->arp_note) {
                trk_note_off(t, t->arp_note);
                t->arp_note = 0;
            }
        }
        t->armp = t->p[P_AMODE];
        t->aholdp = t->p[P_AHOLD];
    }
    keyboard_block();
    while (mi_r != mi_w) {                            /* USB-MIDI (and TRS) in */
        uint32_t pkt = midi_in_q[mi_r % MQ], st = (pkt >> 8) & 0xF0u, ch = (pkt >> 8) & 0x0Fu;
        uint32_t d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        uint32_t status = (pkt >> 8) & 0xFFu, src = midi_in_src[mi_r % MQ], ms = midi_in_ms[mi_r % MQ];
        mi_r++;
        if (status >= 0xF8u) {
            if (src == (uint32_t)song.g[G_CLOCK]) {
                if (status == 0xF8u)
                    midi_clock_pulse(ms);
                else
                    midi_clock_transport(status, ms);
            }
        } else {
            midi_event(st, ch, d1, d2);
        }
    }
    if (song.g[G_CLOCK] && song.playing) {
        uint32_t last = midi_clock.have_pulse ? midi_clock.last_ms : midi_clock.start_ms;
        if ((uint32_t)(fm1_ms - last) > 500u) {
            seq_stop();                              /* lost clock: release sequencer notes */
            midi_clock.tempo_valid = 0;
        } else {
            seq_n = midi_clock_advance(fm1_ms);
        }
    }
    for (i = 0; i < NTRK; i++)
        seq_tick(&trk[i], seq_n);
    for (i = 0; i < NPART; i++)
        arp_tick(&trk[i], n);
    if (song.playing)
        song.tick++;
}
