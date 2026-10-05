/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Panel chord voicings. Roots have already passed through keyboard scale mapping.
 * Stored steps and incoming MIDI contain actual pitches and are never expanded again. */
static uint32_t chord_notes(const track_t *t, uint32_t root, uint8_t out[4])
{
    static const uint8_t shape[10][4] = {
        {0,4,7,255}, {0,3,7,255}, {0,3,6,255}, {0,4,8,255}, {0,2,7,255},
        {0,5,7,255}, {0,4,7,10}, {0,4,7,11}, {0,3,7,10}, {0,3,6,10}};
    int32_t pitch[4], i, j, n = 1, mode = t->p[P_CHMODE];
    uint32_t count = 0;
    if (root == KB_SILENT || root > 127u)
        return 0;
    pitch[0] = (int32_t)root;
    if (mode && !is_drum(t) && !is_gm_sample(t) && !is_slice(t)) {
        if (mode == 3) {
            uint32_t s = (uint32_t)clamp(t->p[P_CHTYPE], 0, 9);
            for (i = 1; i < 4 && shape[s][i] != 255; i++)
                pitch[i] = (int32_t)root + shape[s][i];
            n = i;
        } else {
            /* Stack alternate degrees above this root. With chromatic keyboard
             * input the root stays exact; upper notes follow the selected scale. */
            uint32_t mask = scale_mask(t);
            int32_t p = (int32_t)root, degree = 0;
            n = mode == 2 ? 4 : 3;
            for (i = 1; i < n; i++) {
                while (degree < 2 * i) {
                    int32_t pc = (++p - t->p[P_TRANS] - t->p[P_ROOT]) % 12;
                    if (pc < 0) pc += 12;
                    if ((mask >> pc) & 1u) degree++;
                }
                pitch[i] = p;
            }
        }
        for (i = 0; i < clamp(t->p[P_CHINV], 0, n - 1); i++)
            pitch[i] += 12;
        for (i = 0; i < n; i++)
            for (j = i + 1; j < n; j++)
                if (pitch[j] < pitch[i]) {
                    int32_t p = pitch[i]; pitch[i] = pitch[j]; pitch[j] = p;
                }
        for (i = 0; i < n; i++)
            pitch[i] += t->p[P_CHSPREAD] == 2 ? i * 12 : t->p[P_CHSPREAD] == 1 && (i & 1) ? 12 : 0;
    }
    for (i = 0; i < n; i++)
        for (j = i + 1; j < n; j++)
            if (pitch[j] < pitch[i]) {
                int32_t p = pitch[i]; pitch[i] = pitch[j]; pitch[j] = p;
            }
    for (i = 0; i < n; i++)
        if (pitch[i] >= 0 && pitch[i] <= 127 && (!count || out[count - 1] != pitch[i]))
            out[count++] = (uint8_t)pitch[i];
    return count;
}
