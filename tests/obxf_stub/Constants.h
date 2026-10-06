#pragma once
#include <juce_dsp/juce_dsp.h>
constexpr float pi=juce::MathConstants<float>::pi, twoPi=2*pi, halfPi=pi/2;
constexpr float invPi=1/pi, invTwoPi=1/twoPi, twoByPi=2/pi, dc=1e-18f, mult=0.69314718056f/12.f;
constexpr int NUM_XPANDER_MODES=15, OVERSAMPLE_FACTOR=2;
constexpr std::array<float,21> syncedRates={1.f/12,1.f/8,1.f/6,3.f/16,1.f/4,1.f/3,3.f/8,1.f/2,2.f/3,3.f/4,1,3.f/2,4.f/3,2,8.f/3,3,4,6,8,12,16};
