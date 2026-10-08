/* SPDX-License-Identifier: GPL-3.0-only */
/* Single producer (CPU0 audio ISR), single consumer (CPU1). One outstanding
 * job. Completion publishes the result AND returns ownership of voice state.
 * CPU0 must join before events, engine changes, post processing or flash IO.
 * Platform hooks provide acquire/release accesses, time and fault handling. */
#pragma once
#include <stdint.h>
#ifndef AW_IDLE
#define AW_IDLE() ((void)0)
#endif
typedef void (*audio_worker_fn)(void *);
static struct {
    AW_WORD ready, request, complete;
    audio_worker_fn fn;
    void *context;
    uint32_t jobs, max_job_ticks, max_wait_ticks, timeouts;
} audio_worker;
static int audio_worker_online;

static int audio_worker_submit(audio_worker_fn fn, void *context)
{
    if (!audio_worker_online || AW_LOAD(&audio_worker.request) != AW_LOAD(&audio_worker.complete))
        return 0;
    audio_worker.fn = fn;
    audio_worker.context = context;
    AW_STORE(&audio_worker.request, AW_LOAD(&audio_worker.request) + 1u);
    return 1;
}

static void audio_worker_join(void)
{
    uint32_t start = AW_TICKS();
    while (AW_LOAD(&audio_worker.complete) != AW_LOAD(&audio_worker.request)) {
        if ((uint32_t)(AW_TICKS() - start) >= AW_TIMEOUT_TICKS) {
            audio_worker.timeouts++;
            /* A partly executed voice cannot be rerun on CPU0: reset, rather
             * than reuse a buffer or race the worker. Startup alone falls back. */
            AW_FAULT();
        }
    }
    uint32_t elapsed = AW_TICKS() - start;
    if (elapsed > audio_worker.max_wait_ticks) audio_worker.max_wait_ticks = elapsed;
}

/* This function and its polling helpers must stay in internal RAM on target.
 * It publishes completion only after a callback has returned from XIP. With
 * no job outstanding, CPU0 may turn XIP off for a flash write. Do not use idle:
 * CPU1 has no interrupt to wake it. */
AW_RAM_LOOP void audio_worker_loop(void)
{
    AW_STORE(&audio_worker.ready, 1u);
    for (;;) {
        uint32_t request = AW_LOAD(&audio_worker.request);
        if (request == AW_LOAD(&audio_worker.complete)) { AW_IDLE(); continue; }
        uint32_t start = AW_TICKS();
        audio_worker.fn(audio_worker.context);
        uint32_t elapsed = AW_TICKS() - start;
        audio_worker.jobs++;
        if (elapsed > audio_worker.max_job_ticks) audio_worker.max_job_ticks = elapsed;
        AW_STORE(&audio_worker.complete, request);
    }
}
