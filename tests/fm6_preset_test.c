/* SPDX-License-Identifier: GPL-3.0-only */
/* Independent native preset pools, migration and failure recovery on simulated NOR. */
#define PERSISTENCE_TEST_NO_MAIN
#include "persistence_test.c"

static void reboot_presets(void)
{
    memset(up_bank,0,sizeof up_bank);memset(native_fm,0,sizeof native_fm);memset(native_cz,0,sizeof native_cz);upf_empty();cz_bank_boot();up_boot();
}
int main(void)
{
    int bad=0;uint8_t fm[FM6_PACKED],cz[CZ_BYTES],got[FM6_PACKED];
    reset();upf_empty();fm6_init();cz_init();up_boot();
    fm6_factory(3,fm);memcpy(fm+118,"MY VOICE  ",10);cz_patch_init(cz);memcpy(cz+128,"MY CZ TONE      ",16);cz[20]=0x37;
    bad+=check("64 FM6 and 128 CZ slots are independent from 64 general slots",native_limit(ENGI_FM6)==64 && native_limit(ENGI_CZ)==128 && UP_SLOTS==64 && !native_count(ENGI_FM6) && !native_count(ENGI_CZ));
    bad+=check("F064 stores original 128-byte DX7 VMEM",native_put(ENGI_FM6,63,fm)==0 && !memcmp(native_raw(ENGI_FM6,63),fm,128));
    bad+=check("Z128 stores all original 144 CZ-1 bytes",native_put(ENGI_CZ,127,cz)==0 && !memcmp(native_raw(ENGI_CZ,127),cz,144));
    bad+=check("one engine's native slot cannot occupy the other engine or a general slot",!up_used(63) && native_count(ENGI_FM6)==1 && native_count(ENGI_CZ)==1);
    TSEL->p[P_DIST]=47;TSEL->p[P_LD_PIT]=12;TSEL->step[0]=(step_t){.time=ST_NOTE,.n=1,.note={60}};
    native_load(ENGI_FM6,63,0);fm6_poll();fm6_pack(fm6_patch[0],got);
    bad+=check("native FM6 load keeps effects, modulation and pattern",!memcmp(got,fm,128) && TSEL->p[P_DIST]==47 && TSEL->p[P_LD_PIT]==12 && TSEL->step[0].note[0]==60);
    native_load(ENGI_CZ,127,0);
    bad+=check("native CZ load keeps effects, modulation and pattern",!memcmp(cz_patch[0].raw,cz,144) && TSEL->p[P_DIST]==47 && TSEL->p[P_LD_PIT]==12 && TSEL->step[0].note[0]==60);
    uint32_t total,cur=eng_list_pos(&total);
    bad+=check("Z128 occupies the next ordinary preset position after CZ factory tones",total==66 && cur==65 && user_of(TSEL)==127);
    eng_list_step(1);bad+=check("ordinary engine preset scrolling wraps from Z128 to INIT",TSEL->eng_req==ENGI_CZ && TSEL->preset==0 && !TSEL->user);
    eng_list_step(-1);bad+=check("ordinary engine preset scrolling returns to Z128",TSEL->user==128 && !memcmp(cz_patch[0].raw,cz,144));
    cur=preset_all_pos(&total);uint32_t k,e=preset_all_at(cur,&k);
    bad+=check("global preset browser identifies native slots as user sounds",e==USER_NATIVE_CZ && k==127);
    favorite_set(USER_NATIVE_FM,63,1);favorite_set(USER_NATIVE_CZ,127,1);favorites.filter=1;
    bad+=check("native presets participate in the regular favorites filter",preset_pos(&total)<2 && total==2);
    preset_go(0);bad+=check("filtered global scrolling loads F064 directly",TSEL->eng_req==ENGI_FM6 && TSEL->user==64);
    settings_save();memset(&favorites,0,sizeof favorites);persist_t persisted;int n=st_load(OBJ_SETTINGS,&persisted,sizeof persisted);settings_import(&persisted,n);
    bad+=check("native favorites persist without changing the settings layout",favorite_has(USER_NATIVE_FM,63) && favorite_has(USER_NATIVE_CZ,127));favorites.filter=0;
    reboot_presets();
    bad+=check("both native pools survive reboot byte for byte",native_used(ENGI_FM6,63) && native_used(ENGI_CZ,127) && !memcmp(native_raw(ENGI_FM6,63),fm,128) && !memcmp(native_raw(ENGI_CZ,127),cz,144));
    uint8_t newer[128];memcpy(newer,fm,128);newer[118]='N';fail_after=2;
    bad+=check("torn native save reports failure and rolls RAM back",native_put(ENGI_FM6,63,newer)==2 && !memcmp(native_raw(ENGI_FM6,63),fm,128));fail_after=-1;reboot_presets();
    bad+=check("torn native save keeps the last flash commit",!memcmp(native_raw(ENGI_FM6,63),fm,128));
    song.playing=1;uint32_t writes=erases;bad+=check("native writes refuse playback without flash erase",native_put(ENGI_CZ,0,cz)==2 && erases==writes);song.playing=0;
    native_put(ENGI_CZ,127,0);native_put(ENGI_FM6,63,0);reboot_presets();
    bad+=check("erased native presets stay erased across boot",!native_used(ENGI_CZ,127) && !native_used(ENGI_FM6,63));

    /* Simulate the preceding shared-preset firmware, including both UPF6 objects. */
    reset();upf_empty();fm6_init();set_engine_of(TSEL,ENGI_FM6);
    uint8_t voice[FP_SIZE+1];fm6_unpack(fm,voice);fm6_set_patch(0,voice);fm6_adopt(0);
    up_store(0,"OLD FIRST");up_store(47,"OLD MIDDLE");up_store(63,"OLD LAST");
    /* A historical CZ bank and one ordinary native CZ record must both survive. */
    cz_bank_t oldcz;cz_bank_empty(&oldcz,7);oldcz.used=0x8000u;memcpy(oldcz.tone[15].raw,cz,144);st_save(OBJ_CZBANK0+7,&oldcz,sizeof oldcz);
    set_engine_of(TSEL,ENGI_CZ);memcpy(cz_patch[0].raw,cz,144);cz_patch[0].raw[128]='S';up_store(4,"SHARED CZ");
    fail_after=30;reboot_presets();fail_after=-1;
    bad+=check("interrupted pool migration retains committed native data and old sources",native_used(ENGI_FM6,0) && native_used(ENGI_FM6,47));
    reboot_presets();
    bad+=check("shared FM6 voices migrate even after a reused-object interruption",native_used(ENGI_FM6,0) && native_used(ENGI_FM6,47) && native_used(ENGI_FM6,63) && !memcmp(native_raw(ENGI_FM6,63),fm,128));
    bad+=check("old CZ slot 128 stays in place; shared tone uses a free CZ slot",native_used(ENGI_CZ,127) && native_used(ENGI_CZ,0) && native_raw(ENGI_CZ,0)[128]=='S' && !memcmp(native_raw(ENGI_CZ,127),cz,144));
    native_put(ENGI_FM6,63,0);native_put(ENGI_CZ,0,0);reboot_presets();
    bad+=check("migration markers prevent erased legacy sounds from reappearing",!native_used(ENGI_FM6,63) && !native_used(ENGI_CZ,0));
    int layout=1;
    for(uint32_t a=0;a<OBJ_COUNT;a++)for(uint32_t ac=0;ac<2;ac++){
        uint32_t lo=st_sector(a,ac),len=st_banked(a)?5u*ST_SECTOR:ST_SECTOR;
        layout &= !(lo<0xE5000u && lo+len>0xE0000u);
        for(uint32_t b=a+1;b<OBJ_COUNT;b++)for(uint32_t bc=0;bc<2;bc++){uint32_t blo=st_sector(b,bc),blen=st_banked(b)?5u*ST_SECTOR:ST_SECTOR;layout &=lo+len<=blo || blo+blen<=lo;}
    }
    bad+=check("native storage stays separate from projects, general presets and OTA",layout);
    printf("Native preset test %s\n",bad?"FAILED":"passed");return !!bad;
}
