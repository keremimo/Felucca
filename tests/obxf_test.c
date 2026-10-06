/* OBXF glue, device pages and stores through the actual UI/audio paths. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include <math.h>
static void edit_patch(uint32_t t)
{
    obxf_put_all(t, OXF_ROM[2].v, "EDITED PATCH");
    obxf_put(t, OX_CUT, 1234); obxf_put(t, OX_FOUR, 1); obxf_put(t, OX_XPM, 14);
}
static int lifecycle(void)
{
    int bad=0; int32_t out[CTL];
    ui_power_on(); track_t *t=&trk[0];set_engine_of(t,ENGI_OBXF);apply_preset_to(t,0);
    memcpy(obxf_patch[0],OXF_INIT,sizeof obxf_patch[0]); obxf_pgen[0]++;
    engine_block(t);
    t->p[P_AMODE]=0;t->p[P_VOICE]=V_POLY;t->p[P_GLIDE]=0;
    trk_note_on(t,69,100);
    for(uint32_t b=0;b<FS/CTL;b++)track_render(t,out,CTL);
    uint32_t held=0;for(uint32_t j=0;j<NVOICE;j++)held+=t->v[j].active&&t->v[j].gate;
    bad+=check("OBXF voice stays active while held",held);
    trk_note_off(t,69);
    for(uint32_t b=0;b<4u*FS/CTL;b++)track_render(t,out,CTL);
    uint32_t busy=0;for(uint32_t j=0;j<NVOICE;j++)busy+=t->v[j].active;
    bad+=check("OBXF own envelope frees all released voices",!busy);
    for(uint32_t p=0;p<OXF_NROM;p++) {
        obxf_put_all(0,OXF_ROM[p].v,OXF_ROM[p].name);obxf_block(t);
        trk_note_on(t,48+(p%37),100);
        for(uint32_t b=0;b<100;b++)track_render(t,out,CTL);
        oxf_part_t *part=OXP(t);
        int finite=1;for(uint32_t j=0;j<OXF_NV;j++)for(uint32_t k=0;k<4;k++)finite &= isfinite(part->v[j].pole[k]);
        bad+=check(OXF_ROM[p].name,finite&&obxf_valid(OXF_ROM[p].v));
        trk_all_off(t);memset(t->v,0,sizeof t->v);eng_state_clear(0);memset(obxf_made,0,sizeof obxf_made);
    }
    return bad;
}
/* Frozen FUN8/FBK9 fixture, built from a current snapshot with every extra bank populated. */
static int legacy_bank(void)
{
    static uint8_t current[BANK_STORE_SIZE], old[BANK_SIZE9];
    int bad=0;ui_power_on();
    for(uint32_t t=0;t<NTRK;t++)for(uint32_t b=1;b<NPAT;b++) {
        step_t *st=&pattern_at(t,b)->step[31];
        st->note[0]=127;st->note[1]=60;st->note[2]=1;st->note[3]=0;st->n=4;
        st->time=ST_NOTE;st->flags=3;st->vel=127;st->hit=255;st->acc=129;st->probability=101;
    }
    project_capture(&proj_scratch);bank_pack(current,&proj_scratch,1);
    memcpy(old+8,current+8,PROJ_OXF_OFF);
    memcpy(old+8+PROJ_STORE_V8-4-PROJ_NAME_LEN,current+8+PROJ_NAME_OFF,PROJ_NAME_LEN);
    uint32_t magic=PROJ_MAGIC_V8,size=PROJ_STORE_V8,sum;
    memcpy(old+8,&magic,4);memcpy(old+12,&size,4);sum=proj_hash(old+8,PROJ_STORE_V8-4);memcpy(old+8+PROJ_STORE_V8-4,&sum,4);
    bank_v9=0;uint32_t cs=BANK_EXTRA_OFF,ct=BANK_TIMING_OFF,cf=BANK_FN_OFF;
    bank_v9=1;uint32_t os=BANK_EXTRA_OFF,ot=BANK_TIMING_OFF,of=BANK_FN_OFF;
    memcpy(old+BANK_ACTIVE_OFF,current+8+PROJ_STORE_SIZE,NTRK);
    for(uint32_t j=0;j<NTRK*(NPAT-1)*NSTEP;j++) {
        step_t st;bank_v9=0;bank_step_unpack(&st,current+cs+j*8);uint8_t *p=old+os+j*9;
        memcpy(p,st.note,4);p[4]=st.n|(st.time<<3)|(st.flags<<5);p[5]=st.vel;p[6]=st.hit;p[7]=st.acc;p[8]=st.probability;
    }
    memcpy(old+ot,current+ct,cf+4+NTRK*FM6_NFN-ct);
    magic=BANK_MAGIC9;size=BANK_SIZE9;memcpy(old,&magic,4);memcpy(old+4,&size,4);bank_v9=1;bank_checksum(old);
    bad+=check("legacy FBK9 validates at frozen offsets",bank_valid(old,sizeof old));
    bank_upgrade(old);bad+=check("FBK9 upgrade makes a valid FUN9 bank",bank_valid(old,BANK_STORE_SIZE));
    bank_restore(old);step_t *st=&pattern_at(3,7)->step[31];
    bad+=check("upgrade preserves chord, flags, drum masks and probability",st->n==4&&st->note[0]==127&&st->note[2]==1&&st->flags==3&&st->vel==127&&st->hit==255&&st->acc==129&&st->probability==101);
    bad+=check("legacy bank initializes OBXF patch",!memcmp(proj_scratch.obxf[0],OXF_INIT,sizeof proj_scratch.obxf[0]));
    return bad;
}
int main(void)
{
    int bad=0;uint32_t k,keys;uint16_t v[OX_NP];
    printf("OBXF state %zu bytes; engine union %zu bytes\n",sizeof(oxf_part_t),sizeof eng_state[0]);
    bad+=check("OBXF state fits existing PHYS union",sizeof(oxf_part_t)<=sizeof(phys_slot_t)*PHYS_POLY);
    memcpy(v,OXF_INIT,sizeof v);obxf_shape(v,&k,&keys);
    bad+=check("plain polyphony takes one subvoice per key",k==1&&keys==OXF_NV);
    trk[0].eng_req=ENGI_OBXF;trk[1].eng_req=ENGI_OBXF;obxf_shape(v,&k,&keys);
    bad+=check("several OBXF parts reserve one shared native voice",k==1&&keys==1);
    trk[0].eng_req=trk[1].eng_req=0;
    v[OX_POLY]=0;v[OX_UNI]=1;v[OX_UNIV]=31;obxf_shape(v,&k,&keys);
    bad+=check("unison never exceeds polyphony",k==1&&keys==1);
    v[OX_POLY]=31;obxf_shape(v,&k,&keys);bad+=check("32-way unison fits bounded subvoice pool",k==OXF_NV&&keys==1);
    v[OX_UNIV]=7;obxf_shape(v,&k,&keys);bad+=check("poly-unison shares pool between keys",k*keys<=OXF_NV&&keys<=4);
    v[OX_L1A1]=OXF_ONE;v[OX_L1P1]=1;obxf_shape(v,&k,&keys);
    bad+=check("pitch-modulated patches reserve one native voice",k==1&&keys==1);
    for(uint32_t i=0;i<OX_NP;i++)for(int32_t n=OXF_PD[i].min;n<=OXF_PD[i].max;n++)
        if(oxf_ui(i,oxf_from_ui(i,n))!=n)bad++;
    ui_power_on();set_engine_of(&trk[0],ENGI_OBXF);apply_preset_to(&trk[0],0);edit_patch(0);
    uint16_t saved[OX_NP];memcpy(saved,obxf_patch[0],sizeof saved);
    project_save(0);apply_preset_to(&trk[0],4);project_load(0);obxf_poll();
    bad+=check("project restores edited patch and name, PTCH poll keeps it",!memcmp(saved,obxf_patch[0],sizeof saved)&&!strcmp(obxf_name[0],"EDITED PATCH"));
    template_save();apply_preset_to(&trk[0],5);template_load();obxf_poll();
    bad+=check("template restores full edited patch",!memcmp(saved,obxf_patch[0],sizeof saved));
    undo.keep=0;apply_preset_to(&trk[0],7);undo_swap();obxf_poll();
    bad+=check("sound undo restores edited patch and name",!memcmp(saved,obxf_patch[0],sizeof saved)&&!strcmp(obxf_name[0],"EDITED PATCH"));
    undo_swap();obxf_poll();bad+=check("sound redo restores loaded factory patch",!memcmp(obxf_patch[0],OXF_ROM[7].v,sizeof saved));
    uint32_t covered[OX_NP]={0};
    for(uint32_t i=0;i<NPAGES;i++)if(PAGES[i].scope==SC_OBXF) {
        bad+=check(PAGES[i].title,page_visible(i));
        for(uint32_t c=0;c<4;c++)if(PAGES[i].id[c]<OX_NP) {int16_t *vp;const param_desc_t *d=page_desc(&PAGES[i],c,&vp);bad+=!d||!vp||strlen(d->label)>5;covered[PAGES[i].id[c]]++;}
    }
    for(uint32_t i=0;i<OX_NP;i++)if(i!=OX_HQ)bad+=!covered[i];
    project_capture(&proj_scratch);project_store_t raw;proj_pack(&raw,&proj_scratch);
    raw.raw[PROJ_OXF_OFF]=255;raw.raw[PROJ_OXF_OFF+1]=255;uint32_t sum=proj_hash(raw.raw,sizeof raw-4);memcpy(raw.raw+sizeof raw-4,&sum,4);
    bad+=check("out-of-range patch rejected even with valid project hash",!proj_import(&proj_scratch,&raw,sizeof raw));
    bad+=legacy_bank();bad+=lifecycle();printf("OBXF: %s\n",bad?"FAILED":"passed");return !!bad;
}
