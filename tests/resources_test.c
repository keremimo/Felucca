/* SPDX-License-Identifier: GPL-3.0-only */
/* Resource lifetimes under cache eviction, audio transitions and exhaustion. */
#define PERSISTENCE_TEST_NO_MAIN
#include "persistence_test.c"

static void audio_resources_reset(void)
{
    upf_release();memset(resource,0,sizeof resource);memset(sl_buf,0,sizeof sl_buf);
    cho_buf=rev_comb=perf_audio=0;rev_memory=0;sl_lent=0;
    memset(&fx,0,sizeof fx);memset(sl,0,sizeof sl);
    memset(rev_hold_comb,0,sizeof rev_hold_comb);memset(rev_hold_ap,0,sizeof rev_hold_ap);
    rev_hold_dc=0;cho_idle=rev_scan=0;pf.mode=BM_NONE;pf.src=pf.next=PF_N;pf.w=0;
}
static int cache_tests(void)
{
    int bad=0;uint8_t fm[FM6_PACKED],cz[CZ_BYTES];char name[13];
    reset();audio_resources_reset();up_boot();fm6_factory(3,fm);cz_patch_init(cz);
    for(uint32_t b=0;b<4u;b++){fm[118]=(uint8_t)('A'+b);bad+=check("native FM cache writes across bank evictions",!native_put(ENGI_FM6,b*16u,fm));}
    for(uint32_t b=0;b<8u;b++){cz[128]=(uint8_t)('A'+b);bad+=check("native CZ cache writes across bank evictions",!native_put(ENGI_CZ,b*16u,cz));}
    for(uint32_t b=0;b<4u;b++){set_engine_of(TSEL,0);TSEL->p[P_E0]=(int16_t)b;bad+=check("general cache writes across bank evictions",!up_store(b*16u,"BANK SOUND"));}
    up_boot();uint32_t reads=preset_reads;
    for(uint32_t k=0;k<64u;k++){up_used(k);up_engine(k);up_name(k,name);native_used(ENGI_FM6,k);native_name(ENGI_FM6,k,name);}
    for(uint32_t k=0;k<128u;k++){native_used(ENGI_CZ,k);native_name(ENGI_CZ,k,name);}
    bad+=check("browsing every preset name/slot does no flash reads",preset_reads==reads);
    for(uint32_t b=0;b<4u;b++)bad+=check("FM cache reload preserves each independent patch",native_raw(ENGI_FM6,b*16u)[118]=='A'+b);
    for(uint32_t b=0;b<8u;b++)bad+=check("CZ cache reload preserves each independent patch",native_raw(ENGI_CZ,b*16u)[128]=='A'+b);
    for(uint32_t b=0;b<4u;b++)bad+=check("general cache reload preserves parameters",up_value(up_rec(b*16u),P_E0)==(int16_t)b);
    bad+=check("legacy conversion handles are released after boot",!resource[RES_LEGACY0].size && !resource[RES_LEGACY1].size);
    bad+=check("resident bank payloads are bounded to one bank per type",sizeof up_cache+sizeof native_fm_cache+sizeof native_cz_cache==8208u);
    flash_ok=0;
    bad+=check("missing flash refuses mutations without evicting committed indexes",native_put(ENGI_FM6,0,fm)==2 && native_put(ENGI_CZ,0,cz)==2 && up_store(0,"NO FLASH")==2 && native_used(ENGI_FM6,0) && native_used(ENGI_CZ,0) && up_used(0));
    flash_ok=1;
    bad+=check("failed missing-flash writes preserve stored patches",native_raw(ENGI_FM6,0)[118]=='A' && native_raw(ENGI_CZ,0)[128]=='A' && up_value(up_rec(0),P_E0)==0);
    resource_get(RES_PERFORM,RESOURCE_MAIN_CAPACITY);
    for(uint32_t b=0;b<RESOURCE_BANKS-1u;b++)if(b!=RESOURCE_CACHE_BANK || RESOURCE_CACHE_AVAILABLE)
        resource_get(RES_ENGINE0+b,(uint32_t)((uintptr_t)resource_banks[b].end-(uintptr_t)resource_banks[b].start));
    bad+=check("legacy scratch exhaustion returns a safe missing bank",!upf_bank(0) && !upf_valid(upf_bank(0)));
    resource_release(RES_PERFORM);
    for(uint32_t b=0;b<RESOURCE_BANKS-1u;b++)resource_release(RES_ENGINE0+b);
    return bad;
}
static int audio_tests(void)
{
    int bad=0;int32_t in[CTL]={0},out[CTL],c[CTL]={0},d[CTL]={0},r[CTL]={0};
    audio_resources_reset();host_tracks_init();
    const uint8_t engines[4]={0,ENGI_FM6,3,ENGI_DRUM};uint32_t expected=sizeof(fm6_part_t)+sizeof(drum_lane_t)*DV_NLANE;
    for(uint32_t k=0;k<4u;k++){trk[k].engine=engines[k];eng_state_prepare(&trk[k]);}
    bad+=check("default engine setup uses 8164 bytes rather than 51552",resource_used()==expected && expected==8164u);
    for(uint32_t cycle=0;cycle<2048u;cycle++){
        uint32_t k=cycle%NPART,e=eng_vis(cycle/NPART);
        eng_state_clear(k);trk[k].engine=(uint8_t)e;
        if(!eng_state_prepare(&trk[k])){bad+=check("engine switching does not fragment away capacity",0);break;}
    }
    uint32_t largest=0, largest_engine=0;
    for(uint32_t j=0;j<NENG_SHOWN;j++) {
        uint32_t e=eng_vis(j), size=eng_state_size(e);
        if(size>largest){largest=size;largest_engine=e;}
    }
    uint32_t maximum=largest*NPART;
    for(uint32_t k=0;k<4u;k++){
        eng_state_clear(k);trk[k].engine=(uint8_t)largest_engine;eng_state_prepare(&trk[k]);
        trk[k].p[P_SLCR]=SL_GATE;slicer_track(&trk[k],in,CTL);
    }
    bad+=check("GATE reserves no recording buffers",resource_used()==maximum);
    for(uint32_t k=0;k<4u;k++){trk[k].p[P_SLCR]=SL_STUT;slicer_track(&trk[k],in,CTL);}
    bad+=check("four largest remaining engine tracks and four stutters fit together",resource_used()==maximum+32768u);
    c[0]=d[0]=r[0]=12000;fx_buses(c,d,r,out,CTL);c[0]=d[0]=r[0]=0;
    bad+=check("maximum engines, stutter and the three FX fit simultaneously",resource_used()==maximum+32768u+CHO_LEN*2u+REV_MEMORY_BYTES+DL_N*2u);
    bad+=check("that and the legacy conversion workspace stay within the linker's 152 KiB budget (app.ld)",
               maximum+32768u+CHO_LEN*2u+REV_MEMORY_BYTES+DL_N*2u+2u*sizeof(upf_t)<=152u*1024u);
    perf_buf_start(PF_FRZ);
    int released=1;for(uint32_t k=0;k<4u;k++)released&=!resource[RES_SLICER0+k].size;
    bad+=check("performance loop replaces stutter storage without extra 32 KiB",perf_audio && released && resource[RES_PERFORM].size==32768u);
    pf.next=PF_N;perf_buf_done();bad+=check("performance storage is returned after its fade",!resource[RES_PERFORM].size && !perf_audio);
    for(uint32_t k=0;k<4u;k++){trk[k].p[P_SLCR]=SL_OFF;slicer_track(&trk[k],in,CTL);}
    for(uint32_t t=0;t<30u*FS;t+=CTL)fx_buses(c,d,r,out,CTL);
    bad+=check("chorus/delay/reverb tails eventually release their buffers",!resource[RES_CHORUS].size && !resource[RES_DELAY].size && !resource[RES_REVERB].size && !dly_buf);
    song.g[G_RTYPE]=1;fx_buses(c,d,r,out,CTL);
    bad+=check("switching an idle reverb model needs no delay lines",!resource[RES_REVERB].size);
    r[0]=12000;fx_buses(c,d,r,out,CTL);r[0]=0;
    int spring_tail=0;
    for(uint32_t t=0;t<30u*FS;t+=CTL){fx_buses(c,d,r,out,CTL);for(uint32_t j=0;j<CTL;j++)spring_tail |= out[j];}
    bad+=check("SPRING keeps its audible tail then releases storage",spring_tail && !resource[RES_REVERB].size);
    reverb_prepare();__typeof__(fx) spring_saved=fx;int32_t spring_ref[CTL]={0};r[0]=10000;
    rev_spring(r,spring_ref,CTL);fx=spring_saved;resource_release(RES_REVERB);rev_comb=0;rev_memory=0;
    fx_buses(c,d,r,out,CTL);r[0]=0;
    bad+=check("SPRING resumes with its exact filter and modulation state",!memcmp(spring_ref,out,sizeof spring_ref));
    for(uint32_t t=0;t<30u*FS;t+=CTL)fx_buses(c,d,r,out,CTL);
    song.g[G_RTYPE]=2;fx_buses(c,d,r,out,CTL);
    r[0]=12000;fx_buses(c,d,r,out,CTL);r[0]=0;
    int hall_tail=0;
    for(uint32_t t=0;t<30u*FS;t+=CTL){fx_buses(c,d,r,out,CTL);for(uint32_t j=0;j<CTL;j++)hall_tail |= out[j] | (hl.side?rev_side[j]:0);}
    bad+=check("HALL keeps its audible stereo tail then releases storage",hall_tail && !resource[RES_REVERB].size && !hl.side);
    reverb_prepare();__typeof__(hl) hall_saved=hl;int32_t hall_ref[CTL]={0},hall_sd[CTL];r[0]=10000;
    rev_hall(r,hall_ref,CTL);memcpy(hall_sd,rev_side,sizeof hall_sd);hl=hall_saved;resource_release(RES_REVERB);rev_comb=0;rev_memory=0;
    fx_buses(c,d,r,out,CTL);r[0]=0;
    bad+=check("HALL resumes with its exact line positions and filter state",!memcmp(hall_ref,out,sizeof hall_ref) && hl.side && !memcmp(hall_sd,rev_side,sizeof hall_sd));
    for(uint32_t t=0;t<30u*FS;t+=CTL)fx_buses(c,d,r,out,CTL);
    song.g[G_RTYPE]=0;fx_buses(c,d,r,out,CTL);reverb_prepare();
    uint32_t off=0;
    for(uint32_t k=0;k<4u;k++){fx.comb_lp[k]=-2;for(uint32_t j=0;j<REV_COMB[k];j++)rev_comb[off++]=-2;}
    off=0;for(uint32_t k=0;k<2u;k++)for(uint32_t j=0;j<REV_AP[k];j++)rev_ap[off++]=-16;
    for(uint32_t t=0;t<2u*FS;t+=CTL)fx_buses(c,d,r,out,CTL);
    bad+=check("ROOM constant tail is represented exactly after release",!resource[RES_REVERB].size && rev_hold_dc==-8 && out[0]==-8);
    reverb_prepare();__typeof__(fx) saved=fx;int32_t ref[CTL]={0};r[0]=10000;
    rev_room(r,ref,CTL);fx=saved;resource_release(RES_REVERB);rev_comb=0;rev_memory=0;
    fx_buses(c,d,r,out,CTL);r[0]=0;
    bad+=check("new send restores the previous tail byte for byte",!memcmp(ref,out,sizeof ref));
    bad+=check("capacity exhaustion returns failure and preserves live memory",resource_get(RES_ENGINE0,RESOURCE_CAPACITY+4u)==0 && resource[RES_ENGINE0].size==largest);
    bad+=check("zero-byte requests release rather than return an arena pointer",!resource_get(RES_ENGINE0,0) && !resource[RES_ENGINE0].size);
    printf("resources: default engine bytes %u, worst audio bytes %u, host arena %u\n",expected,maximum+32768u+CHO_LEN*2u+(uint32_t)REV_MEMORY_BYTES+DL_N*2u,RESOURCE_CAPACITY);
    return bad;
}
static int bank_tests(void)
{
    int bad=0;
    audio_resources_reset();
    for(uint32_t b=0;b<RESOURCE_BANKS;b++){
        if(b==RESOURCE_CACHE_BANK && !RESOURCE_CACHE_AVAILABLE)continue;
        uint32_t n=(uint32_t)((uintptr_t)resource_banks[b].end-(uintptr_t)resource_banks[b].start);
        uint8_t *p=resource_get(b,n);
        bad+=check("each disjoint bank can be filled exactly without spanning its boundary",p==resource_banks[b].start && resource[b].size==n);
        if(p)memset(p,(int)(b+1u),n);
    }
    bad+=check("all SRAM tails contribute usable capacity",resource_used()==RESOURCE_CAPACITY && resource_used()>RESOURCE_MAIN_CAPACITY);
    bad+=check("combined capacity cannot be returned as one contiguous allocation",!resource_get(RES_PERFORM,RESOURCE_CAPACITY));
    void *saved=resource[0].ptr;uint32_t size=resource[0].size;
    bad+=check("failed growth leaves original allocation and contents intact",!resource_get(0,size+4u) && resource[0].ptr==saved && resource[0].size==size && ((uint8_t *)saved)[size-1u]==1u);
    bad+=check("overflowing requests fail without releasing a live allocation",!resource_get(0,UINT32_MAX) && !resource_get(0,UINT32_MAX-3u) && resource[0].ptr==saved && resource[0].size==size);
    for(uint32_t b=1;b<RESOURCE_BANKS;b++)if(b!=RESOURCE_CACHE_BANK || RESOURCE_CACHE_AVAILABLE)
        bad+=check("exhaustion does not overwrite another bank",((uint8_t *)resource[b].ptr)[0]==b+1u && ((uint8_t *)resource[b].ptr)[resource[b].size-1u]==b+1u);
    resource_release(0);
    uint32_t *p=resource_get(0,5u);
    bad+=check("released tail is reusable and rounded allocation is zeroed",p==saved && resource[0].size==8u && !p[0] && !p[1]);
    bad+=check("same-size reuse preserves contents",(p[0]=0xA5A5A5A5u,resource_get(0,8u)==p && p[0]==0xA5A5A5A5u));
    audio_resources_reset();
    uint32_t rng=1u;int valid=1;
    for(uint32_t cycle=0;cycle<8192u && valid;cycle++){
        rng=rng*1664525u+1013904223u;uint32_t id=(rng>>16)%RES_COUNT;
        rng=rng*1664525u+1013904223u;
        uint32_t bytes=(rng>>16)%36000u;
        if(!(cycle%7u))resource_release(id);else resource_get(id,bytes);
        for(uint32_t a=0;a<RES_COUNT && valid;a++)if(resource[a].size){
            uintptr_t lo=(uintptr_t)resource[a].ptr,hi=lo+resource[a].size;int contained=0;
            for(uint32_t b=0;b<RESOURCE_BANKS;b++)
                if(b!=RESOURCE_CACHE_BANK || RESOURCE_CACHE_AVAILABLE)
                    contained|=lo>=(uintptr_t)resource_banks[b].start && hi<=(uintptr_t)resource_banks[b].end;
            valid&=contained && !(lo&3u) && !(resource[a].size&3u);
            for(uint32_t b=0;b<a;b++)if(resource[b].size)
                valid&=hi<=(uintptr_t)resource[b].ptr || lo>=(uintptr_t)resource[b].ptr+resource[b].size;
        }
    }
    bad+=check("8192 allocation/release transitions stay aligned, inside one bank and disjoint",valid);
    audio_resources_reset();return bad;
}
int main(void)
{
    resource_host_cache_enabled=0;int bad=bank_tests();
    uint32_t sram_capacity=RESOURCE_CAPACITY;
    resource_host_cache_enabled=1;bad+=bank_tests();
    bad+=check("validated data-cache bank adds 28 KiB, the UI's 14 KiB apart",RESOURCE_CAPACITY==sram_capacity+28672u-UI_CACHE_BYTES && ui_cache()==_resource_cache_start);
#ifndef RESOURCES_BANKS_ONLY
    bad+=cache_tests()+audio_tests();
    resource_host_cache_enabled=0;bad+=cache_tests()+audio_tests();
#endif
    printf("Resource test %s\n",bad?"FAILED":"passed");return !!bad;
}
