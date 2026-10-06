#pragma once
#include "Constants.h"
#include "ParamScales.h"
inline float getPitch(float index) { return 440.f*std::exp(mult*index); }
