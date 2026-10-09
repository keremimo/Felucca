#include <assert.h>
/* SPDX-License-Identifier: GPL-3.0-only */
/* Construct the frozen 92-parameter body from today's serialized project.
 * The eight appended drum controls do not belong to any older fixture. */
static void frozen_project92(uint8_t *dst,const uint8_t *src,uint32_t old_end)
{
    uint32_t from=68,to=68;
    memcpy(dst,src,68);dst[66]=92;
    for(uint32_t t=0;t<NTRK;t++){
        memcpy(dst+to,src+from,84);from+=92;to+=84;
        memcpy(dst+to,src+from,8+2+NSTEP*9);from+=8+2+NSTEP*9;to+=8+2+NSTEP*9;
    }
    assert(old_end>=to);memcpy(dst+to,src+from,old_end-to);
}

static void frozen_template92(uint8_t *dst,const uint8_t *src,uint32_t old_end)
{
    uint32_t from=2u*G_COUNT+2u,to=from;memcpy(dst,src,to);
    for(uint32_t t=0;t<NTRK;t++){
        memcpy(dst+to,src+from,2+84*2);from+=2+92*2;to+=2+84*2;
        memcpy(dst+to,src+from,8*2);from+=8*2;to+=8*2;
    }
    assert(old_end>=to);memcpy(dst+to,src+from,old_end-to);
}
