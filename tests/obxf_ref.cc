/* Compile OB-Xf's actual voice against dependency stubs, compare the C bridge sample by sample.
 * No DSP from the reference is rewritten here. Random state is aligned explicitly; slop amounts
 * are zero except the deterministic unison detune case. -ffp-contract=off on both sides. */
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#define NPOLY 8
#define CTL 32
#include "../firmware/src/obxf_core.c" // native parameter type only; the C bridge renders the port
#define private public
#include "Voice.h"
#include "Smoother.h"
#undef private
extern "C" const void *obxf_parity_setup(int);
extern "C" void obxf_parity_block(float *,int);
static void env(ADSREnvelope &e,float a,float d,float s,float r,float c)
{ e.setAttack(a);e.setDecay(d);e.setSustain(s);e.setRelease(r);e.setAttackCurve(c); }
template<class P> static void lfo(LFO &l,const P &p)
{
 l.setSampleRate(44100);l.setRate(p.hz);l.setRateNormalized(p.raw);l.setTempoSync(p.sync);l.hostSyncRetrigger(120,0,false);
 l.par.wave1blend=p.w1;l.par.wave2blend=p.w2;l.par.wave3blend=p.w3;l.par.pw=p.pw;
 l.state.rng.seed=1;l.state.wave.samplehold=l.state.wave.history=0;
}
int main()
{
 int bad=0; double worst=0,minsnr=200;
 for(int f=0;f<39;f++) {
  const auto &P=*static_cast<const oxf_par_t*>(obxf_parity_setup(f));
  Voice v; Tuning tuning; VoiceMatrix matrix; v.initTuning(&tuning);v.setSampleRate(44100);
  v.oscs.osc1.phase=v.oscs.osc2.phase=0;v.oscs.osc1.tuningSlop=-.25f;v.oscs.osc2.tuningSlop=.25f;
  v.oscs.gen.noise.seedWhiteNoise(17);v.noiseGen.seedWhiteNoise(23);
  auto &o=v.oscs.par;
  o.osc.pitch1=P.pitch1;o.osc.pitch2=P.pitch2;o.osc.detune=P.detune;o.osc.pw=P.pw;
  o.osc.saw1=P.saw1;o.osc.saw2=P.saw2;o.osc.pulse1=P.pul1;o.osc.pulse2=P.pul2;o.osc.sync=P.sync;o.osc.crossmod=P.xmod;o.osc.keytrack2=P.key2;
  o.pitch.tune=P.tune;o.pitch.transpose=P.transpose;o.pitch.unisonDetune=P.uni_det;
  o.mix.osc1=P.mix1;o.mix.osc2=P.mix2;o.mix.noise=P.noise;o.mix.noiseColor=P.ncolor;o.mix.ringMod=P.ring;
  o.mod.envToPitchInvert=P.ptch_inv;o.mod.envToPWInvert=P.pw_inv;
  v.par.osc.portamento=P.porta_hz;v.par.osc.envPitchAmt=P.env_pitch;v.par.osc.envPWAmt=P.env_pw;
  v.par.osc.envPitchBothOscs=P.ptch_both;v.par.osc.envPWBothOscs=P.pw_both;v.par.osc.pwOsc2Offset=P.pw2ofs;
  v.setBrightness(22040); // replaced below by the exact native prewarped coefficient
  v.state.brightnessCoef=P.bright_k/(1-P.bright_k);
  v.par.filter.keytrack=P.keytrack;v.par.filter.envAmt=P.env_amt;v.par.filter.invertEnvScale=P.inv_fenv;
  v.par.filter.fourPole=P.four;v.setFilter2PolePush(P.push);v.filter.par.bpBlend2Pole=P.bp_blend;v.filter.par.xpander4Pole=P.xpander;v.filter.par.xpanderMode=P.xp_mode;
  v.par.extmod.pbUp=P.pb_up;v.par.extmod.pbDown=P.pb_dn;v.par.extmod.pbOsc2Only=P.pb_osc2;v.par.extmod.envLegatoMode=P.legato;
  v.par.extmod.velToAmp=P.vel_amp;v.par.extmod.velToFilter=P.vel_flt;
  env(v.ampEnv,P.aa,P.ad,P.as,P.ar,P.acurve);env(v.filterEnv,P.fa,P.fd,P.fs,P.fr,P.fcurve);
  v.ampEnvAttackBase=P.aa;v.ampEnvReleaseBase=P.ar;v.filterEnvAttackBase=P.fa;v.filterEnvReleaseBase=P.fr;
  auto setdest=[](Voice::Parameters::LFO &d,const auto &s) {d.amt1=s.amt1;d.amt2=s.amt2;d.osc1Pitch=s.p1;d.osc2Pitch=s.p2;d.osc1PW=s.pw1;d.osc2PW=s.pw2;d.cutoff=s.cut;d.volume=s.vol;d.absVolume=s.avol;};
  setdest(v.par.lfo1,P.lfo[0]);setdest(v.par.lfo2,P.lfo[1]);
  LFO l1,vib; lfo(l1,P.lfo[0]);lfo(v.lfo2,P.lfo[1]);v.lfo2BaseRate=P.lfo[1].hz;
  vib.setSampleRate(44100);vib.setRate(P.vib_hz);vib.par.wave1blend=P.vib_sq?0:-1;vib.par.wave2blend=P.vib_sq?-1:0;vib.par.unipolarPulse=true;
  Smoother cut,res,mode,wheel;for(auto *s:{&cut,&res,&mode,&wheel})s->setSampleRate(44100);
  cut.stepValue=cut.integralValue=P.cut;res.stepValue=res.integralValue=P.res;mode.stepValue=mode.integralValue=P.mode;wheel.setStep(f==38?.5f:0.f);
  v.NoteOn(69,.8f,0);
  double err=0,power=0,peak=0;
  for(int b=0;b<1024;b++) {
   float port[CTL];obxf_parity_block(port,b);
   if(b==256)v.NoteOn(f==33?81:69,Voice::reuseVelocitySentinel,0);
   if(b==768)v.NoteOff(0);
   for(int i=0;i<CTL;i++) {
    v.par.filter.cutoff=cut.smoothStep();v.filter.setResonance(.991f-logsc(1.f-res.smoothStep(),0.f,.991f,40.f));v.filter.setMultimode(mode.smoothStep());
    l1.update();vib.update();v.lfo1In=l1.getVal();float w=wheel.smoothStep();v.vibratoLFOIn=vib.getVal()*w*w*4;
    double ref=v.ProcessSample(matrix),d=port[i]-ref;err+=d*d;power+=ref*ref;peak=std::max(peak,std::abs(d));
   }
  }
  double snr=10*std::log10(power/std::max(err,1e-30));worst=std::max(worst,peak);minsnr=std::min(minsnr,snr);
  bool ok=std::isfinite(snr)&&snr>70 && peak<.002;
  std::printf("feature %02d max %.7g SNR %.2f dB %s\n",f,peak,snr,ok?"ok":"FAIL");bad+=!ok;
 }
 std::printf("worst max %.7g, minimum SNR %.2f dB\n",worst,minsnr);return bad?1:0;
}
