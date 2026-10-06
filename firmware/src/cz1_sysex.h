/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Casio CZ-1 MIDI specification pp. 83-98: 128/144 logical bytes,
 * transmitted low nibble first. Keyboard function settings are separate. */
#ifndef MELODEE_CZ1_SYSEX_H
#define MELODEE_CZ1_SYSEX_H
static uint32_t cz_sx_rate(uint32_t e,uint32_t a){return e==0?a*127/99:e==1?8+a*119/99:a*119/99;}
static uint32_t cz_sx_level(uint32_t e,uint32_t a){return e==0?a+(a>63?4:0):e==1?a*127/99:a?a+28:0;}
static uint32_t cz_sx_unrate(uint32_t e,uint32_t b){b&=127;if(e==1)b=b>8?b-8:0;return b?(b*99/(e==0?127:119)+1>99?99:b*99/(e==0?127:119)+1):0;}
static uint32_t cz_sx_unlevel(uint32_t e,uint32_t b){b&=127;uint32_t a=e==0?(b>63?b-4:b):e==1?(b==127?99:b?b*99/127+1:0):b>28?b-28:0;return a>99?99:a;}
static uint32_t cz_sx_vdata(uint32_t n,uint32_t kind)
{
    uint32_t group=n<32?0:(n-16)/16,step=1u<<group;
    if(kind==1){static const uint16_t start[]={0x20,0x460,0x8e0,0x11e0,0x23e0,0x47e0};return start[group]+32*step*(n-(group?16+16*group:0));}
    uint32_t v=group?((n-16-16*group)*step+((17u<<group)-1u)):n;
    return kind==2?(n==99?0x300:v+step):v;
}
static void cz_sx_setwave(uint8_t *d,uint32_t w1,uint32_t w2,uint32_t window,uint32_t mod)
{
    d[0]=(uint8_t)(w1<<5 | (w2?(w2-1)<<2|2:0) | window>>2);
    d[1]=(uint8_t)((window&3)<<6 | (mod==1?4:mod==2?3:0)<<3);
}
static void cz_sx_encode(const uint8_t *p,uint8_t *d,int native)
{
    memset(d,0,144);uint32_t line=p[CZ_LINE];
    d[0]=(uint8_t)((line==2?3:line==3?2:line) | (p[CZ_OCT]==0?2:p[CZ_OCT]==2?1:0)<<2);
    d[1]=p[CZ_SIGN];/* Casio p. 84: convert the display value to six bits, then put
     * those bits in positions 7..2 of PDETL (not positions 5..0). */
    d[2]=(uint8_t)((p[CZ_FINE]+(p[CZ_FINE]?((p[CZ_FINE]-1)/15):0))<<2);d[3]=(uint8_t)(p[CZ_DOCT]*12+p[CZ_NOTE]);
    static const uint8_t vw[]={8,4,32,2};d[4]=vw[p[CZ_VWAVE]];
    /* The display byte precedes the machine value's low byte, then high byte
     * (Casio p. 85; e.g. RATE 50 is 32 e0 09 on the wire). */
    for(uint32_t j=0;j<3;j++){uint32_t val=p[j==0?CZ_VDELAY:j==1?CZ_VRATE:CZ_VDEP],v=cz_sx_vdata(val,j);d[5+j*3]=(uint8_t)val;d[6+j*3]=(uint8_t)v;d[7+j*3]=(uint8_t)(v>>8);}
    static const uint8_t ka[]={0,8,17,26,36,47,58,69,82,95},kw[]={0,31,44,57,70,83,96,111,146,255};
    for(uint32_t l=0;l<2;l++){
        uint32_t b=CZ_LBASE(l),o=l?71:14;cz_sx_setwave(d+o,p[b+CZ_W1],p[b+CZ_W2],p[CZ_WIN(l)],l?0:p[CZ_MOD]);
        d[o+2]=(uint8_t)(p[b+CZ_KA] | (native?(15-p[b+CZ_LEVEL])<<4:0));d[o+3]=ka[p[b+CZ_KA]];
        d[o+4]=p[b+CZ_KW];d[o+5]=kw[p[b+CZ_KW]];
        for(uint32_t e=0;e<3;e++){
            const uint8_t *ep=p+CZ_EBASE(l,e);uint32_t off=o+(e==2?6:e==1?23:40);
            d[off]=(uint8_t)(ep[17] | (native?(15-p[b+(e==0?CZ_VP:e==1?CZ_VW:CZ_VA)])<<4:0));
            uint32_t prev=0;
            for(uint32_t j=0;j<8;j++){uint32_t val=ep[8+j];d[off+1+2*j]=(uint8_t)(cz_sx_rate(e,ep[j]) | (val<prev?128:0));d[off+2+2*j]=(uint8_t)(cz_sx_level(e,val) | (ep[16]==j?128:0));prev=val;}
        }
    }
    if(native)memcpy(d+128,p+CZ_NP,16);
}
/* Function settings and the name of a legacy tone retain their current values. */
static int cz_sx_decode(uint8_t *p,const uint8_t *d,uint32_t n)
{
    if(n!=128&&n!=144)return 0;
    if(((d[0]>>2)&3)==3 ||
       (d[4]!=8&&d[4]!=4&&d[4]!=32&&d[4]!=2))return 0;
    uint32_t line=d[0]&3,oct=(d[0]>>2)&3;p[CZ_LINE]=(uint8_t)(line==2?3:line==3?2:line);p[CZ_OCT]=(uint8_t)(oct==2?0:oct==1?2:1);
    p[CZ_SIGN]=d[1];uint32_t fine=d[2]>>2;p[CZ_FINE]=(uint8_t)(fine-(fine>>4));p[CZ_DOCT]=d[3]/12;p[CZ_NOTE]=d[3]%12;
    p[CZ_VWAVE]=(uint8_t)(d[4]==4?1:d[4]==32?2:d[4]==2?3:0);p[CZ_VDELAY]=d[5];p[CZ_VRATE]=d[8];p[CZ_VDEP]=d[11];
    uint32_t mb=(d[15]>>3)&7;p[CZ_MOD]=(uint8_t)(mb==3?2:mb==4?1:0);
    for(uint32_t l=0;l<2;l++){
        uint32_t b=CZ_LBASE(l),o=l?71:14;p[b+CZ_W1]=d[o]>>5;p[b+CZ_W2]=(uint8_t)((d[o]&2)?1+((d[o]>>2)&7):0);
        p[CZ_WIN(l)]=(uint8_t)(((d[o]&1)<<2)|(d[o+1]>>6));
        p[b+CZ_KA]=d[o+2]&15;p[b+CZ_KW]=d[o+4]&15;p[b+CZ_LEVEL]=(uint8_t)(n==144?15-(d[o+2]>>4):15);
        for(uint32_t e=0;e<3;e++){
            uint8_t *ep=p+CZ_EBASE(l,e);uint32_t off=o+(e==2?6:e==1?23:40);ep[17]=d[off]&15;ep[16]=8;
            p[b+(e==0?CZ_VP:e==1?CZ_VW:CZ_VA)]=(uint8_t)(n==144?15-(d[off]>>4):0);
            for(uint32_t j=0;j<8;j++){ep[j]=(uint8_t)cz_sx_unrate(e,d[off+1+2*j]);ep[8+j]=(uint8_t)cz_sx_unlevel(e,d[off+2+2*j]);if(d[off+2+2*j]&128)ep[16]=(uint8_t)j;}
            /* END is a zero-level target on the CZ, even when an external
             * editor left a nonzero level there (e.g. mu:zines String Bass).
             * Normalize this ignored value; invalid END indices still fail. */
            if(ep[17]<8)ep[8+ep[17]]=0;
        }
    }
    if(n==144)memcpy(p+CZ_NP,d+128,16);
    return cz_valid(p);
}
#endif
