/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Real panel/MIDI paths against the UI harness: tied edits, gesture history and live-record timing. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static track_t *edit_setup(void)
{
    ui_power_on();
    set_engine_of(TSEL, 0u);
    TSEL->engine = TSEL->eng_req;
    TSEL->p[P_VOICE] = V_POLY;
    TSEL->p[P_QUANT] = Q_OFF;
    TSEL->p[P_CHRD] = CH_OFF;
    track_defaults_steps(TSEL);
    go_page(GR_ROLL);
    frame();
    return TSEL;
}

static void put_note(track_t *t, uint32_t at, uint32_t pitch, uint32_t length)
{
    t->step[at].time = ST_NOTE;
    t->step[at].n = 1;
    t->step[at].note[0] = (uint8_t)pitch;
    step_note_resize(t, at, (int32_t)length);
}

/* SAVE+OCT acts before the hold timeout, and never opens the SAVE page on release. */
static void history_key(uint32_t oct)
{
    fm1_in.buttons |= 1u << panel.btn[B_SAVE];
    host_pressed |= 1u << panel.btn[B_SAVE];
    frame();
    press(oct);
    fm1_in.buttons &= ~(1u << panel.btn[B_SAVE]);
    frame();
}

static void resize_selected(int32_t delta)
{
    fm1_in.buttons |= 1u << panel.btn[B_ENV]; host_pressed |= 1u << panel.btn[B_ENV]; frame();
    turn(EN_SELECT, delta);
    fm1_in.buttons &= ~(1u << panel.btn[B_ENV]); frame();
}

static int test_note_edits(void)
{
    int bad = 0, ok;
    track_t *t = edit_setup();
    put_note(t, 14, 60, 4);
    put_note(t, 4, 72, 1);
    t->step[14].n = 3; t->step[14].note[1] = 64; t->step[14].note[2] = 67;
    t->step[14].flags = SF_ACCENT | SF_SLIDE; t->step[14].vel = 105; t->step[14].probability = 50;
    frame();
    bad += check("length resolves ties across the loop", step_note_start(t, 0) == 14 && step_note_length(t, 14) == 4);
    cursor_set(0); resize_selected(10);
    bad += check("SELECT on a tie grows its onset, stops before the next note", step_note_length(t, 14) == 6 && t->step[4].note[0] == 72);
    resize_selected(-100);
    bad += check("length clamps to one and clears only its old ties", step_note_length(t, 14) == 1 && t->step[15].time == ST_REST && t->step[4].note[0] == 72);
    cursor_set(14); resize_selected(2);
    fm1_in.buttons |= 1u << panel.btn[B_SCL]; host_pressed |= 1u << panel.btn[B_SCL]; frame();
    turn(EN_SELECT, 2);
    turn(EN_SELECT, 10);                             /* a fast turn stops at the occupied edge */
    fm1_in.buttons &= ~(1u << panel.btn[B_SCL]); frame();
    bad += check("SCL+SELECT moves the whole chord across the loop, stops before another note, no SCL page",
                 cur_page()->graph == GR_ROLL && ui.cursor == 1 && step_note_length(t, 1) == 3 && t->step[4].note[0] == 72 &&
                 t->step[1].n == 3 && t->step[1].note[1] == 64 && t->step[1].vel == 105 &&
                 t->step[1].flags == (SF_ACCENT | SF_SLIDE) && t->step[1].probability == 50);
    hold(B_SAVE);
    bad += check("several SCL-held moves are one undo", step_note_length(t, 14) == 3 && t->step[14].n == 3 && ui.cursor == 14);
    history_key(B_OCTUP);
    bad += check("move redo preserves all note data", step_note_length(t, 1) == 3 && t->step[1].probability == 50);
    cursor_set(2); press(B_EDIT);
    bad += check("EDIT on a tail deletes the onset and every tie, preserves the neighbour",
                 !step_on(&t->step[1]) && t->step[2].time == ST_REST && t->step[3].time == ST_REST && t->step[4].note[0] == 72);
    hold(B_SAVE);
    bad += check("whole-note deletion is one undo with its tail cursor", step_note_length(t, 1) == 3 && ui.cursor == 2);
    cursor_set(4); turn(EN_K3, 1);
    bad += check("TIME remains NOTE / TIE / REST", t->step[4].time == ST_TIE);
    turn(EN_K3, 1);
    bad += check("TIME reaches REST", t->step[4].time == ST_REST);
    t = edit_setup();
    put_note(t, 0, 60, 1); t->step[2].time = ST_TIE;
    bad += check("resize cannot attach an orphan tie chain", step_note_resize(t, 0, 16) == 1);
    step_delete(t, 2);
    bad += check("orphan tie deletion leaves the adjacent note", t->step[2].time == ST_REST && step_on(&t->step[0]));
    t = edit_setup();
    put_note(t, 0, 60, 16);
    bad += check("full-loop note is bounded and moves without losing its tail", step_note_length(t, 0) == 16 && step_note_move(t, 0, -3) == 13 && step_note_length(t, 13) == 16);
    step_delete(t, 4); ok = 1;
    for (uint32_t i = 0; i < 16; i++) ok &= t->step[i].time == ST_REST;
    bad += check("delete from a full-loop tail clears all steps", ok);
    return bad;
}

static int test_entry_history(void)
{
    int bad = 0;
    track_t *t = edit_setup();
    key_down(7); frame(); turn(EN_SELECT, 3); key_up(7); frame();
    bad += check("held panel entry, SELECT length, release advances by its full length", step_note_length(t, 0) == 4 && ui.cursor == 4);
    hold(B_SAVE);
    bad += check("held entry and resize are one undo, cursor restored", !step_on(&t->step[0]) && t->step[1].time == ST_REST && ui.cursor == 0);
    history_key(B_OCTUP);
    bad += check("SAVE+OCT+ restores the whole held entry", step_note_length(t, 0) == 4 && ui.cursor == 4 && cur_page()->graph == GR_ROLL);
    history_key(B_OCTDN);
    bad += check("SAVE+OCT- undoes without changing octave", !step_on(&t->step[0]) && !song.octave);
    turn(EN_K2, 1);
    history_key(B_OCTUP);
    bad += check("a new edit drops redo", msg_is("NOTHING TO REDO") && step_on(&t->step[0]));

    t = edit_setup(); put_note(t, 0, 60, 1); frame();
    for (uint32_t i = 0; i < 10; i++) turn(EN_K2, 1);
    for (uint32_t i = 0; i < 8; i++) history_key(B_OCTDN);
    bad += check("history keeps the most recent eight edits", t->step[0].note[0] == 62);
    history_key(B_OCTDN);
    bad += check("history never undoes past its retained limit", t->step[0].note[0] == 62 && msg_is("NOTHING TO UNDO"));
    for (uint32_t i = 0; i < 8; i++) history_key(B_OCTUP);
    bad += check("all eight retained edits redo", t->step[0].note[0] == 70);
    history_key(B_OCTDN); turn(EN_K1, 1); history_key(B_OCTUP);
    bad += check("moving the cursor keeps redo", t->step[0].note[0] == 70);
    t->step[0].note[0] = 99; frame(); history_key(B_OCTDN);
    bad += check("editor changes invalidate history", t->step[0].note[0] == 99 && msg_is("NOTHING TO UNDO"));
    turn(EN_K2, 1); t->p[P_SLEN] = 8; frame(); history_key(B_OCTDN);
    bad += check("pattern-length changes invalidate history", msg_is("NOTHING TO UNDO"));
    go_page(GR_CHANCE); frame(); turn(EN_K2, -30); hold(B_SAVE);
    bad += check("CHANCE edits share manual step undo", step_chance(&t->step[ui.cursor]) == 100);
    return bad;
}

static int test_midi_entry(void)
{
    int bad = 0;
    track_t *t = edit_setup();
    midi_note_event(0, 60, 100); frame(); turn(EN_SELECT, 2);
    midi_note_event(0, 60, 0); frame();
    bad += check("MIDI enters STEP, SELECT sets length, source release advances", t->step[0].note[0] == 60 && step_note_length(t, 0) == 3 && ui.cursor == 3 && !step_midi_held);
    midi_note_event(0, 62, 100); midi_note_event(0, 62, 0);
    midi_note_event(0, 64, 100); midi_note_event(0, 64, 0); frame();
    bad += check("two MIDI taps in one frame enter two steps", t->step[3].note[0] == 62 && t->step[4].note[0] == 64 && ui.cursor == 5);
    hold(B_SAVE);
    bad += check("same-frame MIDI taps remain separate undo entries", step_on(&t->step[3]) && !step_on(&t->step[4]) && ui.cursor == 4);
    hold(B_SAVE);
    bad += check("previous same-frame MIDI entry undoes independently", !step_on(&t->step[3]) && ui.cursor == 3);

    t = edit_setup();
    midi_note_event(0, 60, 100); midi_note_event(4, 60, 100); frame();
    midi_note_event(0, 60, 0); frame();
    bad += check("overlapping channels with the same pitch keep entry held", ui.entry_open && ui.cursor == 0 && step_midi_held == 1);
    midi_note_event(4, 60, 0); frame();
    bad += check("last source release advances once", ui.cursor == 1 && !step_midi_held && t->step[0].n == 1);
    midi_channel(0)->pedal = 1;
    midi_note_event(0, 65, 100); frame(); midi_note_event(0, 65, 0); frame();
    bad += check("sustain holds audio while physical MIDI release ends step entry", ui.cursor == 2 && !step_midi_held && (midi_notes[0][65] & MIDI_PEDAL_NOTE));
    midi_pedal_up(0); frame();
    bad += check("pedal-up never advances an extra step", ui.cursor == 2);

    t = edit_setup(); t->p[P_QUANT] = Q_WHITE; t->p[P_ROOT] = 2;
    midi_note_event(0, 61, 100); midi_note_event(0, 61, 0); frame();
    bad += check("WHITE black MIDI keys are silent and enter nothing", !step_on(&t->step[0]) && ui.cursor == 0 && !step_midi_held);
    midi_note_event(0, 60, 100); frame(); midi_note_event(0, 60, 0); frame();
    bad += check("WHITE MIDI entry uses its mapped pitch", t->step[0].note[0] == 62);
    key_down(8); key_down(7); frame(); key_up(7); frame();
    bad += check("a silent black panel key cannot keep a white-key entry held", ui.cursor == 2 && !ui.entry_open);
    key_up(8); frame();
    t = edit_setup(); t->p[P_CHRD] = CH_MAJ;
    midi_note_event(0, 60, 100); frame();
    bad += check("MIDI chord enters up to four notes with one source owner", t->step[0].n == 3 && step_midi_held == 1);
    midi_note_event(0, 60, 0); frame();
    bad += check("chord source release ends one entry", ui.cursor == 1 && !step_midi_held);
    t = edit_setup(); t->p[P_CHRD] = CH_MAJ7;
    for (uint32_t i = 0; i < 8; i++) { midi_note_event(0, 60 + i, 100); midi_note_event(0, 60 + i, 0); }
    frame();
    int burst_ok = ui.cursor == 8 && !step_midi_overflow && !step_midi_held;
    for (uint32_t i = 0; i < 8; i++) burst_ok &= t->step[i].n == 4 && t->step[i].note[0] == 60 + i;
    bad += check("eight four-tone MIDI taps fit the queue and remain separate entries", burst_ok);

    t = edit_setup(); midi_note_event(0, 60, 100); frame();
    turn(EN_ALGO, 1); frame();
    midi_note_event(1, 67, 100); frame(); midi_note_event(0, 60, 0); frame();
    bad += check("old-track release does not release the new track's entry", song.sel == 1 && ui.entry_open && step_midi_held == 1);
    midi_note_event(1, 67, 0); frame();
    bad += check("new-track release advances normally", ui.cursor == 1 && !step_midi_held);
    t = edit_setup(); midi_note_event(0, 60, 100); frame();
    midi_forget_track(0); frame();
    bad += check("panic releases STEP ownership", !step_midi_held && !ui.entry_open && ui.cursor == 1);
    for (uint32_t i = 0; i < STEP_MIDI_Q + 2; i++) step_midi_edge(0, 60, 1, 0, 60);
    frame(); midi_note_event(0, 67, 100); frame(); midi_note_event(0, 67, 0); frame();
    bad += check("queue overflow recovers and accepts the next complete entry", !step_midi_overflow && !step_midi_held && t->step[1].note[0] == 67 && ui.cursor == 2);
    t = edit_setup(); hold(B_HOME); midi_note_event(0, 70, 100); frame(); midi_note_event(0, 70, 0); frame(); hold(B_HOME); frame();
    bad += check("MIDI in the menu never enters a stale STEP on return", !step_on(&t->step[0]) && ui.cursor == 0);
    return bad;
}

static int test_record_timing(void)
{
    int bad = 0;
    track_t *t = edit_setup();
    song.playing = 1; song.rec = 1; t->seq_idx = 5;
    t->seq_pos = step_samples(t, div_samples(t->p[P_SDIV]), 5) - 1;
    cursor_set(10); frame(); midi_note_event(0, 60, 100); frame();
    bad += check("late live MIDI records the playing step and STEP follows it", t->step[5].note[0] == 60 && !step_on(&t->step[6]) && !step_on(&t->step[10]) && ui.cursor == 5);
    history_key(B_OCTDN);
    bad += check("manual undo cannot restore stale steps during live recording", t->step[5].note[0] == 60 && msg_is("NOTHING TO UNDO"));
    t = edit_setup(); song.playing = 1; song.rec = 1; t->seq_pos = 0x7FFFFFFFu; t->seq_idx = 15;
    rec_note(t, 60, 100);
    bad += check("Start and note in one block record step zero with retrigger guard", t->step[0].note[0] == 60 && !step_on(&t->step[15]) && t->rskip_n == 1 && t->rskip_idx == 0);
    return bad;
}

static int test_step_modifiers(void)
{
    int bad = 0;
    track_t *t = edit_setup();
    int32_t bpm = song.g[G_BPM];
    key_down(7); frame();
    host_enc[panel.enc[EN_SELECT]] += 3 * panel.dir[EN_SELECT];
    key_up(7); frame();
    bad += check("final SELECT detent on panel-key release sets the entry length before advancing",
                 step_note_length(t, 0) == 4 && ui.cursor == 4 && song.g[G_BPM] == bpm);
    midi_note_event(0, 65, 100); frame();
    host_enc[panel.enc[EN_SELECT]] += 2 * panel.dir[EN_SELECT]; midi_note_event(0, 65, 0); frame();
    bad += check("final SELECT detent on MIDI release sets length before advancing", step_note_length(t, 4) == 3 && ui.cursor == 7);
    turn(EN_SELECT, 1);
    bad += check("SELECT without a held entry or modifier moves the cursor (the BPM is SEQ > TEMPO's)",
                 ui.cursor == 8 && song.g[G_BPM] == bpm);
    cursor_set(4); frame(); step_t before[NSTEP]; memcpy(before, t->step, sizeof before);
    turn(EN_PRESET, 10);
    bad += check("PRESETS has no STEP editing role", !memcmp(before, t->step, sizeof before) && ui.cursor == 4);

    fm1_in.buttons |= 1u << panel.btn[B_ENV]; host_pressed |= 1u << panel.btn[B_ENV]; frame();
    host_enc[panel.enc[EN_SELECT]] += panel.dir[EN_SELECT]; fm1_in.buttons &= ~(1u << panel.btn[B_ENV]); frame();
    bad += check("ENV+SELECT includes a release-frame detent and consumes the ENV page tap", cur_page()->graph == GR_ROLL && step_note_length(t, 4) == 4 && !ui.ly && !ui.step_move);
    fm1_in.buttons |= 1u << panel.btn[B_SCL]; host_pressed |= 1u << panel.btn[B_SCL]; frame();
    turn(EN_SELECT, 1);
    host_enc[panel.enc[EN_SELECT]] += panel.dir[EN_SELECT]; fm1_in.buttons &= ~(1u << panel.btn[B_SCL]); frame();
    bad += check("SCL+SELECT includes a release-frame detent and consumes the SCL page tap", cur_page()->graph == GR_ROLL && step_note_length(t, 6) == 4 && ui.cursor == 6 && !ui.ly);
    hold(B_SAVE);
    bad += check("one SCL-held movement gesture is one undo", step_note_length(t, 4) == 4 && ui.cursor == 4);
    history_key(B_OCTUP);
    cursor_set(7); frame(); press(B_FX);
    bad += check("FX deletes a selected note and all ties, advances from its onset", !step_on(&t->step[6]) && t->step[7].time == ST_REST && ui.cursor == 7 && cur_page()->graph == GR_ROLL);
    fm1_in.buttons |= 1u << panel.btn[B_FX]; host_pressed |= 1u << panel.btn[B_FX]; frame();
    press(B_OCTDN); fm1_in.buttons &= ~(1u << panel.btn[B_FX]); frame();
    bad += check("FX then OCT- undoes without deletion, navigation or octave shift", step_note_length(t, 6) == 4 && ui.cursor == 7 && !song.octave && cur_page()->graph == GR_ROLL);
    fm1_in.buttons |= 1u << panel.btn[B_OCTUP]; host_pressed |= 1u << panel.btn[B_OCTUP]; frame();
    press(B_FX); fm1_in.buttons &= ~(1u << panel.btn[B_OCTUP]); frame();
    bad += check("OCT+ then FX redoes, either modifier order works", !step_on(&t->step[6]) && ui.cursor == 7 && !song.octave && cur_page()->graph == GR_ROLL);
    press(B_OCTDN); press(B_OCTUP);
    bad += check("plain STEP OCT taps move the cursor instead of shifting octave", ui.cursor == 7 && !song.octave);

    t = edit_setup(); press(B_ENV);
    bad += check("unused ENV tap still opens ENV", cur_page()->fam == FAM_ENV);
    t = edit_setup(); press(B_SCL);
    bad += check("unused SCL tap still opens SCL", cur_page()->fam == FAM_SCL);
    t = edit_setup(); put_note(t, 0, 60, 3); frame(); chain.armed = 1;
    resize_selected(2);
    bad += check("song playback blocks a length modifier and still consumes its page tap", step_note_length(t, 0) == 3 && cur_page()->graph == GR_ROLL && msg_is("STOP TO EDIT"));
    chain.armed = 0;
    return bad;
}

/* a button held for `pre` frames (past the 0.4 s of a layer), a knob turned by s, held `post` more, let go */
static void held_turn(uint32_t b, uint32_t role, int32_t s, uint32_t pre, uint32_t post)
{
    uint32_t i;
    fm1_in.buttons |= 1u << panel.btn[b]; host_pressed |= 1u << panel.btn[b];
    for (i = 0; i < pre; i++) frame();
    host_enc[panel.enc[role]] += s * panel.dir[role]; frame(); host_ticks += 200000u;
    for (i = 0; i < post; i++) frame();
    fm1_in.buttons &= ~(1u << panel.btn[b]); frame();
}
/* held as on the panel (0.5 s and more): ENV / SCL + SELECT or KNOB 1 edit the cursor's note, armed and playing too
 * (the cursor stops following the play head while they are held); OCT shifts the octave while recording live */
static int test_held_gestures(void)
{
    int bad = 0;
    track_t *t = edit_setup();
    put_note(t, 4, 60, 2); cursor_set(4); frame();
    held_turn(B_ENV, EN_SELECT, 2, 30, 10);
    bad += check("ENV held 0.5 s + SELECT: the note 2 -> 4 steps, STEP stays", step_note_length(t, 4) == 4 &&
                 cur_page()->graph == GR_ROLL && !ui.ly);
    held_turn(B_SCL, EN_SELECT, 2, 40, 10);
    bad += check("SCL held 0.7 s + SELECT: the note moves 4 -> 6, no SCL layer or page", step_note_length(t, 6) == 4 &&
                 !step_on(&t->step[4]) && ui.cursor == 6 && cur_page()->graph == GR_ROLL && !ui.ly);
    held_turn(B_ENV, EN_K1, -1, 30, 10);
    bad += check("ENV + KNOB 1: as SELECT (4 -> 3 steps)", step_note_length(t, 6) == 3 && ui.cursor == 6);
    held_turn(B_SCL, EN_K1, -2, 30, 10);
    bad += check("SCL + KNOB 1: as SELECT (6 -> 4)", step_note_length(t, 4) == 3 && ui.cursor == 4);
    cursor_set(12); frame();
    key_down(10); frame(); frame();
    host_enc[panel.enc[EN_K1]] += 2 * panel.dir[EN_K1]; frame(); host_ticks += 200000u;
    key_up(10); frame();
    bad += check("a key held + KNOB 1: its length (3 steps), the cursor past it", step_note_length(t, 12) == 3 &&
                 ui.cursor == 15);

    t = edit_setup();
    put_note(t, 4, 60, 2); cursor_set(4);
    song.rec = 1u << song.sel; song.playing = 1; t->seq_idx = 4; frame();
    held_turn(B_ENV, EN_SELECT, 2, 30, 10);
    bad += check("armed and playing: ENV + SELECT still lengthens the note (2 -> 4)", step_note_length(t, 4) == 4 &&
                 cur_page()->graph == GR_ROLL);
    t->seq_idx = 4; frame();
    fm1_in.buttons |= 1u << panel.btn[B_SCL]; host_pressed |= 1u << panel.btn[B_SCL]; frame();
    t->seq_idx = 9; frame();
    bad += check("  the cursor stays on the note while SCL is held (the play head moved on)", ui.cursor == 4);
    host_enc[panel.enc[EN_SELECT]] += 2 * panel.dir[EN_SELECT]; frame();
    fm1_in.buttons &= ~(1u << panel.btn[B_SCL]); frame(); frame();
    bad += check("  .. and moves it (4 -> 6); let go, it follows the play head again", step_note_length(t, 6) == 4 &&
                 ui.cursor == 9);
    press(B_OCTUP);
    bad += check("  OCT+ while recording live: the octave, not the cursor", song.octave == 1 && ui.cursor == 9);
    put_note(t, 9, 64, 1);
    fm1_in.buttons |= 1u << panel.btn[B_FX]; host_pressed |= 1u << panel.btn[B_FX]; frame(); frame();
    {
        uint32_t ly = ui.ly;
        fm1_in.buttons &= ~(1u << panel.btn[B_FX]); frame();
        bad += check("  FX while recording live: its performance layer, the note stays", ly == LAYER_FX &&
                     step_on(&t->step[9]));
    }
    song.octave = 0; song.rec = 0; song.playing = 0;
    return bad;
}

int main(void)
{
    int bad = test_note_edits() + test_entry_history() + test_midi_entry() + test_record_timing() + test_step_modifiers() +
              test_held_gestures();
    printf("sequencer editing: %s\n", bad ? "FAILED" : "all passed");
    return bad != 0;
}
