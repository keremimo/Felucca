/* SPDX-License-Identifier: GPL-3.0-only */
/* Original performance events through real recording, panel, playback and storage paths. */
#define RECORDING_NO_MAIN 1
#include "recording_test.c"

static int selection_and_delete(void)
{
    raw_begin(); track_t *t = &trk[0];
    seq_advance(600); input_on(t,60,70); input_on(t,64,90);
    seq_advance(300); input_off(t,60);
    seq_advance(300); input_off(t,64);
    seq_advance(600); input_on(t,60,111);
    seq_advance(600); input_off(t,60);
    seq_advance(2400); input_on(t,67,95);
    seq_advance(600); input_off(t,67);
    seq_stop(); song.rec = 0;
    recorded_note_t saved[4]; memcpy(saved,recording,sizeof saved);
    go_page(GR_NOTES); cursor_set(0); frame();
    uint32_t count;
    int bad = check("NOTES selects chronological hits in their actual interval, including a hit rounded to the next overview step",
                    notes_selected(t)==0 && notes_rank(t,0,&count)==1 && count==4 && recording_view(t,&recording[3])==1);
    turn(EN_K2,2);
    bad += check("HIT selects a repeated pitch independently of its chord neighbour",notes_selected(t)==2);
    recorded_note_t unchanged[RECORD_MAX]; memcpy(unchanged,recording,sizeof unchanged);
    turn(EN_K4,4); turn(EN_K3,7); t->p[P_RECQ]=3; frame();
    bad += check("zoom, the read-only NOTE card and QNT preserve selection and original events",
                 ui.note_zoom==4 && notes_span()==1 && notes_selected(t)==2 && !memcmp(unchanged,recording,sizeof unchanged));
    t->p[P_RECQ]=0;
    press(B_EDIT);
    bad += check("EDIT removes only the selected hit and preserves neighbouring event bytes",
                 !recording[2].vel && !memcmp(&saved[0],&recording[0],sizeof saved[0]) &&
                 !memcmp(&saved[1],&recording[1],sizeof saved[1]) && !memcmp(&saved[3],&recording[3],sizeof saved[3]) &&
                 notes_selected(t)==3 && ui.cursor==0 && (t->step[0].flags&SF_RECORDED));
    raw_replay(); seq_advance(600); int exact = gate_note(t,60)&&gate_note(t,64);
    seq_advance(300); exact &= !gate_note(t,60)&&gate_note(t,64);
    seq_advance(300); exact &= !gate_note(t,64);
    seq_advance(600); exact &= !gate_note(t,60);
    seq_advance(3000); exact &= gate_note(t,67);
    bad += check("deleted off-grid hits stay silent; neighbours retain exact onsets and releases",exact);
    seq_stop(); hold(B_SAVE);
    bad += check("undo restores timing, velocity, duration and the selected hit's focus",
                 !memcmp(saved,recording,sizeof saved) && notes_selected(t)==2 && ui.cursor==0);
    fm1_in.buttons |= 1u<<panel.btn[B_EDIT]; host_pressed |= 1u<<panel.btn[B_EDIT]; frame();
    press(B_OCTUP); fm1_in.buttons &= ~(1u<<panel.btn[B_EDIT]); frame();
    bad += check("EDIT plus OCT+ redoes one deletion without deleting its successor",!recording[2].vel&&recording[3].vel);
    project_capture(&proj_scratch);
    int packed=bank_pack(proj_wire_u.raw,&proj_scratch,1);
    project_restore_runtime(&proj_scratch); if(packed) bank_restore(proj_wire_u.raw);
    bad += check("one-hit deletion survives project save/load",packed&&!recording[2].vel&&recording[0].vel&&recording[1].vel&&recording[3].vel);
    return bad;
}

static int deletion_history(void)
{
    raw_begin(); track_t *t=&trk[0];
    for(uint32_t i=0;i<10;i++){seq_advance(180);input_on(t,60,70+i);seq_advance(60);input_off(t,60);}
    seq_stop(); song.rec=0; recorded_note_t saved[10];memcpy(saved,recording,sizeof saved);
    go_page(GR_NOTES);cursor_set(0);frame();
    for(uint32_t i=0;i<10;i++)press(B_EDIT);
    int ok=1;for(uint32_t i=0;i<10;i++)ok&=!recording[i].vel;
    int bad=check("same-pitch deletions each become an edit even when the overview is unchanged",ok&&!step_on(&t->step[0]));
    for(uint32_t i=0;i<8;i++) {step_history_apply(0);frame();}
    ok=!recording[0].vel&&!recording[1].vel;
    for(uint32_t i=2;i<10;i++)ok&=!memcmp(&saved[i],&recording[i],sizeof saved[i]);
    bad+=check("the latest eight deletions undo byte-for-byte, restoring the last hit's overview",ok&&!step_history_apply(0));
    for(uint32_t i=0;i<8;i++) {step_history_apply(1);frame();}
    ok=1;for(uint32_t i=0;i<10;i++)ok&=!recording[i].vel;
    bad+=check("eight deletions redo without resurrecting the oldest two",ok&&!step_history_apply(1));
    step_history_apply(0);frame();step_history_apply(0);frame();
    press(B_EDIT);
    bad+=check("a new one-hit deletion drops the old redo branch",!step_history_apply(1));
    song.rec=1;seq_start();seq_advance(0);seq_advance(180);input_on(t,60,120);seq_advance(60);input_off(t,60);seq_stop();song.rec=0;
    frame();bad+=check("slot reuse by recording invalidates deletion undo",!step_history_apply(0)&&recording[0].vel==120);
    return bad;
}

static int loop_drums_and_playback(void)
{
    raw_begin();track_t *t=&trk[0];
    seq_advance(15u*6144u+5004u);input_on(t,60,100);seq_advance(2004);input_off(t,60);seq_stop();song.rec=0;
    recorded_note_t saved=recording[0];
    go_page(GR_NOTES);cursor_set(15);frame();
    int bad=check("late-loop events select in their final step although the overview is step zero",
                  notes_selected(t)==0&&recording_view(t,&recording[0])==0);
    press(B_EDIT);bad+=check("late-loop deletion clears its step-zero overview",!recording[0].vel&&!step_on(&t->step[0]));
    hold(B_SAVE);bad+=check("late-loop undo restores the event and overview",!memcmp(&saved,&recording[0],sizeof saved)&&step_on(&t->step[0]));
    cursor_set(1);frame();press(B_EDIT);bad+=check("empty interval deletion leaves neighbouring notes alone",!memcmp(&saved,&recording[0],sizeof saved)&&msg_is("NO RECORDED NOTE"));
    turn(EN_K4,100);bad+=check("zoom clamps at one step",ui.note_zoom==4);
    turn(EN_K4,-100);bad+=check("zoom clamps at sixteen steps",ui.note_zoom==0);

    raw_begin();t=&trk[0];set_engine_of(t,ENGI_DRUM);t->engine=t->eng_req;
    seq_advance(600);input_on(t,36,100);input_on(t,38,115);seq_advance(300);input_off(t,36);input_off(t,38);
    seq_advance(900);input_on(t,36,90);seq_advance(300);input_off(t,36);seq_stop();song.rec=0;
    go_page(GR_NOTES);cursor_set(0);frame();turn(EN_K2,2);press(B_EDIT);
    bad+=check("drum deletion preserves repeated kicks and other lane hits",
               !recording[2].vel&&recording[0].vel&&recording[1].vel&&(t->step[0].hit&(1u<<drum_lane(36)))&&(t->step[0].hit&(1u<<drum_lane(38))));
    hold(B_SAVE);bad+=check("drum deletion undoes without opening the EDIT layer",recording[2].vel==90&&!ui.ly&&cur_page()->graph==GR_NOTES);

    raw_begin();t=&trk[0];seq_advance(600);input_on(t,60,70);seq_advance(100);input_off(t,60);
    seq_advance(100);input_on(t,60,90);seq_advance(100);input_off(t,60);seq_stop();song.rec=0;
    go_page(GR_NOTES);cursor_set(0);frame();
    t->p[P_RECQ]=3;raw_replay();
    int ok=recording_refs[0][60]==2&&gate_note(t,60);press(B_EDIT);
    ok&=recording_refs[0][60]==1&&gate_note(t,60)&&!recording[0].vel&&recording[1].vel;
    seq_advance(100);ok&=!gate_note(t,60)&&!recording_refs[0][60];
    bad+=check("sounding-event deletion preserves overlapping same-pitch ownership and its final release",ok);
    seq_stop();song.rec=1;seq_start();seq_advance(0);frame();recorded_note_t before=recording[1];press(B_EDIT);
    bad+=check("precise deletion is refused during live recording",!memcmp(&before,&recording[1],sizeof before)&&msg_is("STOP RECORDING"));
    seq_stop();song.rec=0;
    return bad;
}

static int timing_geometry_and_context(void)
{
    raw_begin();track_t *t=&trk[0];t->p[P_SSWING]=50;t->p[P_SLEN]=5;
    uint32_t period=seq_div_samples((uint32_t)t->p[P_SDIV]), span=step_samples(t,period,0);
    seq_advance(span/4u);input_on(t,61,100);seq_advance(span/8u);input_off(t,61);
    seq_advance(3u*span/8u);input_on(t,64,110);seq_advance(span/8u);input_off(t,64);seq_stop();song.rec=0;
    go_page(GR_NOTES);cursor_set(0);turn(EN_K4,4);
    uint32_t a,b;notes_window(t,period,&a,&b);
    int32_t x=notes_x(recording_raw_on(t,&recording[0],period),a,b), y=pr_row_y(61)+Y_GRAPH;
    int bad=check("swung off-grid notes draw at their original quarter-step position at maximum zoom",
                  x==PR_X0+48&&pr_is_bar((uint32_t)x+1u,(uint32_t)y+2u)&&!pr_is_bar((uint32_t)x-2u,(uint32_t)y+2u));
    turn(EN_K2,1);uint16_t selection=ui.note_pick;
    t->p[P_RECQ]=3;frame();
    bad+=check("playback quantization leaves the displayed original onset unchanged",
               ui.note_pick==selection&&notes_x(recording_raw_on(t,&recording[0],period),a,b)==x);
    t->p[P_RECQ]=0;cursor_set(4);turn(EN_K4,-3);notes_window(t,period,&a,&b);
    bad+=check("zoom windows clip safely to the last step of an odd-length loop",notes_base()==0&&b==recording_loop(t,period));
    turn(EN_K4,1);notes_window(t,period,&a,&b);
    bad+=check("a partially filled zoom window starts at its final bank and ends at the loop",notes_base()==4&&a==recording_prefix(t,period,4)&&b==recording_loop(t,period));
    cursor_set(0);frame();uint32_t saved=notes_selected(t);
    pattern_switch(t,7);frame();bad+=check("switching banks clears stale hit selection",notes_selected(t)==RECORD_MAX);
    pattern_switch(t,0);frame();bad+=check("returning to the recorded bank selects its first original hit",notes_selected(t)==saved);
    track_select(1);frame();bad+=check("switching tracks cannot select another track's recording",notes_selected(TSEL)==RECORD_MAX);
    track_select(0);frame();turn(EN_K2,1);press(B_EDIT);
    bad+=check("event deletion rebuilds the surviving overview's pitch and accent",t->step[1].time==ST_REST&&t->step[0].note[0]==61&&!(t->step[0].flags&SF_ACCENT));
    return bad;
}

static track_t *clash_setup(void)
{
    raw_begin();track_t *t=&trk[0];
    seq_advance(600);input_on(t,60,100);seq_advance(300);input_off(t,60);
    seq_advance(900);input_on(t,64,90);seq_advance(300);input_off(t,64);
    seq_advance(6144u+600u-2100u);input_on(t,67,95);seq_advance(300);input_off(t,67);
    seq_stop();song.rec=0;go_page(GR_NOTES);cursor_set(0);frame();
    return t;
}
static int control_clashes(void)
{
    int bad=0;
    static const uint32_t enc[]={EN_K1,EN_K2,EN_K3,EN_K4,EN_SELECT,EN_PRESET,EN_ALGO};
    for(uint32_t k=0;k<NELEM(enc);k++) {
        track_t *t=clash_setup();recorded_note_t saved[3];memcpy(saved,recording,sizeof saved);
        step_t before[NSTEP];memcpy(before,t->step,sizeof before);
        fm1_in.buttons|=1u<<panel.btn[B_EDIT];host_pressed|=1u<<panel.btn[B_EDIT];frame();
        turn(enc[k],enc[k]==EN_SELECT?-1:1);
        fm1_in.buttons&=~(1u<<panel.btn[B_EDIT]);frame();
        char label[100];snprintf(label,sizeof label,"EDIT held + encoder %u never becomes deletion when released",enc[k]);
        bad+=check(label,!memcmp(saved,recording,sizeof saved)&&!memcmp(before,t->step,sizeof before));
    }
    track_t *t=clash_setup();recorded_note_t saved[3];memcpy(saved,recording,sizeof saved);
    go_page(GR_ROLL);frame();
    fm1_in.buttons|=1u<<panel.btn[B_EDIT];host_pressed|=1u<<panel.btn[B_EDIT];frame();
    turn(EN_SELECT,1);fm1_in.buttons&=~(1u<<panel.btn[B_EDIT]);frame();
    bad+=check("EDIT held while navigating from STEP into NOTES does not delete on release",cur_page()->graph==GR_NOTES&&!memcmp(saved,recording,sizeof saved)&&recording_active(t,0));
    t=clash_setup();memcpy(saved,recording,sizeof saved);
    fm1_in.buttons|=1u<<panel.btn[B_EDIT];host_pressed|=1u<<panel.btn[B_EDIT];frame();
    host_enc[panel.enc[EN_K2]]+=panel.dir[EN_K2];fm1_in.buttons&=~(1u<<panel.btn[B_EDIT]);frame();
    bad+=check("a release-frame HIT detent consumes EDIT instead of deleting the newly selected hit",notes_selected(t)==1&&!memcmp(saved,recording,sizeof saved));
    t=clash_setup();press(B_EDIT);
    fm1_in.buttons|=1u<<panel.btn[B_EDIT];host_pressed|=1u<<panel.btn[B_EDIT];frame();
    hold(B_SAVE);fm1_in.buttons&=~(1u<<panel.btn[B_EDIT]);frame();
    bad+=check("SAVE undo with EDIT already held cannot delete the restored hit on release",recording[0].vel==100&&recording_active(t,0));
    t=clash_setup();turn(EN_K2,1);uint16_t selection=ui.note_pick;
    song.rec=2;seq_start();seq_advance(0);seq_advance(600);input_on(&trk[1],65,100);frame();
    int stable=ui.note_pick==selection;
    seq_advance(300);input_off(&trk[1],65);frame();stable&=ui.note_pick==selection;
    bad+=check("recording and releasing notes on another track preserves the selected hit",stable);
    seq_stop();song.rec=0;
    t=clash_setup();press(B_EDIT);
    song.rec=2;seq_start();seq_advance(0);seq_advance(600);input_on(&trk[1],65,100);
    recorded_note_t replacement=recording[0];
    /* An ISR may reuse a freed slot after the frame's sync but before end. */
    step_history_end();int undone=step_history_apply(0);
    bad+=check("recording between history sync/end cannot be mistaken for an undoable UI edit",
               !undone&&!memcmp(&replacement,&recording[0],sizeof replacement)&&(recording[0].owner&31u)==NPAT);
    input_off(&trk[1],65);seq_stop();song.rec=0;
    t=clash_setup();memcpy(saved,recording,sizeof saved);
    queued(0x90,69,100,1);frame();int audition=gate_note(t,69)&&!ui.entry_open;
    queued(0x80,69,0,1);frame();audition&=!gate_note(t,69);
    bad+=check("MIDI audition on NOTES plays and releases normally without entering or replacing steps",audition&&!memcmp(saved,recording,sizeof saved));
    t=clash_setup();memcpy(saved,recording,sizeof saved);
    fm1_in.buttons|=1u<<panel.btn[B_EDIT];host_pressed|=1u<<panel.btn[B_EDIT];frame();
    press(B_PLAY);fm1_in.buttons&=~(1u<<panel.btn[B_EDIT]);frame();
    bad+=check("PLAY with EDIT held requests transport without deleting a note on release",transport_req==1&&!memcmp(saved,recording,sizeof saved));
    transport_req=0;
    t=clash_setup();memcpy(saved,recording,sizeof saved);chain.running=1;press(B_EDIT);
    bad+=check("song-chain protection also blocks individual note deletion",!memcmp(saved,recording,sizeof saved)&&msg_is("STOP TO EDIT"));
    chain.running=0;
    for(uint32_t layer=LAYER_FX;layer<LAYER_EDIT;layer++) {
        t=clash_setup();memcpy(saved,recording,sizeof saved);uint8_t zoom=ui.note_zoom;
        uint32_t b=LAYERS[layer].btn;
        fm1_in.buttons|=1u<<panel.btn[b];host_pressed|=1u<<panel.btn[b];frame();turn(EN_K4,1);
        int active=ui.ly==layer&&ui.layer==layer;
        fm1_in.buttons&=~(1u<<panel.btn[b]);frame();
        bad+=check("FX/GLO/SCL layers own their knobs on NOTES without editing or deleting hits",active&&ui.note_zoom==zoom&&!memcmp(saved,recording,sizeof saved));
    }
    return bad;
}

int main(void)
{
    int bad=selection_and_delete()+deletion_history()+loop_drums_and_playback()+timing_geometry_and_context()+control_clashes();
    printf("Recorded note editing: %s\n",bad?"FAILED":"all passed");
    return !!bad;
}
