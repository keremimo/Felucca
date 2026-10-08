/* SPDX-License-Identifier: GPL-3.0-only */
/* Prototype DSP and exact native wire round trips. Pass real .syx libraries
 * as arguments; downloaded factory data stays outside the source repository. */
#define MELODEE_PROPHET_PROTOTYPE 1
#define main hostsim_main
#include "hostsim.c"
#undef main
static int failures;
static void check(const char *what, int ok)
{ printf("prophet: %s: %s\n",what,ok?"ok":"FAIL"); failures+=!ok; }
static void setup(void)
{
    memset(trk,0,sizeof trk); eng_state_reset();
    memset(p5_ready,0,sizeof p5_ready);
    host_tracks_init(); host_preset(&trk[0],0,0);
    trk[0].p[P_VOICE]=V_POLY;
    trk[0].p[P_CHOR]=trk[0].p[P_DLY]=trk[0].p[P_REV]=0;
}
static uint32_t active(track_t *t)
{
    uint32_t n=0;for(uint32_t i=0;i<NVOICE;i++)n+=t->v[i].active&&t->v[i].stage!=4u;
    return n;
}
static int32_t tick(void)
{
    int32_t out[CTL], peak=0;track_render(&trk[0],out,CTL);
    for(uint32_t i=0;i<CTL;i++){int32_t a=out[i]<0?-out[i]:out[i];if(a>peak)peak=a;}
    return peak;
}
static void native_tests(void)
{
    p5_patch_t p,q,unchanged;
    uint8_t f[P5_FRAME_MAX],g[P5_FRAME_MAX];
    p5_patch_init(&p);
    int32_t seed=17385;
    int roundtrip=1;
    for(uint32_t len=128;len<=133;len+=5)for(uint32_t cmd=2;cmd<=3;cmd++) {
        p.size=(uint8_t)len;p.command=(uint8_t)cmd;p.group=9;p.program=39;
        for(uint32_t j=0;j<100;j++){
            for(uint32_t k=0;k<len;k++)p.raw[k]=(uint8_t)noise32(&seed);
            uint32_t n=p5_patch_encode(&p,f,sizeof f);
            roundtrip &= n && p5_patch_decode(&q,f,n) && q.size==p.size &&
                         !memcmp(q.raw,p.raw,len) && p5_patch_encode(&q,g,sizeof g)==n && !memcmp(f,g,n);
        }
    }
    check("random single/edit-buffer frames preserve every raw byte (both sizes)",roundtrip);
    uint32_t n=p5_patch_encode(&p,f,sizeof f);
    unchanged=q;
    int rejected=1;
    for(uint32_t k=0;k<n;k++) { q=unchanged; rejected &= !p5_patch_decode(&q,f,k) && !memcmp(&q,&unchanged,sizeof q); }
    uint8_t old=f[4];f[4]=0x80;rejected &= !p5_patch_decode(&q,f,n);f[4]=old;
    f[2]=0x33;rejected &= !p5_patch_decode(&q,f,n);f[2]=0x32;
    f[3]=5;rejected &= !p5_patch_decode(&q,f,n);f[3]=3;
    check("truncation, status bytes and unrelated commands rejected atomically",rejected);
    p.size=128;n=p5_patch_encode(&p,f,sizeof f);f[n-4u]|=4u;
    check("unused bits in the final partial group rejected",!p5_patch_decode(&q,f,n));
    p.command=2;p.group=10;
    check("invalid program address rejected",!p5_patch_encode(&p,f,sizeof f));
}
static void factory_file(const char *path)
{
    FILE *fp=fopen(path,"rb");if(!fp){perror(path);failures++;return;}
    fseek(fp,0,SEEK_END);long size=ftell(fp);rewind(fp);
    if(size<=0 || size>16*1024*1024){fclose(fp);failures++;return;}
    uint8_t *b=malloc((size_t)size);if(!b){fclose(fp);failures++;return;}
    int ok=fread(b,1,(size_t)size,fp)==(size_t)size;fclose(fp);
    uint32_t pos=0,count=0,rendered=0,audible=0;int safe=1;uint8_t encoded[P5_FRAME_MAX];p5_patch_t p;
    while(ok && pos<(uint32_t)size){
        uint32_t end=pos;while(end<(uint32_t)size&&b[end]!=0xF7)end++;
        uint32_t n=end-pos+1u;
        ok=end<(uint32_t)size&&p5_patch_decode(&p,b+pos,n)&&p5_patch_encode(&p,encoded,sizeof encoded)==n&&
           !memcmp(b+pos,encoded,n);
        if(ok){
            setup();p5_patch[0]=p;p5_ready[0]=1;p5_track_accept(&trk[0]);
            for(uint32_t j=0;j<3;j++)trk_note_on(&trk[0],48+j*7,100);
            int32_t peak=0;for(uint32_t j=0;j<FS/CTL;j++){int32_t x=tick();if(x>peak)peak=x;safe &= x<5*VOICE_FS*3;}
            for(uint32_t j=0;j<3;j++)trk_note_off(&trk[0],48+j*7);for(uint32_t j=0;j<200;j++)tick();
            rendered++;audible+=peak>16u;if(peak<=16){char name[21];p5_patch_name(name,&p);printf("prophet: slow/quiet factory program %u: %s (peak %d in first second)\n",count+1,name,peak); }
        }
        pos=end+1u;count++;
    }
    printf("prophet: real bank %s: %u exact frames\n",path,count);
    check("real bank-file byte-for-byte round trip",ok&&count==200u);
    printf("prophet: native factory playback: %u rendered, %u audible\n",rendered,audible);
    check("every factory program reaches the native renderer and stays bounded",safe&&rendered==count&&audible>count*9/10);
    free(b);
}
static void voice_tests(void)
{
    setup();track_t *t=&trk[0];
    for(uint32_t k=0;k<8;k++)trk_note_on(t,60+k,100);
    check("eight notes cap at five sounding Prophet voices",active(t)==5u);
    check("five Prophet voices use fifteen of sixteen shared units",voices_busy()==15u);
    for(uint32_t part=1;part<4;part++){
        host_preset(&trk[part],3,0);trk[part].p[P_VOICE]=V_POLY;
        trk_note_on(&trk[part],48+part,100);
    }
    check("shared allocation keeps three Prophet and all three other tracks",voices_busy()==15u&&active(t)==3u&&active(&trk[1])==1u&&active(&trk[2])==1u&&active(&trk[3])==1u);
    setup();t=&trk[0];p5_patch_t *p=p5_patch_of(t);
    p->raw[P5_RELEASE_AMP]=90;t->p[P_REL]=0;
    trk_note_on(t,60,100);for(uint32_t k=0;k<32;k++)tick();trk_note_off(t,60);
    tick();tick();check("common REL=0 does not terminate native release",active(t)==1u);
    for(uint32_t k=0;k<FS*8/CTL;k++)tick();
    check("native amp envelope frees the released voice",active(t)==0u);
    for(uint32_t mode=0;mode<2;mode++){
        setup();p=p5_patch_of(&trk[0]);p->raw[P5_LEVEL_A]=p->raw[P5_LEVEL_B]=p->raw[P5_NOISE]=0;
        p->raw[P5_RESONANCE]=120;p->raw[P5_CUTOFF]=60;p->raw[P5_FILTER_REV]=(uint8_t)mode;
        trk_note_on(&trk[0],60,100);int32_t peak=0;for(uint32_t k=0;k<FS/CTL;k++){int32_t x=tick();if(x>peak)peak=x;}
        check("native self-oscillation starts with no oscillator or mixer noise",peak>100);
    }
    setup();p=p5_patch_of(&trk[0]);p->raw[P5_VEL_AMP]=0;trk_note_on(&trk[0],60,30);
    int32_t lo=0;for(uint32_t k=0;k<100;k++){int32_t a=tick();if(a>lo)lo=a;}
    setup();trk_note_on(&trk[0],60,127);
    int32_t hi=0;for(uint32_t k=0;k<100;k++){int32_t a=tick();if(a>hi)hi=a;}
    check("native velocity disabled bypasses common velocity gain",lo==hi&&lo>100);
}
static void filter_tests(void)
{
    int bounded=1, distinct=0;
    int32_t seed=42;
    for(uint32_t cut=0;cut<=127;cut+=7)for(uint32_t res=0;res<=127;res+=7){
        p5_voice_t a={0},b={0};
        for(uint32_t k=0;k<2048;k++){
            int32_t x=(int32_t)(noise32(&seed)>>16)-32768;
            int32_t ya=p5_filter(&a,x,(int32_t)cut*256,(int32_t)res,1);
            int32_t yb=p5_filter(&b,x,(int32_t)cut*256,(int32_t)res,0);
            bounded &= ya>=-32768&&ya<=32767&&yb>=-32768&&yb<=32767;
            distinct |= ya!=yb;
            for(uint32_t j=0;j<4;j++)bounded &= abs(a.z[j])<60000&&abs(b.z[j])<60000;
        }
    }
    check("both four-pole modes stable across cutoff/resonance sweep",bounded);
    check("Curtis and SSI character models produce distinct output",distinct);
    for(uint32_t mode=0;mode<=1;mode++){
        p5_voice_t s={0};double power=0;
        for(uint32_t k=0;k<FS*2;k++){
            int32_t y=p5_filter(&s,k==0?10000:0,82*256,127,(int)mode);
            if(k>FS)power+=(double)y*y;
        }
        printf("prophet: %s self-oscillation RMS %.1f\n",mode?"Curtis":"SSI",sqrt(power/FS));
        check("resonance can sustain self-oscillation without oscillators",power/FS>10000);
    }
    setup();p5_patch_t *p=p5_patch_of(&trk[0]);
    p->raw[P5_RESONANCE]=127;p->raw[P5_SYNC]=1;p->raw[P5_POLY_B]=127;
    p->raw[P5_POLY_FREQ]=p->raw[P5_POLY_PW]=p->raw[P5_POLY_FILTER]=1;
    p->raw[P5_SAW_B]=p->raw[P5_PULSE_A]=p->raw[P5_PULSE_B]=p->raw[P5_TRI_B]=1;
    p->raw[P5_LEVEL_B]=127;
    for(uint32_t k=0;k<5;k++)trk_note_on(&trk[0],84+k,127);
    int32_t peak=0;
    uint64_t start=now_ns();
    for(uint32_t k=0;k<FS/CTL;k++){int32_t a=tick();if(a>peak)peak=a;}
    printf("prophet: five-voice host stress %.1f ms per rendered second, peak %d\n",(now_ns()-start)/1e6,peak);
    check("sync/audio-rate Poly-Mod at high pitch remains bounded",peak>100&&peak<5*VOICE_FS*3);
    /* Include the 127 endpoints and ultrasonic Poly-Mod rates in UBSan runs. */
    int extremes=1;
    for(uint32_t mode=0;mode<2;mode++)for(uint32_t cut=0;cut<=127;cut+=127)
    for(uint32_t res=0;res<=127;res+=127){
        setup();p=p5_patch_of(&trk[0]);
        p->raw[P5_FILTER_REV]=(uint8_t)mode;p->raw[P5_CUTOFF]=(uint8_t)cut;
        p->raw[P5_RESONANCE]=(uint8_t)res;p->raw[P5_FREQ_A]=p->raw[P5_FREQ_B]=127;
        p->raw[P5_SAW_B]=p->raw[P5_PULSE_A]=p->raw[P5_PULSE_B]=p->raw[P5_TRI_B]=1;
        p->raw[P5_SYNC]=p->raw[P5_POLY_FREQ]=p->raw[P5_POLY_PW]=p->raw[P5_POLY_FILTER]=1;
        p->raw[P5_POLY_B]=p->raw[P5_POLY_ENV]=127;
        for(uint32_t k=0;k<5;k++)trk_note_on(&trk[0],127-k,127);
        for(uint32_t k=0;k<256;k++)extremes &= tick()<5*VOICE_FS*3;
    }
    check("maximum pitch and cutoff/resonance endpoints remain bounded",extremes);

}
static void demos(void)
{
    const char *dir=getenv("P5_DEMOS");if(!dir)return;
    const char *names[]={"pad","brass","bass","sync","polymod"};
    for(uint32_t sound=0;sound<5;sound++)for(uint32_t mode=0;mode<2;mode++){
        setup();p5_patch_t *p=p5_patch_of(&trk[0]);uint8_t *r=p->raw;
        r[P5_FILTER_REV]=(uint8_t)mode;r[P5_SAW_B]=1;r[P5_LEVEL_B]=100;
        r[P5_CUTOFF]=65;r[P5_RESONANCE]=50;r[P5_ENV_FILTER]=70;r[P5_SUSTAIN_FILTER]=30;
        r[P5_ATTACK_AMP]=sound==0?65:0;r[P5_ATTACK_FILTER]=sound==1?42:0;
        r[P5_RELEASE_AMP]=sound==0?80:50;r[P5_FREQ_B]=25;
        if(sound==2){r[P5_CUTOFF]=45;r[P5_RESONANCE]=70;r[P5_SUSTAIN_AMP]=40;}
        if(sound>=3){r[P5_FREQ_A]=64;r[P5_SYNC]=sound==3;r[P5_POLY_B]=90;
                    r[P5_POLY_FREQ]=1;r[P5_POLY_ENV]=100;r[P5_POLY_FILTER]=sound==4;}
        uint32_t count=sound<2?5u:1u;
        for(uint32_t k=0;k<count;k++)trk_note_on(&trk[0],(sound==2?36:60)+k*3,100);
        char path[512];snprintf(path,sizeof path,"%s/%s-%s.wav",dir,names[sound],mode?"curtis":"ssi");
        FILE *f=fopen(path,"wb");if(!f){perror(path);failures++;continue;}wav_hdr(f,FS*3);
        for(uint32_t frame=0;frame<FS*3;frame+=CTL){
            if(frame>=FS&&frame<FS+CTL)for(uint32_t k=0;k<count;k++)trk_note_off(&trk[0],(sound==2?36:60)+k*3);
            int32_t out[CTL];track_render(&trk[0],out,CTL);
            for(uint32_t j=0;j<CTL&&frame+j<FS*3;j++)wav_put(f,out[j]/4,out[j]/4);
        }
        fclose(f);
    }
}
int main(int argc,char **argv)
{
    int sat_ok=1;int32_t previous=-1;
    for(int32_t x=0;x<=131072;x++){
        int32_t actual=p5_fast_softclip(x),expected=(int32_t)(32767*tanh((double)(x>65536?65536:x)/32768));
        sat_ok &= actual>=previous && abs(actual-expected)<=17 && p5_fast_softclip(-x)==-actual;
        previous=actual;
    }
    check("fast saturation stays odd, monotonic and within 17 Q15 units of tanh",sat_ok);
    int fraction_ok=1;uint32_t seed=43;for(uint32_t k=0;k<10000;k++){seed=seed*1664525u+1013904223u;uint32_t inc=(seed>>1)+1u;uint32_t distance=seed%inc;uint32_t exact=(uint32_t)(((uint64_t)distance<<15)/inc),fast=p5_sync_fraction(distance,inc);fraction_ok &= fast>=exact&&fast-exact<=1u;}
    check("32-bit hard-sync fraction stays within one Q15 unit of exact division",fraction_ok);
    native_tests();for(int k=1;k<argc;k++)factory_file(argv[k]);
    voice_tests();filter_tests();demos();
    printf("prophet: %d failure(s); patch %zu B, state %zu B per track\n",failures,sizeof(p5_patch_t),sizeof(p5_part_t));
    return failures?1:0;
}
