/* SPDX-License-Identifier: GPL-3.0-only */
/* A bounded performance store shared by the project's 32 banks. Step notes
 * are its editable overview; the original onsets, velocities and independent
 * releases live here. QNT changes only the playback schedule. */
static recorded_note_t recording[RECORD_MAX];
static struct { uint32_t held, left; } recording_run[RECORD_MAX];
static uint8_t recording_flags[RECORD_MAX]; /* 1: live hold; 2: skip upcoming snapped onset; 4: heard this loop */
static uint32_t recording_fraction[NTRK];
static volatile uint8_t recording_full;
static uint8_t recording_tracks;

static uint32_t recording_owner(const track_t *t) { return trk_index(t) * NPAT + t->pattern; }
static uint32_t recording_prefix(const track_t *t, uint32_t period, uint32_t idx)
{
    uint32_t a = step_samples(t, period, 0), b = step_samples(t, period, 1);
    return (idx / 2u) * (a + b) + (idx & 1u) * a;
}
static uint32_t recording_loop(const track_t *t, uint32_t period)
{
    return recording_prefix(t, period, (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP));
}
static int recording_valid(const recorded_note_t *r)
{
    return !r->vel || (r->vel <= 127u && r->duration && r->note <= 127u && ((r->step & 128u) || (r->step & 63u) + ((r->step & 64u) != 0u) < NSTEP));
}
static uint32_t recording_view(const track_t *t, const recorded_note_t *r)
{
    (void)t;
    return r->step & 128u ? 0u : (r->step & 63u) + ((r->step & 64u) != 0u);
}
static int recording_present(const recorded_note_t *r)
{
    uint32_t owner = r->owner & 31u, k = owner / NPAT, bank = owner % NPAT;
    const step_t *steps = trk[k].pattern == bank ? trk[k].step : pattern_at(k, bank)->step;
    uint32_t view = recording_view(&trk[k], r);
    return view < NSTEP && steps[view].time == ST_NOTE && (steps[view].flags & SF_RECORDED);
}
static int recording_active(const track_t *t, uint32_t i)
{
    const recorded_note_t *r = &recording[i];
    if (!r->vel || (r->owner & 31u) != recording_owner(t) || (r->step & 63u) >= (uint32_t)t->p[P_SLEN]) return 0;
    uint32_t view = recording_view(t, r);
    return t->step[view].time == ST_NOTE && (t->step[view].flags & SF_RECORDED);
}
static int recording_owns(const track_t *t, uint32_t note)
{
    for (uint32_t i = 0; i < RECORD_MAX; i++)
        if (recording_run[i].left && (recording[i].owner & 31u) == recording_owner(t) && recording[i].note == note)
            return 1;
    return 0;
}
static void recording_off(track_t *t, uint32_t note)
{
    if (recording_owns(t, note) || t->arp_note == note ||
        (live_held[trk_index(t)][note >> 5] & (1u << (note & 31u)))) return;
    for (uint32_t k = 0; k < t->seq_n; k++) if (t->seq_notes[k] == note) return;
    trk_note_off(t, note);
}
static void recording_reset(void)
{
    memset(recording, 0, sizeof recording);
    memset(recording_run, 0, sizeof recording_run);
    memset(recording_flags, 0, sizeof recording_flags);
    memset(recording_fraction, 0, sizeof recording_fraction);
    recording_full = 0;
    recording_tracks = 0;
}
static recorded_note_t recording_snapshot(uint32_t i)
{
    recorded_note_t r = recording[i];
    if (recording_flags[i] & 1u) {
        uint32_t held = recording_run[i].held, exponent = 0;
        while (held > 65535u && exponent < 7u) { held >>= 1; exponent++; }
        r.duration = (uint16_t)clamp((int32_t)held, 1, 65535);
        r.owner = (r.owner & 31u) | (exponent << 5);
    }
    return r;
}
static void recording_finish(uint32_t i)
{
    recording[i] = recording_snapshot(i);
    recording_flags[i] &= (uint8_t)~1u;
}
static void recording_stop(track_t *t)
{
    uint32_t owner = recording_owner(t);
    for (uint32_t i = 0; i < RECORD_MAX; i++) if ((recording[i].owner & 31u) == owner) {
        if ((recording_flags[i] & 1u)) recording_finish(i);
        uint32_t left = recording_run[i].left;
        recording_run[i].left = 0;
        if (left) recording_off(t, recording[i].note);
        recording_flags[i] &= (uint8_t)~6u;
    }
}
/* Onsets are fractions within their original swung step. Durations are
 * nominal-step fractions with a compact exponent. Optional quantization finds the nearest swung grid boundary,
 * considering the end of the loop as another copy of zero. */
static uint32_t recording_on(const track_t *t, const recorded_note_t *r, uint32_t period)
{
    uint32_t loop = recording_loop(t, period);
    uint32_t span = step_samples(t, period, r->step & 63u);
    uint32_t at = (recording_prefix(t, period, r->step & 63u) + (uint32_t)(((uint64_t)r->on * span + RECORD_UNIT / 2u) / RECORD_UNIT)) % loop;
    uint32_t q = (uint32_t)t->p[P_RECQ];
    if (!q) return at;
    uint32_t grid = seq_div_samples(q - 1u), a = step_samples(t, grid, 0), b = step_samples(t, grid, 1);
    uint32_t base = at / (a + b) * (a + b), best = 0, distance = at;
    uint32_t candidates[] = {base, base + a, base + a + b, loop};
    for (uint32_t k = 0; k < NELEM(candidates); k++) {
        uint32_t v = candidates[k];
        if (v > loop) continue;
        uint32_t d = v > at ? v - at : at - v;
        if (d < distance || (d == distance && v > best)) { best = v; distance = d; }
    }
    return best == loop ? 0u : best;
}
static int recording_note(track_t *t, uint32_t note, uint32_t vel, uint32_t step)
{
    uint32_t owner = recording_owner(t), period = seq_div_samples((uint32_t)t->p[P_SDIV]);
    uint32_t at = t->seq_pos >= 0x7FFFFFFFu ? 0u : recording_prefix(t, period, t->seq_idx) + t->seq_pos;
    uint32_t actual = t->seq_pos >= 0x7FFFFFFFu ? 0u : t->seq_idx;
    uint32_t span = step_samples(t, period, actual), pos = t->seq_pos >= 0x7FFFFFFFu ? 0u : t->seq_pos;
    uint32_t on = (uint32_t)(((uint64_t)pos * RECORD_UNIT + span / 2u) / span), free = RECORD_MAX;
    if (on > 65535u) on = 65535u;
    for (uint32_t i = 0; i < RECORD_MAX; i++) {
        recorded_note_t *r = &recording[i];
        if ((r->owner & 31u) == owner && (recording_flags[i] & 1u) && r->note == note) recording_finish(i);
        /* Repeated passes replace the same pitch at the same captured time.
         * Distinct hits in one step remain separate events. */
        uint32_t same_time = r->on == on;
        if (r->vel && (r->owner & 31u) == owner && r->note == note && (r->step & 63u) == actual && same_time) free = i;
        else if (free == RECORD_MAX && (!r->vel || (!recording_present(r) && !recording_run[i].left && !(recording_flags[i] & 1u)))) free = i;
    }
    if (free == RECORD_MAX) { recording_full = 1; return 0; }
    recorded_note_t *r = &recording[free];
    *r = (recorded_note_t){(uint16_t)on, 1, (uint8_t)note, (uint8_t)vel, (uint8_t)owner, (uint8_t)(actual | (step != actual ? 64u : 0u) | (step == 0u && actual ? 128u : 0u))};
    memset(&recording_run[free], 0, sizeof recording_run[free]);
    recording_flags[free] = 5;
    uint32_t target = recording_on(t, r, period);
    if (t->p[P_RECQ] && (target > at || (!target && at > recording_loop(t, period) / 2u))) recording_flags[free] |= 2u;
    t->step[step].flags |= SF_RECORDED;
    recording_tracks |= (uint8_t)(1u << trk_index(t));
    return 1;
}

/* Copy recordings atomically with a bank. Check capacity before mutating
 * either the destination's steps, automation or performance. */
static int recording_copy(track_t *t, uint32_t source, uint32_t dest, int apply)
{
    uint32_t src = trk_index(t) * NPAT + source, dst = trk_index(t) * NPAT + dest, count = 0, free = 0;
    for (uint32_t i = 0; i < RECORD_MAX; i++) {
        if (recording[i].vel && (recording[i].owner & 31u) == src) count++;
        if (!recording[i].vel || (recording[i].owner & 31u) == dst) free++;
    }
    if (count > free) return 2;
    if (!apply) return 0;
    for (uint32_t i = 0; i < RECORD_MAX; i++) if ((recording[i].owner & 31u) == dst) {
        memset(&recording[i], 0, sizeof recording[i]);
        memset(&recording_run[i], 0, sizeof recording_run[i]); recording_flags[i] = 0;
    }
    for (uint32_t i = 0; i < RECORD_MAX; i++) if (recording[i].vel && (recording[i].owner & 31u) == src)
        for (uint32_t j = 0; j < RECORD_MAX; j++) if (!recording[j].vel) {
            recording[j] = recording_snapshot(i); recording[j].owner = (recording[j].owner & 224u) | dst;
            memset(&recording_run[j], 0, sizeof recording_run[j]); recording_flags[j] = 0;
            break;
        }
    return 0;
}
static void recording_release(track_t *t, uint32_t note)
{
    for (uint32_t i = 0; i < RECORD_MAX; i++)
        if ((recording[i].owner & 31u) == recording_owner(t) && recording[i].note == note && (recording_flags[i] & 1u))
            recording_finish(i);
}
static void recording_fire(track_t *t, uint32_t i, uint32_t period, uint32_t late)
{
    recorded_note_t *r = &recording[i];
    if (recording_flags[i] & 4u) return;
    recording_flags[i] |= 4u;
    if ((recording_flags[i] & 2u)) { recording_flags[i] &= (uint8_t)~2u; return; }
    if ((recording_flags[i] & 1u)) return;
    uint32_t chance = step_chance(&t->step[recording_view(t, r)]);
    if (chance < 100u && rng() % 100u >= chance) return;
    uint32_t gate = (uint32_t)(((uint64_t)r->duration << (r->owner >> 5)) * period / RECORD_UNIT);
    if (!gate) gate = 1;
    trk_note_on(t, r->note, r->vel);
    recording_run[i].left = gate > late ? gate - late : 0u;
    if (!recording_run[i].left) recording_off(t, r->note);
}
static void recording_zero(track_t *t)
{
    uint32_t period = seq_div_samples((uint32_t)t->p[P_SDIV]);
    uint32_t phase = recording_prefix(t, period, t->seq_idx) + t->seq_pos;
    for (uint32_t i = 0; i < RECORD_MAX; i++) if ((recording[i].owner & 31u) == recording_owner(t)) {
        recording_flags[i] &= (uint8_t)~4u;
        if (recording_active(t, i)) {
            uint32_t on = recording_on(t, &recording[i], period);
            if (on <= phase) recording_fire(t, i, period, phase - on);
        }
    }
}
static void recording_tick(track_t *t, uint32_t n)
{
    uint32_t period = seq_div_samples((uint32_t)t->p[P_SDIV]), tr = trk_index(t);
    uint32_t a = recording_prefix(t, period, t->seq_idx) + t->seq_pos, b = a + n;
    uint64_t ticks = (uint64_t)n * RECORD_UNIT + recording_fraction[tr];
    recording_fraction[tr] = (uint32_t)(ticks % period);
    uint32_t elapsed = (uint32_t)(ticks / period);
    for (uint32_t i = 0; i < RECORD_MAX; i++) if (recording[i].vel && (recording[i].owner & 31u) == recording_owner(t)) {
        if ((recording_flags[i] & 1u)) recording_run[i].held = (uint32_t)clamp((int32_t)(recording_run[i].held + elapsed), 0, 8388607);
        if (recording_run[i].left) {
            if (!recording_active(t, i) || recording_run[i].left <= n) {
                recording_run[i].left = 0;
                recording_off(t, recording[i].note);
            } else recording_run[i].left -= n;
        }
        if (recording_active(t, i)) {
            uint32_t on = recording_on(t, &recording[i], period);
            if (on > a && on <= b) recording_fire(t, i, period, b - on);
        }
    }
}
