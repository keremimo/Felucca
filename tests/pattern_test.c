/* SPDX-License-Identifier: GPL-3.0-only */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int banks_and_chords(void)
{
    int bad = 0; ui_power_on(); track_t *t = TSEL;
    t->p[P_SLEN] = 64;
    t->step[63] = (step_t){{60,64,67,71},4,ST_NOTE,SF_ACCENT,112,0x81,0x80,41};
    t->step[0] = (step_t){{0},0,ST_TIE};
    motion_set_event(t, 63, P_REV, 83);
    bad += check("bank copy preserves four-note chords, wrapped ties, grid, chance and automation", !pattern_copy(t, 0, 7) &&
        !pattern_request(t, 7) && t->step[63].n == 4 && t->step[63].note[3] == 71 && t->step[0].time == ST_TIE &&
        t->step[63].hit == 0x81 && t->step[63].acc == 0x80 && step_chance(&t->step[63]) == 41 && motion_count(t) == 1);
    motion_step(t, 63, &motion);
    bad += check("copied bank plays its own automation", t->p[P_REV] == 83);
    motion_restore(t); motion_clear(t); t->step[63].note[3] = 72;
    pattern_request(t, 0);
    bad += check("editing or clearing a copy leaves the source chord and motion intact", t->step[63].note[3] == 71 && motion_count(t) == 1);
    step_history_sync(); uint32_t gen = t->pattern_gen;
    pattern_copy(t, 0, 1); pattern_request(t, 1); step_history_sync();
    bad += check("identical-bank switches invalidate manual undo history", t->pattern_gen != gen && step_history.pattern_gen == t->pattern_gen && step_history.count == 1);
    return bad;
}
static int independent_switches(void)
{
    int bad = 0; ui_power_on();
    for (uint32_t k = 0; k < 2; k++) {
        track_t *t = &trk[k]; track_defaults_steps(t); t->p[P_SLEN] = (int16_t)(2+k);
        t->step[0] = (step_t){{60,64,67,71},4,ST_NOTE,0,100};
        pattern_request(t, 1); t->p[P_SLEN] = 4; t->step[0] = (step_t){{62,65,69,72},4,ST_NOTE,0,100};
        pattern_request(t, 0);
    }
    seq_start(); events_block(32);
    pattern_request(&trk[0], 1); pattern_request(&trk[1], 1);
    uint32_t period = div_samples(trk[0].p[P_SDIV]);
    bad += check("playing bank choices queue without replacing the sounding chord", trk[0].pattern == 0 && trk[0].pattern_next == 1 && trk[0].step[0].note[0] == 60);
    events_block(period); events_block(period);
    bad += check("shorter track switches at its own loop end while the longer track keeps playing", trk[0].pattern == 1 && trk[1].pattern == 0 && trk[0].seq_idx == 0 && trk[0].seq_n == 4 && trk[0].seq_notes[3] == 72);
    events_block(period);
    bad += check("longer track switches at its own boundary with its four-note chord", trk[1].pattern == 1 && trk[1].seq_idx == 0 && trk[1].seq_n == 4 && trk[1].seq_notes[3] == 72);
    pattern_request(&trk[0], 3); pattern_request(&trk[0], 1);
    bad += check("selecting the active bank cancels a pending switch", trk[0].pattern_next == 0xff);
    pattern_request(&trk[1], 7); seq_stop();
    bad += check("STOP applies queued banks and releases sequenced notes", trk[1].pattern == 7 && trk[1].pattern_next == 0xff && !trk[1].seq_n);
    return bad;
}
static int all_banks_persist(void)
{
    int bad = 0, ok = 1; ui_power_on();
    for (uint32_t k = 0; k < NTRK; k++) for (uint32_t b = 0; b < NPAT; b++) {
        track_t *t = &trk[k]; pattern_request(t, b);
        t->p[P_SLEN] = (int16_t)(32+b); t->p[P_SSWING] = (int16_t)b;
        t->step[0] = (step_t){{(uint8_t)(40+k*8+b),60,64,67},4,ST_NOTE,SF_SLIDE,100,(uint8_t)(1u<<b),(uint8_t)(1u<<b),(uint8_t)(20+b)};
        t->step[1] = (step_t){{0},0,ST_TIE}; motion_set_event(t, 0, P_REV, (int16_t)(k*8+b));
    }
    chain_config.count = 2; chain_config.row[0] = (chain_row_t){0,3}; chain_config.row[1] = (chain_row_t){7,2};
    for (uint32_t k = 0; k < NTRK; k++) { chain_patterns[0][k] = (uint8_t)k; chain_patterns[1][k] = (uint8_t)(7-k); }
    bad += check("complete project saves all 32 banks", project_save(2) == 0);
    pattern_init(); memset(&motion,0,sizeof motion); project_load(2);
    bad += check("project restores arrangement and active bank selection", chain_config.count == 2 && chain_patterns[0][3] == 3 && chain_patterns[1][3] == 4 && trk[3].pattern == 7);
    for (uint32_t k = 0; k < NTRK; k++) for (uint32_t b = 0; b < NPAT; b++) {
        track_t *t = &trk[k]; pattern_request(t,b);
        ok &= t->step[0].n == 4 && t->step[0].note[0] == 40+k*8+b && t->step[0].note[3] == 67 &&
            t->step[1].time == ST_TIE && t->p[P_SLEN] == 32+(int16_t)b && t->p[P_SSWING] == (int16_t)b &&
            t->step[0].hit == (1u<<b) && t->step[0].acc == (1u<<b) && step_chance(&t->step[0]) == 20+b && motion_count(t)==1;
        motion_begin(); motion_step(t,0,&motion); ok &= t->p[P_REV] == (int16_t)(k*8+b); motion_end();
    }
    bad += check("every bank round-trips chord, ties, timing, chance, hits and bank-specific automation", ok);
    uint8_t raw[BANK_STORE_SIZE]; project_capture(&proj_scratch); bank_pack(raw,&proj_scratch,1);
    raw[BANK_ACTIVE_OFF] = 8; bank_checksum(raw);
    bad += check("invalid bank IDs are rejected even with a valid checksum", !bank_valid(raw,sizeof raw));
    bank_pack(raw,&proj_scratch,1); raw[BANK_EXTRA_OFF+3] |= 0xf0; bank_checksum(raw);
    bad += check("malformed inactive-bank chord is rejected before publication", !bank_valid(raw,sizeof raw) && trk[0].step[0].n == 4);
    return bad;
}
static int panel_banks(void)
{
    int bad=0; ui_power_on(); go_home(); uint8_t original_page=ui.page;
    btn_down(B_SEQ); frame(); key_down(PAT_KEY[7]); frame();
    bad += check("SEQ bank keys stay silent and do not record notes", kb_note[PAT_KEY[7]] == KB_SILENT && !TSEL->nheld);
    key_up(PAT_KEY[7]); frame(); btn_up(B_SEQ); frame();
    bad += check("SEQ plus eighth white key selects bank 8 without a page tap", TSEL->pattern == 7 && ui.home && ui.page == original_page);
    TSEL->step[3] = (step_t){{60,64,67,71},4,ST_NOTE,0,100};
    btn_down(B_SEQ); frame(); key_down(PAT_KEY[7]); frame(); key_down(PAT_KEY[2]); frame();
    key_up(PAT_KEY[2]); key_up(PAT_KEY[7]); frame(); btn_up(B_SEQ); frame();
    pattern_request(TSEL,2);
    bad += check("holding one bank key and pressing another copies its full chord", TSEL->step[3].n == 4 && TSEL->step[3].note[3] == 71);
    return bad;
}
static int arranged_chords(void)
{
    int bad = 0, ok = 1; ui_power_on();
    for (uint32_t k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        for (uint32_t b = 0; b < 2u; b++) {
            pattern_request(t, b); track_defaults_steps(t); t->p[P_SLEN] = 2;
            t->step[0] = (step_t){{(uint8_t)(48+k+b),60,64,67},4,ST_NOTE,0,100};
        }
        pattern_request(t, 0);
        chain_patterns[0][k] = (uint8_t)(1u - (k & 1u));
        chain_patterns[1][k] = (uint8_t)(k & 1u);
    }
    chain_config.count = 2; chain_config.row[0] = (chain_row_t){1,2}; chain_config.row[1] = (chain_row_t){0,1};
    ok &= !chain_prepare(); events_block(32);
    for (uint32_t k = 0; k < NTRK; k++) ok &= trk[k].pattern == chain_patterns[0][k] && trk[k].seq_n == 4 &&
        trk[k].seq_notes[0] == 48+k+chain_patterns[0][k];
    bad += check("SONG starts four independently assigned banks with four-note chords", ok);
    bad += check("SONG protects its bank selections and contents while running", pattern_request(TSEL, 7) && pattern_copy(TSEL, 0, 7));
    uint32_t period = div_samples(trk[0].p[P_SDIV]);
    events_block(period); events_block(period);
    bad += check("SONG repeats the first row without changing its bank assignments", chain.running && chain.row == 0 && chain.remaining == 1 && trk[0].pattern == 1);
    events_block(period); events_block(period); ok = chain.running && chain.row == 1;
    for (uint32_t k = 0; k < NTRK; k++) ok &= trk[k].pattern == chain_patterns[1][k] && trk[k].seq_n == 4 &&
        trk[k].seq_notes[0] == 48+k+chain_patterns[1][k];
    bad += check("SONG changes all four banks together at track one's loop boundary", ok);
    events_block(period); events_block(period); ok = !song.playing && !chain.running;
    for (uint32_t k = 0; k < NTRK; k++) ok &= !trk[k].pattern && !trk[k].seq_n;
    bad += check("SONG completion restores original banks and releases all sequence voices", ok);
    return bad;
}
static int replaced_motion(void)
{
    int bad = 0; ui_power_on();
    motion_set_event(TSEL, 0, P_REV, 40); pattern_request(TSEL, 1);
    motion_store_t edit = {0}; edit.on = 1; edit.count = 1;
    edit.event[0] = (motion_event_t){0,P_REV,80};
    bad += check("replacing motion in another bank preserves valid tagged duplicates", !motion_replace_track(TSEL, &edit) &&
        motion_valid(&motion) && motion.count == 2 && motion_pattern[0] == 0 && motion_pattern[1] == 1);
    bad += check("retired user sample sources remain silent in GRAIN", !SMP_USER_SLOTS && !gr_nz(SMP_NSETS) && gr_stamp(SMP_NSETS) == 0xffffffffu);
    return bad;
}
static int migrated_bank_rename(void)
{
#if !MELODEE_FM4
    ui_power_on();
    motion_set_event(TSEL, 0, P_E0, 40);
    pattern_request(TSEL, 3); motion_set_event(TSEL, 0, P_REV, 80);
    project_capture(&proj_scratch); proj_scratch.t[0].engine = ENGI_DIGITAL;
    if (!bank_pack(proj_wire_u.raw, &proj_scratch, 1)) return check("DIGITAL bank fixture packs", 0);
    memcpy(proj_bank_slot[0], proj_wire_u.raw, BANK_STORE_SIZE);
    memcpy(&proj_slot[0], proj_wire_u.raw + 8u, sizeof proj_slot[0]);
    int ok = !project_rename(0, "MIGRATED"); project_load(0);
    ok &= trk[0].eng_req == ENGI_FM6 && motion.count == 1 && motion_pattern[0] == 3 && motion.event[0].param == P_REV;
    motion_begin(); motion_step(TSEL, 0, &motion); ok &= TSEL->p[P_REV] == 80; motion_end();
    return check("renaming a migrated DIGITAL bank keeps surviving automation on its bank", ok);
#else
    return 0;
#endif
}
int main(void)
{
    int bad = banks_and_chords()+independent_switches()+all_banks_persist()+panel_banks()+arranged_chords()+replaced_motion()+migrated_bank_rename();
    printf("%s\n",bad ? "PATTERN TEST FAILED" : "pattern bank tests passed"); return !!bad;
}
