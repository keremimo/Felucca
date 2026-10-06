#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#define OBLOG(...)
namespace juce {
template<class T> T jlimit(T lo, T hi, T x) { return std::clamp(x, lo, hi); }
template<class T> T jmax(T a, T b) { return std::max(a,b); }
inline int roundToInt(float x) { return (int)std::round(x); }
template<class T> struct MathConstants { static constexpr T pi=T(3.14159265358979323846), twoPi=2*pi, halfPi=pi/2; };
class Random {
 public:
 uint32_t seed=1;
 float nextFloat() { seed=seed*1664525u+1013904223u; return (seed>>8)*(1.f/16777216.f); }
 static Random &getSystemRandom() { static Random r; return r; }
};
namespace dsp { struct FastMathApproximations {
 template<class T> static T sin(T x) {
 T x2=x*x;
 return -x*(-T(11511339840)+x2*(T(1640635920)+x2*(-T(52785432)+x2*T(479249)))) /
 (T(11511339840)+x2*(T(277920720)+x2*(T(3177720)+x2*T(18361))));
 }
}; }
}
