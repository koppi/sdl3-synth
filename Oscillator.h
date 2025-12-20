#pragma once

#include "Utils.h"
#include <SDL3/SDL.h>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <complex>

class Oscillator {
public:
    enum WaveformType { SINE, SQUARE, SAW, TRIANGLE, SAW_UP, SAW_DOWN, PULSE, RANDOM };
    enum EnvelopeState { OFF, ATTACK, DECAY, SUSTAIN, RELEASE };

    Oscillator();

    void setFrequency(float freq);
    void setAmplitude(float amp);
    void setWaveformType(WaveformType type);
    void setPhaseOffsetSec(float s);
    void setPulseWidth(float pw);
    void setPitchShiftSemitones(float s);
    void setDetuneCents(float c);
    void setPitchBend(float bend_semitones);
    void setLfoMod(float mod_semitones);

    float getFrequency() const;
    float getAmplitude() const;
    int getWaveformType() const;
    float getPhaseOffsetSec() const;
    float getPulseWidth() const;
    float getPitchShiftSemitones() const;
    float getDetuneCents() const;
    float getPitchBend() const;
    float getLfoMod() const;

    void setAttackTime(float time);
    void setDecayTime(float time);
    void setSustainLevel(float level);
    void setReleaseTime(float time);

    float getAttackTime() const;
    float getDecayTime() const;
    float getSustainLevel() const;
    float getReleaseTime() const;

    void noteOn(float initialAmplitude);
    void noteOff();

    float generateSample();
    float generateSampleDetuned(float extraDetuneCents, float phaseOffsetSeconds) const;

    // needed by unison rendering
    float getPhase() const;
    float getEnvelopeLevel() const;
    Oscillator::EnvelopeState getEnvelopeState() const;
    uint64_t getNoteOnPerformanceCounter() const;

    // New setters for state write-back
    void setPhase(float p);
    void setEnvelopeState(EnvelopeState s);
    void setEnvelopeLevel(float l);

private:
    float frequency;
    float amplitude;
    float phase;
    WaveformType waveformType;

    EnvelopeState envelopeState;
    float attackTime;
    float decayTime;
    float sustainLevel;
    float releaseTime;
    float envelopeLevel;
    float startTime;
    float releaseStartTime;
    float releaseStartLevel; // New: envelope level at the start of release
    uint64_t noteOnPerformanceCounter; // New: SDL_GetPerformanceCounter() when noteOn was called

    // new
    float phaseOffsetSec; // seconds
    float pulseWidth; // 0..1 for square wave
    float pitchShiftSemitones; // +/- semitones
    float detuneCents; // fine detune
    float pitchBend; // in semitones
    float lfoMod; // in semitones
    mutable uint32_t randState;
};