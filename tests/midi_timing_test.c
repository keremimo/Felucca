/* SPDX-License-Identifier: GPL-3.0-only
 * Timestamped TRS/USB recording through the real queue/parser and sequencer.
 * Use the firmware's 24 MHz clock, including its natural uint32 wrap. */
#include <stdint.h>
static uint32_t timing_now;
#define MIDI_TIME_NOW() timing_now
#define MIDI_TICKS_PER_MS 24000u
#define MIDI_CONTROL_NO_MAIN 1
#include "midi_control_test.c"

static void timing_reset(uint32_t source, uint32_t start)
{
    midi_render_timed = 0;
    timing_now = 0;
    midi_test_reset();
    song.g[G_CLOCK] = (int16_t)source;
    events_block(CTL);
    timing_now = start;
    song.rec = 1;
    trk[0].p[P_VOICE] = V_POLY;
    trk[0].p[P_SLEN] = 16;
    trk[0].p[P_SDIV] = 2;
    trk[0].p[P_SSWING] = song.g[G_SWING] = 0;
    memset(trk[0].step, 0, sizeof trk[0].step);
    for (uint32_t i = 0; i < NSTEP; i++) trk[0].step[i].time = ST_REST;
}
static void timing_packet(uint32_t time, uint32_t st, uint32_t note, uint32_t vel, uint32_t source)
{
    timing_now = time;
    fm1_ms = time / MIDI_TICKS_PER_MS;
    if (source == 2) {
        um_byte(st);
        if (st < 0xF8) { um_byte(note); um_byte(vel); }
    } else
        midi_enqueue((st >= 0xF8 ? 0xF : st >> 4) | st << 8 | note << 16 | vel << 24, source);
}
static int recorded_grid(uint32_t bpm, uint32_t source, uint32_t early, uint32_t burst)
{
    uint32_t start = 24000000u, bad = 0;
    timing_reset(source, start);
    timing_packet(start, 0xFA, 0, 0, source);
    for (uint32_t pulse = 0; pulse < 96; pulse++) {
        uint32_t at = start + (uint32_t)((uint64_t)pulse * 60000u * MIDI_TICKS_PER_MS / (bpm * 24u));
        /* Real MIDI serialization allows notes on either side of their clock.
         * The initial FM-1 tempo is 120 regardless of the master's BPM. */
        if (early && pulse && pulse % 6 == 0)
            timing_packet(at - MIDI_TICKS_PER_MS / 4u, 0x90, 60 + pulse / 6u, 100, source);
        timing_packet(at, 0xF8, 0, 0, source);
        if (pulse % 6 == 0) {
            if (!early || !pulse)
                timing_packet(at + MIDI_TICKS_PER_MS, 0x90, 60 + pulse / 6u, 100, source);
            timing_packet(at + 2u * MIDI_TICKS_PER_MS, 0x80, 60 + pulse / 6u, 0, source);
        }
        if (!burst || pulse % 3 == 2) events_block(CTL);
    }
    events_block(CTL);
    for (uint32_t i = 0; i < 16; i++)
        if (trk[0].step[i].time != ST_NOTE || trk[0].step[i].n != 1 || trk[0].step[i].note[0] != 60 + i)
            bad++;
    return bad;
}
static int grid_test(void)
{
    uint32_t bpm[] = {40, 73, 97, 120, 137, 173, 240};
    int bad = 0;
    for (uint32_t source = 1; source <= 2; source++)
        for (uint32_t early = 0; early < 2; early++)
            for (uint32_t burst = 0; burst < 2; burst++) {
                int errors = 0;
                for (uint32_t i = 0; i < NELEM(bpm); i++) errors += recorded_grid(bpm[i], source, early, burst);
                char label[100];
                snprintf(label, sizeof label, "%s quantized 16ths, %s clock, %s drain, 40..240 BPM", source == 2 ? "TRS" : "USB",
                         early ? "note before" : "note after", burst ? "batched" : "immediate");
                bad += check(label, !errors);
            }
    return bad;
}
static int timeline_test(void)
{
    int bad = 0;
    uint32_t start = 24000000u, interval = 500000u;
    timing_reset(2, start);
    timing_packet(start, 0xFA, 0, 0, 2);
    timing_packet(start, 0xF8, 0, 0, 2);
    midi_render_timed = 1;
    midi_render_time = start;
    events_block(CTL);
    uint32_t cursor = mi_r;
    timing_packet(start + 12000u, 0x90, 60, 100, 2);
    events_block(CTL);
    bad += check("a future MIDI event waits for its audio block", mi_r == cursor && !midi_owners[0]);
    midi_render_time = start + 24000u;
    events_block(CTL);
    bad += check("the event sounds on the first block reaching its timestamp", mi_r == mi_w && gate_note(&trk[0], 60));
    uint32_t previous = trk[0].seq_pos;
    int smooth = 1;
    /* CPU/wall time stays fixed while four audio blocks cover 128 samples. */
    for (uint32_t b = 1; b <= 4; b++) {
        midi_render_time = start + 24000u + b * CTL * 544u;
        events_block(CTL);
        uint32_t step = trk[0].seq_pos - previous;
        if (step < 34 || step > 37) smooth = 0;
        previous = trk[0].seq_pos;
    }
    bad += check("external phase advances evenly across all four audio blocks", smooth);
    midi_render_timed = 0;
    timing_reset(2, 0xFFF00000u);
    start = timing_now;
    timing_packet(start, 0xFA, 0, 0, 2); timing_packet(start, 0xF8, 0, 0, 2); events_block(CTL);
    for (uint32_t p = 1; p <= 6; p++) {
        timing_packet(start + p * interval, 0xF8, 0, 0, 2);
        /* fm1_ms has its own, independent wrap period. */
        fm1_ms = 1000u + p * 21u;
        midi_in_ms[(mi_w - 1u) % MQ] = fm1_ms;
        events_block(CTL);
    }
    bad += check("24 MHz timer wrap preserves the exact six-clock step boundary", trk[0].seq_idx == 1 && trk[0].seq_pos == 0);
    timing_reset(2, 24000000u);
    trk[0].seq_pos = 3072;
    trk[0].arp_pos = 2048;
    song.g[G_CLOCK] = 0;
    events_block(0);
    uint32_t internal_beat = FS * 60u / (uint32_t)song.g[G_BPM];
    bad += check("switching to INT preserves fractional sequence and ARP position",
                 trk[0].seq_pos == 3072u * internal_beat / 24576u && trk[0].arp_pos == 2048u * internal_beat / 24576u);
    return bad;
}
static int phase_test(void)
{
    int bad = 0;
    uint32_t start = 24000000u, time = start;
    timing_reset(2, start);
    timing_packet(start, 0xFA, 0, 0, 2); timing_packet(start, 0xF8, 0, 0, 2); events_block(CTL);
    int exact = 1;
    for (uint32_t pulse = 1; pulse <= 24u * 128u; pulse++) {
        /* Tempo switches and +/- 0.2 ms polling jitter cannot move musical phase. */
        uint32_t bpm = pulse < 192 ? 97 : pulse < 384 ? 173 : 137;
        time += 60000u * MIDI_TICKS_PER_MS / (bpm * 24u);
        uint32_t jitter = pulse % 2 ? 4800 : 0;
        timing_packet(time + jitter, 0xF8, 0, 0, 2); events_block(CTL);
        if (pulse % 6 == 0 && (trk[0].seq_idx != (pulse / 6) % 16 || trk[0].seq_pos != 0)) exact = 0;
    }
    bad += check("128 beats stay on exact steps through tempo changes and polling jitter", exact);
    return bad;
}
static int divisions_test(void)
{
    int bad = 0, exact = 1;
    static const uint32_t periods[] = {24576, 12288, 6144, 3072, 8192, 4096, 49152, 98304, 196608, 393216};
    for (uint32_t div = 0; div < NELEM(periods); div++) {
        uint32_t start = 24000000u;
        timing_reset(2, start);
        trk[0].p[P_SDIV] = (int16_t)div;
        trk[0].p[P_SLEN] = 64;
        timing_packet(start, 0xFA, 0, 0, 2);
        for (uint32_t pulse = 0; pulse <= 24u * 64u; pulse++) {
            uint32_t at = start + (uint32_t)((uint64_t)pulse * 60000u * MIDI_TICKS_PER_MS / (137u * 24u));
            timing_packet(at, 0xF8, 0, 0, 2); events_block(CTL);
            uint32_t units = pulse * 1024u;
            if (trk[0].seq_idx != (units / periods[div]) % 64u || trk[0].seq_pos != units % periods[div]) exact = 0;
        }
    }
    bad += check("all ten divisions, including triplets and 16-beat steps, keep exact phase", exact);
    return bad;
}
static int lengths_and_swing_test(void)
{
    int bad = 0, swing = 1;
    uint32_t start = 24000000u;
    timing_reset(2, start);
    timing_packet(start, 0xFA, 0, 0, 2);
    for (uint32_t pulse = 0; pulse <= 24; pulse++) {
        uint32_t at = start + pulse * 500000u;
        /* A serialized chord straddles the start of step 1. */
        if (pulse == 6) timing_packet(at - 12000u, 0x90, 60, 100, 2);
        timing_packet(at, 0xF8, 0, 0, 2);
        if (pulse == 6) {
            timing_packet(at + 12000u, 0x90, 64, 100, 2);
            timing_packet(at + 24000u, 0x90, 67, 100, 2);
        }
        /* Quantized length: two steps, ending at the start of step 3. */
        if (pulse == 18) {
            timing_packet(at + 12000u, 0x80, 60, 0, 2);
            timing_packet(at + 24000u, 0x80, 64, 0, 2);
            timing_packet(at + 36000u, 0x80, 67, 0, 2);
        }
        events_block(CTL);
    }
    bad += check("a chord straddling a boundary shares one onset and retains its full held length",
                 trk[0].step[1].n == 3 && trk[0].step[1].note[0] == 60 && trk[0].step[1].note[1] == 64 &&
                 trk[0].step[1].note[2] == 67 && trk[0].step[2].time == ST_TIE && trk[0].step[3].time == ST_TIE && step_gate(&trk[0].step[3]) < 8);
    timing_reset(2, start);
    trk[0].p[P_SSWING] = 100;
    timing_packet(start, 0xFA, 0, 0, 2);
    for (uint32_t pulse = 0; pulse <= 192; pulse++) {
        timing_packet(start + pulse * 500000u, 0xF8, 0, 0, 2); events_block(CTL);
        uint32_t units = pulse * 1024u, pair = units / 12288u, within = units % 12288u;
        /* SWING 100: 6144 + floor(6144*100/250) = 8601, then 3687. */
        uint32_t odd = within >= 8601u;
        if (trk[0].seq_idx != (2u * pair + odd) % 16u || trk[0].seq_pos != within - odd * 8601u) swing = 0;
    }
    bad += check("maximum swing preserves every alternating boundary over eight beats", swing);
    /* A note near the delayed odd onset rounds onto it, not the straight grid. */
    timing_packet(start + 193u * 500000u, 0x90, 72, 100, 2); events_block(CTL);
    bad += check("synced MIDI quantization follows the swung onset", trk[0].step[0].note[0] == 72 && !trk[0].step[1].n);
    return bad;
}
static int loop_replay_test(void)
{
    int bad = 0;
    for (uint32_t source = 0; source < 3; source++) {
        timing_reset(2, 24000000u);
        track_t *t = &trk[0];
        timing_packet(timing_now, 0xFA, 0, 0, 2);
        timing_packet(timing_now, 0xF8, 0, 0, 2);
        events_block(CTL);
        seq_advance(15u * 6144u + 5000u);
        /* Three paths share the same late last-step quantization. */
        if (source == 0) input_on(t, 60, 111);
        else { timing_packet(timing_now, 0x90, 60, 111, source); events_block(CTL); }
        seq_advance(2000);
        if (source == 0) input_off(t, 60);
        else { timing_packet(timing_now, 0x80, 60, 0, source); events_block(CTL); }
        bad += check("late loop-start input records only step zero, never both loop ends",
                     t->step[0].n == 1 && t->step[0].note[0] == 60 && t->step[15].time == ST_REST && !t->step[15].n);
        uint32_t captured = step_gate(&t->step[0]) * 6144u / 255u;
        bad += check("quantizing the onset preserves the 2000-unit note duration",
                     captured <= 2000u && captured + 25u > 2000u);
        bad += check("recorded velocity 111 stays 111 rather than becoming an accent",
                     t->step[0].vel == 111 && !(t->step[0].flags & SF_ACCENT));
        seq_stop(); song.rec = 0; t->p[P_SGATE] = 16;
        seq_start(); seq_advance(0);
        seq_advance(captured - 1u);
        bad += check("replay keeps the recorded note held despite a different track GATE", gate_note(t, 60));
        seq_advance(1);
        bad += check("replay releases the note at its captured duration", !gate_note(t, 60));
        seq_advance(16u * 6144u - captured - 1u);
        bad += check("replay has no extra onset on the final step", !gate_note(t, 60) && t->seq_idx == 15);
        seq_advance(1);
        bad += check("replay starts the note once when the loop returns to zero", gate_note(t, 60) && t->seq_idx == 0);
    }
    return bad;
}
static int recorded_gate_storage_test(void)
{
    timing_reset(2, 24000000u);
    trk[0].step[0] = (step_t){{60}, 1, ST_NOTE, 0, 111, 0, 83};
    pattern_commit(&trk[0]);
    pattern_request(&trk[0], 1);
    trk[0].step[2] = (step_t){{64}, 1, ST_NOTE, 0, 70, 0, 211};
    project_capture(&proj_scratch);
    int ok = bank_pack(proj_wire_u.raw, &proj_scratch, 1) && bank_valid(proj_wire_u.raw, BANK_STORE_SIZE);
    if (ok) {
        project_restore_runtime(&proj_scratch); bank_restore(proj_wire_u.raw);
        ok = trk[0].step[2].acc == 211 && pattern_at(0, 0)->step[0].acc == 83;
    }
    return check("recorded gates survive project save/load in active and inactive banks", ok);
}
static int old_formats_and_overdub_test(void)
{
    int bad = 0;
    timing_reset(2, 24000000u);
    trk[0].step[0] = (step_t){{60}, 1, ST_NOTE, 0, 96};
    project_capture(&proj_scratch);
    int ok = bank_pack(proj_wire_u.raw, &proj_scratch, 1);
    uint32_t magic = PROJ_MAGIC_V10, sum;
    memcpy(proj_wire_u.raw + 8, &magic, 4);
    sum = proj_hash(proj_wire_u.raw + 8, PROJ_STORE_SIZE - 4);
    memcpy(proj_wire_u.raw + 8 + PROJ_STORE_SIZE - 4, &sum, 4);
    magic = BANK_MAGIC_D; memcpy(proj_wire_u.raw, &magic, 4);
    bank_checksum(proj_wire_u.raw);
    ok &= bank_valid(proj_wire_u.raw, BANK_STORE_SIZE);
    if (ok) bank_upgrade(proj_wire_u.raw);
    ok &= bank_valid(proj_wire_u.raw, BANK_STORE_SIZE) && *(uint32_t *)proj_wire_u.raw == BANK_MAGIC &&
          *(uint32_t *)(proj_wire_u.raw + 8) == PROJ_MAGIC;
    bad += check("existing FUN10/FBKD projects migrate while retaining legacy GATE behavior", ok && !step_gate(&proj_scratch.t[0].step[0]));
    timing_reset(2, 24000000u);
    track_t *t = &trk[0]; seq_start(); seq_advance(0);
    input_on(t, 60, 111); seq_advance(14000); input_off(t, 60);
    seq_stop(); seq_start(); seq_advance(0);
    input_on(t, 60, 70); seq_advance(2000); input_off(t, 60);
    bad += check("a shorter repeated take removes its old tied tail and keeps softer velocity",
                 t->step[0].vel == 70 && step_gate(&t->step[0]) < 90 && t->step[1].time == ST_REST && t->step[2].time == ST_REST);
    t->seq_notes[0] = 60; t->seq_n = 1;
    input_on(t, 60, 100); seq_release(t);
    bad += check("a sequencer release cannot cut a note still held by the player", gate_note(t, 60));
    input_off(t, 60);
    return bad;
}
int main(void)
{
    int bad = grid_test() + timeline_test() + phase_test() + divisions_test() + lengths_and_swing_test() +
              loop_replay_test() + recorded_gate_storage_test() + old_formats_and_overdub_test();
    printf("%s\n", bad ? "MIDI TIMING TEST FAILED" : "MIDI timing tests passed");
    return bad != 0;
}
