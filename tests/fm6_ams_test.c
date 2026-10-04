/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM6's AMS share (eng_fm6.c fm6_ams_pt) against Dexed's double math, for every modulation:
 * pt = exp((float)sa / 262144 * 0.07 + 12.2), sa = 1 .. 2^24 (host libm, as Dexed runs). */
#define main hostsim_main
#include "hostsim.c"
#undef main

int main(void)
{
    uint32_t sa, bad = 0;
    for (sa = 1; sa <= 1u << 24; sa++) {
        uint32_t want = (uint32_t)exp((float)sa / 262144 * 0.07 + 12.2), got = fm6_ams_pt(sa);
        if (want != got && bad++ < 8)
            printf("FAIL: sa %u: %u, Dexed %u\n", sa, got, want);
    }
    printf(bad ? "FM6 AMS: %u of 16777216 differ from Dexed\n" : "FM6 AMS: all %u modulations as Dexed figures them: OK\n",
           bad ? bad : 1u << 24);
    return bad != 0;
}
