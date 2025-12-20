#include "Preset.h"
#include "Synthesizer.h"
#include "Voice.h"
#include <fstream>
#include <string>
#include <iostream>
#include <mutex>
#include <SDL3/SDL.h>
#include <cJSON.h>

extern Synthesizer g_synth;
extern std::mutex g_synthMutex;
extern SDL_Window* g_window;

void Preset::save(const std::string& filename) {
    cJSON *root = cJSON_CreateObject();

    // Global parameters
    cJSON_AddNumberToObject(root, "MasterVolume", g_synth.masterVolume);
    cJSON_AddNumberToObject(root, "Pan", g_synth.pan);
    cJSON_AddNumberToObject(root, "UnisonCount", g_synth.unisonCount);
    cJSON_AddNumberToObject(root, "UnisonSpreadIndex", g_synth.unisonSpreadIndex);
    cJSON_AddNumberToObject(root, "PitchBend", g_synth.pitchBend);
    cJSON_AddNumberToObject(root, "PitchBendRange", g_synth.pitchBendRange);
    cJSON_AddNumberToObject(root, "ModWheelValue", g_synth.modWheelValue);
    cJSON_AddNumberToObject(root, "ModLfoPhase", g_synth.modLfoPhase);
    cJSON_AddNumberToObject(root, "ModLfoRate", g_synth.modLfoRate);

    // Arpeggiator
    cJSON *arp = cJSON_AddObjectToObject(root, "Arpeggiator");
    cJSON_AddBoolToObject(arp, "Enabled", g_synth.arpEnabled);
    cJSON_AddNumberToObject(arp, "Bpm", g_synth.arpBpm);
    cJSON_AddNumberToObject(arp, "Gate", g_synth.arpGate);
    cJSON_AddNumberToObject(arp, "Direction", g_synth.arpDirection);
    cJSON_AddNumberToObject(arp, "Range", g_synth.arpRange);
    cJSON_AddBoolToObject(arp, "Hold", g_synth.arpHold);

    // Voices
    cJSON *voices = cJSON_AddArrayToObject(root, "Voices");
    for (size_t v = 0; v < g_synth.voices.size(); ++v) {
        Voice& voice = g_synth.voices[v];
        cJSON *vobj = cJSON_CreateObject();
        cJSON_AddNumberToObject(vobj, "AttackTime", voice.getAttackTime());
        cJSON_AddNumberToObject(vobj, "DecayTime", voice.getDecayTime());
        cJSON_AddNumberToObject(vobj, "SustainLevel", voice.getSustainLevel());
        cJSON_AddNumberToObject(vobj, "ReleaseTime", voice.getReleaseTime());
        cJSON_AddNumberToObject(vobj, "MixLevel", voice.getMixLevel());
        cJSON_AddNumberToObject(vobj, "UnisonCount", voice.getUnisonCount());
        cJSON_AddNumberToObject(vobj, "UnisonSpreadIndex", voice.getUnisonSpreadIndex());

        cJSON *vcos = cJSON_AddArrayToObject(vobj, "VCOs");
        for (int i = 0; i < 3; ++i) {
            cJSON *vco = cJSON_CreateObject();
            cJSON_AddNumberToObject(vco, "Waveform", voice.getVcoWaveform(i));
            cJSON_AddNumberToObject(vco, "Mix", voice.getVcoMix(i));
            cJSON_AddNumberToObject(vco, "Detune", voice.getVcoDetune(i));
            cJSON_AddNumberToObject(vco, "PhaseMs", voice.getVcoPhaseMs(i));
            cJSON_AddNumberToObject(vco, "PulseWidth", voice.getVcoPulseWidth(i));
            cJSON_AddNumberToObject(vco, "PitchShift", voice.getVcoPitchShift(i));
            cJSON_AddNumberToObject(vco, "Pan", voice.getVcoPan(i));
            cJSON_AddItemToArray(vcos, vco);
        }
        cJSON_AddItemToArray(voices, vobj);
    }

    // Effects
    cJSON *effects = cJSON_AddObjectToObject(root, "Effects");

    cJSON *flanger = cJSON_AddObjectToObject(effects, "Flanger");
    cJSON_AddBoolToObject(flanger, "Enabled", g_synth.flangerEnabled);
    cJSON_AddNumberToObject(flanger, "Rate", g_synth.flangerRate);
    cJSON_AddNumberToObject(flanger, "Depth", g_synth.flangerDepth);
    cJSON_AddNumberToObject(flanger, "Mix", g_synth.flangerMix);

    cJSON *delay = cJSON_AddObjectToObject(effects, "Delay");
    cJSON_AddBoolToObject(delay, "Enabled", g_synth.delayEnabled);
    cJSON_AddNumberToObject(delay, "TimeSec", g_synth.delayTimeSec);
    cJSON_AddNumberToObject(delay, "Feedback", g_synth.delayFeedback);
    cJSON_AddNumberToObject(delay, "Mix", g_synth.delayMix);

    cJSON *reverb = cJSON_AddObjectToObject(effects, "Reverb");
    cJSON_AddBoolToObject(reverb, "Enabled", g_synth.reverbEnabled);
    cJSON_AddNumberToObject(reverb, "Size", g_synth.reverbSize);
    cJSON_AddNumberToObject(reverb, "Damp", g_synth.reverbDamp);
    cJSON_AddNumberToObject(reverb, "Delay", g_synth.reverbDelay);
    cJSON_AddNumberToObject(reverb, "Diffuse", g_synth.reverbDiffuse);
    cJSON_AddNumberToObject(reverb, "Stereo", g_synth.reverbStereo);
    cJSON_AddNumberToObject(reverb, "DryMix", g_synth.reverbDryMix);
    cJSON_AddNumberToObject(reverb, "WetMix", g_synth.reverbWetMix);

     cJSON *compressor = cJSON_AddObjectToObject(effects, "Compressor");
     cJSON_AddBoolToObject(compressor, "Enabled", g_synth.compressorEnabled);
     cJSON_AddNumberToObject(compressor, "ThresholdDb", g_synth.compressorThresholdDb);
     cJSON_AddNumberToObject(compressor, "Ratio", g_synth.compressorRatio);
     cJSON_AddNumberToObject(compressor, "AttackMs", g_synth.compressorAttackMs);
     cJSON_AddNumberToObject(compressor, "ReleaseMs", g_synth.compressorReleaseMs);
     cJSON_AddNumberToObject(compressor, "MakeupDb", g_synth.compressorMakeupDb);

     cJSON *dcFilter = cJSON_AddObjectToObject(effects, "DCFilter");
     cJSON_AddBoolToObject(dcFilter, "Enabled", g_synth.dcFilterEnabled);
     cJSON_AddNumberToObject(dcFilter, "Alpha", g_synth.dcFilterAlpha);

     cJSON *softClip = cJSON_AddObjectToObject(effects, "SoftClipping");
     cJSON_AddBoolToObject(softClip, "Enabled", g_synth.softClipEnabled);
     cJSON_AddNumberToObject(softClip, "Drive", g_synth.softClipDrive);

     cJSON *autoGain = cJSON_AddObjectToObject(effects, "AutoGain");
     cJSON_AddBoolToObject(autoGain, "Enabled", g_synth.autoGainEnabled);
     cJSON_AddNumberToObject(autoGain, "TargetRMS", g_synth.autoGainTargetRMS);
     cJSON_AddNumberToObject(autoGain, "Alpha", g_synth.autoGainAlpha);

    // Filter
    cJSON *filter = cJSON_AddObjectToObject(root, "Filter");
    cJSON_AddBoolToObject(filter, "Enabled", g_synth.filterEnabled);
    cJSON_AddNumberToObject(filter, "Cutoff", g_synth.filter.getCutoff());
    cJSON_AddNumberToObject(filter, "Resonance", g_synth.filter.getResonance());
    cJSON_AddNumberToObject(filter, "Drive", g_synth.filter.getDrive());
    cJSON_AddNumberToObject(filter, "Inertial", g_synth.filter.getInertial());
    cJSON_AddNumberToObject(filter, "Oversampling", g_synth.filter.getOversampling());

    // Window state
    if (g_window) {
        cJSON *window = cJSON_AddObjectToObject(root, "Window");
        int x, y;
        SDL_GetWindowPosition(g_window, &x, &y);
        cJSON_AddNumberToObject(window, "x", x);
        cJSON_AddNumberToObject(window, "y", y);
        int w, h;
        SDL_GetWindowSize(g_window, &w, &h);
        cJSON_AddNumberToObject(window, "w", w);
        cJSON_AddNumberToObject(window, "h", h);
        Uint32 flags = SDL_GetWindowFlags(g_window);
        bool fullscreen = (flags & SDL_WINDOW_FULLSCREEN);
        cJSON_AddBoolToObject(window, "fullscreen", fullscreen);
    }

    char *json_str = cJSON_Print(root);
    if (json_str) {
        std::ofstream file(filename);
        if (file.is_open()) {
            file << json_str;
            file.close();
            SDL_Log("Preset saved to: %s", filename.c_str());
        } else {
            SDL_Log("Failed to open preset file for writing: %s", filename.c_str());
        }
        cJSON_free(json_str);
    } else {
        SDL_Log("Failed to serialize JSON");
    }
    cJSON_Delete(root);
}

void Preset::load(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        SDL_Log("Failed to open preset file for reading: %s", filename.c_str());
        return;
    }

    std::string json_str((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    cJSON *root = cJSON_Parse(json_str.c_str());
    if (!root) {
        SDL_Log("Failed to parse JSON from: %s", filename.c_str());
        return;
    }

    // Global parameters
    cJSON *item = cJSON_GetObjectItem(root, "MasterVolume");
    if (item) g_synth.masterVolume = item->valuedouble;
    item = cJSON_GetObjectItem(root, "Pan");
    if (item) g_synth.pan = item->valuedouble;
    item = cJSON_GetObjectItem(root, "UnisonCount");
    if (item) g_synth.unisonCount = item->valueint;
    item = cJSON_GetObjectItem(root, "UnisonSpreadIndex");
    if (item) g_synth.unisonSpreadIndex = item->valueint;
    item = cJSON_GetObjectItem(root, "PitchBend");
    if (item) g_synth.pitchBend = item->valuedouble;
    item = cJSON_GetObjectItem(root, "PitchBendRange");
    if (item) g_synth.pitchBendRange = item->valuedouble;
    item = cJSON_GetObjectItem(root, "ModWheelValue");
    if (item) g_synth.modWheelValue = item->valuedouble;
    item = cJSON_GetObjectItem(root, "ModLfoPhase");
    if (item) g_synth.modLfoPhase = item->valuedouble;
    item = cJSON_GetObjectItem(root, "ModLfoRate");
    if (item) g_synth.modLfoRate = item->valuedouble;

    // Arpeggiator
    cJSON *arp = cJSON_GetObjectItem(root, "Arpeggiator");
    if (arp) {
        item = cJSON_GetObjectItem(arp, "Enabled");
        if (item) g_synth.arpEnabled = cJSON_IsTrue(item);
        item = cJSON_GetObjectItem(arp, "Bpm");
        if (item) g_synth.arpBpm = item->valuedouble;
        item = cJSON_GetObjectItem(arp, "Gate");
        if (item) g_synth.arpGate = item->valuedouble;
        item = cJSON_GetObjectItem(arp, "Direction");
        if (item) g_synth.arpDirection = item->valueint;
        item = cJSON_GetObjectItem(arp, "Range");
        if (item) g_synth.arpRange = item->valueint;
        item = cJSON_GetObjectItem(arp, "Hold");
        if (item) g_synth.arpHold = cJSON_IsTrue(item);
    }

    // Voices
    cJSON *voices = cJSON_GetObjectItem(root, "Voices");
    if (voices && cJSON_IsArray(voices)) {
        int num_voices = cJSON_GetArraySize(voices);
        for (int v = 0; v < num_voices && v < (int)g_synth.voices.size(); ++v) {
            cJSON *vobj = cJSON_GetArrayItem(voices, v);
            if (!vobj) continue;
            Voice& voice = g_synth.voices[v];

            item = cJSON_GetObjectItem(vobj, "AttackTime");
            if (item) voice.setAttackTime(item->valuedouble);
            item = cJSON_GetObjectItem(vobj, "DecayTime");
            if (item) voice.setDecayTime(item->valuedouble);
            item = cJSON_GetObjectItem(vobj, "SustainLevel");
            if (item) voice.setSustainLevel(item->valuedouble);
            item = cJSON_GetObjectItem(vobj, "ReleaseTime");
            if (item) voice.setReleaseTime(item->valuedouble);
            item = cJSON_GetObjectItem(vobj, "MixLevel");
            if (item) voice.setMixLevel(item->valuedouble);
            item = cJSON_GetObjectItem(vobj, "UnisonCount");
            if (item) voice.setUnisonCount(item->valueint);
            item = cJSON_GetObjectItem(vobj, "UnisonSpreadIndex");
            if (item) voice.setUnisonSpreadIndex(item->valueint);

            cJSON *vcos = cJSON_GetObjectItem(vobj, "VCOs");
            if (vcos && cJSON_IsArray(vcos)) {
                for (int i = 0; i < 3 && i < cJSON_GetArraySize(vcos); ++i) {
                    cJSON *vco = cJSON_GetArrayItem(vcos, i);
                    if (!vco) continue;
                    item = cJSON_GetObjectItem(vco, "Waveform");
                    if (item) voice.setVcoWaveform(i, static_cast<Oscillator::WaveformType>(item->valueint));
                    item = cJSON_GetObjectItem(vco, "Mix");
                    if (item) voice.setVcoMix(i, item->valuedouble);
                    item = cJSON_GetObjectItem(vco, "Detune");
                    if (item) voice.setVcoDetune(i, item->valuedouble);
                    item = cJSON_GetObjectItem(vco, "PhaseMs");
                    if (item) voice.setVcoPhaseMs(i, item->valuedouble);
                    item = cJSON_GetObjectItem(vco, "PulseWidth");
                    if (item) voice.setVcoPulseWidth(i, item->valuedouble);
                    item = cJSON_GetObjectItem(vco, "PitchShift");
                    if (item) voice.setVcoPitchShift(i, item->valuedouble);
                    item = cJSON_GetObjectItem(vco, "Pan");
                    if (item) voice.setVcoPan(i, item->valuedouble);
                }
            }
        }
    }

    // Effects
    cJSON *effects = cJSON_GetObjectItem(root, "Effects");
    if (effects) {
        cJSON *flanger = cJSON_GetObjectItem(effects, "Flanger");
        if (flanger) {
            item = cJSON_GetObjectItem(flanger, "Enabled");
            if (item) g_synth.flangerEnabled = cJSON_IsTrue(item);
            item = cJSON_GetObjectItem(flanger, "Rate");
            if (item) g_synth.flangerRate = item->valuedouble;
            item = cJSON_GetObjectItem(flanger, "Depth");
            if (item) g_synth.flangerDepth = item->valuedouble;
            item = cJSON_GetObjectItem(flanger, "Mix");
            if (item) g_synth.flangerMix = item->valuedouble;
        }

        cJSON *delay = cJSON_GetObjectItem(effects, "Delay");
        if (delay) {
            item = cJSON_GetObjectItem(delay, "Enabled");
            if (item) g_synth.delayEnabled = cJSON_IsTrue(item);
            item = cJSON_GetObjectItem(delay, "TimeSec");
            if (item) g_synth.delayTimeSec = item->valuedouble;
            item = cJSON_GetObjectItem(delay, "Feedback");
            if (item) g_synth.delayFeedback = item->valuedouble;
            item = cJSON_GetObjectItem(delay, "Mix");
            if (item) g_synth.delayMix = item->valuedouble;
        }

        cJSON *reverb = cJSON_GetObjectItem(effects, "Reverb");
        if (reverb) {
            item = cJSON_GetObjectItem(reverb, "Enabled");
            if (item) g_synth.reverbEnabled = cJSON_IsTrue(item);
            item = cJSON_GetObjectItem(reverb, "Size");
            if (item) g_synth.reverbSize = item->valuedouble;
            item = cJSON_GetObjectItem(reverb, "Damp");
            if (item) g_synth.reverbDamp = item->valuedouble;
            item = cJSON_GetObjectItem(reverb, "Delay");
            if (item) g_synth.reverbDelay = item->valuedouble;
            item = cJSON_GetObjectItem(reverb, "Diffuse");
            if (item) g_synth.reverbDiffuse = item->valuedouble;
            item = cJSON_GetObjectItem(reverb, "Stereo");
            if (item) g_synth.reverbStereo = item->valuedouble;
            item = cJSON_GetObjectItem(reverb, "DryMix");
            if (item) g_synth.reverbDryMix = item->valuedouble;
            item = cJSON_GetObjectItem(reverb, "WetMix");
            if (item) g_synth.reverbWetMix = item->valuedouble;
        }

         cJSON *compressor = cJSON_GetObjectItem(effects, "Compressor");
         if (compressor) {
             item = cJSON_GetObjectItem(compressor, "Enabled");
             if (item) g_synth.compressorEnabled = cJSON_IsTrue(item);
             item = cJSON_GetObjectItem(compressor, "ThresholdDb");
             if (item) g_synth.compressorThresholdDb = item->valuedouble;
             item = cJSON_GetObjectItem(compressor, "Ratio");
             if (item) g_synth.compressorRatio = item->valuedouble;
             item = cJSON_GetObjectItem(compressor, "AttackMs");
             if (item) g_synth.compressorAttackMs = item->valuedouble;
             item = cJSON_GetObjectItem(compressor, "ReleaseMs");
             if (item) g_synth.compressorReleaseMs = item->valuedouble;
             item = cJSON_GetObjectItem(compressor, "MakeupDb");
             if (item) g_synth.compressorMakeupDb = item->valuedouble;
         }

         cJSON *dcFilter = cJSON_GetObjectItem(effects, "DCFilter");
         if (dcFilter) {
             item = cJSON_GetObjectItem(dcFilter, "Enabled");
             if (item) g_synth.dcFilterEnabled = cJSON_IsTrue(item);
             item = cJSON_GetObjectItem(dcFilter, "Alpha");
             if (item) g_synth.dcFilterAlpha = item->valuedouble;
         }

         cJSON *softClip = cJSON_GetObjectItem(effects, "SoftClipping");
         if (softClip) {
             item = cJSON_GetObjectItem(softClip, "Enabled");
             if (item) g_synth.softClipEnabled = cJSON_IsTrue(item);
             item = cJSON_GetObjectItem(softClip, "Drive");
             if (item) g_synth.softClipDrive = item->valuedouble;
         }

         cJSON *autoGain = cJSON_GetObjectItem(effects, "AutoGain");
         if (autoGain) {
             item = cJSON_GetObjectItem(autoGain, "Enabled");
             if (item) g_synth.autoGainEnabled = cJSON_IsTrue(item);
             item = cJSON_GetObjectItem(autoGain, "TargetRMS");
             if (item) g_synth.autoGainTargetRMS = item->valuedouble;
             item = cJSON_GetObjectItem(autoGain, "Alpha");
             if (item) g_synth.autoGainAlpha = item->valuedouble;
         }
    }

    // Filter
    cJSON *filter = cJSON_GetObjectItem(root, "Filter");
    if (filter) {
        item = cJSON_GetObjectItem(filter, "Enabled");
        if (item) g_synth.filterEnabled = cJSON_IsTrue(item);
        item = cJSON_GetObjectItem(filter, "Cutoff");
        if (item) g_synth.filter.setCutoff(item->valuedouble);
        item = cJSON_GetObjectItem(filter, "Resonance");
        if (item) g_synth.filter.setResonance(item->valuedouble);
        item = cJSON_GetObjectItem(filter, "Drive");
        if (item) g_synth.filter.setDrive(item->valuedouble);
        item = cJSON_GetObjectItem(filter, "Inertial");
        if (item) g_synth.filter.setInertial(item->valuedouble);
        item = cJSON_GetObjectItem(filter, "Oversampling");
        if (item) g_synth.filter.setOversampling(item->valueint);
    }

    // Window state
    cJSON *window = cJSON_GetObjectItem(root, "Window");
    if (window && g_window) {
        item = cJSON_GetObjectItem(window, "x");
        int x = item ? item->valueint : SDL_WINDOWPOS_UNDEFINED;
        item = cJSON_GetObjectItem(window, "y");
        int y = item ? item->valueint : SDL_WINDOWPOS_UNDEFINED;
        SDL_SetWindowPosition(g_window, x, y);

        item = cJSON_GetObjectItem(window, "w");
        int w = item ? item->valueint : 800;
        item = cJSON_GetObjectItem(window, "h");
        int h = item ? item->valueint : 600;
        SDL_SetWindowSize(g_window, w, h);

        item = cJSON_GetObjectItem(window, "fullscreen");
        if (item && cJSON_IsTrue(item)) {
            SDL_SetWindowFullscreen(g_window, true);
        }
    }

    cJSON_Delete(root);
    SDL_Log("Preset loaded from: %s", filename.c_str());
}