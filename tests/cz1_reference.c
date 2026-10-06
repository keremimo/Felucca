/* SPDX-License-Identifier: GPL-3.0-only */
/* Offline audition of an external Casio tone through the firmware renderer.
 * cc -O2 -w -Ibuild/gen -Ifirmware/src tests/cz1_reference.c -lm -o build/host/cz1_reference
 * cz1_reference PATCH.syx OUT.wav [NOTE VELOCITY HOLD_SECONDS RELEASE_SECONDS]
 * Dry stereo mix, neutral Melodee macros; keyboard functions use INIT defaults.
 * This is an audition tool, not a hardware-fidelity assertion. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include "../firmware/src/cz1_sysex.h"

int main(int argc,char **argv)
{
    if(argc<3){fprintf(stderr,"usage: %s PATCH.syx OUT.wav [NOTE VELOCITY HOLD_SECONDS RELEASE_SECONDS]\n",argv[0]);return 2;}
    int note=argc>3?atoi(argv[3]):60,velocity=argc>4?atoi(argv[4]):100;
    double hold=argc>5?atof(argv[5]):2,release=argc>6?atof(argv[6]):2;
    if(note<0||note>127||velocity<1||velocity>127||hold<=0||hold>30||release<0||release>30)return 2;
    FILE *f=fopen(argv[1],"rb");if(!f){perror(argv[1]);return 2;}
    uint8_t frame[297],raw[144],encoded[144];size_t n=fread(frame,1,sizeof frame,f);fclose(f);
    uint32_t offset=n>5&&frame[5]==0x30?6:7,bytes=(n>offset?(uint32_t)n-offset-1:0)/2;
    if((n!=263&&n!=264&&n!=295&&n!=296)||frame[0]!=0xf0||frame[n-1]!=0xf7||
       frame[1]!=0x44||frame[2]||frame[3]||(frame[4]&0xf0)!=0x70||
       (frame[5]!=0x20&&frame[5]!=0x21&&frame[5]!=0x30)||
       (frame[5]==0x20&&bytes!=128)||(frame[5]==0x21&&bytes!=144)||
       n!=offset+bytes*2+1){fprintf(stderr,"invalid Casio tone frame: %s\n",argv[1]);return 1;}
    for(uint32_t i=0;i<bytes;i++){
        if(frame[offset+2*i]>15||frame[offset+2*i+1]>15){fprintf(stderr,"invalid nibble\n");return 1;}
        raw[i]=(uint8_t)(frame[offset+2*i]|frame[offset+2*i+1]<<4);
    }
    host_tracks_init();host_preset(&inst,ENGI_CZ1,0);
    if(!cz_sx_decode(cz_patch[0],raw,bytes)){
        fprintf(stderr,"tone rejected: %s\n",argv[1]);
        for(uint32_t i=0;i<CZ_NP;i++)if(cz_patch[0][i]>CZ_MAX[i])fprintf(stderr,"parameter %u: %u exceeds %u\n",i,cz_patch[0][i],CZ_MAX[i]);
        for(uint32_t l=0;l<2;l++)for(uint32_t e=0;e<3;e++){
            const uint8_t *p=cz_patch[0]+CZ_EBASE(l,e);
            if(p[16]!=8&&p[16]>p[17])fprintf(stderr,"line %u envelope %u sustain %u after end %u\n",l,e,p[16],p[17]);
            if(p[17]<8&&p[8+p[17]])fprintf(stderr,"line %u envelope %u END level %u\n",l,e,p[8+p[17]]);
        }
        return 1;
    }
    cz_sx_encode(cz_patch[0],encoded,bytes==144);uint32_t changed=0;
    for(uint32_t i=0;i<bytes;i++)if(raw[i]!=encoded[i]){
        changed++;
        if(getenv("CZ_DIFF"))fprintf(stderr,"byte %u: %02x -> %02x\n",i,raw[i],encoded[i]);
    }
    for(uint32_t i=0;i<7;i++)inst.p[P_E0+i]=0;
    inst.p[P_DIST]=inst.p[P_CHOR]=inst.p[P_DLY]=inst.p[P_REV]=0;
    inst.p[P_LD_AMP]=inst.p[P_LD_PIT]=inst.p[P_LD_FLT]=inst.p[P_ED_FLT]=0;
    inst.p[P_VOICE]=V_POLY;inst.p[P_PAN]=0;
    uint32_t start=FS/10/CTL*CTL,off=start+(uint32_t)(hold*FS)/CTL*CTL;
    uint32_t frames=(off+(uint32_t)(release*FS)+CTL-1)/CTL*CTL;
    f=fopen(argv[2],"wb");if(!f){perror(argv[2]);return 2;}wav_hdr(f,frames);
    int32_t peak=0;uint64_t energy=0;
    for(uint32_t at=0;at<frames;at+=CTL){
        if(at==start)trk_note_on(&inst,(uint32_t)note,(uint32_t)velocity);
        if(at==off)trk_note_off(&inst,(uint32_t)note);
        int32_t out[2*CTL];mix_block(out,CTL);
        for(uint32_t i=0;i<CTL;i++){
            for(uint32_t ch=0;ch<2;ch++){int32_t x=out[2*i+ch],a=x<0?-x:x;if(a>peak)peak=a;energy+=(uint64_t)((int64_t)x*x);}
            wav_put(f,out[2*i],out[2*i+1]);
        }
    }
    if(fclose(f)){perror(argv[2]);return 2;}
    printf("%.16s: note=%d velocity=%d frames=%u peak=%d rms=%.1f canonicalized_bytes=%u\n",cz_patch[0]+CZ_NP,note,velocity,frames,peak,sqrt((double)energy/(2*frames)),changed);
    return 0;
}
