/* SPDX-License-Identifier: GPL-3.0-only */
/* Native Prophet identity, full patch ownership and atomic saves on simulated NOR. */
#define PERSISTENCE_TEST_NO_MAIN
#include "persistence_test.c"
static uint32_t p5_active(track_t *t) {uint32_t n=0;for(uint32_t i=0;i<NVOICE;i++)n+=t->v[i].active&&t->v[i].stage!=4;return n;}
static void p5_tick(track_t *t,uint32_t count){int32_t out[CTL];for(uint32_t k=0;k<count;k++)track_render(t,out,CTL);}
int main(int argc,char **argv)
{
    int bad=0;reset();up_boot();p5_patch_t p,q;
    p5_patch_init(&p);for(uint32_t i=88;i<133;i++)p.raw[i]=(uint8_t)(i*179u);
    int all=1;for(uint32_t slot=0;slot<128;slot++){p.raw[65]=(uint8_t)('A'+slot%26);all &= !native_put(ENGI_PROPHET,slot,(const uint8_t *)&p);}
    bad+=check("all 128 native slots fit without changing historical object IDs",all && OBJ_P5BANK0==OBJ_NATIVEFM0+2 && st_sector(OBJ_P5BANK0,0)==0x89000 && st_sector(OBJ_P5BANK0+4,1)==0x92000);
    p5_user_reset();bad+=check("P128 survives reboot with all opaque bytes and wire metadata",!p5_user_get(127,&q)&&!memcmp(&p,&q,sizeof p));
    uint32_t before=erases;bad+=check("slot 129 is rejected before any erase",native_put(ENGI_PROPHET,128,(const uint8_t *)&p)==1&&erases==before);
    q=p;q.raw[17]=13;fail_after=2;bad+=check("interrupted native save reports failure and rolls RAM back",native_put(ENGI_PROPHET,127,(const uint8_t *)&q)==2&&!memcmp(native_raw(ENGI_PROPHET,127),&p,sizeof p));fail_after=-1;p5_user_reset();
    bad+=check("interrupted native save keeps the last flash commit after reboot",!p5_user_get(127,&q)&&!memcmp(&p,&q,sizeof p));
    song.playing=1;before=erases;bad+=check("native saves during playback cannot erase storage",native_put(ENGI_PROPHET,0,(const uint8_t *)&p)==2&&before==erases);song.playing=0;
    bad+=check("P128 and Prophet INIT favorites use separate persistent masks",favorite_set(USER_NATIVE_P5,127,1)&&favorite_set(ENGI_PROPHET,0,1));p5_user_reset();
    bad+=check("Prophet favorites survive cache reset without moving old identifiers",favorite_has(USER_NATIVE_P5,127)&&favorite_has(ENGI_PROPHET,0)&&USER_GENERAL==16&&USER_NATIVE_FM==17&&USER_NATIVE_CZ==18);
    track_t *t=&trk[0];t->p[P_CHOR]=t->p[P_DLY]=t->p[P_REV]=0;t->p[P_M1AMT]=43;t->step[2]=(step_t){{64},1,ST_NOTE,0,100,0,0,100};
    bad+=check("native load selects engine 19 and keeps track effects, matrix and sequence",!native_load(ENGI_PROPHET,127,0)&&t->eng_req==19&&t->p[P_M1AMT]==43&&t->step[2].note[0]==64);
    static project_t saved,readback;static project_store_t wire;project_capture(&saved);
    bad+=check("project embeds the complete native record without a user-slot dependency",proj_pack(&wire,&saved)&&proj_import(&readback,&wire,sizeof wire)&&!memcmp(&readback.p5[0],&p,sizeof p));
    bad+=check("project restore returns native bytes after changing the current patch",(p5_patch[0].raw[97]=0,!project_restore_runtime(&readback)&&!memcmp(p5_patch_of(t),&p,sizeof p)));
    undo.keep=0;load_begin(t,UNDO_SOUND);q=p;q.raw[97]^=255;p5_patch[0]=q;load_end(t);undo_swap();bad+=check("sound undo restores opaque Prophet bytes",!memcmp(p5_patch_of(t),&p,sizeof p));undo_swap();bad+=check("sound redo restores the second complete Prophet program",!memcmp(p5_patch_of(t),&q,sizeof q));
    bad+=check("erasing P128 clears its star and track origin",!native_put(ENGI_PROPHET,127,0)&&!favorite_has(USER_NATIVE_P5,127)&&!t->user);
    host_preset(t,ENGI_PROPHET,0);p5_patch_t *live=p5_patch_of(t);live->raw[P5_UNISON]=1;live->raw[P5_UNISON_COUNT]=5;p5_track_accept(t);trk_note_on(t,60,100);
    bad+=check("native unison allocates five voices within the shared budget",p5_active(t)==5&&voices_busy()<=VBUDGET);panic_req|=1u;events_block(CTL);
    live->raw[P5_UNISON]=0;live->raw[P5_GLIDE]=120;p5_track_accept(t);trk_note_on(t,48,100);p5_tick(t,2);trk_note_off(t,48);trk_note_on(t,72,100);p5_tick(t,2);voice_t *v=0;for(uint32_t k=0;k<NVOICE;k++)if(t->v[k].gate)v=&t->v[k];
    bad+=check("native poly glide begins between the preceding and target pitches",v&&p5_voice(t,v)->glide_pitch>48*16&&p5_voice(t,v)->glide_pitch<72*16);
    live->raw[P5_GLIDE]=0;p5_tick(t,80);bad+=check("matrix ENV source follows the native amplifier envelope",v&&p5_env_source(t,v)>0);panic_req|=1u;events_block(CTL);
    midi_channel_t control={.bend=8191,.semis=2};live->raw[P5_BEND]=11;midi_expression(t,&control);bad+=check("native maximum bend is twelve semitones",midi_bend_target[0]==12*256);live->raw[P5_BEND]=0;midi_expression(t,&control);bad+=check("native minimum bend is one semitone",midi_bend_target[0]==256);
    template_save();static uint8_t settings_wire[3840];int settings_len=st_load(OBJ_SETTINGS,settings_wire,sizeof settings_wire);
    bad+=check("template with all native patches fits its protected settings sector",settings_len>0&&settings_len<=3840&&tmpl_blob_valid(settings_wire+sizeof(persist_t),settings_len-sizeof(persist_t)));
    p5_patch_t template_patch=*live;live->raw[97]^=255;template_load();
    bad+=check("template load returns all native bytes and empty patterns",!memcmp(p5_patch_of(t),&template_patch,sizeof template_patch)&&seq_is_empty(t));
    trk_note_on(t,60,100);p5_tick(t,2);live=p5_patch_of(t);live->raw[P5_BEND]=11;p5_tick(t,1);
    bad+=check("editing bend range updates a held bend without another MIDI message",midi_bend_target[0]==12*256);
    panic_req|=1u;events_block(CTL);
    if(argc>1){FILE *f=fopen(argv[1],"rb");static uint8_t old[27200];int ok=f&&fread(old,1,sizeof old,f)==sizeof old;if(f)fclose(f);bad+=check("real pre-Prophet runtime imports with legacy engine identities intact",ok&&bank_valid(old,sizeof old)&&proj_scratch.t[0].engine==old[8+68+92]);}
    printf("Prophet integration: %d failure(s)\n",bad);return !!bad;
}
