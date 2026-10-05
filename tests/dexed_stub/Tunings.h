// SPDX-License-Identifier: GPL-3.0-only
// Stand-in for Surge's tuning library (a Dexed submodule): the parity test plays standard tuning.
#pragma once
namespace Tunings {
struct Scale { int count = 12; };
struct Tuning { Scale scale; double logScaledFrequencyForMidiNote(int) const { return 0; } };
}
