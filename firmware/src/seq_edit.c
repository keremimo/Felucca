/* SPDX-License-Identifier: GPL-3.0-only */
/* Note lengths use ordinary NOTE + TIE steps: no project or protocol changes.
 * These helpers run in the UI; playback keeps using the existing ties. */
static int step_on(const step_t *st) { return st->time == ST_NOTE && st->n; }

static void step_clear(step_t *st)
{
    st->n = 0;
    st->time = ST_REST;
    st->flags = 0;
    st->vel = 0;
}

static uint32_t step_pattern_len(const track_t *t)
{
    return (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP);
}

/* Resolve any tied step to its onset, including across the loop. A rest or
 * an orphan tie has no onset (NSTEP). */
static uint32_t step_note_start(const track_t *t, uint32_t at)
{
    uint32_t len = step_pattern_len(t), i;
    if (at >= len)
        return NSTEP;
    for (i = 0; i < len; i++) {
        const step_t *st = &t->step[at];
        if (step_on(st))
            return at;
        if (st->time != ST_TIE)
            break;
        at = (at + len - 1u) % len;
    }
    return NSTEP;
}

static uint32_t step_note_length(const track_t *t, uint32_t start)
{
    uint32_t len = step_pattern_len(t), n = 1;
    if (start >= len || !step_on(&t->step[start]))
        return 0;
    while (n < len && t->step[(start + n) % len].time == ST_TIE)
        n++;
    return n;
}

/* Extend through empty steps, stopping before existing notes. Shrinking only
 * clears this note's old tail. Never join a separate orphan tie chain. */
static uint32_t step_note_resize(track_t *t, uint32_t start, int32_t wanted)
{
    uint32_t len = step_pattern_len(t), old = step_note_length(t, start), n, limit, i;
    if (!old)
        return 0;
    limit = old;
    while (limit < len) {
        const step_t *st = &t->step[(start + limit) % len];
        if (step_on(st) || st->time == ST_TIE || t->step[(start + limit + 1u) % len].time == ST_TIE)
            break;
        limit++;
    }
    n = (uint32_t)clamp(wanted, 1, (int32_t)limit);
    for (i = n; i < old; i++)
        step_clear(&t->step[(start + i) % len]);
    for (i = old; i < n; i++) {
        step_t *st = &t->step[(start + i) % len];
        step_clear(st);
        st->time = ST_TIE;
    }
    return n;
}
