/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM6's AMS share (eng_fm6.c fm6_ams_pt) against Dexed's double reference, within 4 ppm plus one output unit for every modulation:
 * pt = exp((float)sa / 262144 * 0.07 + 12.2), sa = 1 .. 2^24 (host libm, as Dexed runs). */
#define main hostsim_main
#include "hostsim.c"
#undef main

int main(void)
{
    uint32_t sa, bad = 0;
    double max_error=0;
    for (sa = 1; sa <= 1u << 24; sa++) {
        uint32_t want = (uint32_t)exp((float)sa / 262144 * 0.07 + 12.2), got = fm6_ams_pt(sa);
        double error=fabs((double)got-want)/want;if(error>max_error)max_error=error;
        if (fabs((double)got-want)>1.0+want*4e-6 && bad++ < 8)
            printf("FAIL: sa %u: %u, Dexed %u\n", sa, got, want);
    }
    printf("FM6 fp32 AMS: %u failures across 16777216 modulations, maximum relative error %.3g\n",bad,max_error);
    return bad != 0;
}
