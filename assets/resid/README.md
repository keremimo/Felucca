# SID combined waveforms (reSID)

The eight `wave6581_*.dat` / `wave8580_*.dat` files are reSID's samples of the combined waveforms of a real
MOS 6581 and MOS 8580: what the chip's OSC3 register reads back for each of the 4096 values of the top 12
accumulator bits (`accumulator >> 12`) with two or three waveforms selected at once. One byte per value, the top
8 of the 12 output bits (the low 4 read as 0). Names give the waveform bits as reSID does: `P` pulse, `S`
sawtooth, `T` triangle, `_` not selected (`__ST` sawtooth + triangle, `P_T` pulse + triangle, `PS_` pulse +
sawtooth, `PST` all three). The pulse tables hold the pulse-high case; the pulse level is ANDed on at run time.

They are copied unchanged from reSID by Dag Lem (<https://github.com/libsidplayfp/resid>, `master`, files last
changed in 6fc8383, 1998; fetched 2026-10-10), licensed GPL-2.0-or-later and used here under GPL-3.0.
`tools/gen_tables.py` (`sid_tables`) packs them (a small LZ, 4.3 KB for all eight) into
`build/gen/melodee_tables.h`; `firmware/src/eng_sid.c` unpacks the one table its MODEL and WAVE need into the
part's working memory.

SHA-256:

    bd63e518ecd09b836911e2d4609256afeb28603c3a2a57c1314d0b5a928343ad  wave6581__ST.dat
    47ac5fd90e701cdd3fa1bd7f7db90f8ce8ef98169cd17d8c5f8a26545c3944eb  wave6581_P_T.dat
    466043fe9fcfed21f81fa632e8a12d7ab9d4a971a78ba7a19d501102cf658e04  wave6581_PS_.dat
    9fe871586d092ff8fd4daa88c9e784d0451f6d6832b96e5697f18ee671d102d6  wave6581_PST.dat
    3049a3197ee321b962282e01e905c3ee6b0432db346ef106f41d7c9406e181c7  wave8580__ST.dat
    ffa33e7959bf724a7dced993e282501b70ef35a8f50fd7e4f7b5c0eaad106941  wave8580_P_T.dat
    6db028bb1bd523bdfdd70a045722e8545618c9471de28fd685cda001cfc442b1  wave8580_PS_.dat
    ec324d749e726a19e8cc924e06fe2b4005bc02eaf42efb89323c5a25945027cf  wave8580_PST.dat
