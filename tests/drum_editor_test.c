/* SPDX-License-Identifier: GPL-3.0-only */
/* Both kit controls and event edits through actual DSP/UI/project paths. */
#define RECORDING_NO_MAIN 1
#include "recording_test.c"
static void kit(uint32_t model)
{
    midi_test_reset();set_engine_of(TSEL,ENGI_DRUM);apply_preset_to(TSEL,model==DK_909);TSEL->engine=TSEL->eng_req;panic_req=0;song.master_q12=4096;
}
static void scope(uint32_t sc)
{ui.home=0;for(uint32_t i=0;i<NPAGES;i++)if(PAGES[i].scope==sc){ui.page=(uint8_t)i;break;}ui.force=1;}
static uint32_t active_count(void)
{uint32_t n=0;for(uint32_t i=0;i<NVOICE;i++)n+=TSEL->v[i].active;return n;}
static int32_t audio(uint32_t blocks)
{
    int32_t out[2*CTL],peak=0;
    for(uint32_t b=0;b<blocks;b++){mix_block(out,CTL);for(uint32_t i=0;i<2*CTL;i++){int32_t a=out[i]<0?-out[i]:out[i];if(a>peak)peak=a;}}
    return peak;
}
static int voices909(void)
{
    int bad=0;
    for(uint32_t ins=0;ins<D9_NUM;ins++){
        kit(DK_909);trk_note_on(TSEL,DRUM_SOUND_NOTE[ins],110);
        int32_t peak=audio(FS/CTL);
        audio(FS*5/CTL);
        bad+=check("each sample-free 909 voice sounds, stays bounded and frees itself",peak>30 && peak<32768 && !active_count());
    }
    kit(DK_909);trk_note_on(TSEL,46,110);audio(FS/10/CTL);trk_note_on(TSEL,42,110);audio(FS/20/CTL);
    drum909_part_t *k=drum909_of(TSEL);
    bad+=check("909 closed hat chokes open hat without ending the closed hit",k && !k->d.smp[1].playing && k->d.smp[0].playing);
    kit(DK_909);for(uint32_t i=0;i<D9_NUM;i++)trk_note_on(TSEL,DRUM_SOUND_NOTE[i],127);
    bad+=check("909 respects the eight-voice shared budget",active_count()<=8 && audio(128)>30);
    kit(DK_909);for(uint32_t tr=0;tr<NTRK;tr++){set_engine_of(&trk[tr],ENGI_DRUM);apply_preset_to(&trk[tr],1);trk[tr].engine=trk[tr].eng_req;trk_note_on(&trk[tr],36,110);trk_note_on(&trk[tr],38,110);}panic_req=0;
    uint32_t total=0;int32_t peak=audio(128);for(uint32_t tr=0;tr<NTRK;tr++)for(uint32_t v=0;v<NVOICE;v++)total+=trk[tr].v[v].active;
    bad+=check("four independent 909 parts share the eight-voice budget and render together",total==8 && peak>30 && peak<32768);
    return bad;
}
static int controls(void)
{
    int bad=0;kit(DK_808);scope(SC_DRUM);ui.drum_sound=D9_SD;
    trk_note_on(TSEL,38,100);drum_lane_t *k=drum_kit_of(TSEL);int32_t base=k[DV_SNARE].r.f;
    drum_sound_edit(1,12);trk_note_on(TSEL,38,100);
    bad+=check("808 sound tune transposes the same snare by an octave",k[DV_SNARE].r.f>=base*2-2 && k[DV_SNARE].r.f<=base*2+2 && TSEL->v[0].note==38);
    drum_event_pitch=-12;trk_note_on(TSEL,38,100);drum_event_pitch=0;
    bad+=check("individual pitch offsets combine with sound tune without changing instrument",k[DV_SNARE].r.f==base && TSEL->v[0].note==38 && drum_patch[0].c[D9_SD][0]==12);
    kit(DK_909);drum_patch[0].c[D9_SD][1]=-32;trk_note_on(TSEL,38,100);float short_decay=drum909_of(TSEL)->d.bt[D9_SD].decay;
    drum_patch[0].c[D9_SD][1]=32;trk_note_on(TSEL,38,100);
    bad+=check("909 snare decay survives the source engine's trigger pinning",drum909_of(TSEL)->d.bt[D9_SD].decay>short_decay*3.0f);
    for(uint32_t model=DK_808;model<=DK_909;model++){
        kit(model);drum_event_length=FS/20;trk_note_on(TSEL,36,110);drum_event_length=0;audio(FS/10/CTL);
        bad+=check("edited hit length limits the sounding voice independently of key release",!active_count());
        trk_note_on(TSEL,36,110);audio(FS/10/CTL);
        bad+=check("ordinary one-shot hits retain their natural decay",active_count()==1);
    }
    kit(DK_909);scope(SC_DRUM);ui.drum_sound=D9_LT;page_over=0;
    for(uint32_t i=0;i<NPAGES;i++)if(PAGES[i].scope==SC_DRUM && PAGES[i].id[1]==4)ui.page=i;
    drum_sound_edit(1,-127);
    bad+=check("low/mid/high tom levels remain independent",drum_patch[0].c[D9_LT][3]==127 && !drum_patch[0].c[D9_MT][3] && !drum_patch[0].c[D9_HT][3]);
    return bad;
}
static int edits_and_store(void)
{
    int bad=0;kit(DK_909);scope(SC_DRUMHIT);ui.drum_sound=D9_BD;cursor_set(3);
    TSEL->step[3]=(step_t){{0},0,ST_NOTE,0,100,3,2,0};
    drum_hit_edit(2,7);uint32_t i=drum_hit_selected();recorded_note_t hit=recording[i];
    bad+=check("grid pitch edit retains the kick, snare and their velocities",i<RECORD_MAX && hit.note==36 && hit.pitch==7 && !drum_patch[0].c[D9_BD][0] && TSEL->step[3].hit==3);
    step_history_end();step_history_finish();drum_hit_edit(3,-8);step_history_end();step_history_finish();
    bad+=check("hit length is an independent half step",recording[i].length && recording[i].duration*(1u<<(recording[i].owner>>5))==RECORD_UNIT/2);
    bad+=check("hit undo restores pitch and natural length",step_history_apply(0) && recording[i].pitch==7 && !recording[i].length);
    bad+=check("hit redo restores the length override",step_history_apply(1) && recording[i].length);
    settings_preview=1;notes_preview();audition_tick(1);
    bad+=check("hit preview retains edited pitch and duration",TSEL->v[0].note==36 && TSEL->v[0].s[4]==7 && TSEL->v[0].s[5]);audition_stop();settings_preview=0;
    drum_hit_delete();step_history_end();step_history_finish();
    bad+=check("deleting one edited hit retains its neighboring manual snare",!recording[i].vel && TSEL->step[3].hit==2);
    bad+=check("hit deletion undo restores the original event and neighbors",step_history_apply(0) && recording[i].pitch==7 && recording[i].length && TSEL->step[3].hit==3);
    recording_restore_note(TSEL,1,(recorded_note_t){32000,32768,36,80,(uint8_t)recording_owner(TSEL),3,-4,1});
    drum_hit_jog(1);uint32_t second=drum_hit_selected();drum_hit_edit(2,1);
    bad+=check("SELECT can edit a repeated hit without changing the first hit",second==1 && recording[1].pitch==-3 && recording[i].pitch==7);
    step_history_end();step_history_finish();recording_remove(TSEL,1);ui.note_pick=(uint16_t)(i+1);ui.note_identity=recording[i];ui.note_generation=recording_generation;
    drum_patch[0].c[D9_BD][0]=5;drum_patch[0].c[D9_LT][3]=19;
    project_capture(&proj_scratch);project_store_t wire;project_t decoded;
    int ok=proj_pack(&wire,&proj_scratch) && proj_import(&decoded,&wire,sizeof wire);
    bad+=check("project roundtrip keeps sound settings, instrument, pitch and length",ok && decoded.drum[0].c[D9_BD][0]==5 && decoded.drum[0].c[D9_LT][3]==19 && decoded.recording[0].note==36 && decoded.recording[0].pitch==7 && decoded.recording[0].length);
    bad+=check("new bank schema fits existing atomic flash copies",BANK_STORE_SIZE<=27904u && bank_pack(proj_wire_u.raw,&proj_scratch,1) && bank_valid(proj_wire_u.raw,BANK_STORE_SIZE));
    TSEL->engine=ENGI_DRUM;TSEL->eng_req=ENGI_DRUM;song.playing=0;
    bad+=check("general drum preset stores its own sound controls",up_store(0,"OWN 909")==3 && (memset(drum_patch,0,sizeof drum_patch),!up_load(0)) && drum_patch[0].c[D9_BD][0]==5 && drum_patch[0].c[D9_LT][3]==19);
    template_save();memset(drum_patch,0,sizeof drum_patch);template_load();
    bad+=check("template restores the edited drum kit",drum_patch[0].c[D9_BD][0]==5 && drum_patch[0].c[D9_LT][3]==19);
    return bad;
}
static int corners909(void)
{
    int bad=0;kit(DK_909);TSEL->p[P_E2]=0;trk_note_on(TSEL,38,100);float dark=drum909_of(TSEL)->d.bt[D9_SD].noise_decay;TSEL->p[P_E2]=127;trk_note_on(TSEL,38,100);
    bad+=check("909 common tone changes the snare's noise tail",drum909_of(TSEL)->d.bt[D9_SD].noise_decay>dark*10.0f);
    kit(DK_909);TSEL->p[P_E5]=0;trk_note_on(TSEL,36,64);float noacc=drum909_of(TSEL)->d.bt[D9_BD].out_gain;TSEL->p[P_E5]=127;trk_note_on(TSEL,36,64);
    bad+=check("909 common accent changes velocity response",drum909_of(TSEL)->d.bt[D9_BD].out_gain!=noacc);
    kit(DK_909);trk_note_on(TSEL,36,100);float punch=drum909_of(TSEL)->d.bt[D9_BD].bd_df;TSEL->p[P_E6]=1;trk_note_on(TSEL,36,100);
    bad+=check("909 ROUND kick softens its pitch transient",drum909_of(TSEL)->d.bt[D9_BD].bd_df<punch);
    for(uint32_t corner=0;corner<2;corner++)for(uint32_t ins=0;ins<D9_NUM;ins++){
        kit(DK_909);for(uint32_t p=P_E1;p<=P_E7;p++)TSEL->p[p]=p==P_E6?(int16_t)corner:corner?127:0;
        int8_t *c=drum_patch[0].c[ins];c[0]=corner?24:-24;c[1]=c[2]=corner?63:-64;
        drum_event_pitch=corner?24:-24;trk_note_on(TSEL,DRUM_SOUND_NOTE[ins],127);drum_event_pitch=0;
        drum909_part_t *k=drum909_of(TSEL);int finite=1;
        for(uint32_t b=0;b<FS*2/CTL;b++){float f[CTL]={0};float *nz=d9_noise_block(&k->d,CTL);drum909_render_voice(&k->d,ins,nz,f,CTL);for(uint32_t j=0;j<CTL;j++)finite&=isfinite(f[j]) && f[j]>-100.0f && f[j]<100.0f;}
        bad+=check("909 native float voices remain finite at pitch/decay/character corners",finite);
    }
    return bad;
}
static int legacy_current(void)
{
    int bad=0;kit(DK_808);TSEL->step[3]=(step_t){{0},0,ST_NOTE,SF_RECORDED,100,1,0,0};
    recording[0]=(recorded_note_t){43217,32769,36,110,33,3,12,1};recording_reindex();project_capture(&proj_scratch);
    project_store_t wire;proj_pack(&wire,&proj_scratch);uint8_t old[PROJ_STORE_V16];
    memcpy(old,wire.raw,sizeof old);memcpy(old+sizeof old-4u-PROJ_NAME_LEN,wire.raw+PROJ_NAME_OFF,PROJ_NAME_LEN);
    memcpy(old+PROJ_REC_OFF,&recording[0],8u);memset(old+PROJ_REC_OFF+8u,0,(RECORD_MAX-1u)*8u);
    uint32_t magic=PROJ_MAGIC_V16,size=sizeof old,sum;memcpy(old,&magic,4);memcpy(old+4,&size,4);sum=proj_hash(old,sizeof old-4);memcpy(old+sizeof old-4,&sum,4);
    project_t decoded;
    bad+=check("previous FUN16 saves retain exact timing and initialize new hit controls",proj_import(&decoded,old,sizeof old) && decoded.recording[0].on==43217 && decoded.recording[0].duration==32769 && decoded.recording[0].owner==33 && !decoded.recording[0].pitch && !decoded.recording[0].length && !memcmp(decoded.drum,(drum_patch_t[NTRK]){0},sizeof decoded.drum));
    bank_pack(proj_wire_u.raw,&proj_scratch,1);uint8_t bank[BANK_STORE_SIZE]={0};memcpy(bank+8,old,sizeof old);
    frozen_bank_tail(bank+8+sizeof old,proj_wire_u.raw+8+PROJ_STORE_SIZE,BANK_SIZE_J-8-sizeof old);
    magic=BANK_MAGIC_J;size=BANK_SIZE_J;memcpy(bank,&magic,4);memcpy(bank+4,&size,4);sum=proj_hash(bank,size-4);memcpy(bank+size-4,&sum,4);
    int ok=bank_valid(bank,size);if(ok){bank_upgrade(bank);ok=bank_valid(bank,BANK_STORE_SIZE);}
    bad+=check("previous FBKJ banks upgrade within the same flash allocation",ok && proj_scratch.recording[0].on==43217 && proj_scratch.recording[0].duration==32769 && !proj_scratch.recording[0].pitch);
    return bad;
}
static int capacity_and_controls(void)
{
    int bad=0;kit(DK_808);
    project_capture(&proj_scratch);
    for(uint32_t i=0;i<RECORD_MAX;i++)proj_scratch.recording[i]=(recorded_note_t){(uint16_t)(i*61u),(uint16_t)(65535u-i),(uint8_t)(36+i%48),(uint8_t)(1+i%127),(uint8_t)((i%32)|((i%8)<<5)),(uint8_t)(i%64),(int8_t)((int32_t)(i%49)-24),(uint8_t)(i&1)};
    for(uint32_t t=0;t<NTRK;t++)for(uint32_t i=0;i<16;i++){int8_t *c=proj_scratch.drum[t].c[i];c[0]=(int8_t)((i+t)%2?24:-24);c[1]=(int8_t)((i+t)%2?63:-64);c[2]=-c[1]-1;c[3]=(int8_t)(i%2?127:0);}
    project_store_t wire;project_t decoded;
    int ok=proj_pack(&wire,&proj_scratch) && proj_import(&decoded,&wire,sizeof wire);uint32_t at=0;
    if(ok)for(uint32_t owner=0;owner<32;owner++)for(uint32_t i=0;i<RECORD_MAX;i++)if((proj_scratch.recording[i].owner&31u)==owner)ok&=!memcmp(&decoded.recording[at++],&proj_scratch.recording[i],sizeof(recorded_note_t));
    bad+=check("all 1024 hits retain full timing, instrument, velocity, owner, pitch and length",ok && at==RECORD_MAX && !memcmp(decoded.drum,proj_scratch.drum,sizeof decoded.drum));
    memset(proj_scratch.recording,0,sizeof proj_scratch.recording);
    for(uint32_t i=0;i<RECORD_MAX;i++)proj_scratch.recording[i]=(recorded_note_t){0,65535,36,127,255,63,24,1};
    bad+=check("one bank can still hold all 1024 events",proj_pack(&wire,&proj_scratch) && proj_import(&decoded,&wire,sizeof wire) && decoded.recording[1023].owner==255 && decoded.recording[1023].pitch==24);
    for(uint32_t sound=11;sound<16;sound++){
        kit(DK_808);drum_patch[0].c[sound][0]=12;trk_note_on(TSEL,drum_sound_note(TSEL,sound),100);drum_lane_t *k=drum_kit_of(TSEL);
        bad+=check("808 congas, clave and maraca have their own sound controls",drum_sound_of(TSEL,drum_sound_note(TSEL,sound))==sound && k[drum_lane(drum_sound_note(TSEL,sound))].r.pitch==192 && audio(64)>30);
    }
    return bad;
}
int main(void)
{
    int bad=voices909()+controls()+edits_and_store()+capacity_and_controls()+legacy_current()+corners909();
    printf("DRUM EDITOR: %s\n",bad?"FAILED":"all passed");return bad!=0;
}
