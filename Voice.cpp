#include "Voice.h"
#include "Oscillator.h"
#include "Utils.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <iostream>



Voice::Voice() : midiNote(-1), lastUsed(0), mixLevel(1.0f), unisonCount(0), unisonSpreadIndex(-1), baseFrequency(440.0f) {
    for (int i=0;i<3;++i) { vcoMix[i]=1.0f/3.0f; vcoDetune[i]=0.0f; vcoPhaseMs[i]=0.0f; vcoPan[i]=0.0f; }
}

void Voice::noteOn(int note, float velocity) {
    midiNote = note;
    baseFrequency = midiNoteToFrequency(note);
    for (int i=0;i<3;++i) {
        oscs[i].setFrequency(baseFrequency);
        oscs[i].setAmplitude(velocity);
        oscs[i].noteOn(velocity);
    }
    lastUsed = SDL_GetPerformanceCounter();
}

void Voice::noteOff() {
    for (int i=0;i<3;++i) oscs[i].noteOff();
    midiNote = -1; // Indicate that this voice is no longer tied to a specific MIDI note.
}

float Voice::generateSample() {
    float sum = 0.0f;
    for (int i=0;i<3;++i) {
        sum += oscs[i].generateSample() * vcoMix[i];
    }
    return sum; // mix applied, amplitude inside oscs
}

float Voice::generateSampleDetuned(float detuneCents, float phaseOffsetSeconds) const {
    float sum = 0.0f;
    for (int i=0;i<3;++i) {
        float phaseSec = phaseOffsetSeconds + (vcoPhaseMs[i] * 0.001f);
        sum += oscs[i].generateSampleDetuned(detuneCents + vcoDetune[i], phaseSec) * vcoMix[i];
    }
    return sum;
}

void Voice::generateStereoSample(float& left, float& right) {
    left = 0.0f;
    right = 0.0f;
    for (int i = 0; i < 3; ++i) {
        float oscSample = oscs[i].generateSample() * vcoMix[i];
        // Apply panning for this oscillator
        float pan = vcoPan[i];
        float panLeft = 1.0f - std::max(0.0f, pan);  // 1.0 when pan <= 0, decreases to 0 when pan = 1
        float panRight = 1.0f + std::min(0.0f, pan); // 1.0 when pan >= 0, decreases to 0 when pan = -1
        left += oscSample * panLeft;
        right += oscSample * panRight;
    }
}

void Voice::generateStereoSampleDetuned(float detuneCents, float phaseOffsetSeconds, float voicePan, float& left, float& right) const {
    float sample = generateSampleDetuned(detuneCents, phaseOffsetSeconds);
    // Apply voice-level panning for unison stereo spread
    float panLeft = 1.0f - std::max(0.0f, voicePan);
    float panRight = 1.0f + std::min(0.0f, voicePan);
    left = sample * panLeft;
    right = sample * panRight;
}

// global-ish setters (per-voice parameters)
void Voice::setWaveformType(Oscillator::WaveformType type) { for (int i=0;i<3;++i) oscs[i].setWaveformType(type); }
void Voice::setAttackTime(float t) { for (int i=0;i<3;++i) oscs[i].setAttackTime(t); }
void Voice::setDecayTime(float t) { for (int i=0;i<3;++i) oscs[i].setDecayTime(t); }
void Voice::setSustainLevel(float l) { for (int i=0;i<3;++i) oscs[i].setSustainLevel(l); }
void Voice::setReleaseTime(float t) { for (int i=0;i<3;++i) oscs[i].setReleaseTime(t); }
void Voice::setAmplitude(float a) { for (int i=0;i<3;++i) oscs[i].setAmplitude(a); }
void Voice::setFrequency(float f) { baseFrequency = f; for (int i=0;i<3;++i) oscs[i].setFrequency(f); }
void Voice::setMixLevel(float m) { mixLevel = m; }

// per-VCO controls
void Voice::setVcoWaveform(int idx, Oscillator::WaveformType t) { if (idx>=0 && idx<3) oscs[idx].setWaveformType(t); }
void Voice::setVcoMix(int idx, float m) { if (idx>=0 && idx<3) vcoMix[idx]=m; }
void Voice::setVcoDetune(int idx, float cents) { if (idx>=0 && idx<3) { vcoDetune[idx]=cents; oscs[idx].setDetuneCents(cents); } }
void Voice::setVcoPhaseMs(int idx, float ms) { if (idx>=0 && idx<3) { vcoPhaseMs[idx]=ms; oscs[idx].setPhaseOffsetSec(ms*0.001f); } }
void Voice::setVcoPulseWidth(int idx, float pw) { if (idx>=0 && idx<3) oscs[idx].setPulseWidth(pw); }
void Voice::setVcoPitchShift(int idx, float semis) { if (idx>=0 && idx<3) oscs[idx].setPitchShiftSemitones(semis); }
void Voice::setVcoPan(int idx, float pan) { if (idx>=0 && idx<3) vcoPan[idx]=pan; }

void Voice::setPitchBend(float bend_semitones) { for (int i=0;i<3;++i) oscs[i].setPitchBend(bend_semitones); }
void Voice::setLfoMod(float mod_semitones) { for (int i=0;i<3;++i) oscs[i].setLfoMod(mod_semitones); }

// per-voice unison
void Voice::setUnisonCount(int c) { unisonCount = c; }
void Voice::setUnisonSpreadIndex(int si) { unisonSpreadIndex = si; }

// getters
float Voice::getFrequency() const { return baseFrequency; }
float Voice::getAmplitude() const { return oscs[0].getAmplitude(); }
int Voice::getWaveformType() const { return oscs[0].getWaveformType(); }
float Voice::getAttackTime() const { return oscs[0].getAttackTime(); }
float Voice::getDecayTime() const { return oscs[0].getDecayTime(); }
float Voice::getSustainLevel() const { return oscs[0].getSustainLevel(); }
float Voice::getReleaseTime() const { return oscs[0].getReleaseTime(); }
float Voice::getMixLevel() const { return mixLevel; }
int Voice::getUnisonCount() const { return unisonCount; }
int Voice::getUnisonSpreadIndex() const { return unisonSpreadIndex; }
float Voice::getPhaseOffsetMs() const { return oscs[0].getPhaseOffsetSec() * 1000.0f; }
float Voice::getPulseWidth() const { return oscs[0].getPulseWidth(); }
float Voice::getPitchShift() const { return oscs[0].getPitchShiftSemitones(); }
float Voice::getDetune() const { return oscs[0].getDetuneCents(); }

// per-vco getters
float Voice::getVcoMix(int idx) const { return (idx>=0 && idx<3)?vcoMix[idx]:0.0f; }
float Voice::getVcoDetune(int idx) const { return (idx>=0 && idx<3)?vcoDetune[idx]:0.0f; }
float Voice::getVcoPhaseMs(int idx) const { return (idx>=0 && idx<3)?vcoPhaseMs[idx]:0.0f; }
float Voice::getVcoPulseWidth(int idx) const { return (idx>=0 && idx<3)?oscs[idx].getPulseWidth():0.5f; }
int Voice::getVcoWaveform(int idx) const { return (idx>=0&&idx<3)?oscs[idx].getWaveformType():0; }
float Voice::getVcoPitchShift(int idx) const { return (idx>=0&&idx<3)?oscs[idx].getPitchShiftSemitones():0.0f; }
float Voice::getVcoPan(int idx) const { return (idx>=0 && idx<3)?vcoPan[idx]:0.0f; }


int Voice::getMidiNote() const { return midiNote; }
uint64_t Voice::getLastUsed() const { return lastUsed; }

// expose for unison
float Voice::getPhase() const { return oscs[0].getPhase(); }
float Voice::getEnvelopeLevel() const { return oscs[0].getEnvelopeLevel(); }

// New getter for internal oscillators (for state write-back)
Oscillator& Voice::getOscillator(int idx) { return oscs[idx]; }