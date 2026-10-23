#include "Engine.h"
#include "SineTable.h"
#include "Utils.h"

#include <algorithm>
#include <cmath>
#include <cstring>

SynthEngine::SynthEngine() {
    initSineTable();
    for (int i = 0; i < SYNTH_MAX_VOICES; ++i) voiceScopeWriteIdx[i].store(0);
    registerKnobParams();
    setSampleRate(48000.0f);
}

void SynthEngine::setSampleRate(float rate) {
    if (rate <= 0.0f) return;
    std::lock_guard<std::recursive_mutex> lock(mutex);
    sampleRate = rate;
    synth.setSampleRate(rate);
    arpStepCountdown = 0;
    arpGateCountdown = 0;
}

void SynthEngine::addParameter(const std::string& id, const std::string& name,
                               const std::string& units, float minValue, float maxValue,
                               float defaultValue, float* value,
                               std::function<void(float)> apply, bool hidden) {
    SynthParameter p;
    p.id = id;
    p.name = name;
    p.units = units;
    p.minValue = minValue;
    p.maxValue = maxValue;
    p.defaultValue = defaultValue;
    p.value = value;
    p.apply = std::move(apply);
    p.hidden = hidden;
    parameters.push_back(std::move(p));
}

void SynthEngine::registerKnobParams() {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    knobs.params.clear();
    parameters.clear();
    syncKnobValuesFromSynth();

    auto addKnob = [this](float* value, float minValue, float maxValue,
                          int voiceIndex, int vcoIndex, KnobParam::Type type,
                          int parameterIndex,
                          std::function<void(float)> apply = {}) -> KnobParam& {
        knobs.params.emplace_back(value, minValue, maxValue, voiceIndex, vcoIndex,
                                  type, -1, parameterIndex);
        KnobParam& k = knobs.params.back();
        k.applyCallback = std::move(apply);
        return k;
    };

    auto applyVoiceFreq = [this](float v) {
        for (auto& voice : synth.voices) voice.setFrequency(v);
    };
    auto applyVoiceAmp = [this](float v) {
        for (auto& voice : synth.voices) voice.setAmplitude(v);
    };
    auto applyVoiceMix = [this](float v) {
        for (auto& voice : synth.voices) voice.setMixLevel(v);
    };
    auto applyAttack = [this](float v) {
        for (auto& voice : synth.voices) voice.setAttackTime(v);
    };
    auto applyDecay = [this](float v) {
        for (auto& voice : synth.voices) voice.setDecayTime(v);
    };
    auto applySustain = [this](float v) {
        for (auto& voice : synth.voices) voice.setSustainLevel(v);
    };
    auto applyRelease = [this](float v) {
        for (auto& voice : synth.voices) voice.setReleaseTime(v);
    };

    // --- Master -------------------------------------------------------------
    int paramIndex = 0;
    addParameter("master_volume", "Master Volume", "", 0.0f, 1.0f,
                 synth.masterVolume, &synth.masterVolume);
    addKnob(&synth.masterVolume, 0.0f, 1.0f, -1, -1, KnobParam::DIRECT, paramIndex++);

    addParameter("pan", "Pan", "", -1.0f, 1.0f, synth.pan, &synth.pan);
    addKnob(&synth.pan, -1.0f, 1.0f, -1, -1, KnobParam::DIRECT, paramIndex++);

    // --- Voice 1 (applies to all voices) -----------------------------------
    addParameter("voice_frequency", "Voice Frequency", "Hz", 20.0f, 20000.0f,
                 uiVoiceFreq, &uiVoiceFreq, applyVoiceFreq);
    addKnob(&uiVoiceFreq, 20.0f, 20000.0f, -1, -1, KnobParam::VOICE_FREQ,
            paramIndex++, applyVoiceFreq);

    addParameter("voice_amplitude", "Voice Amplitude", "", 0.0f, 1.0f,
                 uiVoiceAmp, &uiVoiceAmp, applyVoiceAmp);
    addKnob(&uiVoiceAmp, 0.0f, 1.0f, -1, -1, KnobParam::VOICE_AMP,
            paramIndex++, applyVoiceAmp);

    addParameter("voice_mix", "Voice Mix", "", 0.0f, 1.0f,
                 uiVoiceMix, &uiVoiceMix, applyVoiceMix);
    addKnob(&uiVoiceMix, 0.0f, 1.0f, -1, -1, KnobParam::VOICE_MIX,
            paramIndex++, applyVoiceMix);

    // --- VCOs ---------------------------------------------------------------
    for (int vco = 0; vco < 3; ++vco) {
        auto applyMix = [this, vco](float v) {
            for (auto& voice : synth.voices) voice.setVcoMix(vco, v);
        };
        auto applyPitch = [this, vco](float v) {
            for (auto& voice : synth.voices) voice.setVcoPitchShift(vco, v);
        };
        auto applyDetune = [this, vco](float v) {
            for (auto& voice : synth.voices) voice.setVcoDetune(vco, v);
        };
        auto applyPhase = [this, vco](float v) {
            for (auto& voice : synth.voices) voice.setVcoPhaseMs(vco, v);
        };
        auto applyPW = [this, vco](float v) {
            for (auto& voice : synth.voices) voice.setVcoPulseWidth(vco, v);
        };
        auto applyPan = [this, vco](float v) {
            for (auto& voice : synth.voices) voice.setVcoPan(vco, v);
        };

        const std::string prefix = "vco" + std::to_string(vco + 1) + "_";
        const std::string label = "VCO " + std::to_string(vco + 1) + " ";

        addParameter(prefix + "mix", label + "Mix", "", 0.0f, 1.0f,
                     uiVcoMix[vco], &uiVcoMix[vco], applyMix);
        addKnob(&uiVcoMix[vco], 0.0f, 1.0f, -1, vco, KnobParam::VCO_MIX,
                paramIndex++, applyMix);

        addParameter(prefix + "pitch", label + "Pitch", "st", -36.0f, 36.0f,
                     uiVcoPitch[vco], &uiVcoPitch[vco], applyPitch);
        addKnob(&uiVcoPitch[vco], -36.0f, 36.0f, -1, vco, KnobParam::VCO_PITCH,
                paramIndex++, applyPitch);

        addParameter(prefix + "detune", label + "Detune", "ct", -100.0f, 100.0f,
                     uiVcoDetune[vco], &uiVcoDetune[vco], applyDetune);
        addKnob(&uiVcoDetune[vco], -100.0f, 100.0f, -1, vco, KnobParam::VCO_DETUNE,
                paramIndex++, applyDetune);

        addParameter(prefix + "phase", label + "Phase", "ms", -50.0f, 50.0f,
                     uiVcoPhase[vco], &uiVcoPhase[vco], applyPhase);
        addKnob(&uiVcoPhase[vco], -50.0f, 50.0f, -1, vco, KnobParam::VCO_PHASE,
                paramIndex++, applyPhase);

        addParameter(prefix + "pulse_width", label + "Pulse Width", "", 0.01f, 0.99f,
                     uiVcoPulseWidth[vco], &uiVcoPulseWidth[vco], applyPW);
        addKnob(&uiVcoPulseWidth[vco], 0.01f, 0.99f, -1, vco, KnobParam::VCO_PW,
                paramIndex++, applyPW);

        addParameter(prefix + "pan", label + "Pan", "", -1.0f, 1.0f,
                     uiVcoPan[vco], &uiVcoPan[vco], applyPan);
        addKnob(&uiVcoPan[vco], -1.0f, 1.0f, -1, vco, KnobParam::VCO_PAN,
                paramIndex++, applyPan);
    }

    // --- ADSR ---------------------------------------------------------------
    addParameter("attack", "Attack", "s", 0.0f, 2.0f,
                 uiAttack, &uiAttack, applyAttack);
    addKnob(&uiAttack, 0.0f, 2.0f, -1, -1, KnobParam::ATTACK,
            paramIndex++, applyAttack);

    addParameter("decay", "Decay", "s", 0.0f, 2.0f,
                 uiDecay, &uiDecay, applyDecay);
    addKnob(&uiDecay, 0.0f, 2.0f, -1, -1, KnobParam::DECAY,
            paramIndex++, applyDecay);

    addParameter("sustain", "Sustain", "", 0.0f, 1.0f,
                 uiSustain, &uiSustain, applySustain);
    addKnob(&uiSustain, 0.0f, 1.0f, -1, -1, KnobParam::SUSTAIN,
            paramIndex++, applySustain);

    addParameter("release", "Release", "s", 0.0f, 5.0f,
                 uiRelease, &uiRelease, applyRelease);
    addKnob(&uiRelease, 0.0f, 5.0f, -1, -1, KnobParam::RELEASE,
            paramIndex++, applyRelease);

    // --- Modulation / arpeggiator ------------------------------------------
    addParameter("pitch_bend_range", "Pitch Bend Range", "st", 0.0f, 12.0f,
                 synth.pitchBendRange, &synth.pitchBendRange);
    addKnob(&synth.pitchBendRange, 0.0f, 12.0f, -1, -1, KnobParam::DIRECT, paramIndex++);

    addParameter("mod_lfo_rate", "Modulation LFO Rate", "Hz", 0.0f, 20.0f,
                 synth.modLfoRate, &synth.modLfoRate);
    addKnob(&synth.modLfoRate, 0.0f, 20.0f, -1, -1, KnobParam::DIRECT, paramIndex++);

    addParameter("arp_bpm", "Arpeggiator BPM", "BPM", 30.0f, 240.0f,
                 synth.arpBpm, &synth.arpBpm);
    addKnob(&synth.arpBpm, 30.0f, 240.0f, -1, -1, KnobParam::DIRECT, paramIndex++);

    addParameter("arp_gate", "Arpeggiator Gate", "", 0.01f, 1.0f,
                 synth.arpGate, &synth.arpGate);
    addKnob(&synth.arpGate, 0.01f, 1.0f, -1, -1, KnobParam::DIRECT, paramIndex++);

    // --- Effects ------------------------------------------------------------
    addParameter("flanger_rate", "Flanger Rate", "Hz", 0.01f, 10.0f,
                 synth.flangerRate, &synth.flangerRate);
    addKnob(&synth.flangerRate, 0.01f, 10.0f, -1, -1, KnobParam::DIRECT, paramIndex++);

    addParameter("flanger_depth", "Flanger Depth", "s", 0.0f, 0.02f,
                 synth.flangerDepth, &synth.flangerDepth);
    addKnob(&synth.flangerDepth, 0.0f, 0.02f, -1, -1, KnobParam::DIRECT, paramIndex++);

    addParameter("flanger_mix", "Flanger Mix", "", 0.0f, 1.0f,
                 synth.flangerMix, &synth.flangerMix);
    addKnob(&synth.flangerMix, 0.0f, 1.0f, -1, -1, KnobParam::DIRECT, paramIndex++);

    addParameter("delay_time", "Delay Time", "s", 0.01f, 2.0f,
                 synth.delayTimeSec, &synth.delayTimeSec);
    addKnob(&synth.delayTimeSec, 0.01f, 2.0f, -1, -1, KnobParam::DIRECT, paramIndex++);

    addParameter("delay_feedback", "Delay Feedback", "", 0.0f, 0.95f,
                 synth.delayFeedback, &synth.delayFeedback);
    addKnob(&synth.delayFeedback, 0.0f, 0.95f, -1, -1, KnobParam::DIRECT, paramIndex++);

    addParameter("delay_mix", "Delay Mix", "", 0.0f, 1.0f,
                 synth.delayMix, &synth.delayMix);
    addKnob(&synth.delayMix, 0.0f, 1.0f, -1, -1, KnobParam::DIRECT, paramIndex++);

    addParameter("reverb_size", "Reverb Size", "", 0.0f, 1.0f,
                 synth.reverbSize, &synth.reverbSize);
    addKnob(&synth.reverbSize, 0.0f, 1.0f, -1, -1, KnobParam::DIRECT, paramIndex++);

    addParameter("reverb_damp", "Reverb Damping", "", 0.0f, 1.0f,
                 synth.reverbDamp, &synth.reverbDamp);
    addKnob(&synth.reverbDamp, 0.0f, 1.0f, -1, -1, KnobParam::DIRECT, paramIndex++);

    addParameter("reverb_predelay", "Reverb Pre-delay", "s", 0.0f, 0.2f,
                 synth.reverbDelay, &synth.reverbDelay);
    addKnob(&synth.reverbDelay, 0.0f, 0.2f, -1, -1, KnobParam::DIRECT, paramIndex++);

    addParameter("reverb_diffuse", "Reverb Diffusion", "", 0.0f, 1.0f,
                 synth.reverbDiffuse, &synth.reverbDiffuse);
    addKnob(&synth.reverbDiffuse, 0.0f, 1.0f, -1, -1, KnobParam::DIRECT, paramIndex++);

    addParameter("reverb_stereo", "Reverb Stereo", "", 0.0f, 1.0f,
                 synth.reverbStereo, &synth.reverbStereo);
    addKnob(&synth.reverbStereo, 0.0f, 1.0f, -1, -1, KnobParam::DIRECT, paramIndex++);

    addParameter("reverb_dry", "Reverb Dry", "", 0.0f, 1.0f,
                 synth.reverbDryMix, &synth.reverbDryMix);
    addKnob(&synth.reverbDryMix, 0.0f, 1.0f, -1, -1, KnobParam::DIRECT, paramIndex++);

    addParameter("reverb_wet", "Reverb Wet", "", 0.0f, 1.0f,
                 synth.reverbWetMix, &synth.reverbWetMix);
    addKnob(&synth.reverbWetMix, 0.0f, 1.0f, -1, -1, KnobParam::DIRECT, paramIndex++);

    auto applyFilterCutoff = [this](float v) { synth.filter.setCutoff(v); };
    auto applyFilterResonance = [this](float v) { synth.filter.setResonance(v); };
    auto applyFilterDrive = [this](float v) { synth.filter.setDrive(v); };
    auto applyFilterInertial = [this](float v) { synth.filter.setInertial(v); };

    addParameter("filter_cutoff", "Filter Cutoff", "Hz", 20.0f, 20000.0f,
                 uiFilterCutoff, &uiFilterCutoff, applyFilterCutoff);
    addKnob(&uiFilterCutoff, 20.0f, 20000.0f, -1, -1, KnobParam::DIRECT,
            paramIndex++, applyFilterCutoff);

    addParameter("filter_resonance", "Filter Resonance", "", 0.1f, 10.0f,
                 uiFilterResonance, &uiFilterResonance, applyFilterResonance);
    addKnob(&uiFilterResonance, 0.1f, 10.0f, -1, -1, KnobParam::DIRECT,
            paramIndex++, applyFilterResonance);

    addParameter("filter_drive", "Filter Drive", "", 0.1f, 10.0f,
                 uiFilterDrive, &uiFilterDrive, applyFilterDrive);
    addKnob(&uiFilterDrive, 0.1f, 10.0f, -1, -1, KnobParam::DIRECT,
            paramIndex++, applyFilterDrive);

    addParameter("filter_inertial", "Filter Inertia", "", 0.0f, 0.99f,
                 uiFilterInertial, &uiFilterInertial, applyFilterInertial);
    addKnob(&uiFilterInertial, 0.0f, 0.99f, -1, -1, KnobParam::DIRECT,
            paramIndex++, applyFilterInertial);

    addParameter("compressor_threshold", "Compressor Threshold", "dB", -60.0f, 0.0f,
                 synth.compressorThresholdDb, &synth.compressorThresholdDb);
    addKnob(&synth.compressorThresholdDb, -60.0f, 0.0f, -1, -1, KnobParam::DIRECT, paramIndex++);

    addParameter("compressor_ratio", "Compressor Ratio", "", 1.0f, 20.0f,
                 synth.compressorRatio, &synth.compressorRatio);
    addKnob(&synth.compressorRatio, 1.0f, 20.0f, -1, -1, KnobParam::DIRECT, paramIndex++);

    addParameter("compressor_attack", "Compressor Attack", "ms", 0.1f, 200.0f,
                 synth.compressorAttackMs, &synth.compressorAttackMs);
    addKnob(&synth.compressorAttackMs, 0.1f, 200.0f, -1, -1, KnobParam::DIRECT, paramIndex++);

    addParameter("compressor_release", "Compressor Release", "ms", 5.0f, 2000.0f,
                 synth.compressorReleaseMs, &synth.compressorReleaseMs);
    addKnob(&synth.compressorReleaseMs, 5.0f, 2000.0f, -1, -1, KnobParam::DIRECT, paramIndex++);

    addParameter("compressor_makeup", "Compressor Makeup", "dB", -12.0f, 12.0f,
                 synth.compressorMakeupDb, &synth.compressorMakeupDb);
    addKnob(&synth.compressorMakeupDb, -12.0f, 12.0f, -1, -1, KnobParam::DIRECT, paramIndex++);

    addParameter("dc_filter_alpha", "DC Filter Alpha", "", 0.9f, 0.999f,
                 synth.dcFilterAlpha, &synth.dcFilterAlpha);
    addKnob(&synth.dcFilterAlpha, 0.9f, 0.999f, -1, -1, KnobParam::DIRECT, paramIndex++);

    addParameter("soft_clip_drive", "Soft Clip Drive", "", 1.0f, 10.0f,
                 synth.softClipDrive, &synth.softClipDrive);
    addKnob(&synth.softClipDrive, 1.0f, 10.0f, -1, -1, KnobParam::DIRECT, paramIndex++);

    addParameter("auto_gain_target", "Auto Gain Target RMS", "", 0.1f, 0.8f,
                 synth.autoGainTargetRMS, &synth.autoGainTargetRMS);
    addKnob(&synth.autoGainTargetRMS, 0.1f, 0.8f, -1, -1, KnobParam::DIRECT, paramIndex++);

    addParameter("auto_gain_alpha", "Auto Gain Alpha", "", 0.9f, 0.999f,
                 synth.autoGainAlpha, &synth.autoGainAlpha);
    addKnob(&synth.autoGainAlpha, 0.9f, 0.999f, -1, -1, KnobParam::DIRECT, paramIndex++);

    // Hidden parameters used for IMidiMapping / MIDI CC automation.
    addParameter("mod_wheel", "Mod Wheel", "", 0.0f, 1.0f,
                 synth.modWheelValue, &synth.modWheelValue, {}, true);
    addParameter("pitch_bend", "Pitch Bend", "", -1.0f, 1.0f,
                 synth.pitchBend, &synth.pitchBend, {}, true);

    knobs.onParameterChanged = [this](int index) { parameterTouched(index); };
}

void SynthEngine::syncKnobValuesFromSynth() {
    if (synth.voices.empty()) return;
    Voice& voice = synth.voices[0];
    uiVoiceFreq = voice.getFrequency();
    uiVoiceAmp = voice.getAmplitude();
    uiVoiceMix = voice.getMixLevel();
    uiFilterCutoff = synth.filter.getCutoff();
    uiFilterResonance = synth.filter.getResonance();
    uiFilterDrive = synth.filter.getDrive();
    uiFilterInertial = synth.filter.getInertial();
    for (int vco = 0; vco < 3; ++vco) {
        uiVcoMix[vco] = voice.getVcoMix(vco);
        uiVcoPitch[vco] = voice.getVcoPitchShift(vco);
        uiVcoDetune[vco] = voice.getVcoDetune(vco);
        uiVcoPhase[vco] = voice.getVcoPhaseMs(vco);
        uiVcoPulseWidth[vco] = voice.getVcoPulseWidth(vco);
        uiVcoPan[vco] = voice.getVcoPan(vco);
    }
    uiAttack = voice.getAttackTime();
    uiDecay = voice.getDecayTime();
    uiSustain = voice.getSustainLevel();
    uiRelease = voice.getReleaseTime();
}

void SynthEngine::syncParametersFromSynth() {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    syncKnobValuesFromSynth();
}

float SynthEngine::getParameterNormalized(int index) const {
    if (index < 0 || index >= (int)parameters.size()) return 0.0f;
    const auto& p = parameters[index];
    return p.value ? p.toNormalized(*p.value) : 0.0f;
}

void SynthEngine::setParameterNormalized(int index, float normalized) {
    if (index < 0 || index >= (int)parameters.size()) return;
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto& p = parameters[index];
    if (!p.value) return;
    *p.value = p.toPlain(normalized);
    if (p.apply) p.apply(*p.value);
}

void SynthEngine::parameterTouched(int index) {
    if (index < 0 || index >= (int)parameters.size()) return;
    auto& p = parameters[index];
    if (p.apply && p.value) p.apply(*p.value);
    if (parameterChanged) parameterChanged(index, getParameterNormalized(index));
}

void SynthEngine::setPitchBend(float value) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    synth.pitchBend = std::clamp(value, -1.0f, 1.0f);
}

void SynthEngine::setModWheel(float value) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    synth.modWheelValue = std::clamp(value, 0.0f, 1.0f);
}

int SynthEngine::allocateVoice() {
    const int nVoices = (int)synth.voices.size();
    if (nVoices == 0) return -1;

    int offVoiceIndex = -1;
    int releaseVoiceIndex = -1;
    uint64_t releaseLastUsed = (uint64_t)-1;
    int activeVoiceIndex = -1;
    uint64_t activeLastUsed = (uint64_t)-1;

    for (int i = 0; i < nVoices; ++i) {
        Oscillator::EnvelopeState envState = synth.voices[i].getOscillator(0).getEnvelopeState();
        if (envState == Oscillator::EnvelopeState::OFF) {
            offVoiceIndex = i;
            break;
        } else if (envState == Oscillator::EnvelopeState::RELEASE) {
            if (synth.voices[i].getLastUsed() < releaseLastUsed) {
                releaseLastUsed = synth.voices[i].getLastUsed();
                releaseVoiceIndex = i;
            }
        } else {
            if (synth.voices[i].getLastUsed() < activeLastUsed) {
                activeLastUsed = synth.voices[i].getLastUsed();
                activeVoiceIndex = i;
            }
        }
    }

    if (offVoiceIndex != -1) return offVoiceIndex;
    if (releaseVoiceIndex != -1) return releaseVoiceIndex;
    if (activeVoiceIndex != -1) return activeVoiceIndex;
    return 0;
}

void SynthEngine::noteOnInternal(int midiNote, float velocity) {
    if (synth.voices.empty()) return;

    int voiceIndex = allocateVoice();
    if (voiceIndex < 0) return;

    int oldMidiNote = synth.voices[voiceIndex].getMidiNote();
    if (oldMidiNote != -1) {
        auto it = synth.noteToVoice.find(oldMidiNote);
        if (it != synth.noteToVoice.end() && it->second == voiceIndex)
            synth.noteToVoice.erase(it);
    }

    if (synth.voices[voiceIndex].getMidiNote() != -1)
        synth.voices[voiceIndex].noteOff();

    synth.noteToVoice[midiNote] = voiceIndex;
    synth.voices[voiceIndex].noteOn(midiNote, std::clamp(velocity, 0.0f, 1.0f));
}

void SynthEngine::noteOffInternal(int midiNote) {
    auto it = synth.noteToVoice.find(midiNote);
    if (it == synth.noteToVoice.end()) return;

    int voiceIndex = it->second;
    if (voiceIndex >= 0 && voiceIndex < (int)synth.voices.size()) {
        if (synth.voices[voiceIndex].getMidiNote() == midiNote)
            synth.voices[voiceIndex].noteOff();
    }
    synth.noteToVoice.erase(it);
}

void SynthEngine::noteOn(int midiNote, float velocity) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (synth.arpEnabled) {
        if (std::find(synth.arpHeldNotes.begin(), synth.arpHeldNotes.end(), midiNote) ==
            synth.arpHeldNotes.end()) {
            synth.arpHeldNotes.push_back(midiNote);
        }
        return;
    }
    noteOnInternal(midiNote, velocity);
}

void SynthEngine::noteOff(int midiNote) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (synth.arpEnabled) {
        if (!synth.arpHold) {
            synth.arpHeldNotes.erase(
                std::remove(synth.arpHeldNotes.begin(), synth.arpHeldNotes.end(), midiNote),
                synth.arpHeldNotes.end());
        }
        return;
    }
    noteOffInternal(midiNote);
}

void SynthEngine::allNotesOff() {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    for (auto& voice : synth.voices) voice.noteOff();
    synth.noteToVoice.clear();
    synth.arpHeldNotes.clear();
    synth.arpActiveVoice = -1;
    synth.arpActiveMidi = -1;
    arpStepCountdown = 0;
    arpGateCountdown = 0;
}

void SynthEngine::handleControlChange(int controller, int value) {
    if (controller == 1) synth.modWheelValue = value / 127.0f;

    if (knobs.midiLearnMode && knobs.midiLearnKnobIdx >= 0 &&
        knobs.midiLearnKnobIdx < (int)knobs.params.size()) {
        knobs.params[knobs.midiLearnKnobIdx].midiCC = controller;
        knobs.midiLearnMode = false;
        knobs.midiLearnKnobIdx = -1;
    } else {
        knobs.applyMidiCCValue(controller, value);
    }
}

void SynthEngine::handleMidiMessage(const uint8_t* data, int length) {
    if (!data || length <= 0) return;
    std::lock_guard<std::recursive_mutex> lock(mutex);

    const int status = data[0] & 0xF0;
    if (status == 0xE0 && length >= 3) {
        int value = (data[2] << 7) | data[1];
        synth.pitchBend = (value - 8192.0f) / 8192.0f;
        return;
    }
    if (status == 0xB0 && length >= 3) {
        handleControlChange(data[1], data[2]);
        return;
    }
    if (status == 0x90 && length >= 3) {
        if (data[2] > 0) {
            if (synth.arpEnabled) {
                if (std::find(synth.arpHeldNotes.begin(), synth.arpHeldNotes.end(), data[1]) ==
                    synth.arpHeldNotes.end()) {
                    synth.arpHeldNotes.push_back(data[1]);
                }
            } else {
                noteOnInternal(data[1], data[2] / 127.0f);
            }
        } else {
            if (synth.arpEnabled) {
                if (!synth.arpHold) {
                    synth.arpHeldNotes.erase(
                        std::remove(synth.arpHeldNotes.begin(), synth.arpHeldNotes.end(), data[1]),
                        synth.arpHeldNotes.end());
                }
            } else {
                noteOffInternal(data[1]);
            }
        }
        return;
    }
    if (status == 0x80 && length >= 2) {
        if (synth.arpEnabled) {
            if (!synth.arpHold) {
                synth.arpHeldNotes.erase(
                    std::remove(synth.arpHeldNotes.begin(), synth.arpHeldNotes.end(), data[1]),
                    synth.arpHeldNotes.end());
            }
        } else {
            noteOffInternal(data[1]);
        }
    }
}

void SynthEngine::stopArpeggiatorNote() {
    if (synth.arpActiveVoice >= 0 && synth.arpActiveVoice < (int)synth.voices.size())
        synth.voices[synth.arpActiveVoice].noteOff();
    synth.arpActiveVoice = -1;
    synth.arpActiveMidi = -1;
    arpGateCountdown = 0;
}

void SynthEngine::startArpeggiatorNote() {
    if (synth.voices.empty() || synth.arpHeldNotes.empty()) return;

    stopArpeggiatorNote();

    std::vector<int> sortedNotes = synth.arpHeldNotes;
    std::sort(sortedNotes.begin(), sortedNotes.end());

    std::vector<int> arpPattern;
    int range = std::clamp(synth.arpRange, 1, 8);
    for (int r = 0; r < range; ++r) {
        for (int note : sortedNotes) arpPattern.push_back(note + r * 12);
    }
    if (arpPattern.empty()) return;

    const int patternSize = (int)arpPattern.size();
    int noteToPlay = arpPattern[0];

    if (synth.arpDirection == 2) { // Up-Down
        int sequenceLength = std::max(1, patternSize * 2 - 2);
        int step = synth.arpStepIndex % sequenceLength;
        noteToPlay = (step < patternSize)
                         ? arpPattern[step]
                         : arpPattern[patternSize - 2 - (step - patternSize)];
    } else {
        if (synth.arpDirection == 1) { // Down
            std::reverse(arpPattern.begin(), arpPattern.end());
        } else if (synth.arpDirection == 3) { // Random
            arpRandomState = arpRandomState * 1664525u + 1013904223u;
            synth.arpStepIndex = (int)(arpRandomState % (uint32_t)patternSize);
        }
        noteToPlay = arpPattern[synth.arpStepIndex % patternSize];
    }

    int voiceIndex = allocateVoice();
    if (voiceIndex < 0) return;

    int oldMidiNote = synth.voices[voiceIndex].getMidiNote();
    if (oldMidiNote != -1) {
        auto it = synth.noteToVoice.find(oldMidiNote);
        if (it != synth.noteToVoice.end() && it->second == voiceIndex)
            synth.noteToVoice.erase(it);
    }
    if (synth.voices[voiceIndex].getMidiNote() != -1)
        synth.voices[voiceIndex].noteOff();

    synth.noteToVoice[noteToPlay] = voiceIndex;
    synth.voices[voiceIndex].noteOn(noteToPlay, 0.8f);

    synth.arpActiveVoice = voiceIndex;
    synth.arpActiveMidi = noteToPlay;
    synth.arpStepIndex++;
}

void SynthEngine::advanceArpeggiator() {
    if (!synth.arpEnabled) {
        if (arpWasEnabled) stopArpeggiatorNote();
        arpWasEnabled = false;
        arpStepCountdown = 0;
        return;
    }
    arpWasEnabled = true;

    if (synth.arpHeldNotes.empty()) {
        stopArpeggiatorNote();
        arpStepCountdown = 0;
        synth.arpStepIndex = 0;
        return;
    }

    if (arpStepCountdown > 0) --arpStepCountdown;
    if (arpStepCountdown == 0) {
        const float bpm = std::clamp(synth.arpBpm, 1.0f, 1000.0f);
        const float stepSeconds = 60.0f / bpm / 4.0f; // 16th notes
        arpStepCountdown = std::max<uint64_t>(1, (uint64_t)(stepSeconds * sampleRate));

        startArpeggiatorNote();

        const float gateSeconds = stepSeconds * std::clamp(synth.arpGate, 0.0f, 1.0f);
        arpGateCountdown = std::max<uint64_t>(1, (uint64_t)(gateSeconds * sampleRate));
    }

    if (synth.arpActiveVoice >= 0) {
        if (arpGateCountdown == 0) {
            stopArpeggiatorNote();
        } else {
            --arpGateCountdown;
        }
    }
}

void SynthEngine::copyScope(float* left, float* right, int count) const {
    if (!left || !right || count <= 0) return;
    const int write = scopeWriteIndex.load(std::memory_order_relaxed);
    for (int i = 0; i < count; ++i) {
        int idx = (write - count + i + SYNTH_SCOPE_BUFFER * 2) % SYNTH_SCOPE_BUFFER;
        left[i] = leftScopeBuffer[idx];
        right[i] = rightScopeBuffer[idx];
    }
}

void SynthEngine::copyVoiceScope(int voice, float* out, int count) const {
    if (!out || count <= 0 || voice < 0 || voice >= SYNTH_MAX_VOICES) return;
    int write = voiceScopeWriteIdx[voice].load(std::memory_order_relaxed);
    for (int i = 0; i < count; ++i) {
        int idx = (write - count + i + SYNTH_SCOPE_VOICE_BUFFER * 2) % SYNTH_SCOPE_VOICE_BUFFER;
        out[i] = voiceScopeBuffers[voice][idx];
    }
}

void SynthEngine::process(float* outLeft, float* outRight, int frames) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (frames <= 0) return;

    const float sr = sampleRate;

    // Update LFO once per block (the original implementation did the same).
    synth.modLfoPhase += synth.modLfoRate * (float)frames / sr;
    if (synth.modLfoPhase >= 1.0f)
        synth.modLfoPhase -= std::floor(synth.modLfoPhase);
    const float lfoValue = fastSin(2.0f * (float)M_PI * synth.modLfoPhase) *
                           synth.modWheelValue;

    for (auto& voice : synth.voices) {
        voice.setPitchBend(synth.pitchBend * synth.pitchBendRange);
        voice.setLfoMod(lfoValue);
    }

    for (int frame = 0; frame < frames; ++frame) {
        advanceArpeggiator();

        float mixedSampleL = 0.0f;
        float mixedSampleR = 0.0f;

        // --- Voice synthesis and unison ------------------------------------
        for (size_t v = 0; v < synth.voices.size(); ++v) {
            int voiceUnison = synth.voices[v].getUnisonCount();
            int N = (voiceUnison > 0) ? voiceUnison : synth.unisonCount;
            N = std::clamp(N, 1, 8);

            int voiceSpreadIdx = synth.voices[v].getUnisonSpreadIndex();
            int spreadIdx = (voiceSpreadIdx >= 0) ? voiceSpreadIdx : synth.unisonSpreadIndex;
            spreadIdx = std::clamp(spreadIdx, 0, 4);
            const int spreadValues[5] = {0, 3, 10, 25, 50};
            int stepCentsLocal = spreadValues[spreadIdx];
            const float phaseSpreadValues[5] = {0.0f, 0.0001f, 0.00025f, 0.0005f, 0.001f};
            float stepPhaseSecLocal = phaseSpreadValues[spreadIdx];

            float voiceSumL = 0.0f;
            float voiceSumR = 0.0f;
            float centerSample = 0.0f;

            int center = (N - 1) / 2;
            for (int k = 0; k < N; ++k) {
                int offset = k - center;
                float detune = offset * (float)stepCentsLocal;
                if (offset == 0) {
                    float voiceLeft, voiceRight;
                    synth.voices[v].generateStereoSample(voiceLeft, voiceRight);
                    voiceSumL += voiceLeft;
                    voiceSumR += voiceRight;
                    centerSample = (voiceLeft + voiceRight) * 0.5f;
                } else {
                    float voicePan = offset > 0 ? 0.5f : -0.5f;
                    float phaseOffSec = offset * stepPhaseSecLocal;
                    float detuneLeft, detuneRight;
                    synth.voices[v].generateStereoSampleDetuned(detune, phaseOffSec, voicePan,
                                                               detuneLeft, detuneRight);
                    voiceSumL += detuneLeft;
                    voiceSumR += detuneRight;
                }
            }
            if (N > 0) {
                voiceSumL /= (float)N;
                voiceSumR /= (float)N;
            }

            mixedSampleL += voiceSumL * synth.voices[v].getMixLevel();
            mixedSampleR += voiceSumR * synth.voices[v].getMixLevel();

            int vid = v < SYNTH_MAX_VOICES ? (int)v : ((int)v % SYNTH_MAX_VOICES);
            int writeIdx = voiceScopeWriteIdx[vid].fetch_add(1, std::memory_order_relaxed) %
                           SYNTH_SCOPE_VOICE_BUFFER;
            voiceScopeBuffers[vid][writeIdx] = centerSample;
        }

        if (!synth.voices.empty()) {
            float normFactor = 1.0f / sqrtf((float)synth.voices.size());
            mixedSampleL *= normFactor;
            mixedSampleR *= normFactor;
        }

        // --- Stereo flanger ------------------------------------------------
        float afterFlangerL = mixedSampleL;
        float afterFlangerR = mixedSampleR;
        if (synth.flangerEnabled && !synth.flangerBufferL.empty()) {
            float lfo = fastSin(2.0f * (float)M_PI * synth.flangerPhase);
            synth.flangerPhase += synth.flangerRate / sr;
            if (synth.flangerPhase >= 1.0f) synth.flangerPhase -= 1.0f;

            float modDelaySec = synth.flangerDepth * (0.5f * (lfo + 1.0f));
            int modDelaySamples = static_cast<int>(modDelaySec * sr);

            int readIndexL = synth.flangerIndexL - modDelaySamples;
            if (readIndexL < 0) readIndexL += (int)synth.flangerBufferL.size();
            float delayedSampleL = synth.flangerBufferL[readIndexL];
            afterFlangerL = (1.0f - synth.flangerMix) * mixedSampleL + synth.flangerMix * delayedSampleL;
            synth.flangerBufferL[synth.flangerIndexL] = mixedSampleL;
            synth.flangerIndexL = (synth.flangerIndexL + 1) % (int)synth.flangerBufferL.size();

            int readIndexR = synth.flangerIndexR - modDelaySamples;
            if (readIndexR < 0) readIndexR += (int)synth.flangerBufferR.size();
            float delayedSampleR = synth.flangerBufferR[readIndexR];
            afterFlangerR = (1.0f - synth.flangerMix) * mixedSampleR + synth.flangerMix * delayedSampleR;
            synth.flangerBufferR[synth.flangerIndexR] = mixedSampleR;
            synth.flangerIndexR = (synth.flangerIndexR + 1) % (int)synth.flangerBufferR.size();
        }

        // --- Stereo delay ---------------------------------------------------
        float afterDelayL = afterFlangerL;
        float afterDelayR = afterFlangerR;
        if (synth.delayEnabled && synth.delayMaxSamples > 0) {
            int delaySamples = static_cast<int>(synth.delayTimeSec * sr);
            if (delaySamples >= synth.delayMaxSamples) delaySamples = synth.delayMaxSamples - 1;
            if (delaySamples < 0) delaySamples = 0;

            int readIndexL = synth.delayIndexL - delaySamples;
            if (readIndexL < 0) readIndexL += synth.delayMaxSamples;
            float delayOutL = synth.delayBufferL[readIndexL];
            synth.delayBufferL[synth.delayIndexL] = afterFlangerL + delayOutL * synth.delayFeedback;
            afterDelayL = (1.0f - synth.delayMix) * afterFlangerL + synth.delayMix * delayOutL;
            synth.delayIndexL = (synth.delayIndexL + 1) % synth.delayMaxSamples;

            int readIndexR = synth.delayIndexR - delaySamples;
            if (readIndexR < 0) readIndexR += synth.delayMaxSamples;
            float delayOutR = synth.delayBufferR[readIndexR];
            synth.delayBufferR[synth.delayIndexR] = afterFlangerR + delayOutR * synth.delayFeedback;
            afterDelayR = (1.0f - synth.delayMix) * afterFlangerR + synth.delayMix * delayOutR;
            synth.delayIndexR = (synth.delayIndexR + 1) % synth.delayMaxSamples;
        }

        // --- Enhanced stereo reverb ----------------------------------------
        float afterReverbL = afterDelayL;
        float afterReverbR = afterDelayR;
        if (synth.reverbEnabled && synth.reverbMaxSamples > 0) {
            int preDelaySamples = static_cast<int>(synth.reverbDelay * sr);
            int preDelayIdxL = (synth.reverbIndexL - preDelaySamples + synth.reverbMaxSamples) %
                               synth.reverbMaxSamples;
            int preDelayIdxR = (synth.reverbIndexR - preDelaySamples + synth.reverbMaxSamples) %
                               synth.reverbMaxSamples;

            float preDelayedL = synth.reverbBufferL[preDelayIdxL];
            float preDelayedR = synth.reverbBufferR[preDelayIdxR];

            float baseDelay = 0.02f + synth.reverbSize * 0.08f;
            float diffusion = 0.3f + synth.reverbDiffuse * 0.4f;

            int taps[6];
            taps[0] = static_cast<int>((baseDelay * 0.8f) * sr);
            taps[1] = static_cast<int>((baseDelay * 1.2f) * sr);
            taps[2] = static_cast<int>((baseDelay * 1.6f + diffusion * 0.1f) * sr);
            taps[3] = static_cast<int>((baseDelay * 2.2f + diffusion * 0.2f) * sr);
            taps[4] = static_cast<int>((baseDelay * 3.1f + diffusion * 0.3f) * sr);
            taps[5] = static_cast<int>((baseDelay * 4.5f + diffusion * 0.4f) * sr);

            float reverbOutL = 0.0f;
            for (int i = 0; i < 6; ++i) {
                int idx = (synth.reverbIndexL - taps[i] + synth.reverbMaxSamples) %
                          synth.reverbMaxSamples;
                reverbOutL += synth.reverbBufferL[idx] * (1.0f / 6.0f);
            }

            float reverbOutR = 0.0f;
            for (int i = 0; i < 6; ++i) {
                int idx = (synth.reverbIndexR - taps[i] + synth.reverbMaxSamples) %
                          synth.reverbMaxSamples;
                reverbOutR += synth.reverbBufferR[idx] * (1.0f / 6.0f);
            }

            reverbOutL *= synth.reverbSize;
            reverbOutR *= synth.reverbSize;

            float crossL = reverbOutR * synth.reverbStereo * 0.3f;
            float crossR = reverbOutL * synth.reverbStereo * 0.3f;
            reverbOutL = reverbOutL * (1.0f - synth.reverbStereo * 0.3f) + crossL;
            reverbOutR = reverbOutR * (1.0f - synth.reverbStereo * 0.3f) + crossR;

            float dampCoeff = 1.0f - synth.reverbDamp * 0.1f;
            reverbDampFilterL = reverbDampFilterL * dampCoeff + reverbOutL * (1.0f - dampCoeff);
            reverbDampFilterR = reverbDampFilterR * dampCoeff + reverbOutR * (1.0f - dampCoeff);
            reverbOutL = reverbDampFilterL;
            reverbOutR = reverbDampFilterR;

            synth.reverbBufferL[synth.reverbIndexL] = preDelayedL + reverbOutL * 0.7f;
            synth.reverbBufferR[synth.reverbIndexR] = preDelayedR + reverbOutR * 0.7f;

            afterReverbL = synth.reverbDryMix * afterDelayL + synth.reverbWetMix * reverbOutL;
            afterReverbR = synth.reverbDryMix * afterDelayR + synth.reverbWetMix * reverbOutR;

            synth.reverbIndexL = (synth.reverbIndexL + 1) % synth.reverbMaxSamples;
            synth.reverbIndexR = (synth.reverbIndexR + 1) % synth.reverbMaxSamples;
        }

        // --- Stereo bus compressor ----------------------------------------
        float processedL = afterReverbL;
        float processedR = afterReverbR;
        if (synth.compressorEnabled) {
            float attackSec = std::max(0.0001f, synth.compressorAttackMs * 0.001f);
            float releaseSec = std::max(0.0001f, synth.compressorReleaseMs * 0.001f);
            float attackCoef = std::exp(-1.0f / (attackSec * sr));
            float releaseCoef = std::exp(-1.0f / (releaseSec * sr));
            float makeup = std::pow(10.0f, synth.compressorMakeupDb / 20.0f);

            float absValL = std::fabs(processedL) + 1e-20f;
            float inDbL = 20.0f * std::log10(absValL);
            float desiredGainL = 1.0f;
            if (inDbL > synth.compressorThresholdDb) {
                float outDb = synth.compressorThresholdDb +
                              (inDbL - synth.compressorThresholdDb) / synth.compressorRatio;
                desiredGainL = std::pow(10.0f, (outDb - inDbL) / 20.0f);
            }
            float coefL = (desiredGainL < synth.compressorGainL) ? attackCoef : releaseCoef;
            synth.compressorGainL = coefL * synth.compressorGainL + (1.0f - coefL) * desiredGainL;
            processedL *= synth.compressorGainL * makeup;

            float absValR = std::fabs(processedR) + 1e-20f;
            float inDbR = 20.0f * std::log10(absValR);
            float desiredGainR = 1.0f;
            if (inDbR > synth.compressorThresholdDb) {
                float outDb = synth.compressorThresholdDb +
                              (inDbR - synth.compressorThresholdDb) / synth.compressorRatio;
                desiredGainR = std::pow(10.0f, (outDb - inDbR) / 20.0f);
            }
            float coefR = (desiredGainR < synth.compressorGainR) ? attackCoef : releaseCoef;
            synth.compressorGainR = coefR * synth.compressorGainR + (1.0f - coefR) * desiredGainR;
            processedR *= synth.compressorGainR * makeup;
        }

        // --- Filter ---------------------------------------------------------
        if (synth.filterEnabled) {
            processedL = synth.filter.process(processedL);
            processedR = synth.filter.process(processedR);
        }

        // --- DC filter -------------------------------------------------------
        if (synth.dcFilterEnabled) {
            float yL = synth.dcFilterAlpha * (synth.dcFilterY1L + processedL - synth.dcFilterX1L);
            synth.dcFilterX1L = processedL;
            synth.dcFilterY1L = yL;
            processedL = yL;

            float yR = synth.dcFilterAlpha * (synth.dcFilterY1R + processedR - synth.dcFilterX1R);
            synth.dcFilterX1R = processedR;
            synth.dcFilterY1R = yR;
            processedR = yR;
        }

        // --- Soft clipping ---------------------------------------------------
        if (synth.softClipEnabled) {
            processedL = std::tanh(processedL * synth.softClipDrive) / synth.softClipDrive;
            processedR = std::tanh(processedR * synth.softClipDrive) / synth.softClipDrive;
        }

        // --- Auto gain -------------------------------------------------------
        if (synth.autoGainEnabled) {
            float rmsL = std::sqrt(processedL * processedL);
            float rmsR = std::sqrt(processedR * processedR);
            synth.autoGainRMSL = synth.autoGainAlpha * synth.autoGainRMSL +
                                 (1.0f - synth.autoGainAlpha) * rmsL;
            synth.autoGainRMSR = synth.autoGainAlpha * synth.autoGainRMSR +
                                 (1.0f - synth.autoGainAlpha) * rmsR;

            float targetGainL = (synth.autoGainRMSL > 0.0f)
                                    ? synth.autoGainTargetRMS / synth.autoGainRMSL : 1.0f;
            float targetGainR = (synth.autoGainRMSR > 0.0f)
                                    ? synth.autoGainTargetRMS / synth.autoGainRMSR : 1.0f;
            synth.autoGainGainL = synth.autoGainAlpha * synth.autoGainGainL +
                                  (1.0f - synth.autoGainAlpha) * targetGainL;
            synth.autoGainGainR = synth.autoGainAlpha * synth.autoGainGainR +
                                  (1.0f - synth.autoGainAlpha) * targetGainR;

            processedL *= synth.autoGainGainL;
            processedR *= synth.autoGainGainR;
        }

        // Master volume and pan.
        processedL *= synth.masterVolume;
        processedR *= synth.masterVolume;
        float panLeft = 1.0f - std::max(0.0f, synth.pan);
        float panRight = 1.0f + std::min(0.0f, synth.pan);
        processedL *= panLeft;
        processedR *= panRight;

        float finalSampleL = std::clamp(processedL, -1.0f, 1.0f);
        float finalSampleR = std::clamp(processedR, -1.0f, 1.0f);

        int idx = scopeWriteIndex.fetch_add(1, std::memory_order_relaxed) % SYNTH_SCOPE_BUFFER;
        leftScopeBuffer[idx] = finalSampleL;
        rightScopeBuffer[idx] = finalSampleR;

        if (outLeft) outLeft[frame] = finalSampleL;
        if (outRight) outRight[frame] = finalSampleR;
    }
}
