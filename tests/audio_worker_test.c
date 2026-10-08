/* SPDX-License-Identifier: GPL-3.0-only */
/* Bounded fault handling and wrap-safe deadlines without waiting in real time. */
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
static jmp_buf fault;
static uint32_t clock_value, callback_calls;
#define AW_WORD uint32_t
#define AW_LOAD(p) (*(p))
#define AW_STORE(p,v) (*(p) = (v))
#define AW_TICKS() (clock_value++)
#define AW_TIMEOUT_TICKS 10u
#define AW_FAULT() longjmp(fault, 1)
#define AW_RAM_LOOP static
#include "../firmware/src/audio_worker.h"
static void callback(void *context) { (void)context; callback_calls++; }
int main(void)
{
    assert(!audio_worker_submit(callback, 0));
    audio_worker_online = 1;
    assert(audio_worker_submit(callback, &callback_calls));
    assert(!audio_worker_submit(callback, 0));
    assert(audio_worker.context == &callback_calls);
    clock_value = UINT32_MAX - 4u;
    if (!setjmp(fault)) {
        audio_worker_join();
        assert(0 && "stalled job must trigger reset hook");
    }
    assert(audio_worker.timeouts == 1);
    assert(callback_calls == 0);
    assert(audio_worker.request != audio_worker.complete);
    assert(!audio_worker_submit(callback, 0));
    audio_worker.complete = audio_worker.request;
    audio_worker_join();
    assert(audio_worker.timeouts == 1);
    puts("audio worker: busy/offline, bounded stall, wrapping deadline and no unsafe rerun ok");
}
