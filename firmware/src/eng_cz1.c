/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* CZ-1 sound engine. Integer phase distortion uses Melodee's CrispyZebra
 * oscillator. Six independent eight-stage envelopes retain rate/level,
 * sustain and end semantics. Timing and response curves are approximations. */
#ifdef FM1_IRQ_TARGET
#define CZ_HOT __attribute__((section(".dsp_text")))
static inline __attribute__((always_inline)) void *cz_far(void *p){void *volatile q=p;return q;}
#define CZ_FAR(f) ((__typeof__(&(f)))cz_far((void *)&(f)))
#else
#define CZ_HOT
#define CZ_FAR(f) (f)
#endif
static inline __attribute__((always_inline)) int32_t cz_clamp(int32_t x,int32_t a,int32_t b){return x<a?a:x>b?b:x;}
static inline __attribute__((always_inline)) int32_t cz_tri(uint32_t ph){int32_t x=(int32_t)(ph>>15);return x<65536?x-32768:98303-x;}
static inline __attribute__((always_inline)) int32_t cz_mul(int32_t a,int32_t b){return (a*b)>>15;}
static inline __attribute__((always_inline)) uint32_t cz_noise(int32_t *p){uint32_t x=(uint32_t)*p;x^=x<<13;x^=x>>17;x^=x<<5;*p=(int32_t)x;return x;}

#include "cz1_sine.h"
static inline __attribute__((always_inline)) int32_t cz_sine(uint32_t ph){return CZ_SINE[ph>>20];}
#include "cz1_tables.h"
#include "cz1_patch.h"
static uint8_t cz_patch[NTRK][CZ_PACKED], cz_compare[NTRK][CZ_PACKED], cz_slot[NTRK];
static void cz_accept_patch(uint32_t tr){memcpy(cz_compare[tr],cz_patch[tr],CZ_PACKED);}
static const char *const CZ_LINES[]={"LINE1","LINE2","1+2","1+1"};
static const char *const CZ_MODES[]={"OFF","RING","NOISE"};
static const char *const CZ_VWAVES[]={"TRI","SAW UP","SAW DN","SQR"};
static const char *const CZ_NAMES[]={"INIT","CZ BRASS","CZ STRINGS","CZ BELL","CZ BASS","CZ ORGAN","CZ RESO","CZ RING","CZ NOISE"};
typedef struct { int32_t value, target, step; uint8_t stage, hold, done, gate; } cz_env_t;
typedef struct {
    cz_env_t env[2][3];
    uint32_t ph[2], age, vphase; int32_t noise, dc, smooth;
    uint8_t toggle[2], gate;
    int32_t porta, glide;
} cz_voice_t;
typedef struct { cz_voice_t v[NPOLY]; int32_t last_note; } cz_part_t;
static inline __attribute__((always_inline)) cz_part_t *cz_part(uint32_t tr);
static inline __attribute__((always_inline)) uint32_t cz_tr(const track_t *t) { return (uint32_t)(t-trk)%NTRK; }
static void cz_default(uint8_t *p, uint32_t slot)
{
    memset(p,0,CZ_PACKED);p[CZ_LINE]=2;p[CZ_OCT]=1;p[CZ_VRATE]=50;p[CZ_BEND]=2;
    p[CZ_WHEEL]=50;p[CZ_ATV]=50;p[CZ_ATA]=4;p[CZ_MODON]=1;p[CZ_CHOR]=1;p[CZ_GLIDENOTE]=24;
    p[CZ_PORTTIME]=40;p[CZ_GLIDETIME]=40;p[CZ_FINE]=slot==2?12:0;
    for(uint32_t l=0;l<2;l++) {
        uint32_t b=CZ_LBASE(l);p[b+CZ_LEVEL]=15;p[b+CZ_VA]=8;
        p[b+CZ_W1]=slot==3||slot==6?6:slot==5?4:0;
        p[CZ_WIN(l)]=slot==3?2:slot==6?1:0;
        p[b+CZ_W2]=slot==1?2:0;
        for(uint32_t e=0;e<3;e++) {
            b=CZ_EBASE(l,e);for(uint32_t j=0;j<8;j++)p[b+j]=70;
            p[b+16]=1;p[b+17]=2;
            p[b+8]=e==0?0:99;p[b+9]=e==0?0:e==1?65:80;
            if(slot==2)p[b]=45;
            if(slot==3 || slot==4){p[b+9]=0;p[b+16]=8;p[b+17]=1;}
            p[b+1]=slot==3?45:slot==4?65:70;p[b+2]=slot==2?45:65;
        }
    }
    if(slot==7)p[CZ_MOD]=1;
    if(slot==8)p[CZ_MOD]=2;
    if(slot==4)p[CZ_OCT]=0;
    if(slot==3)p[CZ_NOTE]=7;
    memset(p+CZ_NP,' ',CZ_NAME);for(uint32_t j=0;j<CZ_NAME&&CZ_NAMES[slot][j];j++)p[CZ_NP+j]=(uint8_t)CZ_NAMES[slot][j];
}
static void cz_init(void) { for(uint32_t t=0;t<NTRK;t++){cz_default(cz_patch[t],0);cz_slot[t]=0;cz_accept_patch(t);} }
static void cz_track_loaded(track_t *t,int factory)
{
    uint32_t tr=cz_tr(t),slot=(uint32_t)t->p[P_E7];
    if(t->eng_req!=ENGI_CZ1)return;
    if(factory || slot!=cz_slot[tr]){cz_default(cz_patch[tr],slot%NELEM(CZ_NAMES));cz_slot[tr]=(uint8_t)slot;cz_accept_patch(tr);}
}
static void cz_poll(void){for(uint32_t t=0;t<NTRK;t++)cz_track_loaded(&trk[t],0);}
static void CZ_HOT cz_env_stage(cz_env_t *s,const uint8_t *p,uint32_t e,uint32_t ka,uint32_t note,int32_t macro)
{
    uint32_t stage=s->stage>p[17]?p[17]:s->stage;
    s->stage=(uint8_t)stage;
    s->target=stage==p[17]?0:(int32_t)p[8+stage]*169466; /* 99 = Q24 */
    uint32_t rate=(uint32_t)cz_clamp((int32_t)p[stage]+macro,0,99);
    int32_t step=(int32_t)(e==0?CZ_PSTEP[rate]:CZ_STEP[rate]);
    if(e==2 && ka && note>48)step+=step*(int32_t)(ka*(note-48))/48;
    s->step=s->target>=s->value?step:-step;
    s->hold=0;
}
static int32_t cz_env_tick(cz_env_t *s,const uint8_t *p,uint32_t e,uint32_t ka,uint32_t note,int32_t macro)
{
    if(s->done || s->hold)return s->value;
    if(!s->step && s->target!=s->value)return s->value;
    int32_t x=s->value+s->step;
    if((s->step>=0 && x>=s->target) || (s->step<0 && x<=s->target)) {
        s->value=s->target;
        if(s->stage>=p[17]){s->done=1;s->value=0;}
        else if(s->gate && s->stage==p[16])s->hold=1;
        else {s->stage++;cz_env_stage(s,p,e,ka,note,macro);}
    }else s->value=x;
    return s->value;
}
/* Advance exactly n ticks, skipping linear spans and held/silent envelopes. */
static void CZ_HOT cz_env_advance(cz_env_t *s,const uint8_t *p,uint32_t e,uint32_t ka,uint32_t note,uint32_t mods,uint32_t n)
{
    while(n && !s->done && !s->hold){
        int32_t d=s->target-s->value;
        uint32_t step=(uint32_t)(s->step<0?-s->step:s->step);
        if(!step && d)return;
        uint32_t dist=(uint32_t)(d<0?-d:d), ticks=step?(dist+step-1u)/step:1u;
        if(!ticks)ticks=1;
        if(ticks>n){s->value+=s->step*(int32_t)n;return;}
        n-=ticks;s->value=s->target;
        if(s->stage>=p[17]){s->done=1;s->value=0;}
        else if(s->gate && s->stage==p[16])s->hold=1;
        else{s->stage++;cz_env_stage(s,p,e,ka,note,(int32_t)((mods>>(s->gate?(s->stage==0?0:7):14))&127u)-64);}
    }
}
static void cz_note_on(track_t *t,voice_t *v)
{
    uint32_t tr=cz_tr(t),vi=(uint32_t)(v-t->v);cz_part_t *p=cz_part(tr);cz_voice_t *s=&p->v[vi%NPOLY];
    int32_t previous=p->last_note;memset(s,0,sizeof *s);s->noise=(int32_t)(0x731ac19u+v->note*977u+vi*119u);s->gate=1;
    s->porta=(cz_patch[tr][CZ_PORTON]&&previous)?previous:(int32_t)v->note*4096;
    s->glide=cz_patch[tr][CZ_GLIDEON]?((int32_t)cz_patch[tr][CZ_GLIDENOTE]-24)*4096:0;p->last_note=(int32_t)v->note*4096;
    for(uint32_t l=0;l<2;l++)for(uint32_t e=0;e<3;e++){
        cz_env_t *senv=&s->env[l][e];senv->gate=1;
        CZ_FAR(cz_env_stage)(senv,&cz_patch[tr][CZ_EBASE(l,e)],e,cz_patch[tr][CZ_LBASE(l)+CZ_KA],v->note,t->p[P_E2]);
    }
}
static void cz_legato(track_t *t,voice_t *v)
{
    cz_voice_t *s=&cz_part(cz_tr(t))->v[(uint32_t)(v-t->v)%NPOLY];
    s->gate=1;
    for(uint32_t l=0;l<2;l++)for(uint32_t e=0;e<3;e++)s->env[l][e].gate=1;
}
static int cz_done(track_t *t,voice_t *v)
{
    cz_voice_t *s=&cz_part(cz_tr(t))->v[(uint32_t)(v-t->v)%NPOLY];uint32_t line=cz_patch[cz_tr(t)][CZ_LINE];
    return line==0?s->env[0][2].done:line==1?s->env[1][2].done:line==3?s->env[0][2].done:s->env[0][2].done&&s->env[1][2].done;
}
static int32_t CZ_HOT cz_lookup(const uint32_t *tab,int32_t value)
{
    uint32_t x=(uint32_t)cz_clamp(value>>8,0,65535)*99u, j=x>>16;
    return j>=99?(int32_t)tab[99]:(int32_t)tab[j]+(int32_t)(((int64_t)((int32_t)tab[j+1]-(int32_t)tab[j])*(x&65535u))>>16);
}
static inline __attribute__((always_inline)) int32_t cz_pd_cos(uint32_t ph){return -cz_sine(((ph&65535u)<<16)+0x40000000u);}
static inline __attribute__((always_inline)) uint32_t cz_pd_slope(uint32_t span,uint32_t len){return (span<<16)/(len?len:1u);}
static void CZ_HOT cz_pd_setup(pd_t *b, uint32_t w, uint32_t dcw)
{
    uint32_t x;
    b->w = w;
    switch (w) {
    case 0:                                                  /* SAW */
        x = 32768u + ((dcw * 32767u) >> 16);
        b->k0 = cz_pd_slope(32768u, x);
        b->k1 = cz_pd_slope(32767u, 65535u - x);
        break;
    case 1:                                                  /* SQUARE */
        x = 32768u - ((dcw * 31120u) >> 16);
        b->k0 = cz_pd_slope(32768u, x);
        b->k1 = cz_pd_slope(32767u, x);
        break;
    case 7:                                                  /* PULSE2: doubled carrier */
    case 2:                                                  /* PULSE */
        x = 65535u - ((dcw * 63487u) >> 16);
        b->k0 = cz_pd_slope(65535u, x);
        break;
    case 4:                                                  /* DOUBLE SINE */
        /* Doubled carrier: two equal cycles at zero DCW; distortion
         * compresses the first into a pulse beside the broad second cycle.
         * The 1/32 minimum width is a provisional fit to Kasploosh's
         * hardware trace, not a measured DCW transfer curve. */
        x = 32768u - ((dcw * 30720u) >> 16);
        b->k0 = cz_pd_slope(65535u, x);
        b->k1 = cz_pd_slope(65535u, 65535u - x);
        break;
    case 5:                                                  /* SAW-PULSE */
        x = 65535u - ((dcw * 32767u) >> 16);
        b->peak = (x * (32768u + ((dcw * 29491u) >> 16))) >> 16;
        if (!b->peak)
            b->peak = 1;
        b->k0 = cz_pd_slope(32768u, b->peak);
        b->k1 = cz_pd_slope(32767u, x - b->peak);
        break;
    case 3:                                                  /* NULL: compressed half-cycle, then silence */
        x=32768u-((dcw*32768u)>>16);
        b->k0=cz_pd_slope(32768u,x);
        break;
    default:                                                 /* MULTI-SINE */
        x = 0;
        b->rez = dcw;
        break;
    }
    b->x = x;
}

/* one sample at phase ph (16 bit): bipolar Q15 */
static inline __attribute__((always_inline)) int32_t cz_pd_wave(const pd_t *b, uint32_t ph)
{
    uint32_t pd, x = b->x;
    switch (b->w) {
    case 0:                                                  /* SAW */
        pd = ph < x ? (ph * b->k0) >> 16 : 32768u + (((ph - x) * b->k1) >> 16);
        break;
    case 1:                                                  /* SQUARE */
        if (ph < x)
            pd = (ph * b->k0) >> 16;
        else if (ph < 32768u)
            pd = 32768u;
        else if (ph < 32768u + x)
            pd = 32768u + (((ph - 32768u) * b->k1) >> 16);
        else
            pd = 65535u;
        break;
    case 7: ph=(ph<<1)&65535u;                         /* PULSE2 */
        /* fall through */
    case 2:                                                  /* PULSE */
        pd = ph < x ? (ph * b->k0) >> 16 : 65535u;
        break;
    case 4:                                                  /* DOUBLE SINE */
        pd = ph < x ? (ph * b->k0) >> 16 : ((ph - x) * b->k1) >> 16;
        break;
    case 5:                                                  /* SAW-PULSE */
        if (ph >= x)
            pd = 65535u;
        else if (ph < b->peak)
            pd = (ph * b->k0) >> 16;
        else
            pd = 32768u + (((ph - b->peak) * b->k1) >> 16);
        break;
    case 3:                                                  /* NULL */
        /* At maximum depth hardware is silent after its DC-step attack. */
        pd=x<=1u?32768u:ph<x?(ph*b->k0)>>16:32768u;
        break;
    default:                                                 /* MULTI-SINE, about 15 cycles at full DCW */
        return cz_pd_cos(ph+((ph*b->rez)>>16)*14u);
    }
    return cz_pd_cos(pd);
}

static inline __attribute__((always_inline)) int32_t cz_wave(const pd_t *b,uint32_t ph,uint32_t dcw,uint32_t window)
{
    int32_t out=cz_pd_wave(b,ph);
    /* Native window is independent of the carrier and shared by both cycles.
     * 6/7 are aliases of 5 on hardware; retain their codes in the patch. */
    if(window){
        uint32_t win;
        if(window==1)win=65535u-ph;
        else if(window==2)win=ph<32768u?ph<<1:(65535u-ph)<<1;
        else if(window==3)win=ph<32768u?65535u:(65535u-ph)<<1;
        else if(window==4)win=ph<32768u?65535u-(ph<<1):0;
        else win=(ph&32767u)<<1;
        out=((((32767+out)*5)>>4)*(int32_t)win)>>16;
    }
    if(window){int32_t pure=cz_pd_cos(ph);out=pure+cz_mul(out-pure,(int32_t)(dcw>>1));}
    return out;
}
static void CZ_HOT cz_render(track_t *t,voice_t *v,int32_t *out,uint32_t n,const vmod_t *m)
{
    uint32_t tr=cz_tr(t),vi=(uint32_t)(v-t->v)%NPOLY;const uint8_t *p=cz_patch[tr];cz_voice_t *s=&cz_part(tr)->v[vi];
    uint32_t mode=p[CZ_LINE], src[2]={mode==1?1u:0u,mode==3?0u:1u}, count=mode<2?1:2;
    int32_t gain[2],vel[2][3],dca0[2],dca1[2],pitch[2],depth[2];uint32_t inc[2],window[2];pd_t b[2][2];
    if(!v->gate && s->gate){s->gate=0;for(uint32_t l=0;l<2;l++)for(uint32_t e=0;e<3;e++){
        cz_env_t *en=&s->env[l][e];const uint8_t *ep=p+CZ_EBASE(l,e);en->gate=0;
        if(!en->done){if(en->stage<=ep[16])en->stage=(uint8_t)(ep[16]<ep[17]?ep[16]+1:ep[17]);cz_env_stage(en,ep,e,p[CZ_LBASE(l)+CZ_KA],v->note,t->p[P_E4]);}
    }}
    /* Six envelopes advance at audio rate. Expensive oscillator coefficients
     * use the block midpoint; output gains ramp between both endpoints. */
    for(uint32_t l=0;l<2;l++) {
        uint32_t lb=CZ_LBASE(l);
        for(uint32_t e=0;e<3;e++)vel[l][e]=32767-(127-(v->mvel?v->mvel:v->vel))*p[lb+(e==0?CZ_VP:e==1?CZ_VW:CZ_VA)]*17;
        for(uint32_t e=0;e<3;e++){
            cz_env_t *en=&s->env[l][e];const uint8_t *ep=p+CZ_EBASE(l,e);
            if(!en->done){int32_t macro=en->gate?(en->stage==0?t->p[P_E2]:t->p[P_E3]):t->p[P_E4];
                uint8_t holding=en->hold;cz_env_stage(en,ep,e,p[lb+CZ_KA],v->note,macro);
                en->hold=holding&&en->value==en->target;}
        }
        dca0[l]=cz_lookup(CZ_GAIN,s->env[l][2].value);
        for(uint32_t e=0;e<3;e++){
            cz_env_t *en=&s->env[l][e];uint32_t mods=(uint32_t)(t->p[P_E2]+64)|((uint32_t)(t->p[P_E3]+64)<<7)|((uint32_t)(t->p[P_E4]+64)<<14);
            uint32_t half=n/2+1u;
            cz_env_advance(en,p+CZ_EBASE(l,e),e,p[lb+CZ_KA],v->note,mods,half);
            if(e==0)pitch[l]=cz_lookup(CZ_PITCH,en->value);if(e==1)depth[l]=cz_lookup(CZ_DEPTH,en->value);
            cz_env_advance(en,p+CZ_EBASE(l,e),e,p[lb+CZ_KA],v->note,mods,n-half);
        }
        dca1[l]=cz_lookup(CZ_GAIN,s->env[l][2].value);
    }
    int32_t target=v->note*4096;if(!p[CZ_PORTON])s->porta=target;if(!p[CZ_GLIDEON])s->glide=0;
    int32_t ps=(100-(int32_t)p[CZ_PORTTIME]);ps=ps*ps+1;
    if(p[CZ_PORTMODE])ps=ps*(target>s->porta?target-s->porta:s->porta-target)/4096+1;
    int32_t diff=target-s->porta;s->porta+=cz_clamp(diff,-ps,ps);
    int32_t gs=(100-(int32_t)p[CZ_GLIDETIME]);gs=gs*gs+1;s->glide-=cz_clamp(s->glide,-gs,gs);
    uint32_t vinc=(uint32_t)(1u+p[CZ_VRATE]*p[CZ_VRATE])*149u;s->vphase+=vinc*n;
    int32_t vib=p[CZ_VWAVE]==0?cz_tri(s->vphase):p[CZ_VWAVE]==1?(int32_t)(s->vphase>>16)-32768:p[CZ_VWAVE]==2?32767-(int32_t)(s->vphase>>16):s->vphase<0x80000000u?32767:-32767;
    uint32_t delay=p[CZ_VDELAY]*p[CZ_VDELAY]*22u;
    int32_t vd=cz_clamp(p[CZ_VDEP]+t->p[P_E6],0,99);if(p[CZ_MODON])vd+=(t->mw*p[CZ_WHEEL]+t->at*p[CZ_ATV])/127;
    vib=s->age<delay?0:cz_mul(vib,vd*32);s->age+=n;
    int32_t pb=(int32_t)t->bend_raw*p[CZ_BEND]/2;
    for(uint32_t j=0;j<count;j++){
        uint32_t l=src[j],lb=CZ_LBASE(l);
        int32_t dp=j?((int32_t)p[CZ_DOCT]*12+p[CZ_NOTE])*4096+(int32_t)p[CZ_FINE]*4096/100:0;if(p[CZ_SIGN])dp=-dp;dp+=j?(int32_t)t->p[P_E5]*4096/100:0;
        int32_t q=s->porta+s->glide+((int32_t)p[CZ_OCT]-1)*12*4096+dp+(int32_t)(((int64_t)pitch[l]*vel[l][0])>>15)+vib+pb;
        q+=m->plog/341+(int32_t)song.g[G_TUNE]*4096/100;
        inc[j]=CZ_FAR(pitch_inc)(cz_clamp(q>>8,0,2047));inc[j]+=(uint32_t)((int32_t)(inc[j]>>12)*((q&255)*236)/4096);
        int32_t d=cz_mul(depth[l]>>1,vel[l][1])*2+(int32_t)t->p[P_E0]*512+m->cutoff*2+(m->shape-(64<<8))*2;
        d=d*(432-cz_clamp((int32_t)v->note-60,0,48)*p[lb+CZ_KW])/432;
        depth[l]=cz_clamp(d,0,65535);cz_pd_setup(&b[j][0],p[lb+CZ_W1],(uint32_t)depth[l]);
        uint32_t wave2=p[lb+CZ_W2]?p[lb+CZ_W2]-1:p[lb+CZ_W1];
        window[j]=p[CZ_WIN(l)];cz_pd_setup(&b[j][1],wave2,(uint32_t)depth[l]);
        gain[j]=cz_mul(cz_clamp(p[lb+CZ_LEVEL]*2184+t->p[P_E1]*256,0,32767),vel[l][2]);
        if(p[CZ_MODON])gain[j]=cz_mul(gain[j],32767-(127-t->at)*p[CZ_ATA]*8);
    }
    int32_t ar[2], da[2];
    for(uint32_t j=0;j<count;j++){uint32_t l=src[j];ar[j]=dca0[l]*65536;da[j]=(dca1[l]-dca0[l])*65536/(int32_t)n;}
    int32_t amp=m->amp0*65536, damp=(m->amp1-m->amp0)*65536/(int32_t)n;
    for(uint32_t i=0;i<n;i++){
        uint32_t old=s->ph[0];
        int32_t w0=cz_wave(&b[0][s->toggle[0]],old>>16,(uint32_t)depth[src[0]],window[0]);
        s->ph[0]+=inc[0];if(s->ph[0]<old)s->toggle[0]^=1;
        int32_t x=cz_mul(w0,cz_mul(ar[0]>>16,gain[0]));ar[0]+=da[0];
        if(count==2){
            old=s->ph[1];uint32_t ph=p[CZ_MOD]==2?cz_noise(&s->noise):old;
            int32_t w1=cz_wave(&b[1][s->toggle[1]],ph>>16,(uint32_t)depth[src[1]],window[1]);
            s->ph[1]+=inc[1];if(s->ph[1]<old)s->toggle[1]^=1;
            if(p[CZ_MOD]==1)w1=cz_mul(w0,w1);
            x+=cz_mul(w1,cz_mul(ar[1]>>16,gain[1]));ar[1]+=da[1];
        }
        x/=2; /* fixed headroom, consistent between single and dual line */
        s->dc+=((x<<15)-s->dc)/1024;x-=s->dc>>15;
        s->smooth+=(x-s->smooth)/2;
        int32_t a=amp>>16;amp+=damp;
        out[i]+=cz_mul(s->smooth,a)*VOICE_FS/32768;
    }
}
static const preset_t CZ_PRESETS[]={
    {"INIT",{0,0,0,0,0,0,0,0},{0,0,127,40},0,0},
    {"CZ BRASS",{0,0,0,0,0,0,0,1},{0,0,127,40},0,0,FX(0,15,10,25)},
    {"CZ STRINGS",{0,0,0,0,0,0,0,2},{0,0,127,40},0,0,FX(0,40,10,35)},
    {"CZ BELL",{0,0,0,0,0,0,0,3},{0,0,127,40},0,0,FX(0,0,15,40)},
    {"CZ BASS",{0,0,0,0,0,0,0,4},{0,0,127,40},0,0},
    {"CZ ORGAN",{0,0,0,0,0,0,0,5},{0,0,127,40},0,0,FX(0,25,0,20)},
    {"CZ RESO",{0,0,0,0,0,0,0,6},{0,0,127,40},0,0},
    {"CZ RING",{0,0,0,0,0,0,0,7},{0,0,127,40},0,0},
    {"CZ NOISE",{0,0,0,0,0,0,0,8},{0,0,127,40},0,0}
};
static const engine_t ENG_CZ1={.name="CZ-1",.page_title={"TONE","ENVELOPE"},
 .edit={{"DCW",F_BIPCT,-64,63,0,0,0},{"LEVEL",F_BIPCT,-64,63,0,0,0},{"ATK",F_INT,-64,63,0,0,0},{"DEC",F_INT,-64,63,0,0,0},
 {"REL",F_INT,-64,63,0,0,0},{"DTN",F_BIPCT,-64,63,0,0,0},{"VIB",F_BIPCT,-64,63,0,0,0},{"PTCH",F_ENUM,0,8,0,CZ_NAMES,0}},
 .presets=CZ_PRESETS,.npresets=NELEM(CZ_PRESETS),.knob={P_E0,P_E2,P_E4,P_E7},.ownenv=1,.note_on=cz_note_on,.render=cz_render,.done=cz_done,.legato=cz_legato};

/* USB interrupt only collects bytes; the main loop validates and publishes. */
#define CZ_RX 296u
static uint8_t cz_rx[CZ_RX],cz_rx_on,cz_rx_req,cz_rx_ready,cz_rx_go,cz_rx_abort;
static uint16_t cz_rx_n;
static void cz_sx_byte(uint8_t b)
{
    if(b>=0xf8)return;
    if(b==0xf0){if(cz_rx_ready)return;cz_rx_on=1;cz_rx_n=0;cz_rx_req=cz_rx_go=0;}
    if(!cz_rx_on||cz_rx_ready)return;
    if((b&128)&&b!=0xf0&&b!=0xf7){cz_rx_on=cz_rx_req=cz_rx_go=0;cz_rx_abort=1;return;}
    if(cz_rx_n>=CZ_RX){cz_rx_on=cz_rx_req=cz_rx_go=0;cz_rx_abort=1;return;}
    cz_rx[cz_rx_n++]=b;
    if(cz_rx_n==2&&b!=0x44){cz_rx_on=0;return;}
    if(cz_rx_n==7 && cz_rx[2]==0 && cz_rx[3]==0 && (cz_rx[4]&0xf0)==0x70){RING_PUBLISH();cz_rx_req=1;}
    if(cz_rx_n==9&&(cz_rx[5]==0x10||cz_rx[5]==0x11)&&cz_rx[7]==cz_rx[4]&&b==0x31){RING_PUBLISH();cz_rx_go=1;}
    if(b==0xf7){cz_rx_on=0;RING_PUBLISH();cz_rx_ready=1;}
}
