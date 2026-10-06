/* SPDX-License-Identifier: GPL-3.0-only */
/* FBK9: a FUN8 sound/current-pattern record plus seven other banks per track,
 * all bank timings, arrangement assignments and bounded motion bank tags.
 * Five sectors per A/B copy; the 20,224-byte record is validated in main-loop
 * staging before any runtime state is replaced. */
#define BANK_MAGIC 0x394B4246u
#define BANK_STORE_SIZE 20224u
#define BANK_ACTIVE_OFF (8u + PROJ_STORE_SIZE)
#define BANK_EXTRA_OFF (BANK_ACTIVE_OFF + NTRK)
#define BANK_TIMING_OFF (BANK_EXTRA_OFF + NTRK * (NPAT - 1u) * NSTEP * 9u)
#define BANK_CHAIN_OFF (BANK_TIMING_OFF + NTRK * NPAT * 8u)
#define BANK_MOTION_OFF (BANK_CHAIN_OFF + CHAIN_ROWS * NTRK)
#define BANK_FN_OFF (BANK_MOTION_OFF + MOTION_MAX)   /* "FN61", then each track's FM6 function settings; 0: Dexed's */
#define BANK_FN_MAGIC 0x31364E46u
_Static_assert(BANK_FN_OFF + 4u + NTRK * FM6_NFN <= BANK_STORE_SIZE - 4u, "bank project extent");
static void bank_checksum(uint8_t *raw)
{
    uint32_t sum = proj_hash(raw, BANK_STORE_SIZE - 4u);
    memcpy(raw + BANK_STORE_SIZE - 4u, &sum, 4);
}
static int bank_step_unpack(step_t *s, const uint8_t *b)
{
    memcpy(s->note, b, 4); s->n = b[4] & 7u; s->time = (b[4] >> 3) & 3u; s->flags = (b[4] >> 5) & 3u;
    s->vel = b[5]; s->hit = b[6]; s->acc = b[7]; s->probability = b[8];
    if (b[4] > 127u || s->n > 4u || s->time > ST_REST || s->vel > 127u || s->probability > 101u || (s->acc & ~s->hit)) return 0;
    for (uint32_t j = 0; j < 4u; j++) if (s->note[j] > 127u) return 0;
    return 1;
}
static int bank_step_pack(uint8_t *b, const step_t *s)
{
    if (s->n > 4u || s->time > ST_REST || s->flags > 3u || s->vel > 127u || s->probability > 101u || (s->acc & ~s->hit)) return 0;
    for (uint32_t j = 0; j < 4u; j++) { if (s->note[j] > 127u) return 0; b[j] = s->note[j]; }
    b[4] = (uint8_t)(s->n | s->time << 3 | s->flags << 5);
    b[5] = s->vel; b[6] = s->hit; b[7] = s->acc; b[8] = s->probability;
    return 1;
}
static int bank_pack(uint8_t *raw, const project_t *q, int runtime)
{
    uint32_t magic = BANK_MAGIC, size = BANK_STORE_SIZE, pos = BANK_EXTRA_OFF, f = motion_guard();
    memset(raw, 0, BANK_STORE_SIZE); memcpy(raw, &magic, 4); memcpy(raw + 4, &size, 4);
    if (!proj_pack((project_store_t *)(raw + 8u), q)) { motion_unguard(f); return 0; }
    for (uint32_t k = 0; k < NTRK; k++) {
        uint32_t active = runtime ? trk[k].pattern : 0u;
        raw[BANK_ACTIVE_OFF + k] = (uint8_t)active;
        if (runtime) pattern_commit(&trk[k]);
        for (uint32_t b = 0; b < NPAT; b++) {
            const pattern_t *p = pattern_at(k, b);
            for (uint32_t i = 0; i < 4u; i++) {
                int16_t v = runtime ? p->timing[i] : (b ? TP[P_SLEN + i].def : q->t[k].p[P_SLEN + i]);
                memcpy(raw + BANK_TIMING_OFF + (k * NPAT + b) * 8u + i * 2u, &v, 2);
            }
            if (b == active) continue;
            for (uint32_t i = 0; i < NSTEP; i++, pos += 9u) {
                step_t empty = {{0},0,ST_REST};
                if (!bank_step_pack(raw + pos, runtime ? &p->step[i] : &empty)) { motion_unguard(f); return 0; }
            }
        }
    }
    if (runtime) {
        memcpy(raw + BANK_CHAIN_OFF, chain_patterns, sizeof chain_patterns);
        memcpy(raw + BANK_MOTION_OFF, motion_pattern, sizeof motion_pattern);
    }
    if (q->fm6_fn_ok) {
        uint32_t m = BANK_FN_MAGIC;
        memcpy(raw + BANK_FN_OFF, &m, 4);
        memcpy(raw + BANK_FN_OFF + 4u, q->fm6_fn, sizeof q->fm6_fn);
    }
    motion_unguard(f);
    bank_checksum(raw);
    return 1;
}
static uint8_t bank_import_tags[MOTION_MAX];
static int bank_valid(const uint8_t *raw, uint32_t len)
{
    uint32_t magic, size, sum, pos = BANK_EXTRA_OFF;
    if (len != BANK_STORE_SIZE) return 0;
    memcpy(&magic, raw, 4); memcpy(&size, raw + 4, 4); memcpy(&sum, raw + len - 4u, 4);
    if (magic != BANK_MAGIC || size != len || sum != proj_hash(raw, len - 4u) ||
        !proj_import_any(&proj_scratch, raw + 8u, PROJ_STORE_SIZE)) return 0;
    for (uint32_t k = 0; k < NTRK; k++) {
        if (raw[BANK_ACTIVE_OFF + k] >= NPAT) return 0;
        for (uint32_t b = 0; b < NPAT; b++) {
            for (uint32_t i = 0; i < 4u; i++) {
                int16_t v; memcpy(&v, raw + BANK_TIMING_OFF + (k * NPAT + b) * 8u + i * 2u, 2);
                if (v < TP[P_SLEN + i].min || v > TP[P_SLEN + i].max) return 0;
                if (b == raw[BANK_ACTIVE_OFF + k] && v != proj_scratch.t[k].p[P_SLEN + i]) return 0;
            }
            if (b == raw[BANK_ACTIVE_OFF + k]) continue;
            for (uint32_t i = 0; i < NSTEP; i++, pos += 9u) { step_t s; if (!bank_step_unpack(&s, raw + pos)) return 0; }
        }
    }
    for (uint32_t i = 0; i < CHAIN_ROWS * NTRK; i++) if (raw[BANK_CHAIN_OFF + i] >= NPAT) return 0;
    for (uint32_t i = 0; i < proj_scratch.motion.count; i++) {
        if (raw[BANK_MOTION_OFF + i] >= NPAT) return 0;
        for (uint32_t j = 0; j < i; j++) if (raw[BANK_MOTION_OFF + i] == raw[BANK_MOTION_OFF + j] &&
            proj_scratch.motion.event[i].place == proj_scratch.motion.event[j].place &&
            proj_scratch.motion.event[i].param == proj_scratch.motion.event[j].param) return 0;
    }
    memset(bank_import_tags, 0, sizeof bank_import_tags);
    uint32_t n = 0;
    for (uint32_t i = 0; i < proj_scratch.motion.count; i++) {
#if !MELODEE_FM4
        const motion_event_t *e = &proj_scratch.motion.event[i];
        if (proj_scratch.t[e->place >> 6].engine == ENGI_DIGITAL &&
            (e->param >= P_E0 || (e->param >= P_FM1_ATK && e->param <= P_FM4_LEVEL))) continue;
#endif
        bank_import_tags[n++] = raw[BANK_MOTION_OFF + i];
    }
    {   /* the FM6 function settings (an FBK9 record of before: none, Dexed's) */
        uint32_t m, k;
        memcpy(&m, raw + BANK_FN_OFF, 4);
        if (m == BANK_FN_MAGIC) {
            const uint8_t *f = raw + BANK_FN_OFF + 4u;
            for (k = 0; k < NTRK; k++)
                if (!fm6_fn_ok(f + k * FM6_NFN))
                    return 0;
            memcpy(proj_scratch.fm6_fn, f, sizeof proj_scratch.fm6_fn);
            proj_scratch.fm6_fn_ok = 1;
            proj_scratch.sum = proj_sum(&proj_scratch);
        }
    }
    proj_fm4(&proj_scratch);
    return 1;
}
/* Only after bank_valid and project_restore_runtime. */
static void bank_restore(const uint8_t *raw)
{
    uint32_t pos = BANK_EXTRA_OFF, f = motion_guard();
    chain_config = proj_scratch.chain;
    memcpy(chain_patterns, raw + BANK_CHAIN_OFF, sizeof chain_patterns);
    memcpy(motion_pattern, bank_import_tags, sizeof motion_pattern);
    for (uint32_t k = 0; k < NTRK; k++) {
        trk[k].pattern = raw[BANK_ACTIVE_OFF + k]; trk[k].pattern_next = 0xff; trk[k].pattern_gen++;
        for (uint32_t b = 0; b < NPAT; b++) {
            pattern_t *p = pattern_at(k, b);
            memcpy(p->timing, raw + BANK_TIMING_OFF + (k * NPAT + b) * 8u, sizeof p->timing);
            if (b == trk[k].pattern) memcpy(p->step, trk[k].step, sizeof p->step);
            else for (uint32_t i = 0; i < NSTEP; i++, pos += 9u) bank_step_unpack(&p->step[i], raw + pos);
        }
    }
    motion_unguard(f);
}
