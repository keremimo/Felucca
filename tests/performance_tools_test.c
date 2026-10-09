/* SPDX-License-Identifier: GPL-3.0-only */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include <assert.h>
#include "frozen_project.h"

static void reset_tools(void)
{
    seq_stop(); momentary_restore(); ui_power_on(); recording_reset(); memset(&midi_clock,0,sizeof midi_clock); song.master_q12=4096;
    settings_click=settings_countin=settings_preview=settings_chord_add=0;
    settings_click_level=1; memset(cin_n,0,sizeof cin_n); clk.env=click_req=0;
}
static void advance(uint32_t samples)
{
    while(samples){uint32_t n=samples>CTL?CTL:samples;events_block(n);samples-=n;}
}
static void page_named(const char *name)
{
    for(uint32_t i=0;i<NPAGES;i++)if(!strcmp(PAGES[i].title,name)){ui.home=0;ui.page=i;return;}
    assert(0);
}
static void test_recording_tools(void)
{
    reset_tools();settings_countin=1;press(B_REC);assert(song.rec==1 && !transport_req && !song.playing);
    press(B_REC);assert(!song.rec && !transport_req);press(B_REC);press(B_PLAY);events_block(0);
    assert(seq_counting() && !song.playing && transport_busy() && click_req==2);
    uint32_t beat=beat_samples();advance(3*beat+beat/2);assert(cin_left==1 && !song.playing);
    input_on(TSEL,60,100);assert(recording_head[0]>=RECORD_MAX);
    advance(beat-beat/2-1);assert(!song.playing);
    advance(1);assert(song.playing && !seq_counting() && TSEL->seq_idx==0);
    assert(TSEL->rh_n==1);input_off(TSEL,60);seq_stop();
    transport_req=1;events_block(0);assert(seq_counting());transport_req=2;events_block(1);assert(!seq_counting() && !song.playing);
    settings_countin=2;transport_req=1;events_block(0);advance(8*beat-1);assert(seq_counting());advance(1);assert(song.playing);
    seq_stop();song.g[G_CLOCK]=1;transport_req=1;events_block(0);assert(!seq_counting() && song.playing);seq_stop();
    reset_tools();settings_click=CLICK_ON;seq_start();assert(click_req==2);
    int32_t audio[2*CTL]={0};click_render(audio,CTL);int energy=0;
    for(uint32_t i=0;i<2*CTL;i++)energy|=audio[i];assert(energy);
    seq_advance(beat_samples());assert(click_req==1);
    seq_stop();settings_click=CLICK_OFF;seq_start();assert(!click_req);seq_stop();
}
static void test_click_volume(void)
{
    reset_tools();
    int32_t reference[2*CTL], actual[2*CTL];
    uint32_t previous_peak=0;
    for(uint32_t level=0;level<3;level++) {
        settings_click_level=level;song.master_q12=4096;
        memset(reference,0,sizeof reference);click_req=2;click_render(reference,CTL);
        uint32_t peak=0;
        for(uint32_t i=0;i<2*CTL;i++) { uint32_t a=(uint32_t)abs(reference[i]);if(a>peak)peak=a; }
        assert(peak>previous_peak);previous_peak=peak;
        for(uint32_t master=0;master<4096;master+=1024) {
            song.master_q12=master;memset(actual,0,sizeof actual);click_req=2;click_render(actual,CTL);
            assert(!memcmp(reference,actual,sizeof actual));
        }
    }
    /* The same independent level is used when only count-in requests the click. */
    seq_stop();settings_click=CLICK_OFF;settings_countin=1;song.rec=1;song.master_q12=0;
    transport_req=1;events_block(0);assert(seq_counting() && click_req==2);
    memset(actual,0,sizeof actual);click_render(actual,CTL);
    assert(!memcmp(reference,actual,sizeof actual));seq_stop();
}
static void test_editing_tools(void)
{
    reset_tools();go_page(GR_ROLL);TSEL->p[P_VOICE]=V_POLY;settings_chord_add=1;
    uint8_t a=60,b=64;seq_entry_notes(&a,1);seq_entry_finish();assert(ui.cursor==0 && TSEL->step[0].n==1);
    seq_entry_notes(&b,1);seq_entry_finish();assert(TSEL->step[0].n==2);
    seq_entry_notes(&a,1);seq_entry_finish();assert(TSEL->step[0].n==1 && TSEL->step[0].note[0]==64);
    settings_chord_add=0;seq_entry_notes(&a,1);seq_entry_finish();assert(ui.cursor==1);
    settings_preview=1;cursor_set(0);assert(audition_request);events_block(1);assert(audition_active && recording_head[0]>=RECORD_MAX);
    advance(FS/6);assert(!audition_active);input_on(TSEL,60,90);cursor_set(0);events_block(1);
    assert(!audition_n);advance(FS/6);assert(live_held[0][60>>5]&(1u<<(60&31u)));input_off(TSEL,60);
    reset_tools();ui.home=1;uint32_t param=ENGINES[TSEL->eng_req]->knob[0];int16_t before=TSEL->p[param];btn_down(B_LFO);turn(EN_K1,5);
    assert(momentary.active && TSEL->p[param]!=before);fm1_in.buttons&=~(1u<<panel.btn[B_LFO]);frame();
    assert(!momentary.active && TSEL->p[param]==before && ui.home);
    btn_down(B_LFO);turn(EN_K1,-4);int16_t kept=TSEL->p[param];press(B_OCTUP);
    fm1_in.buttons&=~(1u<<panel.btn[B_LFO]);frame();assert(TSEL->p[param]==kept && song.octave==0);
    set_engine_of(TSEL,ENGI_FM6);TSEL->engine=TSEL->eng_req;page_named("ALGO");
    uint8_t patch[FP_SIZE+1];memcpy(patch,fm6_patch[0],sizeof patch);
    btn_down(B_LFO);turn(EN_K1,1);assert(momentary.active);fm1_in.buttons=0;frame();assert(!memcmp(patch,fm6_patch[0],sizeof patch));
    set_engine_of(TSEL,ENGI_CZ);TSEL->engine=TSEL->eng_req;
    page_named("CZ LINE");
    cz_patch_t cz=cz_patch[0];btn_down(B_LFO);turn(EN_K1,1);assert(momentary.active);fm1_in.buttons=0;frame();assert(!memcmp(&cz,cz_patch,sizeof cz));
    set_engine_of(TSEL,ENGI_PROPHET);TSEL->engine=TSEL->eng_req;page_named("P5 FILTER");
    p5_patch_t p5=*p5_patch_of(TSEL);TSEL->p[P_PRIO]=2;btn_down(B_LFO);turn(EN_K1,-1);assert(momentary.active);fm1_in.buttons=0;frame();
    assert(!memcmp(&p5,p5_patch_of(TSEL),sizeof p5) && TSEL->p[P_PRIO]==2);
}
static void test_midi_and_levels(void)
{
    reset_tools();midi_event(0xb0,0,7,127);assert(TSEL->p[P_LEVEL]==127);
    midi_event(0xb0,0,10,64);assert(TSEL->p[P_PAN]==0);
    midi_event(0xb0,0,74,0);assert(TSEL->p[P_E4]==0);
    midi_event(0xb0,0,71,127);assert(TSEL->p[P_E5]==127);
    song.g[G_ROUTE]=0;midi_event(0xb0,1,91,88);assert(trk[1].p[P_REV]==88 && trk[0].p[P_REV]!=88);
    set_engine_of(TSEL,ENGI_PROPHET);TSEL->engine=TSEL->eng_req;
    midi_event(0xb0,0,73,70);assert(p5_patch_of(TSEL)->raw[P5_ATTACK_AMP]==70);
    reset_tools();song.rec=1;seq_start();seq_advance(0);midi_event(0xb0,0,7,17);
    assert(motion.count==1 && motion.event[0].param==P_LEVEL && motion.event[0].value==17);seq_stop();
    reset_tools();track_select(3);TSEL->p[P_LN0]=0;trk_note_on(TSEL,36,127);
    int32_t out[2*CTL];mix_block(out,CTL);for(uint32_t i=0;i<2*CTL;i++)assert(out[i]==0);
    TSEL->p[P_LN0]=127;mix_block(out,CTL);int energy=0;for(uint32_t i=0;i<2*CTL;i++)energy|=out[i];assert(energy);
}
static void test_compatibility(void)
{
    reset_tools();settings_click=2;settings_click_level=0;settings_countin=2;settings_preview=settings_chord_add=1;
    persist_t p={0};settings_export(&p);settings_click=settings_countin=settings_preview=settings_chord_add=0;settings_click_level=1;
    assert(settings_import(&p,sizeof p));assert(settings_click==2 && !settings_click_level && settings_countin==2 && settings_preview && settings_chord_add);
    p.zoom=1;assert(settings_import(&p,sizeof p));assert(!settings_click && settings_click_level==1 && !settings_countin && !settings_preview && !settings_chord_add);
    project_t q,back;project_store_t current,old;project_capture(&q);q.t[0].p[P_E4]=53;
    for(uint32_t t=0;t<NTRK;t++)q.t[t].p[P_DLY]=77;   /* delay sends and their automation (a FUN16's are kept) */
    q.g[G_DTYPE]=3;q.g[G_DWEAR]=9;q.g[G_DTIME]=2;q.motion.count=1;q.motion.event[0]=(motion_event_t){0,P_DLY,100};q.sum=proj_sum(&q);
    assert(proj_pack(&current,&q));
    assert(proj_import(&back,&current,PROJ_STORE_SIZE) && back.t[1].p[P_DLY]==77 && back.g[G_DTYPE]==3 && back.g[G_DWEAR]==9 &&
           back.motion.count==1 && back.motion.event[0].value==100);
    memcpy(old.raw,current.raw,68);uint32_t src=68,dst=68;
    for(uint32_t t=0;t<NTRK;t++){
        memcpy(old.raw+dst,current.raw+src,84);src+=P_E0;dst+=84;
        memcpy(old.raw+dst,current.raw+src,8+2+NSTEP*9);src+=8+2+NSTEP*9;dst+=8+2+NSTEP*9;
    }
    memcpy(old.raw+dst,current.raw+src,PROJ_STORE_SIZE-src-4);
    uint32_t magic=PROJ_MAGIC_V14,size=PROJ_STORE_V14;memcpy(old.raw,&magic,4);memcpy(old.raw+4,&size,4);old.raw[66]=92;
    uint32_t hash=proj_hash(old.raw,size-4);memcpy(old.raw+size-4,&hash,4);
    assert(proj_import(&back,&old,size));assert(back.t[0].p[P_E4]==53);
    for(uint32_t t=0;t<NTRK;t++)assert(back.t[t].p[P_DLY]==0);   /* before the delay came back: its sends 0, */
    assert(back.g[G_DTYPE]==GP[G_DTYPE].def && back.g[G_DWEAR]==GP[G_DWEAR].def && back.g[G_DTIME]==GP[G_DTIME].def);   /* x0x's defaults */
    assert(back.motion.count==1 && back.motion.event[0].param==P_DLY && back.motion.event[0].value==0);   /* (its place kept) */
    for(uint32_t t=0;t<NTRK;t++)for(uint32_t lane=P_LN0;lane<=P_LN7;lane++)assert(back.t[t].p[lane]==127);
    assert(!memcmp(q.p5,back.p5,sizeof q.p5));assert(!memcmp(q.fm6,back.fm6,sizeof q.fm6));
    static uint8_t bank[BANK_STORE_SIZE],legacy[BANK_STORE_SIZE];assert(bank_pack(bank,&q,0));
    memcpy(legacy,bank,8);frozen_project92(legacy+8,bank+8,PROJ_STORE_V14-4);
    magic=PROJ_MAGIC_V14;size=PROJ_STORE_V14;memcpy(legacy+8,&magic,4);memcpy(legacy+12,&size,4);
    hash=proj_hash(legacy+8,size-4);memcpy(legacy+8+size-4,&hash,4);
    frozen_bank_tail(legacy+8+size,bank+8+PROJ_STORE_SIZE,BANK_SIZE_H-8-size-4);
    magic=BANK_MAGIC_H;size=BANK_SIZE_H;memcpy(legacy,&magic,4);memcpy(legacy+4,&size,4);
    hash=proj_hash(legacy,size-4);memcpy(legacy+size-4,&hash,4);
    assert(bank_valid(legacy,size));bank_upgrade(legacy);assert(bank_valid(legacy,BANK_STORE_SIZE));
    assert(proj_scratch.t[0].p[P_E4]==53 && proj_scratch.t[0].p[P_LN0]==127 && !proj_scratch.t[2].p[P_DLY]);
    for(uint32_t t=0;t<NTRK;t++)trk[t].p[P_DLY]=77;
    template_save();tmpl_t before=tmpl;uint8_t old_template[TMPL_SIZE_C];
    frozen_template92(old_template,(const uint8_t *)&tmpl,TMPL_SIZE_C-8);
    magic=TMPL_MAGIC_C;size=TMPL_SIZE_C;memcpy(old_template+size-8,&size,4);memcpy(old_template+size-4,&magic,4);
    assert(tmpl_take(old_template,sizeof old_template));
    assert(!memcmp(before.p5,tmpl.p5,sizeof tmpl.p5) && !memcmp(before.cz,tmpl.cz,sizeof tmpl.cz));
    for(uint32_t tr=0;tr<NTRK;tr++)for(uint32_t lane=P_LN0;lane<=P_LN7;lane++)assert(tmpl.t[tr].p[lane]==127);
    for(uint32_t tr=0;tr<NTRK;tr++)assert(before.t[tr].p[P_DLY]==77 && !tmpl.t[tr].p[P_DLY]);
    {   /* a user preset: a record of today's keeps its delay send, one of before (np 100) has none */
        up_rec_t r;int16_t v[P_COUNT];memset(&r,0,sizeof r);r.used=UP_USED;r.ver=UP_VER_GRID;r.engine=0;r.np=P_COUNT;memcpy(r.name,"DLY",3);
        for(uint32_t i=0;i<P_COUNT;i++)up_set_value(&r,i,param_desc_of(0,i)->def);
        up_set_value(&r,P_DLY,77);up_values(&r,v);assert(v[P_DLY]==77);
        r.np=100;up_values(&r,v);assert(v[P_DLY]==0);
    }
    {   /* a factory preset: no delay send (its sends predate the delay coming back) */
        TSEL->eng_req=0;TSEL->p[P_DLY]=77;apply_preset_to(TSEL,8);assert(TSEL->p[P_DLY]==0 && TSEL->p[P_REV]>0);
    }
}
int main(void)
{
    test_recording_tools();test_click_volume();test_editing_tools();test_midi_and_levels();test_compatibility();
    puts("performance tools: count-in, click, additive entry, audition, momentary restore/keep, MIDI, drum levels, FUN14 migration and the revived delay's zeroed old sends passed");return 0;
}
