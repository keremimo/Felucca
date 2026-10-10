/* SPDX-License-Identifier: GPL-3.0-only */
/* SID (src/eng_sid.c) against the chip as reSID documents it: the noise register, waveforms, ring modulation, hard
 * sync, the R-2R DACs of both revisions, the envelope generator cycle for cycle (rates, the exponential counter,
 * sustain, the zero freeze, the ADSR delay), the 16-bit frequency register, the cutoff curves and resonance laws,
 * the filter's sum, the 6581's DC thump through the C64's output stage, band-limited edges, and the 32 presets. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(const char *s, int ok) { printf("SID: %-86s %s\n", s, ok ? "ok" : "FAIL"); fails += !ok; }

static int unique_presets(void)
{
    for (uint32_t i = 0; i < ENG_SID.npresets; i++) {
        if (!ENG_SID.presets[i].name[0] || strlen(ENG_SID.presets[i].name) > 12u)
            return 0;
        for (uint32_t j = 0; j < i; j++)
            if (!strcmp(ENG_SID.presets[i].name, ENG_SID.presets[j].name))
                return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------------------------- envelope --- */
static int16_t ep[P_COUNT];
static sid_voice_t es;
static void env_reset(int a, int d, int s, int r)
{
    memset(&es, 0, sizeof es);
    es.exp_period = 1;
    es.hold_zero = 1;
    es.state = SID_RELEASE;
    ep[P_ATK] = (int16_t)a; ep[P_DEC] = (int16_t)d; ep[P_SUS] = (int16_t)s; ep[P_REL] = (int16_t)r;
}
static void env_gate(void) { es.state = SID_ATTACK; es.hold_zero = 0; }
static uint32_t env_run(uint32_t cycles) { sid_env_clock(&es, ep, cycles); return es.env; }

static void envelope_tests(void)
{
    /* attack rate 0 (period 9): 255 steps, one every 9 cycles, then decay */
    env_reset(0, 127, 127, 0); env_gate();
    check("attack rate 0: 0 -> 0xFE in 254 x 9 cycles, 0xFF on cycle 2295 (2.33 ms), then decay",
          env_run(2294) == 0xfe && es.state == SID_ATTACK && env_run(1) == 0xff && es.state == SID_DECAY_SUSTAIN);
    /* attack rate 15 (period 31251): 8.09 s */
    env_reset(127, 0, 127, 0); env_gate();
    check("attack rate 15 (period 31251): one step per 31251 cycles", env_run(31250) == 0 && env_run(1) == 1);
    /* decay rate 0 to sustain 0: 756 rate periods (the exponential counter: 1 2 4 8 16 30) */
    env_reset(0, 0, 0, 0); env_gate(); env_run(2295);
    {
        uint32_t c93, c54, c26, c14, c6, c0, n = 0;
        for (c93 = 0; es.env != 0x5d; c93++) env_run(1);
        for (c54 = c93; es.env != 0x36; c54++) env_run(1);
        for (c26 = c54; es.env != 0x1a; c26++) env_run(1);
        for (c14 = c26; es.env != 0x0e; c14++) env_run(1);
        for (c6 = c14; es.env != 0x06; c6++) env_run(1);
        for (c0 = c6; es.env != 0x00; c0++) env_run(1);
        n = c0;
        check("decay 0xFF -> 0: every period to 0x5D, then every 2, 4, 8, 16, 30 (756 x 9 cycles)",
              c93 == 162u * 9u && c54 - c93 == 39u * 18u && c26 - c54 == 28u * 36u && c14 - c26 == 12u * 72u &&
              c6 - c14 == 8u * 144u && c0 - c6 == 6u * 270u && n == 756u * 9u);
        check("the counter freezes at zero (no wrap to 0xFF)", es.hold_zero && env_run(100000) == 0);
    }
    /* sustain: 16 levels n * 0x11; raising it does not raise the counter, the decay goes on to zero */
    env_reset(0, 0, 64, 0); env_gate(); env_run(2295 + 200000);
    check("sustain 64 of 127 -> nibble 8: the counter holds at 0x88", es.env == 0x88);
    ep[P_SUS] = 127;
    check("sustain raised above the counter: it decays on to zero (the chip compares for equality)",
          env_run(200000) == 0 && es.hold_zero);
    /* release */
    env_reset(0, 0, 127, 0); env_gate(); env_run(5000);
    es.state = SID_RELEASE;
    es.rate_counter = 0;
    check("release from 0xFF (rate 0) reaches 0 in 756 x 9 cycles and stays", env_run(756 * 9 - 1) == 1 && env_run(1) == 0 &&
          env_run(50000) == 0);
    /* the ADSR delay: a rate counter past the new period counts to its 15-bit wrap first */
    env_reset(0, 0, 127, 0);
    es.rate_counter = 100;
    env_gate();
    check("ADSR delay bug: counter 100, period 9: the first step only after 32676 cycles",
          env_run(32675) == 0 && env_run(1) == 1);
    /* a gate during the release continues from the current level (no reset to zero) */
    env_reset(0, 0, 127, 0); env_gate(); env_run(5000);
    es.state = SID_RELEASE; env_run(500);
    {
        uint32_t lvl = es.env;
        env_gate();
        check("a new gate in the release: the attack counts up from the level reached", lvl > 0x80 && lvl < 0xff &&
              env_run(9) == lvl + 1);
    }
    check("the track's ADSR -> SID rates: 1 ms = rate 0, 10 s = attack 15 / decay 13 (9 s)",
          SID_ADSR_RATE[0] == 0x00 && SID_ADSR_RATE[127] == 0xdf);
}

/* ------------------------------------------------------------------------------- render helpers --- */
static track_t *T;
static voice_t *V;
static vmod_t M;
static void direct(uint32_t preset, uint32_t note)
{
    memset(trk, 0, sizeof trk); host_tracks_init(); host_preset(&trk[0], 3, preset);
    T = &trk[0]; V = &T->v[0];
    memset(V, 0, sizeof *V);
    V->vel = 127; V->gate = V->active = 1;
    voice_was = 0;
    sid_note_on(T, V);
    memset(&M, 0, sizeof M);
    M.pitch16 = (int32_t)note * 16; M.amp0 = M.amp1 = 32767; M.shape = 64 << 8;
}
static uint32_t inc_of(uint32_t note, uint32_t model)   /* the phase step SID plays for a note (voice 1) */
{
    int32_t out[1] = {0};
    direct(0, note); T->p[P_E0] = (int16_t)model;
    sid_render(T, V, out, 1, &M);
    return V->ph[0];
}

static double rms(const int32_t *x, uint32_t n) { double s = 0; for (uint32_t i = 0; i < n; i++) s += (double)x[i] * x[i]; return sqrt(s / n); }

/* a held note through the whole voice path (voice.c, the engine's envelope), ms milliseconds into buf */
static uint32_t play(int32_t *buf, uint32_t ms, uint32_t note, uint32_t release_ms)
{
    uint32_t n = ms * FS / 1000u / CTL * CTL, i;
    trk_note_on(T, note, 100);                         /* (no accent: it would open the filter) */
    for (i = 0; i < n; i += CTL) {
        if (release_ms && i == release_ms * FS / 1000u / CTL * CTL)
            trk_note_off(T, note);
        track_render(T, buf + i, CTL);
    }
    return n;
}
static void fresh(uint32_t preset)
{
    memset(trk, 0, sizeof trk); host_tracks_init(); host_preset(&trk[0], 3, preset); T = &trk[0];
    T->p[P_DIST] = T->p[P_CHOR] = T->p[P_DLY] = T->p[P_REV] = 0;
}

/* Hann-windowed DFT from f0 / 2 to FS / 4: the share of energy (dB) outside +-3 bins of the harmonics of f0 */
static double alias_db(const int32_t *x, uint32_t n, double f0)
{
    double in = 0, all = 0;
    for (uint32_t b = 1; b < n / 2; b++) {
        double re = 0, im = 0, f = (double)b * FS / n, h;
        for (uint32_t i = 0; i < n; i++) {
            double w = 0.5 - 0.5 * cos(2 * M_PI * i / n);
            re += w * x[i] * cos(2 * M_PI * b * i / n);
            im -= w * x[i] * sin(2 * M_PI * b * i / n);
        }
        h = f / f0;
        if (h < 0.5 || f > FS / 4)                      /* the fundamental up to 11 kHz, where the aliases are heard */
            continue;
        all += re * re + im * im;
        if (fabs(h - floor(h + 0.5)) * f0 > 3.0 * FS / n)
            in += re * re + im * im;
    }
    return 10 * log10(in / all);
}

/* the combined waveforms: reSID's samples (assets/resid), unpacked bit for bit; how the engine applies them */
static void combined_tests(void)
{
    static const char *const NAME[4] = {"_ST", "P_T", "PS_", "PST"};
    static uint8_t got[4096], want[4096];
    int same = 1, missing = 0;
    for (uint32_t m = 0; m < 2u; m++)
        for (uint32_t w = 0; w < 4u; w++) {
            char path[64];
            FILE *f;
            snprintf(path, sizeof path, "assets/resid/wave%s_%s.dat", m ? "8580" : "6581", NAME[w]);
            if (!(f = fopen(path, "rb")) || fread(want, 1, 4096, f) != 4096u) {
                missing = 1;
                if (f) fclose(f);
                continue;
            }
            fclose(f);
            memset(got, 0xa5, sizeof got);
            sid_unpack(got, SID_WAVE_LZ + SID_WAVE_AT[m][w], 4096);
            same &= !memcmp(got, want, 4096);
        }
    check("combined waveforms: the 8 packed tables unpack to reSID's samples bit for bit", same && !missing);
    {
        uint32_t sum6 = 0, sum8 = 0, a;
        int sparse;
        direct(0, 60);
        const uint8_t *st = sid_table(T, SID_6581, 3);
        sparse = sid_wave12(SID_TRI_SAW, st, 0x07e00000u, 0x07e00000u, 0, 0) == 0x030u &&
                 sid_wave12(SID_TRI_SAW, st, 0x30000000u, 0x30000000u, 0, 0) == 0;   /* (the AND would be 0x200) */
        for (a = 0; a < 4096u; a++) sum6 += st[a];
        st = sid_table(T, SID_8580, 3);
        for (a = 0; a < 4096u; a++) sum8 += st[a];
        check("6581 TRI+SAW: reSID's 0x030 at 0x07E, silent where the AND of the parts would sound; the 8580's louder",
              sparse && sum8 > 2 * sum6 && sum6 > 0);
    }
    {
        const uint8_t *ps = (direct(0, 60), sid_table(T, SID_8580, 6));
        uint32_t hi = 0, lo = 0;
        for (uint32_t a = 0x800; a < 0x1000; a++) {
            hi |= sid_wave12(SID_SAW_PULSE, ps, a << 20, a << 20, 0x400u << 20, 0);
            lo |= sid_wave12(SID_SAW_PULSE, ps, a << 20, a << 20, 0xfffu << 20, 0) & (a < 0xfff ? 0xfffu : 0);
        }
        check("SAW+PULSE: the sample ANDed with the pulse level (silent while the pulse is low)", hi && !lo);
    }
    {   /* ring with a combined waveform: the table's index MSB is the accumulator's XOR NOT the source's */
        const uint8_t *pt = (direct(0, 60), sid_table(T, SID_8580, 5));
        uint32_t a = 0x40000000u;
        check("RING on TRI+PULSE: the index's MSB flips with the source's inverted MSB",
              sid_wave12(SID_TRI_PULSE, pt, a, a ^ (~0u & 0x80000000u), 0, 0) == (uint32_t)pt[(a ^ 0x80000000u) >> 20] << 4);
    }
    {   /* the 6581's sawtooth combinations pull the accumulator's MSB low where the output's bit 11 is low */
        int32_t o1[1];
        uint32_t i, up6 = 0, up8 = 0;
        for (uint32_t model = 0; model < 2u; model++) {
            direct(0, 60); T->p[P_E0] = (int16_t)model; T->p[P_E1] = SID_SAW_PULSE; T->p[P_E3] = 64;
            V->ph[0] = 0x7ff00000u;
            for (i = 0; i < 400u; i++) {
                sid_render(T, V, o1, 1, &M);
                *(model ? &up8 : &up6) += V->ph[0] >= 0x80000000u;
            }
        }
        printf("SID:   SAW+PULSE, PW 50 %%: samples in the accumulator's upper half: 6581 %u, 8580 %u of 400\n", up6, up8);
        check("6581 SAW+PULSE: the MSB pulled low (the period changes), the 8580's free", up8 > 150 && up6 < up8 / 2);
    }
}

int main(void)
{
    int32_t out[CTL];
    static int32_t buf[FS * 2];
    check("engine 3 keeps its stored ID and is named SID", ENGINES[3] == &ENG_SID && !strcmp(ENG_SID.name, "SID"));
    check("32 uniquely named factory presets", ENG_SID.npresets == 32u && unique_presets());

    /* noise */
    check("noise register: all ones read out as 0xFF0 (the low four DAC bits grounded)", sid_noise_bits(0x7fffffu) == 0xff0u);
    check("noise feedback: bit 22 XOR bit 17 into bit 0", sid_noise_clock(0x7fffffu) == 0x7ffffeu &&
          sid_noise_clock(0x400000u) == 0x000001u && sid_noise_clock(0x020000u) == 0x040001u);
    check("noise clocks on each rise of accumulator bit 19 (ph bit 27), not on its fall",
          sid_noise_advance(0x7fffffu, 0x07fffff0u, 0x20u) == 0x7ffffeu &&
          sid_noise_advance(0x7fffffu, 0x0ffffff0u, 0x20u) == 0x7fffffu &&
          sid_noise_advance(0x7fffffu, 0x00000000u, 0x10000000u) == 0x7ffffeu &&
          sid_noise_advance(0x7fffffu, 0x00000000u, 0x18000000u) == 0x7ffffcu);
    {
        uint32_t sr = 0x7fffff, n = 0;
        do { sr = sid_noise_clock(sr); n++; } while (sr != 0x7fffffu && n < 0x1000000u);
        check("noise register period 2^23 - 1 (maximal length)", n == 0x7fffffu);
    }
    /* waveforms */
    check("triangle: accumulator bits 22..12 folded at the MSB, bit 0 low",
          sid_triangle(0, 0) == 0 && sid_triangle(0x40000000u, 0x40000000u) == 0x800u &&
          sid_triangle(0x7ff00000u, 0x7ff00000u) == 0xffeu && sid_triangle(0x80000000u, 0x80000000u) == 0xffeu &&
          sid_triangle(0xfff00000u, 0xfff00000u) == 0);
    check("ring modulation: the source's MSB XOR folds the triangle",
          sid_triangle(0x20000000u, 0x20000000u ^ 0x80000000u) == (0xffeu ^ sid_triangle(0x20000000u, 0x20000000u)));
    check("pulse: the 12-bit comparator (accumulator >> 12 >= PW), PW 0 constant high",
          sid_wave12(SID_PULSE, 0, 0x3ff00000u, 0x3ff00000u, 0x400u << 20, 0) == 0 &&
          sid_wave12(SID_PULSE, 0, 0x40000000u, 0x40000000u, 0x400u << 20, 0) == 0xfffu &&
          sid_wave12(SID_PULSE, 0, 0, 0, 0, 0) == 0xfffu);
    combined_tests();
    /* the DACs */
    {
        const int16_t (*d6)[64] = SID_WDAC[SID_6581], (*d8)[64] = SID_WDAC[SID_8580];
        int mono8 = 1, emono8 = 1;
        for (uint32_t c = 1; c < 4096u; c++)
            mono8 &= sid_dac(d8, c) > sid_dac(d8, c - 1);
        for (uint32_t e = 1; e < 256u; e++)
            emono8 &= sid_edac(SID_8580, e) > sid_edac(SID_8580, e - 1);
        check("6581 waveform DAC (2R/R 2.20, unterminated): 0x7FF above 0x800, full scale 4095 x 4",
              sid_dac(d6, 0x7ffu) > sid_dac(d6, 0x800u) && sid_dac(d6, 0xfffu) == 16380 && sid_dac(d6, 0) == 0);
        check("6581 bit weights: bit 0 ~2.07, bit 11 ~1983 of 4095", d6[0][1] == 8 && d6[1][32] == 7931);
        check("8580 waveform and envelope DACs: monotonic, near-ideal", mono8 && emono8 && sid_dac(d8, 0x800u) == 8190);
        check("6581 envelope DAC: 0x7F above 0x80, full scale Q15", sid_edac(SID_6581, 0x7f) > sid_edac(SID_6581, 0x80) &&
              sid_edac(SID_6581, 0xff) == 32767 && sid_edac(SID_6581, 0) == 0);
        check("waveform zero: 6581 at 0x380 (a DC offset), 8580 at 0x800", SID_ZERO[0] == 0x380 * 4 && SID_ZERO[1] == 0x800 * 4);
    }
    envelope_tests();

    /* the frequency register */
    {
        int ok = 1;
        for (uint32_t note = 12; note < 108u; note += 7) {
            uint32_t inc = inc_of(note, SID_6581);
            uint32_t fn = (uint32_t)floor(inc / (985248.0 * 256 / FS) + 0.5);
            double hz = 440 * pow(2, ((int)note - 69) / 12.0), want = hz * 16777216 / 985248;
            ok &= inc == (uint32_t)(((uint64_t)fn * SID_FREQ_K16) >> 16) && fabs(fn - want) <= 1.0;
        }
        check("pitch -> the nearest 16-bit frequency register value (PAL clock), played exactly", ok);
        check("nothing above 65535 (3848 Hz): B7, C8 and G9 play the same", inc_of(107, 0) == inc_of(108, 0) &&
              inc_of(108, 0) == inc_of(127, 0) && inc_of(108, 0) == (uint32_t)((65535ull * SID_FREQ_K16) >> 16));
    }
    /* hard sync: voice 2 restarts on voice 1's MSB rising, with the rest of the sample after it */
    {
        uint32_t i0, i1;
        direct(0, 60); T->p[P_E2] = SID_SYNC;
        sid_render(T, V, out, 1, &M);
        i0 = V->ph[0]; i1 = V->ph[1];
        direct(0, 60); T->p[P_E2] = SID_SYNC;
        V->ph[0] = 0x80000000u - i0 / 4; V->ph[1] = 0x12345678u;
        sid_render(T, V, out, 1, &M);
        check("hard sync: voice 2 at 3/4 of its step after a reset 1/4 into the sample",
              V->ph[1] > i1 / 4 * 3 - (i1 >> 14) && V->ph[1] < i1 / 4 * 3 + (i1 >> 14));
    }
    /* the filter */
    {
        int mono = 1;
        for (uint32_t i = 1; i < 129u; i++)
            mono &= SID_FC_G[1][i] >= SID_FC_G[1][i - 1];
        check("6581 cutoff: 220 Hz at FC 0, 18 kHz at 2047, the drop at FC 0x400 (6 -> 4.6 kHz)",
              SID_FC_G[0][0] == 64 && SID_FC_G[0][63] > SID_FC_G[0][64] && SID_FC_G[0][128] == 13801);
        check("8580 cutoff: 0 Hz at FC 0 rising to 12.5 kHz", SID_FC_G[1][0] == 0 && mono && SID_FC_G[1][128] == 5062);
        check("resonance: 6581 1/Q = ~res/8 + 1/4, 8580 1/Q = 2^((4 - res) / 8)",
              SID_RES_K[0][0] == 8704 && SID_RES_K[0][15] == 1024 && SID_RES_K[1][4] == 4096 && SID_RES_K[1][15] == 1579);
    }
    /* the filter's sum: NOTCH (LP + HP) cancels at the cutoff, LP passes; a triangle at f0 on the 8580 */
    {
        double lp, notch;
        uint32_t n, cut = 9;
        fresh(0); T->p[P_E0] = SID_8580; T->p[P_E1] = SID_TRI; T->p[P_E4] = (int16_t)cut; T->p[P_E6] = SID_LP;
        T->p[P_SUS] = 127;
        n = play(buf, 400, 81, 0); lp = rms(buf + n / 2, n / 2);
        fresh(0); T->p[P_E0] = SID_8580; T->p[P_E1] = SID_TRI; T->p[P_E4] = (int16_t)cut; T->p[P_E6] = SID_NOTCH;
        T->p[P_SUS] = 127;
        n = play(buf, 400, 81, 0); notch = rms(buf + n / 2, n / 2);
        printf("SID:   880 Hz triangle, cutoff ~900 Hz: LP rms %.0f, NOTCH rms %.0f\n", lp, notch);
        check("NOTCH = LP + HP as the chip sums them: at the cutoff it cancels what LP passes", notch < 0.4 * lp && lp > 1000);
    }
    /* the 6581's thump: its waveform zero at 0x380 makes the envelope a DC step, which the coupling capacitor
     * (16 Hz) lets through and takes away; the 8580 has none. Means over whole periods of a 440 Hz sawtooth */
    {
        double m6 = 0, m8 = 0, late = 0;
        uint32_t i, n, w = (uint32_t)floor(8 * 4294967296.0 / inc_of(69, SID_6581) + 0.5), t0 = 3u * FS / 1000u;
        for (uint32_t model = 0; model < 2u; model++) {
            double s = 0;
            fresh(0); T->p[P_E0] = (int16_t)model; T->p[P_E1] = SID_SAW; T->p[P_E6] = SID_OFF; T->p[P_SUS] = 127;
            n = play(buf, 600, 69, 0);
            for (i = t0; i < t0 + w; i++) s += buf[i];
            *(model ? &m8 : &m6) = s / w;
            if (!model) {
                for (s = 0, i = n - w; i < n; i++) s += buf[i];
                late = s / w;
            }
        }
        printf("SID:   mean over 8 periods from 3 ms: 6581 %.0f, 8580 %.0f; 6581 after 0.6 s %.0f\n", m6, m8, late);
        check("6581 note-on: a DC thump the 8580 does not have, gone after the coupling capacitor",
              m6 > 2000 && fabs(m8) < m6 / 8 && fabs(late) < m6 / 20);
    }
    /* band-limited edges: a 2093 Hz sawtooth (8580, no filter) against the same accumulator naive */
    {
        static int32_t naive[4096];
        uint32_t i, n, inc = inc_of(96, SID_8580), ph = 0;
        int32_t xlp = 0;
        double f0 = inc * (double)FS / 4294967296.0, a, b;
        fresh(0); T->p[P_E0] = SID_8580; T->p[P_E1] = SID_SAW; T->p[P_E6] = SID_OFF; T->p[P_SUS] = 127;
        n = play(buf, 400, 96, 0);
        for (i = 0; i < 4096u; i++) {          /* the naive sawtooth through the same 16 kHz RC */
            ph += inc;
            xlp += (int32_t)(((int64_t)((int32_t)(ph >> 20) * 4 - 8192 - xlp) * SID_XLP_A) >> 15);
            naive[i] = xlp;
        }
        a = alias_db(buf + n - 4096, 4096, f0);
        b = alias_db(naive, 4096, f0);
        printf("SID:   2093 Hz sawtooth, energy off its harmonics: %.1f dB band-limited, %.1f dB naive\n", a, b);
        check("sawtooth edges band-limited: at least 10 dB less aliasing than the naive accumulator", a < b - 10.0);
    }
    /* the voice ends: released to zero, then the filter and the output stage settled */
    {
        fresh(0); T->p[P_REL] = 0;
        play(buf, 53, 60, 50);
        int still = T->v[0].active;
        for (uint32_t i = 0; i < 300u * FS / 1000u / CTL; i++)
            track_render(T, out, CTL);
        check("REL 0: the voice sounds out its release and its output stage, then ends", still && !T->v[0].active);
    }
    /* every preset: deterministic, sounding, bounded, and over after its release */
    {
        int sound = 1, bounded = 1, ends = 1, det = 1;
        for (uint32_t p = 0; p < ENG_SID.npresets; p++) {
            uint64_t h[2] = {1469598103934665603ull, 1469598103934665603ull};
            int32_t peak = 0;
            for (uint32_t r = 0; r < 2u; r++) {
                uint32_t n, i;
                fresh(p);
                n = play(buf, 1500, 48 + p % 24, 600);
                for (i = 0; i < n; i++) {
                    h[r] = (h[r] ^ (uint32_t)buf[i]) * 1099511628211ull;
                    if (abs(buf[i]) > peak) peak = abs(buf[i]);
                }
                for (i = 0; i < 12000u && T->v[0].active; i++)   /* up to 8.7 s more for a release */
                    track_render(T, out, CTL);
                ends &= !T->v[0].active;
            }
            det &= h[0] == h[1];
            sound &= peak > 2000;
            bounded &= peak < 32767 * 2;
            if (peak <= 2000 || peak >= 32767 * 2)
                printf("SID:   preset %s peak %d\n", ENG_SID.presets[p].name, (int)peak);
        }
        check("all 32 presets: deterministic, sounding, bounded", det && sound && bounded);
        check("all 32 presets: the voice ends after its release", ends);
    }
    return fails != 0;
}
