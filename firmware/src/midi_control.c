/* SPDX-License-Identifier: GPL-3.0-only */
/* Shared USB/TRS channel controls. Included by seq.c after its input helpers.
 * USB and TRS intentionally share channel state, matching the existing routing.
 * A synth part has one live bend/wheel state; channels assigned to the same part
 * share it (last controller wins). Drum hits ignore bend, wheel and sustain. */
typedef struct {
    int16_t bend;                           /* signed 14-bit value, zero = centre */
    uint8_t wheel, pedal, targets;
    uint8_t foot, breath, press, porta;     /* CC 4, CC 2, channel pressure, CC 65 (FM6's DX7 controllers) */
    uint8_t owned[NTRK];                    /* held/pedal notes per track, at most 128 per channel */
    uint8_t ready, semis, cents;
    uint8_t rpn_msb, rpn_lsb;
} midi_channel_t;
static midi_channel_t midi_ch[16];
/* High byte: track + 1. Low byte: mapped pitch. Bit 15: pedal-held note.
 * The source note indexes this array, so scale changes cannot misroute release. */
static uint16_t midi_notes[16][128];
static uint16_t midi_owners[NTRK];           /* avoids rescanning all 2048 entries for CC123 */
#define MIDI_PEDAL_NOTE 0x8000u
#define MIDI_NOTE_TRACK(v) (((v) >> 8) & 0x7Fu)
#define MIDI_NOTE_PITCH(v) ((v) & 0x7Fu)

static midi_channel_t *midi_channel(uint32_t ch)
{
    midi_channel_t *c = &midi_ch[ch];
    if (!c->ready) {
        c->semis = 2;
        c->rpn_msb = c->rpn_lsb = 127;
        c->ready = 1;
    }
    return c;
}

static uint32_t midi_targets(uint32_t ch)
{
    return midi_channel(ch)->targets | (1u << trk_index(midi_track(ch)));
}

static void midi_expression(track_t *t, const midi_channel_t *c)
{
    int32_t range = ((int32_t)c->semis * 100 + c->cents) * 256 / 100;
    if (is_drum(t))
        return;
    t->bend_target = (int32_t)c->bend * range / (c->bend < 0 ? 8192 : 8191);
    t->wheel_target = (int32_t)c->wheel * 256;
    t->bend_raw = c->bend;
    t->cc_foot = c->foot;
    t->cc_breath = c->breath;
    t->cc_press = c->press;
    t->cc_porta = c->porta;
}

static void midi_expression_channel(uint32_t ch)
{
    uint32_t i, mask = midi_targets(ch);
    for (i = 0; i < NPART; i++)
        if (mask & (1u << i))
            midi_expression(&trk[i], midi_channel(ch));
}

static void midi_release(uint32_t ch, uint32_t note)
{
    uint32_t held = midi_notes[ch][note], id = MIDI_NOTE_TRACK(held);
    midi_notes[ch][note] = 0;
    if (id) {
        midi_owners[id - 1u]--;
        if (!--midi_ch[ch].owned[id - 1u])
            midi_ch[ch].targets &= (uint8_t)~(1u << (id - 1u));
    }
    if (id) {
        step_midi_edge(id - 1u, MIDI_NOTE_PITCH(held), 0);
        input_off(&trk[id - 1u], MIDI_NOTE_PITCH(held));
    }
}

static void midi_note_event(uint32_t ch, uint32_t note, uint32_t vel)
{
    midi_channel_t *c = midi_channel(ch);
    uint32_t id = MIDI_NOTE_TRACK(midi_notes[ch][note]);
    if (vel) {
        track_t *t = midi_track(ch);
        uint32_t mapped;
        /* Repeated notes replace the previous press, including a pedal-held one. */
        if (id)
            midi_release(ch, note);
        mapped = midi_map(t, note);
        if (mapped == KB_SILENT)
            return;
        midi_expression(t, c);
        c->targets |= (uint8_t)(1u << trk_index(t));
        input_on(t, mapped, vel);
        step_midi_edge(trk_index(t), mapped, 1);
        midi_notes[ch][note] = (uint16_t)(((trk_index(t) + 1u) << 8) | mapped);
        midi_owners[trk_index(t)]++;
        c->owned[trk_index(t)]++;
    } else if (id) {
        if (c->pedal && id != TRK_DRUM + 1u)
            midi_notes[ch][note] |= MIDI_PEDAL_NOTE;
        else
            midi_release(ch, note);
    }
}

static void midi_pedal_up(uint32_t ch)
{
    uint32_t note;
    midi_channel(ch)->pedal = 0;
    for (note = 0; note < 128u; note++)
        if (midi_notes[ch][note] & MIDI_PEDAL_NOTE)
            midi_release(ch, note);
}

/* Preset/project panic and CC120 must discard ownership so a later pedal-up
 * or note-off cannot release notes subsequently started on another patch. */
static void __attribute__((noinline)) midi_forget_track(uint32_t track)
{
    uint32_t ch, note;
    step_midi_edge(track, 0, 2);                /* one UI release for the whole track */
    for (ch = 0; ch < 16u; ch++) {
        if (!(midi_ch[ch].targets & (1u << track)))
            continue;
        for (note = 0; note < 128u; note++)
            if (MIDI_NOTE_TRACK(midi_notes[ch][note]) == track + 1u)
                midi_notes[ch][note] = 0;
        midi_ch[ch].targets &= (uint8_t)~(1u << track);
        midi_ch[ch].owned[track] = 0;
    }
    midi_owners[track] = 0;
}

static void midi_silence_track(uint32_t track)
{
    track_t *t = &trk[track];
    uint32_t i;
    trk_all_off(t);
    t->nheld = t->arp_phys = t->arp_note = t->rh_n = 0;
    t->seq_n = t->seq_hold = t->slide_glide = 0;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active)
            voice_kill(&t->v[i]);             /* one-block fade, regardless of RELEASE */
    if (is_drum(t))
        drums_off();
    sl[track].rec = sl[track].loop = 0;       /* do not keep replaying captured sound */
    midi_forget_track(track);
    live_forget(track);
}

static int midi_track_held(uint32_t track)
{
    uint32_t k;
    if (midi_owners[track])
        return 1;
    for (k = 0; k < 27u; k++)
        if ((fm1_in.notes & (1u << k)) && kb_trk[k] == track)
            return 1;
    return 0;
}

static void midi_control(uint32_t ch, uint32_t cc, uint32_t value)
{
    midi_channel_t *c = midi_channel(ch);
    uint32_t i, mask;
    switch (cc) {
    case 1:
        c->wheel = (uint8_t)value;
        midi_expression_channel(ch);
        break;
    case 2:
        c->breath = (uint8_t)value;
        midi_expression_channel(ch);
        break;
    case 4:
        c->foot = (uint8_t)value;
        midi_expression_channel(ch);
        break;
    case 65:
        c->porta = value >= 64u;
        midi_expression_channel(ch);
        break;
    case 5:                                        /* portamento time: FM6 parts keep it (as Dexed) */
        mask = midi_targets(ch);
        for (i = 0; i < NPART; i++)
            if ((mask & (1u << i)) && ENGINES[trk[i].engine] == &ENG_FM6)
                fm6_ed[i][FN_PTIME] = (int16_t)value;
        break;
    case 120:                                      /* All Sound Off: ignores the pedal */
        mask = midi_targets(ch);
        for (i = 0; i < NTRK; i++)
            if (mask & (1u << i))
                midi_silence_track(i);
        break;
    case 123:                                      /* All Notes Off: normal releases, honours pedal */
        mask = midi_targets(ch);
        for (i = 0; i < 128u; i++)
            if (midi_notes[ch][i])
                midi_note_event(ch, i, 0);
        for (i = 0; i < NTRK; i++)
            if ((mask & (1u << i)) && !midi_track_held(i)) {
                trk_all_off(&trk[i]);
                trk[i].nheld = trk[i].arp_phys = trk[i].arp_note = trk[i].rh_n = 0;
            }
        break;
    case 121:                                      /* Reset All Controllers, keep bend sensitivity */
        c->bend = 0;
        c->wheel = 0;
        c->press = 0;
        c->porta = 0;
        c->rpn_msb = c->rpn_lsb = 127;
        midi_expression_channel(ch);
        midi_pedal_up(ch);
        break;
    case 64:
        if (value >= 64u)
            c->pedal = 1;
        else
            midi_pedal_up(ch);
        break;
    case 101: c->rpn_msb = (uint8_t)value; break;
    case 100: c->rpn_lsb = (uint8_t)value; break;
    case 99: case 98:                              /* NRPN selection cancels RPN data entry */
        c->rpn_msb = c->rpn_lsb = 127;
        break;
    case 6: case 38:
        if (!c->rpn_msb && !c->rpn_lsb) {           /* RPN 0: +/-0..24 semitones, 0..99 cents */
            if (cc == 6u)
                c->semis = (uint8_t)(value > 24u ? 24u : value);
            else
                c->cents = (uint8_t)(value > 99u ? 99u : value);
            midi_expression_channel(ch);
        }
        break;
    default: break;
    }
}

/* Keep the occasional controller/panic dispatch outside the hot rendering loop. */
static void __attribute__((noinline)) midi_event(uint32_t st, uint32_t ch, uint32_t d1, uint32_t d2)
{
    if (st == 0x90u || st == 0x80u)
        midi_note_event(ch, d1, st == 0x90u ? d2 : 0);
    else if (st == 0xE0u) {
        midi_channel(ch)->bend = (int16_t)((int32_t)(d1 | (d2 << 7)) - 8192);
        midi_expression_channel(ch);
    } else if (st == 0xB0u) {
        midi_control(ch, d1, d2);
    } else if (st == 0xD0u) {                      /* channel pressure (aftertouch) */
        midi_channel(ch)->press = (uint8_t)d1;
        midi_expression_channel(ch);
    }
}
