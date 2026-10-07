/* SPDX-License-Identifier: GPL-3.0-only */
#define MIDI_TIMING_NO_MAIN 1
#include "midi_timing_test.c"

static void raw_begin(void)
{
    timing_reset(2, 24000000u);
    timing_packet(timing_now, 0xFA, 0, 0, 2);
    timing_packet(timing_now, 0xF8, 0, 0, 2);
    events_block(0);
}
static void raw_replay(void)
{
    seq_stop(); song.rec = 0;
    seq_start(); seq_advance(0);
}
static int repeated_hits(void)
{
    int bad = 0;
    for (uint32_t source = 0; source < 3; source++) {
        raw_begin(); track_t *t = &trk[0];
        seq_advance(600);
        if (!source) input_on(t, 60, 70);
        else { timing_packet(timing_now, 0x90, 60, 70, source); events_block(0); }
        seq_advance(300);
        if (!source) input_off(t, 60);
        else { timing_packet(timing_now, 0x80, 60, 0, source); events_block(0); }
        seq_advance(900);
        if (!source) input_on(t, 60, 111);
        else { timing_packet(timing_now, 0x90, 60, 111, source); events_block(0); }
        seq_advance(600);
        if (!source) input_off(t, 60);
        else { timing_packet(timing_now, 0x80, 60, 0, source); events_block(0); }
        bad += check("OFF records two same-pitch hits inside one step with separate velocities/durations",
                     !t->p[P_RECQ] && recording[0].on == 6400 && recording[1].on == 19200 &&
                     recording[0].duration == 3200 && recording[1].duration == 6400 &&
                     recording[0].vel == 70 && recording[1].vel == 111);
        recorded_note_t saved[2]; memcpy(saved, recording, sizeof saved);
        raw_replay(); seq_advance(599);
        int exact = !gate_note(t, 60);
        seq_advance(1); exact &= gate_note(t, 60);
        seq_advance(299); exact &= gate_note(t, 60);
        seq_advance(1); exact &= !gate_note(t, 60);
        seq_advance(899); exact &= !gate_note(t, 60);
        seq_advance(1); exact &= gate_note(t, 60);
        seq_advance(600); exact &= !gate_note(t, 60);
        bad += check("OFF replays both hits at their exact captured edges through local/USB/TRS input", exact);
        seq_stop(); t->p[P_RECQ] = 3; raw_replay();
        bad += check("optional 1/16 playback moves these hits to grid zero", gate_note(t, 60));
        seq_stop(); t->p[P_RECQ] = 0; raw_replay(); seq_advance(599);
        bad += check("turning QNT off restores the original take byte-for-byte",
                     !gate_note(t,60) && !memcmp(saved, recording, sizeof saved));
    }
    return bad;
}
static int independent_lengths(void)
{
    raw_begin(); track_t *t = &trk[0];
    seq_advance(600); input_on(t,60,100); input_on(t,64,90);
    seq_advance(600); input_off(t,60);
    seq_advance(1200); input_off(t,64);
    int bad = check("overlapping chord notes store independent releases", recording[0].duration == 6400 && recording[1].duration == 19200);
    raw_replay(); seq_advance(600); seq_advance(600);
    bad += check("replay releases the short chord note while keeping the longer one held", !gate_note(t,60) && gate_note(t,64));
    seq_advance(1200);
    bad += check("the longer chord note releases at its own captured time", !gate_note(t,64));
    return bad;
}
static int raw_loop(void)
{
    raw_begin(); track_t *t = &trk[0];
    uint32_t on = 15u * 6144u + 5004u;
    seq_advance(on); input_on(t,60,100); seq_advance(2004); input_off(t,60);
    recorded_note_t original = recording[0];
    raw_replay(); int exact = !gate_note(t,60);
    seq_advance(on - 1u); exact &= !gate_note(t,60);
    seq_advance(1); exact &= gate_note(t,60);
    seq_advance(16u * 6144u - on); exact &= gate_note(t,60);
    seq_advance(2004u - (16u * 6144u - on)); exact &= !gate_note(t,60);
    int bad = check("an unquantized late-loop note crosses zero without an extra beginning onset", exact);
    seq_stop(); t->p[P_RECQ]=3; raw_replay();
    exact = gate_note(t,60); seq_advance(2004); exact &= !gate_note(t,60);
    seq_advance(16u * 6144u - 2005u); exact &= !gate_note(t,60);
    seq_advance(1); exact &= gate_note(t,60);
    bad += check("quantized loop zero fires once and shifts the release with the onset",
                 exact && !memcmp(&original,&recording[0],sizeof original));
    return bad;
}
static int raw_storage(void)
{
    raw_begin(); track_t *t = &trk[0];
    seq_advance(606); input_on(t,60,101); seq_advance(1200); input_off(t,60); seq_stop();
    recorded_note_t original = recording[0];
    pattern_switch(t,7); t->p[P_RECQ]=5;
    seq_start(); seq_advance(0); seq_advance(1800); input_on(t,67,81); seq_advance(600); input_off(t,67); seq_stop();
    project_capture(&proj_scratch);
    int packed = bank_pack(proj_wire_u.raw,&proj_scratch,1);
    int valid = packed && bank_valid(proj_wire_u.raw,BANK_STORE_SIZE);
    int ok = valid;

    if(ok){ project_restore_runtime(&proj_scratch);bank_restore(proj_wire_u.raw); }
    ok &= t->pattern==7 && t->p[P_RECQ]==5 && !memcmp(&original,&recording[0],sizeof original) && recording[1].owner==7;
    pattern_switch(t,0); t->p[P_RECQ]=0; raw_replay(); seq_advance(605);ok &= !gate_note(t,60);seq_advance(1);ok &= gate_note(t,60);
    int bad = check("active and inactive bank recordings survive save/load and replay at their original time",ok);
    bad += check("the complete new project still fits its existing five-sector flash allocation", BANK_STORE_SIZE<=(5u * 4096u - 256u));
    return bad;
}
static int raw_capacity(void)
{
    raw_begin(); track_t *t=&trk[0];
    for(uint32_t i=0;i<RECORD_MAX;i++) { seq_advance(54); input_on(t,60,100);seq_advance(12);input_off(t,60); }
    recorded_note_t saved[RECORD_MAX];memcpy(saved,recording,sizeof saved);
    seq_advance(12); input_on(t,62,99);input_off(t,62);
    int bad=check("a full recorder preserves all existing notes and reports the capacity limit",recording_full && !memcmp(saved,recording,sizeof saved));
    bad+=check("a full bank copy fails before changing the destination or source",pattern_copy(t,0,7)==2 && !memcmp(saved,recording,sizeof saved));
    seq_stop();step_clear(&t->step[0]);song.rec=1;seq_start();seq_advance(0);input_on(t,65,95);input_off(t,65);
    bad+=check("clearing recorded steps makes their event slots reusable",recording[0].note==65);
    return bad;
}
static int replace_during_replay(void)
{
    raw_begin();track_t *t=&trk[0];seq_advance(600);input_on(t,60,70);seq_advance(300);input_off(t,60);
    raw_replay();seq_advance(600);song.rec=1;input_on(t,60,111);seq_advance(600);input_off(t,60);
    int bad=check("recording over a sounding event replaces it without a duplicate entry",recording[0].vel==111 && recording[0].duration==6400 && !recording[1].vel);
    seq_stop();
    return bad;
}
static int standalone_and_template(void)
{
    raw_begin();track_t *t=&trk[0];seq_stop();pattern_switch(t,7);seq_start();seq_advance(0);
    seq_advance(600);input_on(t,60,90);seq_advance(300);input_off(t,60);seq_stop();
    project_capture(&proj_scratch);project_store_t bytes;project_t decoded;
    int ok=proj_pack(&bytes,&proj_scratch) && proj_import_any(&decoded,bytes.raw,sizeof bytes) && !project_restore_runtime(&decoded);
    raw_replay();seq_advance(599);ok &= t->pattern==7 && !gate_note(t,60);seq_advance(1);ok &= gate_note(t,60);
    int bad=check("standalone FUN12 restores the active bank and original recorded timing",ok);
    seq_stop();t->p[P_RECQ]=6;template_save();tmpl_t saved=tmpl;
    tmpl_take((const uint8_t *)&saved,sizeof saved);
    bad+=check("new templates preserve per-track timing quantization",tmpl.t[0].p[P_RECQ]==6);
    saved.magic=TMPL_MAGIC_A;tmpl_take((const uint8_t *)&saved,sizeof saved);
    bad+=check("legacy templates initialize the repurposed inert parameter to OFF",!tmpl.t[0].p[P_RECQ]);
    bad+=check("playback timing QNT cannot become recorded knob automation",!motion_param(P_RECQ));
    return bad;
}
static int copy_and_edit(void)
{
    raw_begin(); track_t *t = &trk[0];
    seq_advance(600); input_on(t,60,100); seq_advance(300); input_off(t,60); seq_stop();
    int bad = check("bank copy includes original timing and independent duration", !pattern_copy(t,0,7) &&
                    recording[1].owner==7 && recording[1].on==recording[0].on && recording[1].duration==recording[0].duration);
    pattern_switch(t,7); raw_replay(); seq_advance(599);
    int exact = !gate_note(t,60);seq_advance(1);exact &= gate_note(t,60);
    bad += check("copied recordings replay at their captured time",exact);
    seq_stop(); go_page(GR_ROLL); step_history_sync();step_delete(t,0);step_history_end();raw_replay();seq_advance(600);
    bad += check("deleting the overview step silences its timed notes",!gate_note(t,60));
    seq_stop();int undone=step_history_apply(0);raw_replay();seq_advance(600);
    bad += check("manual undo restores the original timed note",undone && gate_note(t,60));
    seq_stop(); t->p[P_RECQ]=6;trk[1].p[P_RECQ]=0;int16_t scale=t->p[P_QUANT];
    apply_preset_to(t,1);
    bad += check("timing QNT stays per-track, survives sound loads and leaves scale QNT separate",
                 t->p[P_RECQ]==6 && !trk[1].p[P_RECQ] && t->p[P_QUANT]==scale);
    return bad;
}
static int slow_precision(void)
{
    raw_begin();track_t *t=&trk[0];t->p[P_SDIV]=9;t->p[P_SLEN]=64;
    uint32_t period=seq_div_samples(9), at=period*63u+12345u;
    seq_advance(at);input_on(t,60,100);seq_advance(1234);input_off(t,60);
    uint32_t captured=recording_on(t,&recording[0],period);
    uint32_t gate=(uint32_t)(((uint64_t)recording[0].duration<<(recording[0].owner>>5))*period/RECORD_UNIT);
    int bad=check("slow divisions retain onset/release precision even at the end of a 64-step pattern",
                  captured+3u>=at && captured<=at+3u && gate+6u>=1234u && gate<=1234u);
    raw_replay();seq_advance(captured-1);int exact=!gate_note(t,60);seq_advance(1);exact &= gate_note(t,60);
    seq_advance(gate);exact &= !gate_note(t,60);
    bad+=check("a long pattern replays its late unquantized note at the captured edge",exact);
    return bad;
}
int main(void)
{
    int bad=repeated_hits()+independent_lengths()+raw_loop()+raw_storage()+raw_capacity()+copy_and_edit()+slow_precision()+replace_during_replay()+standalone_and_template();
    printf("%s\n",bad?"RAW RECORDING TEST FAILED":"Raw recording tests passed");return !!bad;
}
