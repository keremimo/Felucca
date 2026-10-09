/* SPDX-License-Identifier: GPL-3.0-only */
/* Numerical contracts for native fp32 synthesis. Reference calculations use
 * host doubles/libm, independently of the target's tables and fast math. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>

static void fm6_numerics(void)
{
    fm6_voice_t voice={0};float input[64],output[64];
    voice.ph[0]=0x13579u;voice.fq[0]=139817;voice.g[0]=0.2f;voice.dg[0]=1.1f/64.0f;
    uint32_t phase=voice.ph[0];double gain=voice.g[0],step=voice.dg[0],error=0;
    for(uint32_t i=0;i<64;i++)input[i]=(float)(sin(i*0.19)*3.7);
    fm6_op(&voice,0,output,input,0,FM6_MODERN,64);
    for(uint32_t i=0;i<64;i++){
        gain+=step;
        double want=sin(6.283185307179586*((phase&0xFFFFFFu)/16777216.0+input[i]))*gain;
        double e=fabs(output[i]-want);if(e>error)error=e;
        phase+=(uint32_t)voice.fq[0];
    }
    assert(error<2e-5 && voice.ph[0]==phase);
    /* The Q24 log pitch lookup must remain exact, including negative logs.
     * Two short partial products replace the original wide interpolation. */
    for(int32_t octave=-4;octave<=20;octave++)for(uint32_t i=0;i<1024;i++)
    for(uint32_t f=0;f<16384;f+=127){
        int32_t lf=octave*16777216+(int32_t)(i*16384+f);
        int32_t y=FM6_FREQ[i]+(int32_t)(((int64_t)(FM6_FREQ[i+1]-FM6_FREQ[i])*f)>>14);
        int sh=20-octave;int32_t want=sh<=0?y:sh>31?0:y>>sh;
        assert(fm6_freq(lf)==want);
    }
    int32_t levels[6];for(uint32_t k=0;k<6;k++)levels[k]=15*16777216;
    fm6_plan(&voice,levels,31,FM6_MARK1,1);
    fm6_plan(&voice,levels,31,FM6_MODERN,1);
    for(uint32_t k=0;k<6;k++)assert(voice.g[k]==0.0f&&voice.gout[k]>1.9f&&voice.gout[k]<2.1f);
    printf("fp32 FM6: modulated operator error %.3g; exact Q24 pitch steps\n",error);
}

static void cz_numerics(void)
{
    for(uint32_t raw=0;raw<128;raw++)for(uint32_t e=0;e<3;e++){
        uint32_t chip=(8u|(raw&7u))<<(raw>>3);
        double domain=e==0?131072.0:e==1?16384.0:131072.0;
        double want=chip*(double)CTL*40000/FS/(e==0?8:e==1?4:2)/domain;
        assert(fabs(cz_hw_rate(raw,e)/want-1)<2e-7);
    }
    /* Short rising/falling segments consume fractions of the same tick. */
    cz_native_def_t d={0};cz_native_env_t env={0};d.sustain=2;d.end=3;
    d.level[0]=0.25f;d.level[1]=0.125f;d.level[2]=0.75f;
    for(uint32_t i=0;i<4;i++)d.rate[i]=1.0f;
    assert(cz_native_env_tick(&env,&d,1)==0.75f && env.stage==2);
    assert(cz_native_env_tick(&env,&d,0)==0.0f && env.stage==4);
    /* At zero distortion the native carrier is -cos, with continuous phase. */
    cz_hw_pd_t pd;cz_native_pd(&pd,0,0.0f);double error=0;
    for(uint32_t i=0;i<10000;i++){
        uint32_t ph=i*429497u;
        double want=-cos(ph*(6.283185307179586/4294967296.0));
        double e=fabs(cz_native_wave(&pd,ph,0)-want);if(e>error)error=e;
    }
    assert(error<6e-6);
    float a=cz_native_wave(&pd,0x40000000u,0),b=cz_native_wave(&pd,0x40080000u,0);
    assert(fabsf(a-b)>0.0001f); /* both phases occupied one old 11-bit bin */
    assert(cz_native_amp(0.0001f,0,0,127)>0.0f);
    for(uint32_t attenuation=0;attenuation<16;attenuation++)
    for(uint32_t velocity=0;velocity<128;velocity++){
        float gain=cz_native_amp(126.5f,attenuation,15,velocity);
        assert(isfinite(gain)&&gain>=0.0f&&gain<=1.0f);
    }
    printf("fp32 CZ-1: cosine error %.3g; fractional envelope time and quiet gain preserved\n",error);
}

/* Independent double-precision trapezoidal filter, including tanh at each
 * SSI pole. Compare below self-oscillation, where phases cannot diverge. */
static double filter_ref(double z[4],double input,double g,double k,double gain,int curtis)
{
    double g2=g*g,g3=g2*g,g4=g3*g;
    double sigma=(1-g)*(g3*z[0]+g2*z[1]+g*z[2]+z[3]);
    double u=tanh((input*gain-k*sigma)/(1+k*g4));
    for(uint32_t j=0;j<4;j++){
        double delta=g*(u-z[j]),y=z[j]+delta;
        z[j]=fmax(-60000.0/32768,fmin(60000.0/32768,y+delta));
        u=curtis?y:tanh(y);
    }
    return tanh(u);
}
static void prophet_numerics(void)
{
    double max_error=0;
    for(uint32_t mode=0;mode<2;mode++)for(uint32_t cutoff=10;cutoff<128;cutoff+=17){
        p5_voice_t voice={0};double z[4]={0};
        double hz=30*pow(16000.0/30,cutoff/127.0);
        double tan_g=tan(3.141592653589793*hz/(FS*P5_OVERSAMPLE)),g=tan_g/(1+tan_g);
        double k=64.0*(mode?19200:18800)/(127*4096.0),gain=1-64.0*(mode?100:45)/32768.0;
        for(uint32_t i=0;i<4096;i++){
            float input=(float)(sin(i*0.71)*0.25+cos(i*0.123)*0.13);
            float actual=p5_filter(&voice,input,(float)cutoff,64.0f,(int)mode);
            double want=filter_ref(z,input,g,k,gain,(int)mode),e=fabs(actual-want);
            if(e>max_error)max_error=e;
        }
    }
    assert(max_error<2e-5);
    p5_voice_t quiet={0};float y=0;
    for(uint32_t i=0;i<4096;i++)y=p5_filter(&quiet,1e-7f,80.0f,0.0f,1);
    assert(y>5e-8f && y<2e-7f); /* sub-PCM state must survive until final conversion */
    p5_patch_t patch;p5_patch_init(&patch);uint8_t values[55];memcpy(values,patch.raw,55);
    values[P5_DECAY_AMP]=127;values[P5_SUSTAIN_AMP]=0;
    p5_env_t env={.value=0.002f,.stage=2};float before=env.value;
    p5_env_tick(&env,1,patch.raw,P5_ATTACK_AMP,P5_DECAY_AMP,P5_SUSTAIN_AMP,P5_RELEASE_AMP,0,values,0);
    assert(env.value>0 && env.value<before && before-env.value<1e-6f);
    printf("fp32 Prophet: double filter error %.3g; sub-PCM filter/envelope state preserved\n",max_error);
}
int main(void)
{
    fm6_numerics();cz_numerics();prophet_numerics();
    puts("native fp32 numerical contracts passed");return 0;
}
