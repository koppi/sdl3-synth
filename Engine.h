#pragma once

#include "Synthesizer.h"
#include "Knobs.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

static constexpr int SYNTH_SCOPE_BUFFER = 2048;
static constexpr int SYNTH_MAX_VOICES = 16;
static constexpr int SYNTH_SCOPE_VOICE_BUFFER = 512;

struct SynthParameter {
    std::string id;
    std::string name;
    std::string units;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float defaultValue = 0.0f;
    float* value = nullptr;
    std::function<void(float)> apply;
    bool hidden = false;

    float toNormalized(float plain) const {
        if (maxValue <= minValue) return 0.0f;
        float v = (plain - minValue) / (maxValue - minValue);
        return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    }

    float toPlain(float normalized) const {
        if (normalized < 0.0f) normalized = 0.0f;
        if (normalized > 1.0f) normalized = 1.0f;
        return minValue + normalized * (maxValue - minValue);
    }
};

// Shared real-time synthesis engine used by both the standalone application and
// the VST3 plug-in. All timing is sample based, so a host can run it at any
// sample rate.
class SynthEngine {
public:
    SynthEngine();

    void setSampleRate(float rate);
    float getSampleRate() const { return sampleRate; }

    // Audio processing. Public entry points lock the engine mutex; the audio
    // callback should call process() once per block.
    void process(float* outLeft, float* outRight, int frames);

    // Note and MIDI handling.
    void noteOn(int midiNote, float velocity);
    void noteOff(int midiNote);
    void allNotesOff();
    void setPitchBend(float value);
    void setModWheel(float value);
    void handleMidiMessage(const uint8_t* data, int length);
    void handleControlChange(int controller, int value);

    // Parameter support (continuous controls exposed to the host).
    int getParameterCount() const { return (int)parameters.size(); }
    const SynthParameter& getParameter(int index) const { return parameters[index]; }
    float getParameterNormalized(int index) const;
    void setParameterNormalized(int index, float normalized);
    void parameterTouched(int index);
    void setParameterChangedCallback(std::function<void(int, float)> cb) {
        parameterChanged = std::move(cb);
    }

    // Knob / MIDI-learn state.
    void registerKnobParams();
    void syncKnobValuesFromSynth();
    void syncParametersFromSynth();

    // Visualisation data (written by the audio thread).
    float leftScopeBuffer[SYNTH_SCOPE_BUFFER]{};
    float rightScopeBuffer[SYNTH_SCOPE_BUFFER]{};
    std::atomic<int> scopeWriteIndex{0};
    float voiceScopeBuffers[SYNTH_MAX_VOICES][SYNTH_SCOPE_VOICE_BUFFER]{};
    std::atomic<int> voiceScopeWriteIdx[SYNTH_MAX_VOICES];
    void copyScope(float* left, float* right, int count) const;
    void copyVoiceScope(int voice, float* out, int count) const;

    Synthesizer synth;
    KnobContext knobs;
    // Recursive so UI, preset and parameter helpers can re-enter safely.
    std::recursive_mutex mutex;

private:
    void noteOnInternal(int midiNote, float velocity);
    void noteOffInternal(int midiNote);
    int allocateVoice();
    void startArpeggiatorNote();
    void advanceArpeggiator();
    void stopArpeggiatorNote();
    void addParameter(const std::string& id, const std::string& name,
                      const std::string& units, float minValue, float maxValue,
                      float defaultValue, float* value,
                      std::function<void(float)> apply = {}, bool hidden = false);

    float sampleRate = 48000.0f;
    std::function<void(int, float)> parameterChanged;

    // Cached "first voice" values used by the UI / host parameters.
    float uiVoiceFreq = 440.0f;
    float uiVoiceAmp = 1.0f;
    float uiVoiceMix = 1.0f;
    float uiVcoMix[3] = {1.0f, 1.0f, 1.0f};
    float uiVcoPitch[3] = {0.0f, 0.0f, 0.0f};
    float uiVcoDetune[3] = {0.0f, 0.0f, 0.0f};
    float uiVcoPhase[3] = {0.0f, 0.0f, 0.0f};
    float uiVcoPulseWidth[3] = {0.5f, 0.5f, 0.5f};
    float uiVcoPan[3] = {0.0f, 0.0f, 0.0f};
    float uiAttack = 0.01f;
    float uiDecay = 0.1f;
    float uiSustain = 0.8f;
    float uiRelease = 0.3f;
    float uiFilterCutoff = 20000.0f;
    float uiFilterResonance = 1.0f;
    float uiFilterDrive = 1.0f;
    float uiFilterInertial = 0.0f;

    std::vector<SynthParameter> parameters;

    // Sample based arpeggiator state.
    uint64_t arpStepCountdown = 0;
    uint64_t arpGateCountdown = 0;
    bool arpWasEnabled = false;
    uint32_t arpRandomState = 0x12345678u;

    // Per-engine reverb damping state (the original used function statics).
    float reverbDampFilterL = 0.0f;
    float reverbDampFilterR = 0.0f;
};
