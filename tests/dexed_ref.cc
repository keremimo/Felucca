// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// The Dexed side of the FM6 parity test (tests/fm6_parity.sh): renders a score (tests/fm6_score.h)
// through Dexed's own synthesis sources (a Dexed checkout, DEXED_SRC: msfa/ and the Mark I / OPL
// engines), with the voice handling of Dexed's DexedAudioProcessor (keydown / keyup / chooseNote,
// sustain, mono, portamento, the one LFO) restated here without JUCE. MPE is off, as Dexed turns it
// off at the first chord on one channel.
//   dexed_ref SCORE OUT    OUT: per sample, int64 raw sum of the voices (Q24, 1 << 24 = a unit
//                          sine) and int64 sum of Dexed's clipped 16-bit voice outputs
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include "synth.h"
#include "freqlut.h"
#include "exp2.h"
#include "sin.h"
#include "lfo.h"
#include "env.h"
#include "pitchenv.h"
#include "porta.h"
#include "controllers.h"
#include "dx7note.h"
#include "EngineMkI.h"
#include "EngineOpl.h"
extern "C" {
#include "fm6_score.h"
}

void dexed_trace(const char *, const char *, ...) {}

struct StandardTuning : public TuningState {              // msfa/tuning.cc without JUCE
    int32_t t[128];
    StandardTuning() {
        for (int mn = 0; mn < 128; ++mn)
            t[mn] = 50857777 + ((1 << 24) / 12) * mn;
    }
    int32_t midinote_to_logfreq(int midinote) override { return t[midinote]; }
};

struct Voice {
    int channel, midi_note, velocity;
    bool keydown, sustained, live;
    int32_t keydown_seq;
    Dx7Note *note;
};

static const int MAX_ACTIVE_NOTES = 16;
static Voice voices[MAX_ACTIVE_NOTES];
static int currentNote, lastActiveVoice, nextKeydownSeq;
static bool sustain, monoMode;
static Controllers controllers;
static Lfo lfo;
static uint8_t data[161];

static FmMod mod(const sc_mod_t &m)
{
    FmMod r;
    r.range = m.range;
    r.pitch = m.pitch != 0;
    r.amp = m.amp != 0;
    r.eg = m.eg != 0;
    return r;
}

static int chooseNote(uint8_t pitch)
{
    int bestNote = currentNote, bestScore = -1, note = currentNote;
    for (int i = 0; i < MAX_ACTIVE_NOTES; i++) {
        int score = 0;
        if (!voices[note].note->isPlaying()) score += 4;
        if (!voices[note].keydown) score += 2;
        if (voices[note].midi_note == pitch) score += 1;
        if ((score > bestScore) || (score == bestScore && voices[note].keydown_seq < voices[bestNote].keydown_seq)) {
            bestNote = note;
            bestScore = score;
        }
        note = (note + 1) % MAX_ACTIVE_NOTES;
    }
    return bestNote;
}

static void keyup(uint8_t pitch);

static void keydown(uint8_t pitch, uint8_t velo)
{
    if (velo == 0) {
        keyup(pitch);
        return;
    }
    pitch += data[144] - 24;
    bool triggerLfo = true;
    for (int i = 0; i < MAX_ACTIVE_NOTES; i++)
        if (voices[i].keydown) {
            triggerLfo = false;
            break;
        }
    if (triggerLfo)
        lfo.keydown();
    int note = chooseNote(pitch);
    currentNote = (note + 1) % MAX_ACTIVE_NOTES;
    voices[note].channel = 1;
    voices[note].midi_note = pitch;
    voices[note].velocity = velo;
    voices[note].sustained = sustain;
    voices[note].keydown = true;
    voices[note].keydown_seq = nextKeydownSeq++;
    bool voice_steal = voices[note].note->isPlaying();
    voices[note].note->init(data, pitch, velo, 1, &controllers);
    if (data[136] && !voice_steal)
        voices[note].note->oscSync();
    if ((voices[lastActiveVoice].midi_note != -1 && controllers.portamento_enable_cc) && controllers.portamento_cc > 0)
        voices[note].note->initPortamento(*voices[lastActiveVoice].note);
    if (monoMode) {
        for (int i = 0; i < MAX_ACTIVE_NOTES; i++) {
            if (voices[i].live) {
                if (!voices[i].keydown) {
                    voices[i].live = false;
                    voices[note].note->transferSignal(*voices[i].note);
                    break;
                }
                if (voices[i].midi_note < pitch) {
                    voices[i].live = false;
                    voices[note].note->transferState(*voices[i].note);
                    break;
                }
                return;
            }
        }
    } else if (!data[136]) {
        for (int i = 0; i < MAX_ACTIVE_NOTES; i++)
            if (i != note && voices[i].note->isPlaying() && voices[i].midi_note == pitch) {
                voices[note].note->transferPhase(*voices[i].note);
                break;
            }
    }
    voices[note].live = true;
    lastActiveVoice = note;
}

static void keyup(uint8_t pitch)
{
    pitch += data[144] - 24;
    int note;
    for (note = 0; note < MAX_ACTIVE_NOTES; ++note)
        if (voices[note].midi_note == pitch && voices[note].keydown) {
            voices[note].keydown = false;
            break;
        }
    if (note >= MAX_ACTIVE_NOTES)
        return;
    if (monoMode) {
        int highNote = -1, target = 0;
        for (int i = 0; i < MAX_ACTIVE_NOTES; i++)
            if (voices[i].keydown && voices[i].midi_note > highNote) {
                target = i;
                highNote = voices[i].midi_note;
            }
        if (highNote != -1 && voices[note].live) {
            voices[note].live = false;
            voices[target].live = true;
            voices[target].note->transferState(*voices[note].note);
        }
    }
    if (sustain)
        voices[note].sustained = true;
    else
        voices[note].note->keyup();
}

static void event(const sc_event_t &e)
{
    switch (e.type) {
    case SC_ON:
        keydown((uint8_t)e.a, (uint8_t)e.b);
        break;
    case SC_OFF:
        keyup((uint8_t)e.a);
        break;
    case SC_BEND:
        controllers.values_[kControllerPitch] = e.a;
        break;
    case SC_PRESS:
        controllers.aftertouch_cc = e.a;
        controllers.refresh();
        break;
    case SC_CC:
        switch (e.a) {
        case 1: controllers.modwheel_cc = e.b; controllers.refresh(); break;
        case 2: controllers.breath_cc = e.b; controllers.refresh(); break;
        case 4: controllers.foot_cc = e.b; controllers.refresh(); break;
        case 5: controllers.portamento_cc = e.b; break;
        case 64:
            sustain = e.b > 63;
            if (!sustain)
                for (int n = 0; n < MAX_ACTIVE_NOTES; n++)
                    if (voices[n].sustained && !voices[n].keydown) {
                        voices[n].note->keyup();
                        voices[n].sustained = false;
                    }
            break;
        case 65: controllers.portamento_enable_cc = e.b >= 64; break;
        }
        break;
    }
}

int main(int argc, char **argv)
{
    static score_t s;
    if (argc < 3 || score_read(&s, argv[1])) {
        fprintf(stderr, "usage: dexed_ref SCORE OUT\n");
        return 2;
    }
    const double sr = 44100;
    Exp2::init();
    Tanh::init();
    Sin::init();
    Freqlut::init(sr);
    Lfo::init(sr);
    PitchEnv::init(sr);
    Env::init_sr(sr);
    Porta::init_sr(sr);
    static EngineMkI mk1;
    static EngineOpl opl;
    static FmCore msfa;
    controllers.core = s.engine == 1 ? (FmCore *)&mk1 : s.engine == 2 ? (FmCore *)&opl : &msfa;
    memcpy(data, s.voice, 155);
    for (int k = 0; k < 6; k++)                          // opSwitch[0] is OP6
        controllers.opSwitch[k] = s.ops[5 - k];
    controllers.opSwitch[6] = 0;
    controllers.values_[kControllerPitch] = 0x2000;
    controllers.values_[kControllerPitchRangeUp] = s.pb_up;
    controllers.values_[kControllerPitchRangeDn] = s.pb_down;
    controllers.values_[kControllerPitchStep] = s.pb_step;
    controllers.masterTune = s.tune;
    controllers.portamento_cc = s.porta_time;
    controllers.portamento_gliss_cc = s.porta_gliss != 0;
    controllers.portamento_enable_cc = false;
    controllers.mpeEnabled = false;
    controllers.wheel = mod(s.wheel);
    controllers.foot = mod(s.foot);
    controllers.breath = mod(s.breath);
    controllers.at = mod(s.at);
    controllers.modwheel_cc = controllers.foot_cc = controllers.breath_cc = controllers.aftertouch_cc = 0;
    controllers.refresh();
    monoMode = s.mono != 0;
    auto tuning = std::make_shared<StandardTuning>();
    for (int n = 0; n < MAX_ACTIVE_NOTES; n++) {          // zeroed memory: Dexed leaves fb_buf_ unset
        void *m = calloc(1, sizeof(Dx7Note));
        voices[n].note = new (m) Dx7Note(tuning, nullptr);
        voices[n].midi_note = -1;
        voices[n].keydown = voices[n].sustained = voices[n].live = false;
        voices[n].keydown_seq = -1;
    }
    memset((void *)&lfo, 0, sizeof lfo);
    lfo.reset(data + 137);
    FILE *f = fopen(argv[2], "wb");
    if (!f)
        return 2;
    int ev = 0;
    for (int b = 0; b < s.len; b++) {
        while (ev < s.nev && s.ev[ev].block <= b)
            event(s.ev[ev++]);
        int32_t lfovalue = lfo.getsample(), lfodelay = lfo.getdelay();
        int64_t raw[N] = {0}, clip[N] = {0};
        int32_t buf[N];
        for (int n = 0; n < MAX_ACTIVE_NOTES; n++) {
            if (!voices[n].live)
                continue;
            memset(buf, 0, sizeof buf);
            voices[n].note->compute(buf, lfovalue, lfodelay, &controllers);
            for (int j = 0; j < N; j++) {
                int32_t val = buf[j] >> 4;
                raw[j] += buf[j];
                clip[j] += val < -(1 << 24) ? 0x8000 : val >= (1 << 24) ? 0x7fff : val >> 9;
            }
        }
        for (int j = 0; j < N; j++) {
            fwrite(&raw[j], 8, 1, f);
            fwrite(&clip[j], 8, 1, f);
        }
    }
    fclose(f);
    return 0;
}
