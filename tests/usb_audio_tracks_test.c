/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Real render path (fx.c mix_part -> track_capture -> usb_audio_stream.c ua_audio): Melodee In's four channels are
 * the four tracks, each isolated, after DIST, the SLICER and LEVEL, independent of the monitor's pan, sends and
 * MASTER; a silent block clears its channel. */
#include <assert.h>
#define MELODEE_USB_AUDIO 1
#define main hostsim_main
#include "hostsim.c"
#undef main

struct result { uint64_t hash, energy, monitor; };

static struct result render(uint32_t channel, int sliced, int variant)
{
    int fd[2], status;
    struct result result = {2166136261u, 0, 0};
    assert(pipe(fd) == 0);
    pid_t child = fork();                     /* fresh DSP state for each comparison */
    assert(child >= 0);
    if (!child) {
        int32_t out[CTL * 2];
        track_t *t = &trk[channel];
        close(fd[0]);
        host_tracks_init();
        for (uint32_t k = 0; k < NPART; k++) host_preset(&trk[k], 0, 5);
        t->p[P_DIST] = 60;
        t->p[P_SLCR] = sliced ? SL_GATE : SL_OFF;
        t->p[P_SLPAT] = 1;
        t->p[P_SLDEPTH] = 127;
        t->p[P_PAN] = variant == 1 ? -64 : variant == 2 ? 64 : 0;
        t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = variant == 1 ? 127 : 0;
        song.master_q12 = variant == 2 ? 0 : 4096;
        if (variant == 3)
            t->p[P_LEVEL] = 0;
        trk_note_on(t, 60, 100);
        for (uint32_t block = 0; block < 2048; block++) {
            mix_block(out, CTL);
            ua.cap_alt = 1;
            ua.cw = ua.cr = 0;
            ua_audio(out, track_capture, CTL, song.master_q12);
            for (uint32_t i = 0; i < CTL; i++) {
                for (uint32_t ch = 0; ch < NTRK; ch++) {
                    int32_t x = track_capture[i * NTRK + ch];
                    assert(ua.cap[i * NTRK + ch] == ua_clip(x));
                    if (ch != channel) assert(x == 0);
                    else {
                        result.hash = (result.hash ^ (uint32_t)x) * 16777619u;
                        result.energy += (int64_t)x * x;
                    }
                }
                result.monitor += (int64_t)out[i * 2] * out[i * 2];
                result.monitor += (int64_t)out[i * 2 + 1] * out[i * 2 + 1];
            }
        }
        memset(trk, 0, sizeof trk);
        memset(sl, 0, sizeof sl);
        mix_block(out, CTL);
        for (uint32_t i = 0; i < CTL * NTRK; i++) assert(track_capture[i] == 0);
        assert(write(fd[1], &result, sizeof result) == sizeof result);
        _exit(0);
    }
    close(fd[1]);
    assert(read(fd[0], &result, sizeof result) == sizeof result);
    close(fd[0]);
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    return result;
}

int main(void)
{
    for (uint32_t ch = 0; ch < NTRK; ch++) {
        struct result dry = render(ch, 0, 0);
        assert(dry.energy && dry.monitor);
        for (int sliced = 0; sliced <= 1; sliced++) {
            struct result base = render(ch, sliced, 0);
            assert(base.energy && base.monitor);
            if (sliced) assert(base.hash != dry.hash);
            for (int variant = 1; variant <= 2; variant++) {
                struct result changed = render(ch, sliced, variant);
                assert(changed.hash == base.hash && changed.energy == base.energy);
                if (variant == 2) assert(changed.monitor == 0);
            }
            assert(render(ch, sliced, 3).energy == 0);
        }
    }
    puts("USB track capture: tracks 1..4 isolated, SLICER, level, pan/FX/master isolation, stale blocks: OK");
    return 0;
}
