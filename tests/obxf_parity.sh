#!/bin/sh
# OB-Xf's actual engine headers; no upstream source is vendored. The staging step only
# replaces the plugin/JUCE, matrix and tuning dependencies with a tiny host stub.
set -e
cd "$(dirname "$0")/.."
D="${OBXF_SRC:?set OBXF_SRC to an OB-Xf checkout}"
OUT=build/host/obxf_reference
mkdir -p "$OUT"
cp "$D"/src/engine/*.h "$OUT/"
cp tests/obxf_stub/Tuning.h tests/obxf_stub/VoiceMatrix.h "$OUT/"
printf '#pragma once\n#include "Utils.h"\n#include "BlepData.h"\n#include "DelayLine.h"\n#include "AudioUtils.h"\n' > "$OUT/SynthEngine.h"
for mode in native libm; do
    flags=""
    [ "$mode" = libm ] && flags="-DOXF_LIBM=1"
    ${CC:-cc} -O2 -ffp-contract=off -w $flags -Ibuild/gen -Ifirmware/src -c tests/obxf_parity.c -o "$OUT/port.o"
    ${CXX:-c++} -std=c++20 -O2 -ffp-contract=off -w -Itests/obxf_stub -I"$OUT" tests/obxf_ref.cc "$OUT/port.o" -o "$OUT/parity"
    echo "OBXF parity: $mode math"
    "$OUT/parity"
done
