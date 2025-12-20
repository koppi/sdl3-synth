#pragma once

#include "Oscillator.h"
#include "Utils.h"
#include <SDL3/SDL.h>
#include <cstdint>

class Voice {
public:
    Voice();

    void noteOn(int note, float velocity);
    void noteOff();

    float generateSample();
    float generateSampleDetuned(float detuneCents, float phaseOffsetSeconds) const;
    void generateStereoSample(float& left, float& right);
    void generateStereoSampleDetuned(float detuneCents, float phaseOffsetSeconds, float voicePan, float& left, float& right) const;

    // global-ish setters (per-voice parameters)
    void setWaveformType(Oscillator::WaveformType type);
    void setAttackTime(float t);
    void setDecayTime(float t);
    void setSustainLevel(float l);
    void setReleaseTime(float t);
    void setAmplitude(float a);
    void setFrequency(float f);
    void setMixLevel(float m);

    // per-VCO controls
    void setVcoWaveform(int idx, Oscillator::WaveformType t);
    void setVcoMix(int idx, float m);
    void setVcoDetune(int idx, float cents);
    void setVcoPhaseMs(int idx, float ms);
    void setVcoPulseWidth(int idx, float pw);
    void setVcoPitchShift(int idx, float semis);
    void setVcoPan(int idx, float pan);

    void setPitchBend(float bend_semitones);
    void setLfoMod(float mod_semitones);

    // per-voice unison
    void setUnisonCount(int c);
    void setUnisonSpreadIndex(int si);

    // getters
    float getFrequency() const;
    float getAmplitude() const;
    int getWaveformType() const;
    float getAttackTime() const;
    float getDecayTime() const;
    float getSustainLevel() const;
    float getReleaseTime() const;
    float getMixLevel() const;
    int getUnisonCount() const;
    int getUnisonSpreadIndex() const;
    float getPhaseOffsetMs() const;
    float getPulseWidth() const;
    float getPitchShift() const;
    float getDetune() const;

    // per-vco getters
    float getVcoMix(int idx) const;
    float getVcoDetune(int idx) const;
    float getVcoPhaseMs(int idx) const;
    float getVcoPulseWidth(int idx) const;
    int getVcoWaveform(int idx) const;
    float getVcoPitchShift(int idx) const;
    float getVcoPan(int idx) const;

    int getMidiNote() const;
    uint64_t getLastUsed() const;

    // expose for unison
    float getPhase() const;
    float getEnvelopeLevel() const;

    // New getter for internal oscillators (for state write-back)
    Oscillator& getOscillator(int idx);

private:
    Oscillator oscs[3];
    int midiNote;
    uint64_t lastUsed;
    float mixLevel;
    int unisonCount; // 0 means use global
    int unisonSpreadIndex; // -1 means use global
    float baseFrequency;
    float vcoMix[3];
    float vcoDetune[3];
    float vcoPhaseMs[3];
    float vcoPan[3];
};