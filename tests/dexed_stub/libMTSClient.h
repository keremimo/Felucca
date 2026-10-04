// SPDX-License-Identifier: GPL-3.0-only
// Stand-in for the MTS-ESP client (a Dexed submodule): no MTS-ESP master in the parity test.
#pragma once
struct MTSClient;
static inline bool MTS_HasMaster(const MTSClient *) { return false; }
static inline double MTS_NoteToFrequency(const MTSClient *, char, char) { return 0; }
static inline bool MTS_ShouldFilterNote(const MTSClient *, char, char) { return false; }
