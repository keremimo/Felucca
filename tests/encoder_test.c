/* SPDX-License-Identifier: GPL-3.0-only */
/* Feed the real matrix decoder one encoder state per frame. */
#include <stdio.h>
#include <string.h>
#include "../firmware/hal/fm1_input.h"

static unsigned failures;

static void reset_decoder(void)
{
    unsigned e;
    memset((void *)&fm1_in, 0, sizeof fm1_in);
    for (e = 0; e < FM1_NENC; e++)
        fm1_in.enc_prev[e] = fm1_in.enc_last[e] = 0xFF;
}

static void hold(unsigned e, unsigned state, unsigned frames)
{
    const uint8_t *m = FM1_ENC[e];
    unsigned i;
    for (i = 0; i < frames; i++) {
        memset((void *)fm1_in.raw, 0, sizeof fm1_in.raw);
        fm1_in.raw[m[0]] |= (uint8_t)(((state >> 1) & 1u) << m[1]);
        fm1_in.raw[m[2]] |= (uint8_t)((state & 1u) << m[3]);
        fm1__frame();
    }
}

static void expect(unsigned e, int steps, const char *case_name)
{
    if (fm1_in.enc_steps[e] != steps) {
        fprintf(stderr, "encoder %u %s: got %d, expected %d\n",
                e, case_name, fm1_in.enc_steps[e], steps);
        failures++;
    }
}

int main(void)
{
    unsigned e;
    for (e = 0; e < FM1_NENC; e++) {
        reset_decoder();
        hold(e, 0, FM1_REST_FRAMES + 2);
        hold(e, 1, 3);
        hold(e, 3, FM1_REST_FRAMES + 2);
        expect(e, 1, "first half-cycle click");
        hold(e, 2, 3);
        hold(e, 0, 3);
        expect(e, 2, "second half-cycle click");

        reset_decoder();
        hold(e, 0, FM1_REST_FRAMES + 2);
        hold(e, 1, 3);
        hold(e, 0, 3);
        expect(e, 0, "reversed transition");
        hold(e, 1, 3);
        hold(e, 3, 3);
        hold(e, 2, 3);
        hold(e, 0, 3);
        expect(e, 1, "full-cycle click");

        reset_decoder();
        hold(e, 0, FM1_REST_FRAMES + 2);
        hold(e, 2, 3);
        hold(e, 3, FM1_REST_FRAMES + 2);
        expect(e, -1, "first reverse click");
    }
    if (failures)
        return 1;
    puts("encoder decoder passed");
    return 0;
}
