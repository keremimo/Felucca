/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MELODEE_CZ1_PATCH_H
#define MELODEE_CZ1_PATCH_H
#define ENGI_CZ1 14u
#define CZ_NAME 16u
/* Flat, byte-sized patch IDs: all native controls are available on device. */
enum { CZ_LINE, CZ_MOD, CZ_OCT, CZ_SIGN, CZ_DOCT, CZ_NOTE, CZ_FINE,
       CZ_VWAVE, CZ_VRATE, CZ_VDEP, CZ_VDELAY, CZ_BEND, CZ_WHEEL, CZ_ATV, CZ_ATA,
       CZ_MODON, CZ_PORTON, CZ_PORTMODE, CZ_PORTTIME, CZ_GLIDEON, CZ_GLIDENOTE, CZ_GLIDETIME,
       CZ_CHOR, CZ_GLOBALS };
#define CZ_LBASE(l) (CZ_GLOBALS + (l)*8u)
enum { CZ_W1, CZ_W2, CZ_KW, CZ_KA, CZ_LEVEL, CZ_VP, CZ_VW, CZ_VA };
#define CZ_EBASE(l,e) (CZ_GLOBALS+16u+((l)*3u+(e))*18u)
#define CZ_OLD_NP 147u
#define CZ_OLD_PACKED 163u
#define CZ_WIN(l) (CZ_OLD_NP+(l))
#define CZ_NP (CZ_OLD_NP+2u)
#define CZ_PACKED (CZ_NP+CZ_NAME)
static const uint8_t CZ_MAX[CZ_NP]={3,2,2,1,3,11,60,3,99,99,99,12,99,99,15,1,1,1,99,1,48,99,1,7,8,9,9,15,15,15,15,7,8,9,9,15,15,15,15,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,8,7,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,8,7,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,8,7,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,8,7,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,8,7,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,8,7,7,7};
static const int8_t CZ_PRESET_MIN[92]={0,0,0,0,0,-64,-64,-64,-64,0,0,0,0,-64,-64,-64,0,0,0,1,1,0,0,0,0,0,0,0,-24,1,0,0,1,0,0,0,0,0,0,-64,0,0,0,0,0,0,1,0,0,0,0,-64,0,0,-64,0,0,-64,0,0,-64,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,-64,-64,-64,-64,-64,-64,-64,0};
static const uint8_t CZ_PRESET_WIDTH[92]={7,7,7,7,7,7,7,7,7,7,3,7,7,7,7,7,7,3,4,2,7,7,7,1,1,4,4,3,6,6,4,7,7,7,7,7,7,2,7,7,1,1,2,1,7,2,4,3,7,4,5,7,4,5,7,4,5,7,4,5,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,4,3,4,7,7,7,7,7,7,7,4};
static const int8_t CZ_PRESET_MAX[92]={127,127,127,127,127,63,63,63,63,127,4,127,127,63,63,63,127,6,9,4,127,100,127,1,1,11,15,4,24,64,9,100,127,127,127,127,127,3,127,63,1,1,2,1,127,2,16,5,127,8,19,63,8,19,63,8,19,63,8,19,63,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,9,4,12,63,63,63,63,63,63,63,8};
/* 871 native control bits + 16 seven-bit name characters. */
#define CZ_COMPACT_BITS 983u
static int cz_valid(const uint8_t *p)
{
    for(uint32_t j=0;j<CZ_NP;j++)if(p[j]>CZ_MAX[j])return 0;
    for(uint32_t l=0;l<2;l++){
        uint32_t b=CZ_LBASE(l);
        if(!p[b+CZ_LEVEL])return 0;
        for(uint32_t e=0;e<3;e++){b=CZ_EBASE(l,e);if(p[b+16]!=8 && p[b+16]>p[b+17])return 0;if(p[b+8+p[b+17]])return 0;}
    }
    for(uint32_t j=CZ_NP;j<CZ_PACKED;j++)if(p[j]<32||p[j]>126)return 0;
    return 1;
}
/* Version 1 used panel wave numbers and an implicit resonance window.
 * Read it into the native carrier/window model without changing its sound. */
static int cz_upgrade(uint8_t *p,const uint8_t *old)
{
    static const uint8_t carrier[]={0,1,2,4,5,6,6,6};
    memcpy(p,old,CZ_OLD_NP);memcpy(p+CZ_NP,old+CZ_OLD_NP,CZ_NAME);
    for(uint32_t l=0;l<2;l++){
        uint32_t b=CZ_LBASE(l),w1=old[b],w2=old[b+1];
        if(w1>7||w2>8||(w1>=5&&w2>=6))return 0;
        p[CZ_WIN(l)]=(uint8_t)(w1>=5?w1-4:w2>=6?w2-5:0);
        p[b]=carrier[w1];p[b+1]=(uint8_t)(w2?carrier[w2-1]+1:0);
    }
    return cz_valid(p);
}
#endif
