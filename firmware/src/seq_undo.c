/* SPDX-License-Identifier: GPL-3.0-only */
/* Eight manual STEP edits for the current track/pattern. Frames of a held
 * entry or modifier gesture form one edit. Exact comparisons invalidate stale
 * history after recording, editor changes, pattern/track changes or a load. */
#define STEP_HISTORY 8u
static struct {
    struct { step_t step[NSTEP]; uint8_t cursor; } state[STEP_HISTORY + 1u];
    step_t live[NSTEP];
    uint8_t valid, track, pattern, len;
    uint8_t head, count, pos, pending, cursor_before;
} step_history;

static void step_history_clear(void) { step_history.valid = 0; }

static uint32_t step_history_index(void)
{
    return (step_history.head + step_history.pos) % (STEP_HISTORY + 1u);
}

static int step_history_context(void)
{
    return step_history.valid && step_history.track == song.sel && step_history.pattern == TSEL->pat &&
           step_history.len == step_pattern_len(TSEL) && !(song.playing && (song.rec & (1u << song.sel)));
}

/* Before input: a change from outside the manual editor starts a fresh history.
 * The audio ISR may switch patterns or record, so compare/copy atomically. */
static void step_history_sync(void)
{
    fm1_irq_off();
    if (!step_history_context() || memcmp(step_history.live, TSEL->step, sizeof step_history.live)) {
        step_history.head = step_history.pos = step_history.pending = 0;
        step_history.count = 1;
        step_history.track = song.sel;
        step_history.pattern = TSEL->pat;
        step_history.len = (uint8_t)step_pattern_len(TSEL);
        memcpy(step_history.state[0].step, TSEL->step, sizeof step_history.live);
        memcpy(step_history.live, TSEL->step, sizeof step_history.live);
        step_history.state[0].cursor = ui.cursor;
        step_history.valid = !(song.playing && (song.rec & (1u << song.sel)));
    }
    step_history.cursor_before = ui.cursor;
    fm1_irq_on();
}

/* Commit only actual changes: cursor navigation and blocked edits keep redo. */
static void step_history_finish(void)
{
    uint32_t at = step_history_index();
    if (!step_history_context()) {
        step_history_clear();
        return;
    }
    if (memcmp(step_history.state[at].step, step_history.live, sizeof step_history.live)) {
        step_history.count = (uint8_t)(step_history.pos + 1u);   /* discard redo */
        if (step_history.count == STEP_HISTORY + 1u) {
            step_history.head = (uint8_t)((step_history.head + 1u) % (STEP_HISTORY + 1u));
            step_history.pos--;
            step_history.count--;
        }
        step_history.pos++;
        step_history.count++;
        at = step_history_index();
        memcpy(step_history.state[at].step, step_history.live, sizeof step_history.live);
        step_history.state[at].cursor = ui.cursor;
    }
    step_history.pending = 0;
}

/* After input: keep an expected copy even during a gesture, so an external
 * change on the next frame cannot become part of an undoable panel edit. */
static void step_history_end(void)
{
    fm1_irq_off();
    if (!step_history_context()) {
        step_history_clear();
        fm1_irq_on();
        return;
    }
    if (memcmp(step_history.live, TSEL->step, sizeof step_history.live)) {
        if (ui.home || cur_page()->scope != SC_STEP) {  /* sound/library/tools edits are not manual STEP edits */
            step_history_clear();
            fm1_irq_on();
            return;
        }
        if (!step_history.pending)
            step_history.state[step_history_index()].cursor = step_history.cursor_before;
        step_history.pending = 1;
        memcpy(step_history.live, TSEL->step, sizeof step_history.live);
    }
    fm1_irq_on();
    if (!ui.entry_open && !(ui.step_env && ui.step_env_used) && !(ui.step_scl && ui.step_scl_used))
        step_history_finish();
}

static void step_history_apply(int redo)
{
    uint32_t at;
    fm1_irq_off();
    step_history_finish();                       /* a currently held gesture is undoable too */
    if (!step_history_context() || (redo ? step_history.pos + 1u >= step_history.count : !step_history.pos)) {
        fm1_irq_on();
        ui_message(redo ? "NOTHING TO REDO" : "NOTHING TO UNDO");
        return;
    }
    step_history.pos = (uint8_t)(step_history.pos + (redo ? 1 : -1));
    at = step_history_index();
    memcpy(TSEL->step, step_history.state[at].step, sizeof step_history.live);
    memcpy(step_history.live, TSEL->step, sizeof step_history.live);
    fm1_irq_on();
    cursor_set(step_history.state[at].cursor);
    ui.step_env_used |= ui.step_env;
    ui.step_scl_used |= ui.step_scl;
    ui.hot_t = 0;
    ui.force = 1;
    ui_message(redo ? "REDO" : "UNDO");
}
