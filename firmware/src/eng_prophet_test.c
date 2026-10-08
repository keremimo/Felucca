/* SPDX-License-Identifier: GPL-3.0-only */
/* Opt-in measurement prototype, NOT a persistent engine. In this test build
 * only, index 0 renders this engine. Never save projects/presets from it.
 * The release still renders ANALOG at 0. Assign a new ID only after the
 * performance gate and the preset/favourite namespace migration. */
#include "prophet_patch.c"
#define P5_POLY 5u
typedef struct { int32_t value; uint8_t stage; } p5_env_t;
typedef struct {
    uint32_t phase[2];
    int32_t z[4], triangle, noise, sync_tail;
    int32_t drift, filter_drift, amp_drift, env_drift;
    int32_t filter_prev, amp_prev;
    p5_env_t filter, amp;
} p5_voice_t;
typedef struct { p5_voice_t voice[P5_POLY]; uint32_t lfo; int32_t lfo_value; } p5_part_t;
static p5_part_t *p5_part(uint32_t part);
static p5_patch_t p5_patch[NPART] __attribute__((section(".pool")));
static uint8_t p5_ready[NPART];
static p5_patch_t *p5_patch_of(const track_t *t)
{
    uint32_t k = (uint32_t)(t - trk) % NPART;
    if (!p5_ready[k]) { p5_patch_init(&p5_patch[k]); p5_ready[k] = 1; }
    return &p5_patch[k];
}
static inline __attribute__((always_inline)) p5_voice_t *p5_voice(track_t *t, voice_t *v)
{
    p5_part_t *part = p5_part((uint32_t)(t - trk));
    return part ? &part->voice[(uint32_t)(v - t->v) % P5_POLY] : 0;
}
/* Forced-inline local helpers keep RAM code from branching back to XIP.
 * Arithmetic matches dsp.c; the common renderer remains unchanged. */
static inline __attribute__((always_inline)) int32_t p5_fast_mulq15(int32_t a, int32_t b) { return (a * b) >> 15; }
static inline __attribute__((always_inline)) int32_t p5_fast_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline __attribute__((always_inline)) int32_t p5_fast_blep(uint32_t ph, uint32_t inc)
{
    uint32_t d = inc >> 15;
    int32_t x;
    if (!d)
        return 0;
    if (ph < inc) {
        x = (int32_t)(ph / d);                       /* 0..32767 */
        return x + x - ((x * x) >> 15) - 32768;
    }
    if (ph > 0xFFFFFFFFu - inc) {
        x = -(int32_t)((0xFFFFFFFFu - ph) / d);       /* -32767..0 */
        return ((x * x) >> 15) + x + x + 32768;
    }
    return 0;
}
static inline __attribute__((always_inline)) int32_t p5_fast_osc_saw(uint32_t ph, uint32_t inc)
{
    return ((int32_t)(ph >> 16) - 32768) - p5_fast_blep(ph, inc);
}
static inline __attribute__((always_inline)) int32_t p5_fast_osc_pulse(uint32_t ph, uint32_t inc, uint32_t pw)
{
    return (p5_fast_osc_saw(ph, inc) - p5_fast_osc_saw(ph + pw, inc)) >> 1;
}
static inline __attribute__((always_inline)) int32_t p5_fast_softclip(int32_t x)
{
    int32_t a = x < 0 ? -x : x, i = a >> 8, y;
    if (i >= 256)
        y = P5_TANH_Q15[256];                           /* continuous with the table */
    else
        y = P5_TANH_Q15[i] + (((P5_TANH_Q15[i + 1] - P5_TANH_Q15[i]) * (a & 255)) >> 8);
    return x < 0 ? -y : y;
}
static inline __attribute__((always_inline)) uint32_t p5_fast_noise32(int32_t *st)
{
    uint32_t s = (uint32_t)*st;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    *st = (int32_t)s;
    return s;
}
static inline __attribute__((always_inline)) int32_t p5_fast_amp_at(const vmod_t *m, uint32_t i)
{
    int32_t x = (m->amp1 - m->amp0) * (int32_t)i;
    return m->amp0 + ((x + ((x >> 31) & (CTL - 1))) >> CTL_LOG2);
}
static inline __attribute__((always_inline)) int32_t p5_fast_voice_amp(int32_t s, const vmod_t *m, uint32_t i)
{
    return p5_fast_mulq15(p5_fast_mulq15(s, p5_fast_amp_at(m, i)), VOICE_FS);
}

static int32_t p5_value(const uint8_t *p, uint32_t k) { return p[k] > 127u ? 127 : p[k]; }
static inline __attribute__((always_inline)) int32_t p5_m12(int32_t a, int32_t b) { return (a * b) >> 12; }

static int32_t p5_env_tick(p5_env_t *e, int gate, const uint8_t *p,
                          uint32_t a, uint32_t d, uint32_t s, uint32_t r, int32_t variation)
{
    if (!gate && e->stage != 0u) e->stage = 3;
    int32_t target = p5_value(p, s) * (1 << 24) / 127;
    uint32_t rel = p[P5_RELEASE_ON] ? (uint32_t)p5_value(p, r) : 0u;
    if (e->stage == 1u) {
        int32_t step = (int32_t)ENV_LIN[p5_value(p, a)];
        step += (step >> 8) * variation / 128;
        e->value += step;
        if (e->value >= (1 << 24)) { e->value = 1 << 24; e->stage = 2; }
    } else if (e->stage == 2u) {
        e->value += mulq16(target - e->value, ENV_EXP[p5_value(p, d)]);
    } else if (e->stage == 3u) {
        e->value -= mulq16(e->value, ENV_EXP[rel]);
        if (e->value < 512) { e->value = 0; e->stage = 0; }
    }
    return e->value >> 9;
}

static void p5_note_on(track_t *t, voice_t *v)
{
    p5_voice_t *s = p5_voice(t, v);
    const uint8_t *p = p5_patch_of(t)->raw;
    if (!s) return;
    if (!voice_was) {
        memset(s, 0, sizeof *s);
        s->triangle = 32767;
        s->noise = (int32_t)(0x5A1793u + v->age * 17u + (uint32_t)(v-t->v) * 101u);
    }
    s->filter.stage = s->amp.stage = 1;
    int32_t vintage = 127 - p5_value(p, P5_VINTAGE);
    /* Fixed mismatch for the life of the voice, deterministic per note/slot.
     * This is an initial engineering range, awaiting reference calibration. */
    s->drift = ((int32_t)(p5_fast_noise32(&s->noise) & 255u) - 128) * vintage / 1024;
    s->filter_drift = ((int32_t)(p5_fast_noise32(&s->noise) & 255u) - 128) * vintage / 32;
    s->amp_drift = ((int32_t)(p5_fast_noise32(&s->noise) & 255u) - 128) * vintage / 16;
    s->env_drift = ((int32_t)(p5_fast_noise32(&s->noise) & 255u) - 128) * vintage / 128;
}
static int p5_done(track_t *t, voice_t *v)
{
    p5_voice_t *s = p5_voice(t, v);
    return !s || (!v->gate && s->amp.stage == 0u);
}
static void p5_block(track_t *t)
{
    p5_part_t *s = p5_part((uint32_t)(t - trk));
    const uint8_t *p = p5_patch_of(t)->raw;
    if (!s) return;
    s->lfo += LFO_INC[p5_value(p, P5_LFO_RATE)];
    int32_t y = 0;
    if (p[P5_LFO_SAW]) y += ((int32_t)(s->lfo >> 16) - 32768) / 3;
    if (p[P5_LFO_TRI]) y += osc_tri(s->lfo) / 3;
    if (p[P5_LFO_PULSE]) y += (s->lfo < 0x80000000u ? 32767 : -32767) / 3;
    s->lfo_value = y;
}

/* Four trapezoidal one-poles with an algebraic feedback solution. Curtis:
 * concentrated input saturation with resonance gain loss. SSI: distributed
 * stage saturation and different resonance compensation. These are separate
 * provisional character models, NOT calibrated chip emulations. Q12 products
 * stay within int32 bounds; state clamps are protection, not the normal knee. */
static inline __attribute__((always_inline)) int32_t p5_filter_core(p5_voice_t *s, int32_t input, int32_t cutoff, int32_t k, int32_t gain, int curtis)
{
    cutoff = p5_fast_clamp(cutoff, 0, 127 << 8);
    uint32_t i = (uint32_t)cutoff >> 8;
    int32_t g = P5_TPT_G[i];
    if (i < 127u) g += (P5_TPT_G[i+1u] - g) * (cutoff & 255) >> 8;
    int32_t g2 = p5_m12(g,g), g3 = p5_m12(g2,g), g4 = p5_m12(g3,g);
    int32_t sigma = p5_m12(4096-g, p5_m12(g3,s->z[0]) + p5_m12(g2,s->z[1]) +
                           p5_m12(g,s->z[2]) + s->z[3]);
    input = p5_fast_mulq15(input, gain);
    int32_t drive = p5_fast_clamp(input - p5_m12(k,sigma), -262143, 262143);
    uint32_t d=(uint32_t)p5_m12(k,g4), index=d>>5;
    int32_t reciprocal=P5_INV_DEN[index];
    reciprocal+=((P5_INV_DEN[index+1u]-reciprocal)*(int32_t)(d&31u))>>5;
    int32_t u=(drive*reciprocal)>>13;
    u = p5_fast_softclip(u);
    /* Fixed four-pole stages: no indexed loop or per-stage mode branch. */
#define P5_STAGE(j, nonlinear) do { \
    int32_t delta=p5_m12(g,u-s->z[j]); \
    int32_t y=s->z[j]+delta; \
    s->z[j]=p5_fast_clamp(y+delta,-60000,60000); \
    u=(nonlinear)?p5_fast_softclip(y):y; \
} while(0)
    if(curtis){P5_STAGE(0,0);P5_STAGE(1,0);P5_STAGE(2,0);P5_STAGE(3,0);}
    else {P5_STAGE(0,1);P5_STAGE(1,1);P5_STAGE(2,1);P5_STAGE(3,1);}
#undef P5_STAGE
    return p5_fast_softclip(u);
}

static int32_t p5_filter(p5_voice_t *s,int32_t input,int32_t cutoff,int32_t resonance,int curtis)
{
    return p5_filter_core(s,input,cutoff,resonance*(curtis?19200:18800)/127,
                          32767-resonance*(curtis?100:45),curtis);
}

static inline __attribute__((always_inline)) int32_t p5_wave(uint32_t phase, uint32_t inc, uint32_t pw, int saw, int pulse, int tri, int32_t *triangle)
{
    /* Simultaneous waveforms share their discontinuity correction. */
    int32_t y=0, a=p5_fast_osc_saw(phase,inc), square=0;
    if(saw)y+=a>>1;
    if(pulse){square=(a-p5_fast_osc_saw(phase+pw,inc))>>1;y+=square;}
    if(tri){
        if(!pulse || pw!=0x80000000u)square=(a-p5_fast_osc_saw(phase+0x80000000u,inc))>>1;
        *triangle=p5_fast_clamp(p5_fast_mulq15(*triangle,32767)+
            (int32_t)(((int64_t)square*2*(int32_t)(inc>>15))>>15),-40000,40000);
        y+=*triangle>>1;
    }
    return y;
}

static inline __attribute__((always_inline)) uint32_t p5_pw(int32_t value)
{
    /* Avoid degenerate pulse widths; signed modulation is applied before p5_fast_clamp. */
    return (uint32_t)p5_fast_clamp(value, 1024, 31743) << 17;
}
static __attribute__((section(".dsp_text"))) uint32_t p5_inc(uint32_t base, int32_t mod)
{
    /* Linear frequency Poly-Mod at audio rate; bounded reverse/ultrasonic rates
     * for this first prototype. Through-zero behaviour requires a later model. */
    if (!mod) return base;
    int32_t delta = (int32_t)(((int64_t)(base >> 2) * p5_fast_clamp(mod,-131071,131071)) >> 15);
    int32_t y = (int32_t)(base >> 2) + delta;
    return (uint32_t)p5_fast_clamp(y, 1, 483183820) * 4u;
}
typedef struct { int32_t f0, a0, f1, a1, ia, ib, cut, k, filter_gain, filter_velocity, amp_velocity, env_amount, poly_env, poly_b, lfo, pwm_a, pwm_b, la, lb, ln; } p5_render_params_t;
static __attribute__((section(".dsp_text"))) void p5_samples(p5_voice_t *restrict s, const uint8_t *restrict p, int32_t *restrict out, uint32_t n, const vmod_t *m, const p5_render_params_t *c)
{
    int32_t f0=c->f0;
    int32_t a0=c->a0;
    int32_t f1=c->f1;
    int32_t a1=c->a1;
    int32_t ia=c->ia;
    int32_t ib=c->ib;
    int32_t cut=c->cut;
    int32_t k=c->k, filter_gain=c->filter_gain;
    int32_t filter_velocity=c->filter_velocity;
    int32_t amp_velocity=c->amp_velocity;
    int32_t env_amount=c->env_amount;
    int32_t poly_env=c->poly_env;
    int32_t poly_b=c->poly_b;
    int32_t lfo=c->lfo;
    int32_t pwm_a=c->pwm_a;
    int32_t pwm_b=c->pwm_b;
    int32_t la=c->la;
    int32_t lb=c->lb;
    int32_t ln=c->ln;
    for (uint32_t i=0; i<n; i++) {
        int32_t fe=f0+(((f1-f0)*(int32_t)i)>>CTL_LOG2);
        int32_t ae=a0+(((a1-a0)*(int32_t)i)>>CTL_LOG2);
        int32_t sum=0;
        for (uint32_t os=0; os<P5_OVERSAMPLE; os++) {
            uint32_t db=p5_inc(ib,p[P5_WHEEL_FREQ_B]?lfo:0);
            int32_t b=p5_wave(s->phase[1],db,p5_pw(pwm_b+(p[P5_WHEEL_PW_B]?(lfo>>1):0)),p[P5_SAW_B],p[P5_PULSE_B],p[P5_TRI_B],&s->triangle);
            int32_t pm=p5_fast_mulq15(fe,poly_env)+p5_fast_mulq15(b,poly_b)*4;
            int32_t fm=(p[P5_POLY_FREQ]?pm:0)+(p[P5_WHEEL_FREQ_A]?lfo:0);
            uint32_t da=p5_inc(ia,fm);
            uint32_t pw=p5_pw(pwm_a+(p[P5_POLY_PW]?(pm>>2):0)+(p[P5_WHEEL_PW_A]?(lfo>>1):0));
            int32_t unused=0;
            int32_t a=p5_wave(s->phase[0],da,pw,p[P5_SAW_A],p[P5_PULSE_A],0,&unused)+s->sync_tail;
            s->sync_tail=0;
            uint32_t nb=s->phase[1]+db, na=s->phase[0]+da;
            if (p[P5_SYNC] && nb<s->phase[1] && db) {
                uint32_t frac=(uint32_t)(((uint64_t)(0u-s->phase[1])<<15)/db);
                uint32_t at=s->phase[0]+(uint32_t)(((uint64_t)da*frac)>>15);
                int32_t before=p5_wave(at,da,pw,p[P5_SAW_A],p[P5_PULSE_A],0,&unused);
                int32_t after=p5_wave(0,da,pw,p[P5_SAW_A],p[P5_PULSE_A],0,&unused);
                int32_t jump=after-before, remain=32768-(int32_t)frac;
                /* Fractional discontinuity correction over this/next sub-sample. */
                a += p5_fast_mulq15((jump>>1),p5_fast_mulq15(remain,remain));
                s->sync_tail=-p5_fast_mulq15((jump>>1),p5_fast_mulq15((int32_t)frac,(int32_t)frac));
                na=(uint32_t)(((uint64_t)da*(uint32_t)remain)>>15);
            }
            s->phase[0]=na; s->phase[1]=nb;
            int32_t noise=(int32_t)(p5_fast_noise32(&s->noise)>>17)-16384;
            int32_t x=p5_fast_mulq15(a,la)+p5_fast_mulq15(b,lb)+p5_fast_mulq15(noise,ln);
            int32_t fc=cut+p5_fast_mulq15(p5_fast_mulq15(fe,filter_velocity),env_amount);
            if (p[P5_POLY_FILTER]) fc+=(pm>>1);
            if (p[P5_WHEEL_FILTER]) fc+=(lfo>>1);
            sum+=p5_filter_core(s,p5_fast_softclip(x),fc,k,filter_gain,!!p[P5_FILTER_REV]);
        }
        #if P5_OVERSAMPLE == 2
        sum >>= 1;
        #endif
        int32_t gain=p5_fast_mulq15(p5_fast_mulq15(ae,amp_velocity),32767+s->amp_drift);
        out[i]+=p5_fast_voice_amp(p5_fast_mulq15(sum,gain),m,i)*2;
    }
}
/* XIP stalls dominated the first hardware run. This bounded hot path uses
 * the existing RAMTEXT region; startup copies it before audio starts. */
static void p5_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    p5_voice_t *s = p5_voice(t, v);
    p5_part_t *part = p5_part((uint32_t)(t - trk));
    const uint8_t *p = p5_patch_of(t)->raw;
    if (!s || !part) return;
    int32_t f0=s->filter_prev, a0=s->amp_prev;
    int32_t f1=p5_env_tick(&s->filter,v->gate,p,P5_ATTACK_FILTER,P5_DECAY_FILTER,P5_SUSTAIN_FILTER,P5_RELEASE_FILTER,s->env_drift);
    int32_t a1=p5_env_tick(&s->amp,v->gate,p,P5_ATTACK_AMP,P5_DECAY_AMP,P5_SUSTAIN_AMP,P5_RELEASE_AMP,s->env_drift);
    s->filter_prev=f1; s->amp_prev=a1;
    /* Frequency mapping is provisional: native knob values -> 48 semitones,
     * with C at value 24. B fine is positive, as the user guide specifies. */
    int32_t pitch_a = m->pitch16 + (p5_value(p,P5_FREQ_A)-24)*6;
    int32_t pitch_b = (p[P5_KEY_B] ? m->pitch16 : 60*16) + (p5_value(p,P5_FREQ_B)-24)*6;
    uint32_t ia = cents_inc(pitch_a, s->drift, m->fine) / P5_OVERSAMPLE;
    uint32_t ib = cents_inc(pitch_b, p5_value(p,P5_FINE_B)*100/128-s->drift, m->fine) / P5_OVERSAMPLE;
    if (p[P5_LOW_B]) ib >>= 10;
    int32_t filter_velocity=p[P5_VEL_FILTER] ? v->mvel*258 : 32767;
    int32_t amp_velocity=p[P5_VEL_AMP] ? v->mvel*258 : 32767;
    int32_t cut=(p5_value(p,P5_CUTOFF)<<8) + m->cutoff + t->p[P_E0]*256 + s->filter_drift;
    cut += (m->pitch16-60*16) * p5_fast_clamp(p[P5_KEY_FILTER],0,2)*21;
    int32_t resonance=p5_fast_clamp(p5_value(p,P5_RESONANCE)+t->p[P_E1],0,127);
    int curtis=!!p[P5_FILTER_REV];
    int32_t k=resonance*(curtis?19200:18800)/127;
    int32_t filter_gain=32767-resonance*(curtis?100:45);
    int32_t env_amount=p5_value(p,P5_ENV_FILTER)*180;
    int32_t poly_env=p5_value(p,P5_POLY_ENV)*258;
    int32_t poly_b=p5_value(p,P5_POLY_B)*258;
    int32_t lfo=p5_fast_mulq15(part->lfo_value,p5_value(p,P5_LFO_INITIAL)*258);
    int32_t pwm_a=p5_value(p,P5_PW_A)*258, pwm_b=p5_value(p,P5_PW_B)*258;
    int32_t la=p5_value(p,P5_LEVEL_A)*258, lb=p5_value(p,P5_LEVEL_B)*258, ln=p5_value(p,P5_NOISE)*258;
    const p5_render_params_t c={f0,a0,f1,a1,ia,ib,cut,k,filter_gain,filter_velocity,amp_velocity,env_amount,poly_env,poly_b,lfo,pwm_a,pwm_b,la,lb,ln};
    /* An indirect call crosses the XIP/RAM distance beyond direct branch range. */
    void (*volatile run)(p5_voice_t *,const uint8_t *,int32_t *,uint32_t,const vmod_t *,const p5_render_params_t *)=p5_samples;
    run(s,p,out,n,m,&c);
}

static const preset_t P5_TEST_PRESETS[] = {
    {"P5 TEST",{0,0,0,0,0,0,0,0},{0,0,127,0},0,0,FX(0,0,0,0),PAT(1)}
};
/* Hardware: five heavy Prophet voices fit; five plus three other engines do
 * not. Charge three of sixteen units before allocation, using the existing
 * per-engine budget hook. Three Prophet + three regular voices cost fifteen. */
static uint32_t p5_units(const track_t *t) { (void)t; return 3u; }
static const engine_t ENG_P5_TEST = {
    .name="P5TEST", .page_title={"TEST","TEST"},
    .edit={{"CUT",F_INT,-63,63,0,0,0},{"RES",F_INT,-63,63,0,0,0},
           {"-",F_INT,0,0,0,0,0},{"-",F_INT,0,0,0,0,0},
           {"-",F_INT,0,0,0,0,0},{"-",F_INT,0,0,0,0,0},
           {"-",F_INT,0,0,0,0,0},{"-",F_INT,0,0,0,0,0}},
    .presets=P5_TEST_PRESETS,.npresets=1,.poly=P5_POLY,.ownenv=1,
    .note_on=p5_note_on,.render=p5_render,.done=p5_done,.block=p5_block,.units=p5_units,
    .knob={P_E0,P_E1,P_CHOR,P_REV}
};
