#include "Oscillator.h"
#include "Utils.h"
#include "SineTable.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <cmath>
#include <complex>

Oscillator::Oscillator() : frequency(440.0f), amplitude(0.0f), phase(0.0f), waveformType(SINE),
                           envelopeState(OFF), attackTime(0.01f), decayTime(0.1f), sustainLevel(0.5f), releaseTime(0.2f),
                           envelopeLevel(0.0f), startTime(0.0f), releaseStartTime(0.0f), releaseStartLevel(0.0f), noteOnPerformanceCounter(0), phaseOffsetSec(0.0f), pulseWidth(0.5f), pitchShiftSemitones(0.0f), detuneCents(0.0f), pitchBend(0.0f), lfoMod(0.0f), randState(22222u) {}

void Oscillator::setFrequency(float freq) { frequency = freq; }
void Oscillator::setAmplitude(float amp) { amplitude = amp; }
void Oscillator::setWaveformType(WaveformType type) { waveformType = type; }
void Oscillator::setPhaseOffsetSec(float s) { phaseOffsetSec = s; }
void Oscillator::setPulseWidth(float pw) { pulseWidth = std::clamp(pw, 0.01f, 0.99f); }
void Oscillator::setPitchShiftSemitones(float s) { pitchShiftSemitones = s; }
void Oscillator::setDetuneCents(float c) { detuneCents = c; }
void Oscillator::setPitchBend(float bend_semitones) { pitchBend = bend_semitones; }
void Oscillator::setLfoMod(float mod_semitones) { lfoMod = mod_semitones; }

float Oscillator::getFrequency() const { return frequency; }
float Oscillator::getAmplitude() const { return amplitude; }
int Oscillator::getWaveformType() const { return static_cast<int>(waveformType); }
float Oscillator::getPhaseOffsetSec() const { return phaseOffsetSec; }
float Oscillator::getPulseWidth() const { return pulseWidth; }
float Oscillator::getPitchShiftSemitones() const { return pitchShiftSemitones; }
float Oscillator::getDetuneCents() const { return detuneCents; }
float Oscillator::getPitchBend() const { return pitchBend; }
float Oscillator::getLfoMod() const { return lfoMod; }

void Oscillator::setAttackTime(float time) { attackTime = time; }
void Oscillator::setDecayTime(float time) { decayTime = time; }
void Oscillator::setSustainLevel(float level) { sustainLevel = level; }
void Oscillator::setReleaseTime(float time) { releaseTime = time; }

float Oscillator::getAttackTime() const { return attackTime; }
float Oscillator::getDecayTime() const { return decayTime; }
float Oscillator::getSustainLevel() const { return sustainLevel; }
float Oscillator::getReleaseTime() const { return releaseTime; }

void Oscillator::noteOn(float initialAmplitude) {
    envelopeState = ATTACK;
    startTime = SDL_GetPerformanceCounter() / (float)SDL_GetPerformanceFrequency();
    amplitude = initialAmplitude; // Store the initial amplitude from MIDI velocity
    noteOnPerformanceCounter = SDL_GetPerformanceCounter(); // Record note on time
}

void Oscillator::noteOff() {
    if (envelopeState != OFF) {
        envelopeState = RELEASE;
        releaseStartTime = SDL_GetPerformanceCounter() / (float)SDL_GetPerformanceFrequency();
        releaseStartLevel = envelopeLevel; // Store current envelopeLevel for release phase
    }
}

float Oscillator::generateSample() {
    float sample = 0.0f;
    float t = (phase / SAMPLE_RATE) + phaseOffsetSec; // Time in seconds with phase offset

    // compute effective frequency with pitch shift and detune (optimized: avoid std::pow)
    float finalPitchMod = pitchShiftSemitones + pitchBend + lfoMod;
    float effFreq = frequency * std::exp(finalPitchMod * 0.0577622650466621f) * std::exp(detuneCents * 0.00057807807701174f);

    switch (waveformType) {
        case SINE:
            sample = fastSin(2.0f * M_PI * effFreq * t);
            break;
        case SQUARE: {
            float pos = effFreq * t - std::floor(effFreq * t);
            sample = (pos < pulseWidth) ? 1.0f : -1.0f;
            break;
        }
        case PULSE: {
            float pos = effFreq * t - std::floor(effFreq * t);
            sample = (pos < pulseWidth) ? 1.0f : -1.0f;
            break;
        }
        case SAW:
            sample = 2.0f * (t * effFreq - std::floor(t * effFreq + 0.5f));
            break;
        case SAW_UP:
            sample = 2.0f * (t * effFreq - std::floor(t * effFreq)) - 1.0f; // rising saw
            break;
        case SAW_DOWN:
            sample = 1.0f - 2.0f * (t * effFreq - std::floor(t * effFreq)); // falling saw
            break;
        case TRIANGLE:
            sample = 2.0f * std::abs(2.0f * (2.0f * t * effFreq - std::floor(2.0f * t * effFreq + 0.5f))) - 1.0f;
            break;
        case RANDOM: {
            // simple LCG noise
            randState = randState * 1664525u + 1013904223u;
            uint32_t v = (randState >> 9) & 0x7FFFFF; // 23 bits
            sample = (static_cast<float>(v) / 4194303.5f) * 2.0f - 1.0f;
            break;
        }
    }

    // Apply ADSR envelope
    float currentTime = SDL_GetPerformanceCounter() / (float)SDL_GetPerformanceFrequency();
    float elapsedTime = 0.0f;

    switch (envelopeState) {
        case OFF:
            envelopeLevel = 0.0f;
            break;
        case ATTACK:
            elapsedTime = currentTime - startTime;
            if (attackTime == 0) { // Instant attack
                envelopeLevel = 1.0f;
            } else {
                envelopeLevel = std::min(1.0f, elapsedTime / attackTime);
            }
              if (elapsedTime >= attackTime) {
                  envelopeState = DECAY;
                  startTime = currentTime; // Reset startTime for decay phase
              }
            break;
        case DECAY:
            elapsedTime = currentTime - startTime;
            if (decayTime == 0) { // Instant decay
                envelopeLevel = sustainLevel;
            }
            else {
                envelopeLevel = std::max(sustainLevel, 1.0f - (elapsedTime / decayTime) * (1.0f - sustainLevel));
            }
              if (elapsedTime >= decayTime) {
                  envelopeState = SUSTAIN;
              }
            break;
        case SUSTAIN:
            envelopeLevel = sustainLevel;
            break;
        case RELEASE:
            elapsedTime = currentTime - releaseStartTime;
            if (releaseTime == 0) { // Instant release
                envelopeLevel = 0.0f;
            } else {
                // Calculate release from releaseStartLevel
                envelopeLevel = std::max(0.0f, releaseStartLevel - (elapsedTime / releaseTime) * releaseStartLevel);
            }
              if (elapsedTime >= releaseTime || envelopeLevel <= 0.001f) { // Fade to near zero
                  envelopeState = OFF;
                  envelopeLevel = 0.0f;
             }
            break;
    }

    phase += 1.0f;
    if (phase >= SAMPLE_RATE) {
        phase -= SAMPLE_RATE;
    }

    return sample * amplitude * envelopeLevel;
}

float Oscillator::generateSampleDetuned(float extraDetuneCents, float phaseOffsetSeconds) const {
    // compute local phase without modifying internal state
    float localPhase = phase / SAMPLE_RATE + phaseOffsetSec + phaseOffsetSeconds; // seconds
    float combinedCents = detuneCents + extraDetuneCents;
    float finalPitchMod = pitchShiftSemitones + pitchBend + lfoMod;
    float effFreq = frequency * std::exp(finalPitchMod * 0.0577622650466621f) * std::exp(combinedCents * 0.00057807807701174f);
    float sample = 0.0f;
    switch (waveformType) {
        case SINE:
            sample = fastSin(2.0f * M_PI * effFreq * localPhase);
            break;
        case SQUARE: {
            float pos = effFreq * localPhase - std::floor(effFreq * localPhase);
            sample = (pos < pulseWidth) ? 1.0f : -1.0f;
            break;
        }
        case PULSE: {
            float pos = effFreq * localPhase - std::floor(effFreq * localPhase);
            sample = (pos < pulseWidth) ? 1.0f : -1.0f;
            break;
        }
        case SAW:
            sample = 2.0f * (localPhase * effFreq - std::floor(localPhase * effFreq + 0.5f));
            break;
        case SAW_UP:
            sample = 2.0f * (localPhase * effFreq - std::floor(localPhase * effFreq)) - 1.0f;
            break;
        case SAW_DOWN:
            sample = 1.0f - 2.0f * (localPhase * effFreq - std::floor(localPhase * effFreq));
            break;
        case TRIANGLE:
            sample = 2.0f * std::abs(2.0f * (2.0f * localPhase * effFreq - std::floor(2.0f * localPhase * effFreq + 0.5f))) - 1.0f;
            break;
        case RANDOM: {
            // stateless pseudo-random using phase (not great) fallback
            uint32_t s = static_cast<uint32_t>(std::fmod(localPhase * 100000.0f, 4294967295.0f));
            s = s * 1664525u + 1013904223u;
            uint32_t v = (s >> 9) & 0x7FFFFF;
            sample = (static_cast<float>(v) / 4194303.5f) * 2.0f - 1.0f;
            break;
        }
    }
    return sample * amplitude * envelopeLevel;
}



// needed by unison rendering
float Oscillator::getPhase() const { return phase; }
float Oscillator::getEnvelopeLevel() const { return envelopeLevel; }
Oscillator::EnvelopeState Oscillator::getEnvelopeState() const { return envelopeState; }
uint64_t Oscillator::getNoteOnPerformanceCounter() const { return noteOnPerformanceCounter; }

// New setters for state write-back
void Oscillator::setPhase(float p) { phase = p; }
void Oscillator::setEnvelopeState(EnvelopeState s) { envelopeState = s; }
void Oscillator::setEnvelopeLevel(float l) { envelopeLevel = l; }