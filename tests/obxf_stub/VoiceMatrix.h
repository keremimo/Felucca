#pragma once
// Matrix and microtuning are deliberately outside this port. Native values pass through.
enum class MatrixTarget { LFO2Rate,LFO1ModAmount1,LFO1ModAmount2,LFO2ModAmount1,LFO2ModAmount2,
 FilterEnvAttack,FilterEnvRelease,FilterCutoff,Osc2PWOffset,OscPitch,Osc1Pitch,Osc2Pitch,
 Osc1Vol,Osc2Vol,NoiseVol,RingModVol,Osc2Detune,UnisonDetune,OscPW,OscCrossmod,AmpEnvAttack,AmpEnvRelease };
struct Scaling { float nativeMin=0, nativeMax=120; constexpr float span() const { return nativeMax-nativeMin; } };
constexpr Scaling matrixTargetScaling(MatrixTarget) { return {}; }
struct VoiceMatrixAdjustments { void clear() {} float nativeOr(MatrixTarget,float v) const { return v; } float modFor(MatrixTarget) const { return 0; } };
struct VoiceMatrixSourceValues { void clear() {} };
enum class MatrixSource { Strike,Lift };
inline void setMatrixSource(VoiceMatrixSourceValues &,MatrixSource,float) {}
struct VoiceMatrix {};
