#include <assert.h>
/* SPDX-License-Identifier: GPL-3.0-only */
/* Construct the frozen 92-parameter body from today's serialized project.
 * The eight appended drum controls do not belong to any older fixture. */
static void frozen_project92(uint8_t *dst,const uint8_t *src,uint32_t old_end)
{
    uint32_t from=68,to=68;
    memcpy(dst,src,68);dst[66]=92;
    for(uint32_t t=0;t<NTRK;t++){
        memcpy(dst+to,src+from,84);from+=P_E0;to+=84;
        memcpy(dst+to,src+from,8+2+NSTEP*9);from+=8+2+NSTEP*9;to+=8+2+NSTEP*9;
    }
    assert(old_end>=to);memcpy(dst+to,src+from,old_end-to);
    uint32_t rec=PROJ_REC_OFF-PROJ_PARAM_EXTRA;
    if(old_end>rec){
        project_t q;memset(&q,0,sizeof q);assert(proj_record_unpack(&q,src));
        uint32_t n=(old_end-rec)/8u;if(n>RECORD_MAX)n=RECORD_MAX;
        for(uint32_t i=0;i<n;i++)memcpy(dst+rec+i*8u,&q.recording[i],8u);
    }
}

static void frozen_template92(uint8_t *dst,const uint8_t *src,uint32_t old_end)
{
    uint32_t from=2u*G_COUNT+2u,to=from;memcpy(dst,src,to);
    for(uint32_t t=0;t<NTRK;t++){
        memcpy(dst+to,src+from,2+84*2);from+=2+P_E0*2;to+=2+84*2;
        memcpy(dst+to,src+from,8*2);from+=8*2;to+=8*2;
    }
    assert(old_end>=to);memcpy(dst+to,src+from,old_end-to);
}

/* Restore int16 timings of old bank schemas, including overlap-safe in-place fixtures. */
static void frozen_bank_tail(uint8_t *dst,const uint8_t *src,uint32_t n)
{
    uint32_t time=NTRK+NTRK*(NPAT-1u)*NSTEP*8u;
    uint8_t tail[512]={0};memcpy(tail,src+time,NTRK*NPAT*4u+CHAIN_ROWS*NTRK+MOTION_MAX+4u+NTRK*FM6_NFN);
    memmove(dst,src,time);
    for(uint32_t i=0;i<NTRK*NPAT*4u;i++){int16_t v=tail[i];memcpy(dst+time+i*2u,&v,2);}
    memcpy(dst+time+NTRK*NPAT*8u,tail+NTRK*NPAT*4u,n-time-NTRK*NPAT*8u);
}
