/* SPDX-License-Identifier: GPL-3.0-only */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
int main(void)
{
    int bad=0;int32_t out[CTL];ui_power_on();cz_init();track_t *t=&trk[0];
    set_engine_of(t,ENGI_CZ1);apply_preset_to(t,0);engine_block(t);
    for(uint32_t j=0;j<8;j++)trk_note_on(t,48+j,100);
    for(uint32_t j=0;j<FS/CTL;j++)track_render(t,out,CTL);
    uint32_t held=0;for(uint32_t j=0;j<NVOICE;j++)held+=t->v[j].active&&t->v[j].gate;
    bad+=check("CZ-1 holds eight two-line voices",held==8&&voices_busy()==VBUDGET);
    for(uint32_t j=0;j<8;j++)trk_note_off(t,48+j);
    for(uint32_t j=0;j<3*FS/CTL;j++)track_render(t,out,CTL);
    held=0;for(uint32_t j=0;j<NVOICE;j++)held+=t->v[j].active;
    bad+=check("CZ-1 releases every voice",held==0);
    for(uint32_t mode=0;mode<4;mode++)for(uint32_t mod=0;mod<3;mod++)for(uint32_t w=0;w<8;w++){
        memset(t->v,0,sizeof t->v);eng_state_clear(0);cz_default(cz_patch[0],0);
        cz_patch[0][CZ_LINE]=mode;cz_patch[0][CZ_MOD]=mod;
        cz_patch[0][CZ_LBASE(0)+CZ_W1]=w;cz_patch[0][CZ_LBASE(1)+CZ_W1]=7-w;
        trk_note_on(t,96,127);int64_t energy=0;
        for(uint32_t j=0;j<150;j++){track_render(t,out,CTL);for(uint32_t k=0;k<CTL;k++)energy+=out[k]<0?-out[k]:out[k];}
        bad+=check("CZ-1 waveform/routing sounds",energy>1000);
    }
    /* Coarse structural checks against Casio's diagrams and Kasploosh's
     * CZ-1 recordings. Broad harmonic limits guard the model's shape;
     * they do not establish parity at unknown source patch settings. */
    { pd_t wave;double mag[17]={0};
      cz_pd_setup(&wave,4,65535);
      for(uint32_t h=1;h<=16;h++){
        double re=0,im=0;
        for(uint32_t j=0;j<4096;j++){double a=2*3.141592653589793*h*j/4096;
          int32_t y=cz_pd_wave(&wave,j*16);re+=y*cos(a);im+=y*sin(a);}
        mag[h]=sqrt(re*re+im*im);
      }
      double h2=20*log10(mag[2]/mag[1]),h3=20*log10(mag[3]/mag[1]);
      bad+=check("CZ double sine is a broad sine beside a narrow pulse",h2>-26&&h2<-17&&h3>-29&&h3<-18);
      cz_pd_setup(&wave,4,0);
      bad+=check("undistorted double sine has two carrier cycles",cz_pd_wave(&wave,0)<-32000&&cz_pd_wave(&wave,16384)>32000&&cz_pd_wave(&wave,32768)<-32000&&cz_pd_wave(&wave,49152)>32000);
      cz_pd_setup(&wave,6,65535);int64_t early=0,middle=0,late=0;uint32_t crossings=0;int32_t prev=cz_wave(&wave,0,65535,3);int rising=0;
      for(uint32_t j=0;j<4096;j++){int32_t y=cz_wave(&wave,j*16,65535,3);
        if(y>prev)rising=1;else if(y<prev&&rising){crossings++;rising=0;}prev=y;
        int64_t e=(int64_t)y*y;if(j<1024)early+=e;else if(j<2048)middle+=e;else if(j>=3072)late+=e;
      }
      bad+=check("CZ resonance core reaches about fifteen cycles",crossings>=14&&crossings<=15);
      bad+=check("CZ trapezoid holds its first half then decays",early*5>middle*4&&early*4<middle*5&&late*5<early);
      cz_pd_setup(&wave,1,65535);
      bad+=check("resonance windows also shape the alternate basic wave",cz_wave(&wave,49152,65535,1)<cz_wave(&wave,16384,65535,1)&&cz_wave(&wave,65520,65535,1)>-100);
    }
    { pd_t pulse,doubled,multi,null;int periodic=1,bounded=1,aliases=1;
      cz_pd_setup(&pulse,2,65535);cz_pd_setup(&doubled,7,65535);
      cz_pd_setup(&multi,6,65535);cz_pd_setup(&null,3,65535);
      for(uint32_t ph=0;ph<65536;ph+=17){
        periodic &= cz_pd_wave(&doubled,ph)==cz_pd_wave(&pulse,(ph<<1)&65535u);
        aliases &= cz_wave(&multi,ph,65535,5)==cz_wave(&multi,ph,65535,6)&&cz_wave(&multi,ph,65535,6)==cz_wave(&multi,ph,65535,7);
      }
      bad+=check("native PULSE2 repeats twice per carrier period",periodic);
      bad+=check("native NULL settles to DC after the initial half-cycle",cz_pd_wave(&null,0)>32000&&cz_pd_wave(&null,1)>32000&&cz_pd_wave(&null,32768)>32000&&cz_pd_wave(&null,65535)>32000);
      bad+=check("native windows 6 and 7 reproduce window 5",aliases);
      bad+=check("half-saw window mutes the second half",abs(cz_wave(&multi,40000,65535,4))<3&&abs(cz_wave(&multi,10000,65535,4))>100);
      bad+=check("double-saw window resets at the middle",abs(cz_wave(&multi,32768,65535,5))<3&&abs(cz_wave(&multi,16384,65535,5))>100&&abs(cz_wave(&multi,49152,65535,5))>100);
      for(uint32_t depth=0;depth<65536;depth+=16383)for(uint32_t w=0;w<8;w++){
        pd_t pd;cz_pd_setup(&pd,w,depth);
        for(uint32_t win=0;win<8;win++)for(uint32_t ph=0;ph<65536;ph+=257){int32_t y=cz_wave(&pd,ph,depth,win);bounded &= y>=-32768&&y<=32768;}
      }
      bad+=check("all native carriers/windows stay bounded across DCW",bounded);
    }
    cz_env_t en={0};uint8_t ep[18]={0};ep[0]=99;ep[8]=99;ep[1]=80;ep[9]=50;ep[2]=99;ep[16]=1;ep[17]=2;en.gate=1;
    cz_env_stage(&en,ep,2,0,60,0);for(uint32_t j=0;j<FS;j++)cz_env_tick(&en,ep,2,0,60,0);
    bad+=check("CZ envelope holds the selected sustain",en.hold&&en.stage==1&&en.value==50*169466);
    en.gate=0;en.stage=2;cz_env_stage(&en,ep,2,0,60,0);for(uint32_t j=0;j<FS;j++)cz_env_tick(&en,ep,2,0,60,0);
    bad+=check("CZ envelope END is silent",en.done&&!en.value);
    en=(cz_env_t){0};ep[0]=0;en.gate=1;cz_env_stage(&en,ep,2,0,60,0);for(uint32_t j=0;j<1000;j++)cz_env_tick(&en,ep,2,0,60,0);
    bad+=check("CZ rate zero freezes a nonzero transition",!en.value&&!en.done&&en.stage==0);
    for(uint32_t rate=0;rate<100;rate++)for(uint32_t kind=0;kind<3;kind++){
        for(uint32_t j=0;j<8;j++){ep[j]=(uint8_t)((rate+j*13)%100);ep[8+j]=(uint8_t)((rate+j*29)%100);}ep[16]=6;ep[17]=7;
        cz_env_t a={0},b; a.gate=1;cz_env_stage(&a,ep,kind,3,72,0);b=a;
        for(uint32_t j=0;j<256;j++){
            if(j==37){a.gate=b.gate=0;a.hold=b.hold=0;a.stage=b.stage=7;cz_env_stage(&a,ep,kind,3,72,0);cz_env_stage(&b,ep,kind,3,72,0);}
            for(uint32_t k=0;k<32;k++)cz_env_tick(&a,ep,kind,3,72,0);
            cz_env_advance(&b,ep,kind,3,72,64u|(64u<<7)|(64u<<14),32);
            if(memcmp(&a,&b,sizeof a)){printf("rate %u kind %u block %u A %d %d %d %u %u %u B %d %d %d %u %u %u\n",rate,kind,j,a.value,a.target,a.step,a.stage,a.hold,a.done,b.value,b.target,b.step,b.stage,b.hold,b.done);bad+=check("analytic CZ envelope equals sample ticks",0);break;}
        }
    }
    { uint8_t raw[144],round[CZ_PACKED],tone[CZ_PACKED];int exact=1;
      for(uint32_t k=0;k<9;k++){cz_default(tone,k);cz_sx_encode(tone,raw,1);memcpy(round,tone,sizeof round);
        exact &= cz_sx_decode(round,raw,144)&&!memcmp(tone,round,CZ_PACKED);}
      bad+=check("CZ-1 native SysEx preserves factory tones, names and velocity",exact);
      /* Independent offsets and inverted nibbles from Casio's pp. 87-92. */
      cz_default(tone,0);cz_sx_encode(tone,raw,1);raw[16]=0x80;raw[73]=0x40;raw[20]=0xf2;raw[37]=0xb2;raw[54]=0x22;raw[77]=0x02;raw[94]=0xe2;raw[111]=0x62;
      memcpy(raw+128,"CASIO FIELD TEST",16);memcpy(round,tone,sizeof round);
      bad+=check("Casio native extra fields use the documented byte offsets",cz_sx_decode(round,raw,144)&&round[CZ_LBASE(0)+CZ_LEVEL]==7&&round[CZ_LBASE(1)+CZ_LEVEL]==11&&round[CZ_LBASE(0)+CZ_VA]==0&&round[CZ_LBASE(0)+CZ_VW]==4&&round[CZ_LBASE(0)+CZ_VP]==13&&round[CZ_LBASE(1)+CZ_VA]==15&&round[CZ_LBASE(1)+CZ_VW]==1&&round[CZ_LBASE(1)+CZ_VP]==9&&!memcmp(round+CZ_NP,"CASIO FIELD TEST",16));
      for(uint32_t val=0;val<100;val++)for(uint32_t kind=0;kind<3;kind++){
        bad+=check("Casio envelope rate/level conversion is reversible",cz_sx_unrate(kind,cz_sx_rate(kind,val))==val&&cz_sx_unlevel(kind,cz_sx_level(kind,val))==val);
      }
      bad+=check("Casio vibrato machine bytes match the manual",cz_sx_vdata(32,0)==0x21&&cz_sx_vdata(99,0)==0x27f&&cz_sx_vdata(50,1)==0x9e0&&cz_sx_vdata(99,1)==0x53e0&&cz_sx_vdata(99,2)==0x300);
      cz_default(tone,0);tone[CZ_VDELAY]=53;tone[CZ_VRATE]=50;tone[CZ_VDEP]=99;cz_sx_encode(tone,raw,1);
      bad+=check("external CZ vibrato fixture uses low then high machine byte",raw[5]==53&&raw[6]==0x57&&raw[7]==0&&raw[8]==50&&raw[9]==0xe0&&raw[10]==9&&raw[11]==99&&raw[12]==0&&raw[13]==3);
      raw[37]=2;raw[43]=0x3d;raw[20]=2;raw[26]=0x62;memcpy(round,tone,sizeof round);
      bad+=check("external editor END levels normalize to CZ zero targets",cz_sx_decode(round,raw,144)&&round[CZ_EBASE(0,1)+10]==0&&round[CZ_EBASE(0,2)+10]==0);
      /* Independent p. 84 table endpoints plus the two magazine files.
       * Round trips alone missed the high-six-bit wire placement bug. */
      { const uint8_t display[]={0,15,16,30,31,45,46,60};
        const uint8_t wire[]={0,60,68,124,132,188,196,252};
        for(uint32_t j=0;j<NELEM(display);j++){
          cz_default(tone,0);tone[CZ_FINE]=display[j];cz_sx_encode(tone,raw,1);
          bad+=check("Casio fine detune uses the high six bits",raw[2]==wire[j]);
          raw[2]=wire[j];memcpy(round,tone,sizeof round);
          bad+=check("independent Casio fine-detune fixture decodes",cz_sx_decode(round,raw,144)&&round[CZ_FINE]==display[j]);
        }
        cz_default(tone,0);cz_sx_encode(tone,raw,1);raw[2]=0x50;memcpy(round,tone,sizeof round);
        bad+=check("Martha fine-detune byte is valid",cz_sx_decode(round,raw,144)&&round[CZ_FINE]==19);
        raw[2]=0xfc;memcpy(round,tone,sizeof round);
        bad+=check("Timpani fine-detune byte is valid",cz_sx_decode(round,raw,144)&&round[CZ_FINE]==60);
      }
      cz_default(tone,0);
      /* Independent bit layout: first wave bits 7..5, second 4..2,
       * enable bit 1; window split between bit 0 and the next byte 7..6. */
      int combinations=1;
      for(uint32_t w1=0;w1<8;w1++)for(uint32_t w2=0;w2<9;w2++)for(uint32_t win=0;win<8;win++){
        cz_sx_encode(tone,raw,1);
        uint8_t lo=(uint8_t)(w1<<5|(w2?(w2-1)<<2|2:0)|win>>2),hi=(uint8_t)((win&3)<<6);
        raw[14]=raw[71]=lo;raw[15]=raw[72]=hi;
        memcpy(round,tone,sizeof round);
        combinations &= cz_sx_decode(round,raw,144);
        for(uint32_t l=0;l<2;l++)combinations &= round[CZ_LBASE(l)]==w1&&round[CZ_LBASE(l)+1]==w2&&round[CZ_WIN(l)]==win;
        uint8_t exported[144];cz_sx_encode(round,exported,1);
        combinations &= exported[14]==lo&&exported[15]==hi&&exported[71]==lo&&exported[72]==hi;
      }
      bad+=check("all 576 native wave/window encodings preserve both lines",combinations);
      cz_sx_encode(tone,raw,1);
      raw[37]=8;memcpy(round,tone,sizeof round);
      bad+=check("normalizing END levels does not accept invalid END indices",!cz_sx_decode(round,raw,144));
    }
    { uint8_t tone[CZ_PACKED],round[CZ_PACKED];ui_power_on();set_engine_of(TSEL,ENGI_CZ1);apply_preset_to(TSEL,6);
      memcpy(cz_patch[0]+CZ_NP,"PERSIST FULL CZ!",16);cz_patch[0][CZ_LBASE(0)+CZ_VP]=15;cz_patch[0][CZ_LBASE(1)+CZ_LEVEL]=7;cz_patch[0][CZ_LBASE(0)]=7;cz_patch[0][CZ_WIN(0)]=7;cz_patch[0][CZ_WIN(1)]=4;
      memcpy(tone,cz_patch[0],sizeof tone);TSEL->p[P_E0]=-40;TSEL->p[P_E5]=37;up_store(31,"CZ PERSIST");
      up_cz_decode(up_rec(31),round);bad+=check("CZ user preset losslessly keeps 165 native bytes and macros",up_used(31)&&!memcmp(tone,round,sizeof tone)&&up_value(up_rec(31),P_E0)==-40&&up_value(up_rec(31),P_E5)==37);
      cz_default(cz_patch[0],0);up_load(31);cz_poll();bad+=check("CZ preset load retains the complete custom tone",!memcmp(tone,cz_patch[0],sizeof tone));
      project_t q,after;project_store_t wire;project_capture(&q);bad+=check("FUNB round trip keeps complete CZ tone",proj_pack(&wire,&q)&&proj_import(&after,&wire,sizeof wire)&&!memcmp(tone,after.cz1[0],sizeof tone));
      mod_midi(TSEL,0xb0,65,127);mod_midi(TSEL,0xb0,5,127);bad+=check("CZ CC65 and CC5 control native portamento",cz_patch[0][CZ_PORTON]&&cz_patch[0][CZ_PORTTIME]==99);
      mod_midi(TSEL,0xb0,121,0);bad+=check("CZ CC121 clears native portamento",!cz_patch[0][CZ_PORTON]&&!TSEL->porta);
      cz_default(cz_patch[0],0);name_open(NK_CZ_NAME,0);memcpy(nm.s,"SIXTEEN CHAR CZ!",16);nm.s[16]=0;nm.len=16;nm.cur=16;name_ok();
      bad+=check("CZ naming stores all 16 characters",!memcmp(cz_patch[0]+CZ_NP,"SIXTEEN CHAR CZ!",16));
      cz_patch[0][23]=7;cz_patch[0][24]=4;cz_patch[0][CZ_WIN(0)]=6;cz_patch[0][CZ_WIN(1)]=1;
      go_page(GR_CZTOOLS);ui.act=2;act_do();
      bad+=check("CZ line copy keeps the native carrier pair and its window",cz_patch[0][31]==7&&cz_patch[0][32]==4&&cz_patch[0][CZ_WIN(1)]==6);
      go_title("CZ WINDOW");turn(EN_K1,1);
      bad+=check("CZ window controls edit independent native window fields",cz_patch[0][CZ_WIN(0)]==7&&cz_patch[0][CZ_WIN(1)]==6&&cz_valid(cz_patch[0]));
    }
    { /* Frozen v1 offsets and compact widths: exercise each storage boundary. */
      uint8_t old[CZ_OLD_PACKED],expected[CZ_PACKED],decoded[CZ_PACKED];
      cz_default(expected,0);memcpy(old,expected,CZ_OLD_NP);
      memcpy(old+147,"OLD WINDOW TONE ",16);old[23]=7;old[24]=2;old[31]=3;
      memcpy(expected+CZ_NP,old+147,16);expected[23]=6;expected[24]=2;expected[31]=4;expected[CZ_WIN(0)]=3;
      bad+=check("old panel wave tones upgrade to independent native windows",cz_upgrade(decoded,old)&&!memcmp(decoded,expected,CZ_PACKED));
      up_rec_t legacy={0};legacy.used=UP_USED;legacy.ver=6;legacy.engine=14;legacy.np=92;memcpy(legacy.name,"OLD CZ",6);
      uint32_t pos=0;
      for(uint32_t k=0;k<163;k++){uint32_t width=k<147?up_cz_width(k):7;up_cz_put_bits(&legacy,pos,width,old[k]);pos+=width;}
      for(uint32_t k=0;k<92;k++)up_set_value(&legacy,k,TP[k].def);
      up_set_value(&legacy,P_E0,-37);up_cz_decode(&legacy,decoded);
      bad+=check("v6 compact preset retains old name, windows and common bits",pos==977&&up_valid(&legacy)&&!memcmp(decoded,expected,CZ_PACKED)&&up_value(&legacy,P_E0)==-37);
      project_t q,after;project_store_t newwire;project_capture(&q);proj_pack(&newwire,&q);
      uint8_t oldwire[4236];memcpy(oldwire,newwire.raw,PROJ_CZ_OFF);
      for(uint32_t l=0;l<NTRK;l++)memcpy(oldwire+PROJ_CZ_OFF+l*163,old,163);
      memcpy(oldwire+4220,newwire.raw+PROJ_NAME_OFF,12);uint32_t magic=0x46554e41u,size=4236,sum;
      memcpy(oldwire,&magic,4);memcpy(oldwire+4,&size,4);sum=proj_hash(oldwire,4232);memcpy(oldwire+4232,&sum,4);
      int projects=proj_import(&after,oldwire,4236);for(uint32_t l=0;l<NTRK;l++)projects &= !memcmp(after.cz1[l],expected,CZ_PACKED);
      bad+=check("frozen FUNA project converts all four CZ tones",projects);
      uint8_t oldtemplate[TMPL_SIZE8];memset(oldtemplate,0,sizeof oldtemplate);
      for(uint32_t l=0;l<NTRK;l++)memcpy(oldtemplate+TMPL_SIZE6-8+l*163,old,163);
      magic=0x384c5054u;size=sizeof oldtemplate;memcpy(oldtemplate+size-8,&size,4);memcpy(oldtemplate+size-4,&magic,4);
      int templates=tmpl_take(oldtemplate,sizeof oldtemplate);for(uint32_t l=0;l<NTRK;l++)templates &= !memcmp(tmpl.cz1[l],expected,CZ_PACKED);
      bad+=check("frozen TPL8 template converts all four CZ tones",templates);
      uint8_t bank[BANK_STORE_SIZE];
      step_t *saved_step=&pattern_at(0,3)->step[17];saved_step->note[0]=67;saved_step->n=1;saved_step->time=ST_NOTE;saved_step->vel=109;saved_step->flags=SF_ACCENT;saved_step->probability=77;
      project_capture(&q);bank_pack(bank,&q,1);
      uint8_t oldbank[BANK_STORE_SIZE];memcpy(oldbank,bank,8);memcpy(oldbank+8,oldwire,4236);
      memcpy(oldbank+8+4236,bank+8+PROJ_STORE_SIZE,BANK_SIZEB-4-8-4236);
      magic=0x424b4246u;size=19084;memcpy(oldbank,&magic,4);memcpy(oldbank+4,&size,4);sum=proj_hash(oldbank,size-4);memcpy(oldbank+size-4,&sum,4);
      int banks=bank_valid(oldbank,19084);if(banks){bank_upgrade(oldbank);banks=bank_valid(oldbank,BANK_STORE_SIZE)&&!memcmp(oldbank+BANK_ACTIVE_OFF,bank+BANK_ACTIVE_OFF,BANK_FN_OFF+4u+NTRK*FM6_NFN-BANK_ACTIVE_OFF);}
      for(uint32_t l=0;l<NTRK;l++)banks &= !memcmp(proj_scratch.cz1[l],expected,CZ_PACKED);
      bad+=check("FBKB archive upgrades to FBKC without losing tones or patterns",banks);
    }
    printf("CZ-1: %d failures\n",bad);return bad?1:0;
}
