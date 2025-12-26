#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <filesystem>
#include <iostream>
#include <vector>
#include <cmath>
#include <numeric>
#include <cstdint>
#include <map>
#include <algorithm>
#include <iterator>
#include <cstdio>
#include <complex>
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <mutex>   // For thread synchronization
#include <fstream> // For file stream operations
#include <sstream> // For string stream operations
#include <thread>  // For std::thread
#include <chrono>  // For std::chrono timing

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"

#include "libremidi/libremidi.hpp"
#include "SDL3/SDL.h"

#include "Utils.h"
#include "Oscillator.h"
#include "Voice.h"
#include "Synthesizer.h"
#include "Preset.h"
#include "Melody.h"
#include "SineTable.h"

// Global Melody instance
Melody g_melody;

// Visualization constants
static const int SCOPE_BUFFER = 2048;
static const int FFT_SIZE = 1024; // power of two
static const int WATERFALL_WIDTH = 512;
static const int WATERFALL_HEIGHT = 256;
static const int MAX_VOICES = 16;
static const int SCOPE_VOICE_BUFFER = 512;

Synthesizer g_synth;
std::mutex g_synthMutex;


SDL_Window* g_window = nullptr;
std::vector<std::string> presetFiles;
static SDL_DialogFileFilter filters[] = {{"JSON files", "json"}};
std::string statusMessage;
std::atomic<bool> g_arpThreadShouldExit = false;
static char g_presetFilename[128] = "default_preset.json";



// Thread-safe ring buffer for visualization (written by audio thread)
static float g_leftScopeBuffer[SCOPE_BUFFER];
static float g_rightScopeBuffer[SCOPE_BUFFER];
static std::atomic<int> g_scopeWriteIndex{0};

// Per-voice scope buffers
static float g_voiceScopeBuffers[MAX_VOICES][SCOPE_VOICE_BUFFER];
static std::atomic<int> g_voiceScopeWriteIdx[MAX_VOICES];

// Trigger settings
// 0=zero,1=level(edge),2=edge,3=hysteresis
static int g_triggerMode = 0;
static float g_triggerLevel = 0.0f; // -1..1
static int g_triggerEdge = 0; // 0=rising,1=falling
static float g_triggerHysteresis = 0.01f; // 0..0.2

// Round-robin voice allocator index
static std::atomic<int> g_roundRobinIndex{0};

// Waterfall texture and pixel buffer
static GLuint g_waterfallTex = 0;
static std::vector<unsigned char> g_waterfallPixels(WATERFALL_WIDTH * WATERFALL_HEIGHT * 3, 0);

// Simple in-place radix-2 FFT
void fft(std::vector<std::complex<float>>& a) {
    const int n = (int)a.size();
    int j = 0;
    for (int i = 1; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        float ang = 2.0f * M_PI / len * -1.0f;
        std::complex<float> wlen(std::cos(ang), fastSin(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (int k = 0; k < len/2; ++k) {
                std::complex<float> u = a[i+k];
                std::complex<float> v = a[i+k+len/2] * w;
                a[i+k] = u + v;
                a[i+k+len/2] = u - v;
                w *= wlen;
            }
        }
    }
}

// Map magnitude (dB  -100..0) to RGB
static void magToColor(float db, unsigned char &r, unsigned char &g, unsigned char &b) {
    float t = std::clamp((db + 100.0f) / 100.0f, 0.0f, 1.0f);
    // blue -> cyan -> yellow -> red
    if (t < 0.33f) {
        float u = t / 0.33f;
        r = static_cast<unsigned char>(0.0f * 255);
        g = static_cast<unsigned char>((0.0f + u * 255.0f) );
        b = static_cast<unsigned char>((128.0f + u * 127.0f));
    } else if (t < 0.66f) {
        float u = (t - 0.33f) / 0.33f;
        r = static_cast<unsigned char>((u * 255.0f));
        g = static_cast<unsigned char>((255.0f));
        b = static_cast<unsigned char>((255.0f - u * 255.0f));
    } else {
        float u = (t - 0.66f) / 0.34f;
        r = static_cast<unsigned char>((255.0f));
        g = static_cast<unsigned char>((255.0f - u * 255.0f));
        b = static_cast<unsigned char>(0);
    }
}

// Draw background grid into ImGui window draw list
static void drawGrid(ImDrawList* draw, const ImVec2& pos, const ImVec2& size, int cols, int rows, ImU32 color) {
    if (!draw) return;
    // vertical lines
    for (int c = 0; c <= cols; ++c) {
        float x = pos.x + (size.x * ((float)c / (float)cols));
        draw->AddLine(ImVec2(x, pos.y), ImVec2(x, pos.y + size.y), color, 1.0f);
    }
    // horizontal lines
    for (int r = 0; r <= rows; ++r) {
        float y = pos.y + (size.y * ((float)r / (float)rows));
        draw->AddLine(ImVec2(pos.x, y), ImVec2(pos.x + size.x, y), color, 1.0f);
    }
}


// Audio callback function
void SDLCALL audioCallback(void* userdata, SDL_AudioStream *stream, int additional_amount, int total_amount) {
    std::lock_guard<std::mutex> lock(g_synthMutex);
    Synthesizer* synth = (Synthesizer*)userdata;
    int numSamples = total_amount / sizeof(Sint16);
    Sint16* buffer = (Sint16*)SDL_malloc(total_amount);
    if (!buffer) {
        SDL_Log("Failed to allocate audio buffer: %s", SDL_GetError());
        return;
    }

    int channels = 2;
    int numFrames = numSamples / channels;

    // Update LFO
    g_synth.modLfoPhase += g_synth.modLfoRate / static_cast<float>(SAMPLE_RATE);
    if (g_synth.modLfoPhase >= 1.0f) {
        g_synth.modLfoPhase -= 1.0f;
    }
    float lfoValue = fastSin(2.0f * M_PI * g_synth.modLfoPhase) * g_synth.modWheelValue * 1.0f; // 1 semitone max depth

    // Apply global pitch mods to all voices
    for (auto& voice : synth->voices) {
        voice.setPitchBend(g_synth.pitchBend * g_synth.pitchBendRange);
        voice.setLfoMod(lfoValue);
    }

    float lastMixedL = 0.0f, lastMixedR = 0.0f; // For debug
    float lastOutL = 0.0f, lastOutR = 0.0f; // For debug
    for (int frame = 0; frame < numFrames; ++frame) {
        float mixedSampleL = 0.0f;
        float mixedSampleR = 0.0f;

        // --- Voice Synthesis and Unison ---
        for (size_t v = 0; v < synth->voices.size(); ++v) {
            int voiceUnison = synth->voices[v].getUnisonCount();
            int N = (voiceUnison > 0) ? voiceUnison : synth->unisonCount;
            N = std::clamp(N, 1, 8);

            int voiceSpreadIdx = synth->voices[v].getUnisonSpreadIndex();
            int spreadIdx = (voiceSpreadIdx >= 0) ? voiceSpreadIdx : synth->unisonSpreadIndex;
            spreadIdx = std::clamp(spreadIdx, 0, 4);
            const int spreadValues[5] = {0, 3, 10, 25, 50}; // detune in cents
            int stepCentsLocal = spreadValues[spreadIdx];
            const float phaseSpreadValues[5] = {0.0f, 0.0001f, 0.00025f, 0.0005f, 0.001f}; // phase offset in seconds
            float stepPhaseSecLocal = phaseSpreadValues[spreadIdx];

            float voiceSumL = 0.0f;
            float voiceSumR = 0.0f;
            float centerSample = 0.0f;

            int center = (N - 1) / 2;
            for (int k = 0; k < N; ++k) {
                int offset = k - center;
                float detune = offset * static_cast<float>(stepCentsLocal);
                if (offset == 0) {
                    float voiceLeft, voiceRight;
                    synth->voices[v].generateStereoSample(voiceLeft, voiceRight);
                    voiceSumL += voiceLeft;
                    voiceSumR += voiceRight;
                    centerSample = (voiceLeft + voiceRight) * 0.5f; // For visualization
                } else {
                    // For unison detuned voices, apply voice-level panning based on offset for stereo spread
                    float voicePan = offset > 0 ? 0.5f : -0.5f; // Positive offset = right, negative = left
                    float phaseOffSec = (offset * stepPhaseSecLocal);
                    float detuneLeft, detuneRight;
                    synth->voices[v].generateStereoSampleDetuned(detune, phaseOffSec, voicePan, detuneLeft, detuneRight);
                    voiceSumL += detuneLeft;
                    voiceSumR += detuneRight;
                }
            }
            if (N > 0) {
                voiceSumL /= static_cast<float>(N);
                voiceSumR /= static_cast<float>(N);
            }

            mixedSampleL += voiceSumL * synth->voices[v].getMixLevel();
            mixedSampleR += voiceSumR * synth->voices[v].getMixLevel();

            int vid = v < MAX_VOICES ? v : (v % MAX_VOICES);
            int writeIdx = g_voiceScopeWriteIdx[vid].fetch_add(1) % SCOPE_VOICE_BUFFER;
            g_voiceScopeBuffers[vid][writeIdx] = centerSample;
        }

        if (!synth->voices.empty()) {
            float normFactor = 1.0f / sqrtf(synth->voices.size());
            mixedSampleL *= normFactor;
            mixedSampleR *= normFactor;
        }

        lastMixedL = mixedSampleL;
        lastMixedR = mixedSampleR;

        // --- Stereo Flanger ---
        float afterFlangerL = mixedSampleL;
        float afterFlangerR = mixedSampleR;
        if (synth->flangerEnabled && !synth->flangerBufferL.empty()) {
            float lfo = fastSin(2.0f * M_PI * synth->flangerPhase);
            synth->flangerPhase += synth->flangerRate / static_cast<float>(SAMPLE_RATE);
            if (synth->flangerPhase >= 1.0f) synth->flangerPhase -= 1.0f;

            float modDelaySec = synth->flangerDepth * (0.5f * (lfo + 1.0f));
            int modDelaySamples = static_cast<int>(modDelaySec * SAMPLE_RATE);

            // Left
            int readIndexL = synth->flangerIndexL - modDelaySamples;
            if (readIndexL < 0) readIndexL += (int)synth->flangerBufferL.size();
            float delayedSampleL = synth->flangerBufferL[readIndexL];
            afterFlangerL = (1.0f - synth->flangerMix) * mixedSampleL + synth->flangerMix * delayedSampleL;
            synth->flangerBufferL[synth->flangerIndexL] = mixedSampleL;
            synth->flangerIndexL = (synth->flangerIndexL + 1) % (int)synth->flangerBufferL.size();

            // Right
            int readIndexR = synth->flangerIndexR - modDelaySamples;
            if (readIndexR < 0) readIndexR += (int)synth->flangerBufferR.size();
            float delayedSampleR = synth->flangerBufferR[readIndexR];
            afterFlangerR = (1.0f - synth->flangerMix) * mixedSampleR + synth->flangerMix * delayedSampleR;
            synth->flangerBufferR[synth->flangerIndexR] = mixedSampleR;
            synth->flangerIndexR = (synth->flangerIndexR + 1) % (int)synth->flangerBufferR.size();
        }

        // --- Stereo Delay ---
        float afterDelayL = afterFlangerL;
        float afterDelayR = afterFlangerR;
        if (synth->delayEnabled && synth->delayMaxSamples > 0) {
            int delaySamples = static_cast<int>(synth->delayTimeSec * SAMPLE_RATE);
            if (delaySamples >= synth->delayMaxSamples) delaySamples = synth->delayMaxSamples - 1;
            
            // Left
            int readIndexL = synth->delayIndexL - delaySamples;
            if (readIndexL < 0) readIndexL += synth->delayMaxSamples;
            float delayOutL = synth->delayBufferL[readIndexL];
            synth->delayBufferL[synth->delayIndexL] = afterFlangerL + delayOutL * synth->delayFeedback;
            afterDelayL = (1.0f - synth->delayMix) * afterFlangerL + synth->delayMix * delayOutL;
            synth->delayIndexL = (synth->delayIndexL + 1) % synth->delayMaxSamples;

            // Right
            int readIndexR = synth->delayIndexR - delaySamples;
            if (readIndexR < 0) readIndexR += synth->delayMaxSamples;
            float delayOutR = synth->delayBufferR[readIndexR];
            synth->delayBufferR[synth->delayIndexR] = afterFlangerR + delayOutR * synth->delayFeedback;
            afterDelayR = (1.0f - synth->delayMix) * afterFlangerR + synth->delayMix * delayOutR;
            synth->delayIndexR = (synth->delayIndexR + 1) % synth->delayMaxSamples;
        }

        // --- Enhanced Stereo Reverb ---
        float afterReverbL = afterDelayL;
        float afterReverbR = afterDelayR;
        if (synth->reverbEnabled && synth->reverbMaxSamples > 0) {
            // Pre-delay processing
            int preDelaySamples = static_cast<int>(synth->reverbDelay * SAMPLE_RATE);
            int preDelayIdxL = (synth->reverbIndexL - preDelaySamples + synth->reverbMaxSamples) % synth->reverbMaxSamples;
            int preDelayIdxR = (synth->reverbIndexR - preDelaySamples + synth->reverbMaxSamples) % synth->reverbMaxSamples;

            // Get pre-delayed signals
            float preDelayedL = synth->reverbBufferL[preDelayIdxL];
            float preDelayedR = synth->reverbBufferR[preDelayIdxR];

            // Calculate tap delays based on size and diffusion
            float baseDelay = 0.02f + synth->reverbSize * 0.08f; // 20ms to 100ms
            float diffusion = 0.3f + synth->reverbDiffuse * 0.4f; // Spread taps

            int taps[6];
            taps[0] = static_cast<int>((baseDelay * 0.8f) * SAMPLE_RATE);
            taps[1] = static_cast<int>((baseDelay * 1.2f) * SAMPLE_RATE);
            taps[2] = static_cast<int>((baseDelay * 1.6f + diffusion * 0.1f) * SAMPLE_RATE);
            taps[3] = static_cast<int>((baseDelay * 2.2f + diffusion * 0.2f) * SAMPLE_RATE);
            taps[4] = static_cast<int>((baseDelay * 3.1f + diffusion * 0.3f) * SAMPLE_RATE);
            taps[5] = static_cast<int>((baseDelay * 4.5f + diffusion * 0.4f) * SAMPLE_RATE);

            // Left channel processing
            float reverbOutL = 0.0f;
            for (int i = 0; i < 6; ++i) {
                int idx = (synth->reverbIndexL - taps[i] + synth->reverbMaxSamples) % synth->reverbMaxSamples;
                reverbOutL += synth->reverbBufferL[idx] * (1.0f / 6.0f);
            }

            // Right channel processing
            float reverbOutR = 0.0f;
            for (int i = 0; i < 6; ++i) {
                int idx = (synth->reverbIndexR - taps[i] + synth->reverbMaxSamples) % synth->reverbMaxSamples;
                reverbOutR += synth->reverbBufferR[idx] * (1.0f / 6.0f);
            }

            // Apply size/decay
            reverbOutL *= synth->reverbSize;
            reverbOutR *= synth->reverbSize;

            // Stereo cross-mixing
            float crossL = reverbOutR * synth->reverbStereo * 0.3f;
            float crossR = reverbOutL * synth->reverbStereo * 0.3f;
            reverbOutL = reverbOutL * (1.0f - synth->reverbStereo * 0.3f) + crossL;
            reverbOutR = reverbOutR * (1.0f - synth->reverbStereo * 0.3f) + crossR;

            // Apply damping (low-pass filter effect)
            static float dampFilterL = 0.0f, dampFilterR = 0.0f;
            float dampCoeff = 1.0f - synth->reverbDamp * 0.1f;
            dampFilterL = dampFilterL * dampCoeff + reverbOutL * (1.0f - dampCoeff);
            dampFilterR = dampFilterR * dampCoeff + reverbOutR * (1.0f - dampCoeff);
            reverbOutL = dampFilterL;
            reverbOutR = dampFilterR;

            // Write back to buffer with pre-delay input
            synth->reverbBufferL[synth->reverbIndexL] = preDelayedL + reverbOutL * 0.7f; // Feedback
            synth->reverbBufferR[synth->reverbIndexR] = preDelayedR + reverbOutR * 0.7f;

            // Mix dry/wet
            afterReverbL = synth->reverbDryMix * afterDelayL + synth->reverbWetMix * reverbOutL;
            afterReverbR = synth->reverbDryMix * afterDelayR + synth->reverbWetMix * reverbOutR;

            // Update indices
            synth->reverbIndexL = (synth->reverbIndexL + 1) % synth->reverbMaxSamples;
            synth->reverbIndexR = (synth->reverbIndexR + 1) % synth->reverbMaxSamples;
        }

        // --- Stereo Bus Compressor ---
        float processedL = afterReverbL;
        float processedR = afterReverbR;
        if (synth->compressorEnabled) {
            float attackSec = std::max(0.0001f, synth->compressorAttackMs * 0.001f);
            float releaseSec = std::max(0.0001f, synth->compressorReleaseMs * 0.001f);
            float attackCoef = std::exp(-1.0f / (attackSec * SAMPLE_RATE));
            float releaseCoef = std::exp(-1.0f / (releaseSec * SAMPLE_RATE));
            float makeup = std::pow(10.0f, synth->compressorMakeupDb / 20.0f);

            // Left
            float absValL = std::fabs(processedL) + 1e-20f;
            float inDbL = 20.0f * std::log10(absValL);
            float desiredGainL = 1.0f;
            if (inDbL > synth->compressorThresholdDb) {
                float outDb = synth->compressorThresholdDb + (inDbL - synth->compressorThresholdDb) / synth->compressorRatio;
                desiredGainL = std::pow(10.0f, (outDb - inDbL) / 20.0f);
            }
            float coefL = (desiredGainL < synth->compressorGainL) ? attackCoef : releaseCoef;
            synth->compressorGainL = coefL * synth->compressorGainL + (1.0f - coefL) * desiredGainL;
            processedL *= synth->compressorGainL * makeup;

            // Right
            float absValR = std::fabs(processedR) + 1e-20f;
            float inDbR = 20.0f * std::log10(absValR);
            float desiredGainR = 1.0f;
            if (inDbR > synth->compressorThresholdDb) {
                float outDb = synth->compressorThresholdDb + (inDbR - synth->compressorThresholdDb) / synth->compressorRatio;
                desiredGainR = std::pow(10.0f, (outDb - inDbR) / 20.0f);
            }
            float coefR = (desiredGainR < synth->compressorGainR) ? attackCoef : releaseCoef;
             synth->compressorGainR = coefR * synth->compressorGainR + (1.0f - coefR) * desiredGainR;
             processedR *= synth->compressorGainR * makeup;
         }

           // --- Filter ---
           if (synth->filterEnabled) {
                processedL = synth->filter.process(processedL);
                processedR = synth->filter.process(processedR);
           }

          // --- DC Filter ---
          if (synth->dcFilterEnabled) {
              // High-pass filter for DC removal: y[n] = alpha * (y[n-1] + x[n] - x[n-1])
              float yL = synth->dcFilterAlpha * (synth->dcFilterY1L + processedL - synth->dcFilterX1L);
              synth->dcFilterX1L = processedL;
              synth->dcFilterY1L = yL;
              processedL = yL;

              float yR = synth->dcFilterAlpha * (synth->dcFilterY1R + processedR - synth->dcFilterX1R);
              synth->dcFilterX1R = processedR;
              synth->dcFilterY1R = yR;
              processedR = yR;
          }

          // --- Soft Clipping ---
          if (synth->softClipEnabled) {
              processedL = std::tanh(processedL * synth->softClipDrive) / synth->softClipDrive;
              processedR = std::tanh(processedR * synth->softClipDrive) / synth->softClipDrive;
          }

           // --- Auto Gain ---
           if (synth->autoGainEnabled) {
               // Compute RMS

               float rmsL = std::sqrt(processedL * processedL);
               float rmsR = std::sqrt(processedR * processedR);
               synth->autoGainRMSL = synth->autoGainAlpha * synth->autoGainRMSL + (1.0f - synth->autoGainAlpha) * rmsL;
               synth->autoGainRMSR = synth->autoGainAlpha * synth->autoGainRMSR + (1.0f - synth->autoGainAlpha) * rmsR;

               // Adjust gain if RMS is below target
               float targetGainL = (synth->autoGainRMSL > 0.0f) ? synth->autoGainTargetRMS / synth->autoGainRMSL : 1.0f;
               float targetGainR = (synth->autoGainRMSR > 0.0f) ? synth->autoGainTargetRMS / synth->autoGainRMSR : 1.0f;
               synth->autoGainGainL = synth->autoGainAlpha * synth->autoGainGainL + (1.0f - synth->autoGainAlpha) * targetGainL;
               synth->autoGainGainR = synth->autoGainAlpha * synth->autoGainGainR + (1.0f - synth->autoGainAlpha) * targetGainR;

                processedL *= synth->autoGainGainL;
                processedR *= synth->autoGainGainR;
           }

          // Apply master volume
          processedL *= synth->masterVolume;
          processedR *= synth->masterVolume;

          // Apply panning (linear pan law)
          float panLeft = 1.0f - std::max(0.0f, synth->pan);  // 1.0 when pan <= 0, decreases to 0 when pan = 1
          float panRight = 1.0f + std::min(0.0f, synth->pan); // 1.0 when pan >= 0, decreases to 0 when pan = -1
          processedL *= panLeft;
          processedR *= panRight;

          // Clamp final samples
         float finalSampleL = std::clamp(processedL, -1.0f, 1.0f);
         float finalSampleR = std::clamp(processedR, -1.0f, 1.0f);

        // write to visualization ring buffer
        int idx = g_scopeWriteIndex.fetch_add(1) % SCOPE_BUFFER;
        g_leftScopeBuffer[idx] = finalSampleL;
        g_rightScopeBuffer[idx] = finalSampleR;

        Sint16 outSampleL = static_cast<Sint16>(finalSampleL * 32767);
        Sint16 outSampleR = static_cast<Sint16>(finalSampleR * 32767);
        buffer[frame * 2] = outSampleL;     // left
        buffer[frame * 2 + 1] = outSampleR; // right
        lastOutL = outSampleL;
        lastOutR = outSampleR;
    }



    int putResult = SDL_PutAudioStreamData(stream, buffer, total_amount);
    if (putResult < 0) {
        SDL_Log("SDL_PutAudioStreamData failed: %s", SDL_GetError());
    }
    SDL_free(buffer);
}

// A custom struct to hold information about the MIDI port
struct PortData {
    unsigned int portNumber;
    std::string portName;
};

// Global MIDI tracking for Web MIDI async handling
std::vector<std::unique_ptr<libremidi::midi_in>> g_midi_inputs;
std::vector<PortData> g_midi_port_data;


void handleNoteOff(int midiNote) {
    // Find the voice that should handle this note off
    auto it = g_synth.noteToVoice.find(midiNote);
    if (it != g_synth.noteToVoice.end()) {
        int voiceIndex = it->second;
        
        // Additional validation: ensure voice index is valid
        if (voiceIndex >= 0 && voiceIndex < (int)g_synth.voices.size()) {
            // Double-check that this voice is actually playing the note we're turning off
            if (g_synth.voices[voiceIndex].getMidiNote() == midiNote) {
                g_synth.voices[voiceIndex].noteOff();
            }
        }
        
        // Remove the note-to-voice mapping regardless of voice state
        g_synth.noteToVoice.erase(it);
    }
}

#ifdef EMSCRIPTEN
// JavaScript-callable MIDI callback function
extern "C" void midiCallbackFromJS(unsigned char* data, int length, int inputIndex) {
    std::lock_guard<std::mutex> lock(g_synthMutex);
    
    if (length == 0) return;
    
    int status = data[0] & 0xF0;
    int midiNote = (length >= 2) ? data[1] : 0;
    int vel = (length >= 3) ? data[2] : 0;
    
    // Debug output for incoming MIDI packets
    std::cout << "MIDI: [";
    for (int i = 0; i < length; i++) {
        std::cout << std::hex << (int)data[i];
        if (i < length - 1) std::cout << " ";
    }
    std::cout << std::dec << "] status=0x" << std::hex << status << std::dec 
              << " note=" << midiNote << " vel=" << vel << " input=" << inputIndex << std::endl;
    
    // Process MIDI message the same way as libremidi
    int voiceIndex = -1;
    
    // Pitch Bend
    if (status == 0xE0) {
        if (length >= 3) {
            int lsb = data[1];
            int msb = data[2];
            int value = (msb << 7) | lsb;
            g_synth.pitchBend = (value - 8192.0f) / 8192.0f;
        }
        return;
    }
    
    // Control Change (for Mod Wheel)
    if (status == 0xB0) {
        if (length >= 3) {
            int controller = data[1];
            if (controller == 1) { // Modulation Wheel
                g_synth.modWheelValue = data[2] / 127.0f;
            }
        }
        return;
    }
    
    // Only process Note On (0x90) and Note Off (0x80) from here
    if (status != 0x90 && status != 0x80) return;
    
    // Note On
    if (status == 0x90 && vel > 0) {
        // Allocate voice using round-robin
        int nVoices = (int)g_synth.voices.size();
        voiceIndex = g_roundRobinIndex.fetch_add(1) % nVoices;
        
        // Check if this voice was playing another note and clean up
        int oldMidiNote = g_synth.voices[voiceIndex].getMidiNote();
        if (oldMidiNote != -1) {
            auto it = g_synth.noteToVoice.find(oldMidiNote);
            if (it != g_synth.noteToVoice.end() && it->second == voiceIndex) {
                g_synth.noteToVoice.erase(it);
            }
            g_synth.voices[voiceIndex].noteOff();
        }
        
        g_synth.voices[voiceIndex].noteOn(midiNote, vel);
        g_synth.noteToVoice[midiNote] = voiceIndex;
    }
    // Note Off
    else if (status == 0x80 || (status == 0x90 && vel == 0)) {
        auto it = g_synth.noteToVoice.find(midiNote);
        if (it != g_synth.noteToVoice.end()) {
            voiceIndex = it->second;
            if (voiceIndex >= 0 && voiceIndex < (int)g_synth.voices.size()) {
                if (g_synth.voices[voiceIndex].getMidiNote() == midiNote) {
                    g_synth.voices[voiceIndex].noteOff();
                }
            }
            g_synth.noteToVoice.erase(it);
        }
    }
}
#endif

// MIDI callback function
void midiCallback(const libremidi::message& message, void* userData) {
    std::lock_guard<std::mutex> lock(g_synthMutex);
    PortData* data = static_cast<PortData*>(userData);

    unsigned int nBytes = message.bytes.size();
    if (nBytes == 0) return;

    int status = message.bytes[0] & 0xF0;
    int midiNote = message.bytes[1];
    int vel = (nBytes >= 3) ? message.bytes[2] : 0;

	// Debug output for incoming MIDI packets
	std::cout << "MIDI: [";
	for (unsigned int i = 0; i < nBytes; i++) {
		std::cout << std::hex << (int)message.bytes[i];
		if (i < nBytes - 1) std::cout << " ";
	}
	std::cout << std::dec << "] status=0x" << std::hex << status << std::dec 
			  << " note=" << midiNote << " vel=" << vel << std::endl;
    int voiceIndex = -1; // Declare voiceIndex here

    // Pitch Bend
    if (status == 0xE0) {
        if (nBytes >= 3) {
            int lsb = message.bytes[1];
            int msb = message.bytes[2];
            int value = (msb << 7) | lsb;
            g_synth.pitchBend = (value - 8192.0f) / 8192.0f;
        }
        return;
    }

    // Control Change (for Mod Wheel)
    if (status == 0xB0) {
        if (nBytes >= 3) {
            int controller = message.bytes[1];
            if (controller == 1) { // Modulation Wheel
                g_synth.modWheelValue = message.bytes[2] / 127.0f;
            }
        }
        return;
    }

    // Only process Note On (0x90) and Note Off (0x80) from here
    if (status != 0x90 && status != 0x80) return;

    if (g_synth.arpEnabled) {
        if (status == 0x90 && vel > 0) { // Actual Note On for arpeggiator
            if (std::find(g_synth.arpHeldNotes.begin(), g_synth.arpHeldNotes.end(), midiNote) == g_synth.arpHeldNotes.end()) {
                g_synth.arpHeldNotes.push_back(midiNote);
            }
        } else if (status == 0x80 || (status == 0x90 && vel == 0)) { // Note Off (0x80 or 0x90 with vel == 0)
            if (!g_synth.arpHold) {
                g_synth.arpHeldNotes.erase(std::remove(g_synth.arpHeldNotes.begin(), g_synth.arpHeldNotes.end(), midiNote), g_synth.arpHeldNotes.end());
            }
        }
    } else { // Arpeggiator is disabled
        if (status == 0x90 && vel > 0) { // Actual Note On (0x90 with velocity > 0)
            float velocity = vel / 127.0f;
            int nVoices = (int)g_synth.voices.size();
            if (nVoices == 0) return;

            // Attempt 2 & 3: Find an inactive voice OR the least recently used active voice
            int offVoiceIndex = -1;
            int releaseVoiceIndex = -1;
            uint64_t releaseLastUsed = (uint64_t)-1; // Max uint64_t value
            int activeVoiceIndex = -1;
            uint64_t activeLastUsed = (uint64_t)-1; // Max uint64_t value

            for (int i = 0; i < nVoices; ++i) {
                // Determine the state of the first oscillator in the voice
                // Assuming all oscillators in a voice share the same envelope state for voice allocation purposes
                Oscillator::EnvelopeState envState = g_synth.voices[i].getOscillator(0).getEnvelopeState();
                
                if (envState == Oscillator::EnvelopeState::OFF) {
                    offVoiceIndex = i; // Found an OFF voice, prefer this and stop searching for this category
                    break; // Prefer OFF voices immediately
                } else if (envState == Oscillator::EnvelopeState::RELEASE) {
                    if (g_synth.voices[i].getLastUsed() < releaseLastUsed) {
                        releaseLastUsed = g_synth.voices[i].getLastUsed();
                        releaseVoiceIndex = i;
                    }
                } else { // ATTACK, DECAY, SUSTAIN (active states)
                    if (g_synth.voices[i].getLastUsed() < activeLastUsed) {
                        activeLastUsed = g_synth.voices[i].getLastUsed();
                        activeVoiceIndex = i;
                    }
                }
            }
            
            // Prioritize voice selection: OFF > RELEASE > ACTIVE (LRU within each)
            if (offVoiceIndex != -1) {
                voiceIndex = offVoiceIndex;
            } else if (releaseVoiceIndex != -1) {
                voiceIndex = releaseVoiceIndex;
            } else {
                // If all voices are active (ATTACK, DECAY, SUSTAIN), steal the LRU active voice.
                // This covers the case where activeVoiceIndex might still be -1 if nVoices is 0,
                // but nVoices is checked at the beginning of the function.
                voiceIndex = activeVoiceIndex;
            }

            // At this point, voiceIndex should be a valid index (either inactive, LRU release, or LRU active).

            int oldMidiNoteOnStolenVoice = g_synth.voices[voiceIndex].getMidiNote();
            if (oldMidiNoteOnStolenVoice != -1) {
                auto it = g_synth.noteToVoice.find(oldMidiNoteOnStolenVoice);
                if (it != g_synth.noteToVoice.end() && it->second == voiceIndex) {
                    g_synth.noteToVoice.erase(it);
                }
            }

            if (g_synth.voices[voiceIndex].getMidiNote() != -1) {
                g_synth.voices[voiceIndex].noteOff();
            }
            g_synth.noteToVoice[midiNote] = voiceIndex;
            g_synth.voices[voiceIndex].noteOn(midiNote, velocity);

        } else if (status == 0x80 || (status == 0x90 && vel == 0)) { // Note Off (0x80 or 0x90 with velocity 0)
            handleNoteOff(midiNote);
        }
    }
}

void arpThreadFunction() {
    float perfFreq = (float)SDL_GetPerformanceFrequency();
    while (!g_arpThreadShouldExit) {
        uint64_t currentTime = SDL_GetPerformanceCounter();

        { // Lock scope for arpeggiator logic
            std::lock_guard<std::mutex> lock(g_synthMutex);

            if (g_synth.arpEnabled) {
                // Check if current arp note needs to be turned off (gate)
                if (g_synth.arpActiveVoice != -1 && currentTime >= g_synth.arpOffDeadline) {
                    g_synth.voices[g_synth.arpActiveVoice].noteOff();
                    g_synth.arpActiveVoice = -1;
                }

                if (!g_synth.arpHeldNotes.empty()) { // ONLY if held notes exist
                    float stepDuration = 60.0f / g_synth.arpBpm / 4.0f; // 16th notes
                    if ((currentTime - g_synth.arpLastStepTime) / perfFreq >= stepDuration) {
                        g_synth.arpLastStepTime = currentTime;

                        // Stop previous note if still playing
                        if (g_synth.arpActiveVoice != -1) {
                            g_synth.voices[g_synth.arpActiveVoice].noteOff();
                            g_synth.arpActiveVoice = -1;
                        }

                        // Generate the list of notes to play
                        std::vector<int> sortedNotes = g_synth.arpHeldNotes;
                        std::sort(sortedNotes.begin(), sortedNotes.end());
                        
                        std::vector<int> arpPattern;
                        for (int r = 0; r < g_synth.arpRange; ++r) {
                            for (int note : sortedNotes) {
                                arpPattern.push_back(note + r * 12);
                            }
                        }

                        if (!arpPattern.empty()) {
                            int patternSize = arpPattern.size();
                            int noteToPlay;

                            // Handle direction
                            if (g_synth.arpDirection == 2) { // Up-Down
                                int sequenceLength = std::max(1, patternSize * 2 - 2);
                                int step = g_synth.arpStepIndex % sequenceLength;
                                if (step < patternSize) {
                                    noteToPlay = arpPattern[step];
                                } else {
                                    noteToPlay = arpPattern[patternSize - 2 - (step - patternSize)];
                                }
                            } else {
                                if (g_synth.arpDirection == 1) { // Down
                                    std::reverse(arpPattern.begin(), arpPattern.end());
                                } else if (g_synth.arpDirection == 3) { // Random
                                    g_synth.arpStepIndex = rand() % patternSize;
                                }
                                noteToPlay = arpPattern[g_synth.arpStepIndex % patternSize];
                            }

                            // Play the note
                            int voiceIndex = g_roundRobinIndex.fetch_add(1) % g_synth.voices.size();
                            g_synth.voices[voiceIndex].noteOn(noteToPlay, 0.8f); // Fixed velocity for now
                            g_synth.arpActiveVoice = voiceIndex;
                            g_synth.arpActiveMidi = noteToPlay;

                            // Set note off time
                            float gateDuration = stepDuration * g_synth.arpGate;
                            g_synth.arpOffDeadline = currentTime + (uint64_t)(gateDuration * perfFreq);

                            g_synth.arpStepIndex++;
                        }
                    }
                } else {
                    // No notes held, so stop any playing arp note
                    if (g_synth.arpActiveVoice != -1) {
                        g_synth.voices[g_synth.arpActiveVoice].noteOff();
                        g_synth.arpActiveVoice = -1;
                    }
                    g_synth.arpStepIndex = 0; // Reset step index
                }
            } else {
                // Arp is disabled, ensure any active arp note is turned off
                if (g_synth.arpActiveVoice != -1) {
                    g_synth.voices[g_synth.arpActiveVoice].noteOff();
                    g_synth.arpActiveVoice = -1;
                }
            }
        } // End lock scope for arpeggiator logic

        // Sleep to prevent busy-waiting.
        // The arpeggiator needs to be fairly responsive, so a short sleep is appropriate.
        // Perhaps dynamically adjust based on whether arp is enabled/active.
        std::this_thread::sleep_for(std::chrono::milliseconds(5)); // Sleep for 5ms
    }
}



void fileDialogCallback(void* userdata, const char* const* filelist, int filter) {
    if (!filelist || !filelist[0]) return;
    strcpy(g_presetFilename, filelist[0]);
    int action = (int)(uintptr_t)userdata;
    if (action == 1) { // load
        Preset::load(g_presetFilename);
        statusMessage = "Preset loaded: " + std::string(g_presetFilename);
        // Rescan preset files
        presetFiles.clear();
        for (const auto& entry : std::filesystem::directory_iterator(".")) {
            if (entry.path().extension() == ".json") {
                presetFiles.push_back(entry.path().filename().string());
            }
        }
    } else if (action == 2) { // save
        Preset::save(g_presetFilename);
        statusMessage = "Preset saved: " + std::string(g_presetFilename);
        // Rescan preset files
        presetFiles.clear();
        for (const auto& entry : std::filesystem::directory_iterator(".")) {
            if (entry.path().extension() == ".json") {
                presetFiles.push_back(entry.path().filename().string());
            }
        }
    }
}

int main(int argc, char* argv[]) {
    srand(time(NULL));

    // Initialize sine lookup table for optimized oscillator processing
    initSineTable();

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        std::cerr << "SDL could not initialize! SDL_Error: " << SDL_GetError() << std::endl;
        return 1;
    }

    int display_count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&display_count);
    if (display_count == 0) {
        std::cerr << "No video display available!" << std::endl;
        SDL_Quit();
        return 1;
    }
    SDL_free(displays);

    std::string cwd = std::filesystem::current_path().string();
	
#ifndef __EMSCRIPTEN__
    // Scan for preset files
    for (const auto& entry : std::filesystem::directory_iterator(".")) {
        if (entry.path().extension() == ".json") {
            presetFiles.push_back(entry.path().filename().string());
        }
    }
#endif
	
    // Setup SDL window with OpenGL context
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_Window* window = SDL_CreateWindow("SDL3 Synthesizer", 800, 600, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE); // | SDL_WINDOW_FULLSCREEN);
    g_window = window;
    if (window == nullptr) {
        std::cerr << "Window could not be created! SDL_Error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (gl_context == nullptr) {
        std::cerr << "OpenGL context could not be created! SDL_Error: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Enable vertical sync
    SDL_GL_SetSwapInterval(1);

    SDL_GL_MakeCurrent(window, gl_context);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

#ifndef __EMSCRIPTEN__
    ImGui::LoadIniSettingsFromDisk("imgui.ini"); // Load GUI state
#endif

    // Setup Dear ImGui style
    //ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 300 es");

    ImGuiIO& io = ImGui::GetIO();

	// Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

#ifdef __EMSCRIPTEN__
	// Disable loading of imgui.ini file
	io.IniFilename = nullptr;
#endif
	
	// Load Fonts
	io.Fonts->AddFontFromFileTTF("data/xkcd-script.ttf", 23.0f);
	io.Fonts->AddFontDefault();
	
    // Setup audio device
    SDL_AudioSpec desiredSpec;
    SDL_zero(desiredSpec);
    desiredSpec.freq = SAMPLE_RATE;
    desiredSpec.format = SDL_AUDIO_S16LE;
    desiredSpec.channels = 2; // stereo

    SDL_AudioStream* audioStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desiredSpec, audioCallback, &g_synth);
    if (!audioStream) {
        std::cerr << "Failed to open audio device stream! SDL_Error: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_GL_DestroyContext(gl_context);
        SDL_Quit();
        return 1;
    }

    if (!SDL_ResumeAudioStreamDevice(audioStream)) {
        std::cerr << "Failed to resume audio device stream! SDL_Error: " << SDL_GetError() << std::endl;
        SDL_DestroyAudioStream(audioStream);
        SDL_DestroyWindow(window);
        SDL_GL_DestroyContext(gl_context);
        SDL_Quit();
        return 1;
    }

    // Setup MIDI input - disable automatic polling to prevent errors
    try {
        std::cout << "Initializing Web MIDI..." << std::endl;
        
        // Create observer callbacks
        libremidi::observer_configuration callbacks{
            .input_added =
                [&](const libremidi::input_port& id) {
                std::cout << "MIDI Input connected: " << id.port_name << std::endl;
                
                // Create port data for this input
                g_midi_port_data.push_back({.portNumber = (unsigned int)g_midi_port_data.size(), 
                                           .portName = id.port_name});
                
                auto conf = libremidi::input_configuration{
                    .on_message = [&, portIndex = g_midi_port_data.size() - 1](const libremidi::message& msg) { 
                        midiCallback(msg, &g_midi_port_data[portIndex]); 
                    }
                };
                
                auto& input = g_midi_inputs.emplace_back(
                    std::make_unique<libremidi::midi_in>(conf));
                input->open_port(id);
            },
            
            .input_removed =
                [&](const libremidi::input_port& id) {
                std::cout << "MIDI Input removed: " << id.port_name << std::endl;
                // Find and remove the corresponding input
                auto it = std::find_if(g_midi_port_data.begin(), g_midi_port_data.end(),
                                      [&id](const PortData& pd) { return pd.portName == id.port_name; });
                if (it != g_midi_port_data.end()) {
                    auto index = std::distance(g_midi_port_data.begin(), it);
                    g_midi_inputs.erase(g_midi_inputs.begin() + index);
                    g_midi_port_data.erase(it);
                }
            }
        };

        // Create observer to detect MIDI devices
#ifdef __EMSCRIPTEN__
        libremidi::observer obs{
            std::move(callbacks), libremidi::observer_configuration_for(libremidi::API::WEBMIDI)};
#else
        libremidi::observer obs{
            std::move(callbacks), libremidi::observer_configuration_for(libremidi::API::UNSPECIFIED)};
#endif
        
        // Get all existing input ports and open them
        auto input_ports = obs.get_input_ports();
        std::cout << "Found " << input_ports.size() << " MIDI input ports:" << std::endl;
        
        for (const auto& port : input_ports) {
            std::cout << "  Opening MIDI Input: " << port.port_name << std::endl;
            
            // Create port data for this input
            g_midi_port_data.push_back({.portNumber = (unsigned int)g_midi_port_data.size(), 
                                       .portName = port.port_name});
            
            auto conf = libremidi::input_configuration{
                .on_message = [&, portIndex = g_midi_port_data.size() - 1](const libremidi::message& msg) { 
                    midiCallback(msg, &g_midi_port_data[portIndex]); 
                }
            };
            
            auto& input = g_midi_inputs.emplace_back(
                std::make_unique<libremidi::midi_in>(conf));
            input->open_port(port);
        }
        
        std::cout << "MIDI observer initialized - monitoring for device changes..." << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize Web MIDI: " << e.what() << std::endl;
        std::cerr << "Continuing without MIDI input." << std::endl;
    }

    bool quit = false;
    SDL_Event e;

    // CPU monitoring variables
    long long lastTotalCpuTime = 0;
    long long lastIdleCpuTime = 0;
    float cpuUsage = 0.0f;
    uint64_t lastCpuUpdateTime = SDL_GetPerformanceCounter();
    const uint64_t cpuUpdateInterval = SDL_GetPerformanceFrequency() / 2; // Update every 0.5 seconds

    // Get initial CPU times
    std::pair<long long, long long> initialCpuTimes = getCpuTimes();
    lastTotalCpuTime = initialCpuTimes.first;
    lastIdleCpuTime = initialCpuTimes.second;

#ifndef __EMSCRIPTEN__
    std::thread arpThread(arpThreadFunction); // Launch arpeggiator thread
#endif

#ifndef __EMSCRIPTEN__
    Preset::load("default_preset.json"); // Load default preset at startup
#endif
	
	g_melody.startMelody(); // Start melody at app startup
	std::cout << "Melody started - should play automatically" << std::endl;

    while (!quit) {
        uint64_t currentTime = SDL_GetPerformanceCounter();
        float perfFreq = (float)SDL_GetPerformanceFrequency(); // Moved perfFreq here

        // Update CPU usage periodically
        if (currentTime - lastCpuUpdateTime >= cpuUpdateInterval) {
            std::pair<long long, long long> currentCpuTimes = getCpuTimes();
            long long totalCpuTimeDelta = currentCpuTimes.first - lastTotalCpuTime;
            long long idleCpuTimeDelta = currentCpuTimes.second - lastIdleCpuTime;

            if (totalCpuTimeDelta > 0) {
                cpuUsage = 100.0f * (1.0f - static_cast<float>(idleCpuTimeDelta) / totalCpuTimeDelta);
            } else {
                cpuUsage = 0.0f;
            }

            // Update window title
            char titleBuf[256];
            snprintf(titleBuf, sizeof(titleBuf), "SDL3 Synthesizer | CPU: %.1f%%", cpuUsage);
            SDL_SetWindowTitle(window, titleBuf);

            lastTotalCpuTime = currentCpuTimes.first;
            lastIdleCpuTime = currentCpuTimes.second;
            lastCpuUpdateTime = currentTime;
        }

        // --- Melody Playback Logic ---
        { // Lock scope for melody playback logic
            std::lock_guard<std::mutex> lock(g_synthMutex);
            g_melody.updateMelodyPlayback(currentTime, perfFreq, g_synth, g_roundRobinIndex);
        } // End lock scope for melody playback logic


        while (SDL_PollEvent(&e) != 0) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT) {
                quit = true;
            }
            if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == SDL_GetWindowID(window)) {
                quit = true;
            }
            if (e.type == SDL_EVENT_KEY_DOWN) {
                if (e.key.key == SDLK_ESCAPE) {
                    quit = true;
                } else if (e.key.key == SDLK_SPACE) { // Toggle startup melody
                    std::lock_guard<std::mutex> lock(g_synthMutex);
                    // Always stop all playing notes first
                    for (auto& voice : g_synth.voices) {
                        voice.noteOff();
                    }
                    g_synth.noteToVoice.clear();
                    // Clear arpeggiator state
                    g_synth.arpHeldNotes.clear();
                    g_synth.arpActiveVoice = -1;
                    g_synth.arpActiveMidi = -1;
                    
                    if (g_melody.melodyPlaying) {
                        // Stop melody if currently playing
                        g_melody.stopMelody();
                    } else {
                        // Start melody
                        g_melody.startMelody();
                    }
                } else if (e.key.key == SDLK_F1) { // Detect F1 key press
                    std::lock_guard<std::mutex> lock(g_synthMutex);
                    // Stop melody if playing
                    if (g_melody.melodyPlaying) {
                        g_melody.stopMelody();
                    }
                    // Stop all voices
                    for (auto& voice : g_synth.voices) {
                        voice.noteOff();
                    }
                    // Clear arpeggiator state
                    g_synth.arpHeldNotes.clear();
                    g_synth.arpActiveVoice = -1;
                    g_synth.arpActiveMidi = -1;
                    // Clear note-to-voice mapping
                    g_synth.noteToVoice.clear();
                }
            }
        }

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // Our synthesizer GUI will go here
        ImGui::Begin("Synthesizer Controls");
		{
            std::lock_guard<std::mutex> lock(g_synthMutex);
            ImGui::Text("Master Volume");
            // Gain presets buttons
            float _presets_vals[] = {0.0f, 0.12f, 0.25f, 0.5f, 0.75f, 0.87f, 1.0f};
            const char* _presets_lbl[] = {"0%","12%","25%","50%","75%","87%","100%"};
            for (int pi = 0; pi < 7; ++pi) {
                if (ImGui::Button(_presets_lbl[pi])) { g_synth.masterVolume = _presets_vals[pi]; }
                if (pi < 6) ImGui::SameLine();
            }
            ImGui::SliderFloat("##masterVolume", &g_synth.masterVolume, 0.0f, 1.0f);

            ImGui::Text("Pan");
            ImGui::SliderFloat("##pan", &g_synth.pan, -1.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

            for (int i = 0; i < 1; ++i) { // Controls for Voice 1 only
                if (i >= g_synth.voices.size()) break;
                ImGui::PushID(i);
                ImGui::Separator();
                ImGui::Text("Voice %d", i + 1);

                float freq = g_synth.voices[i].getFrequency();
                if (ImGui::SliderFloat("Frequency", &freq, 20.0f, 20000.0f, "%.1f Hz")) {
                    for (size_t voiceIdx = 0; voiceIdx < g_synth.voices.size(); ++voiceIdx) {
                        g_synth.voices[voiceIdx].setFrequency(freq);
                    }
                }

                float amp = g_synth.voices[i].getAmplitude();
                if (ImGui::SliderFloat("Gain", &amp, 0.0f, 1.0f)) {
                    for (size_t voiceIdx = 0; voiceIdx < g_synth.voices.size(); ++voiceIdx) {
                        g_synth.voices[voiceIdx].setAmplitude(amp);
                    }
                }
                float mix = g_synth.voices[i].getMixLevel();
                if (ImGui::SliderFloat("Mix", &mix, 0.0f, 1.0f)) {
                    for (size_t voiceIdx = 0; voiceIdx < g_synth.voices.size(); ++voiceIdx) {
                        g_synth.voices[voiceIdx].setMixLevel(mix);
                    }
                }

                // Frequency (base)
                // single freq control shown below with per-voice controls

                // Per-VCO controls
                const char* vcoWaveNames[] = {"Sine","Square","Saw","Triangle","Saw Up","Saw Down","Pulse","Random"};
                for (int vi_vco = 0; vi_vco < 3; ++vi_vco) {
                    ImGui::PushID(vi_vco);
                    ImGui::Separator();
                    char title[32]; snprintf(title, sizeof(title), "VCO %d", vi_vco+1);
                    ImGui::Text("%s", title);
                    int widx = g_synth.voices[i].getVcoWaveform(vi_vco);
                    if (ImGui::Combo("Waveform", &widx, vcoWaveNames, IM_ARRAYSIZE(vcoWaveNames))) {
                        for (size_t voiceIdx = 0; voiceIdx < g_synth.voices.size(); ++voiceIdx) {
                            g_synth.voices[voiceIdx].setVcoWaveform(vi_vco, static_cast<Oscillator::WaveformType>(widx));
                        }
                    }
                    float vmix = g_synth.voices[i].getVcoMix(vi_vco);
                    if (ImGui::SliderFloat("VCO Gain", &vmix, 0.0f, 1.0f)) {
                        for (size_t voiceIdx = 0; voiceIdx < g_synth.voices.size(); ++voiceIdx) {
                            g_synth.voices[voiceIdx].setVcoMix(vi_vco, vmix);
                        }
                    }
                    float vpitch = g_synth.voices[i].getVcoPitchShift(vi_vco);
                    if (ImGui::SliderFloat("VCO Pitch (st)", &vpitch, -36.0f, 36.0f)) {
                        for (size_t voiceIdx = 0; voiceIdx < g_synth.voices.size(); ++voiceIdx) {
                            g_synth.voices[voiceIdx].setVcoPitchShift(vi_vco, vpitch);
                        }
                    }
                    float vdet = g_synth.voices[i].getVcoDetune(vi_vco);
                    if (ImGui::SliderFloat("VCO Detune (c)", &vdet, -100.0f, 100.0f)) {
                        for (size_t voiceIdx = 0; voiceIdx < g_synth.voices.size(); ++voiceIdx) {
                            g_synth.voices[voiceIdx].setVcoDetune(vi_vco, vdet);
                        }
                    }
                    float vphase = g_synth.voices[i].getVcoPhaseMs(vi_vco);
                    if (ImGui::SliderFloat("Phase (ms)", &vphase, -50.0f, 50.0f)) {
                        for (size_t voiceIdx = 0; voiceIdx < g_synth.voices.size(); ++voiceIdx) {
                            g_synth.voices[voiceIdx].setVcoPhaseMs(vi_vco, vphase);
                        }
                    }
                    float vpw = g_synth.voices[i].getVcoPulseWidth(vi_vco);
                    if (ImGui::SliderFloat("Pulse Width", &vpw, 0.01f, 0.99f)) {
                        for (size_t voiceIdx = 0; voiceIdx < g_synth.voices.size(); ++voiceIdx) {
                            g_synth.voices[voiceIdx].setVcoPulseWidth(vi_vco, vpw);
                        }
                    }
                    float vpan = g_synth.voices[i].getVcoPan(vi_vco);
                    if (ImGui::SliderFloat("Pan", &vpan, -1.0f, 1.0f, "%.2f")) {
                        for (size_t voiceIdx = 0; voiceIdx < g_synth.voices.size(); ++voiceIdx) {
                            g_synth.voices[voiceIdx].setVcoPan(vi_vco, vpan);
                        }
                    }
                    ImGui::PopID();
                }

                // Per-voice unison controls (0 = use global)
                int vUnison = g_synth.voices[i].getUnisonCount();
                if (ImGui::SliderInt("Unison Voices (per voice, 0=global)", &vUnison, 0, 8)) {
                    for (size_t voiceIdx = 0; voiceIdx < g_synth.voices.size(); ++voiceIdx) {
                        g_synth.voices[voiceIdx].setUnisonCount(vUnison);
                    }
                }
                const char* vSpreadNames[] = {"Global","Off","Tight","Medium","Wide","Extra Wide"};
                int vSpreadUi = g_synth.voices[i].getUnisonSpreadIndex() + 1; // -1->0
                if (ImGui::Combo("Unison Spread (per voice)", &vSpreadUi, vSpreadNames, IM_ARRAYSIZE(vSpreadNames))) {
                    for (size_t voiceIdx = 0; voiceIdx < g_synth.voices.size(); ++voiceIdx) {
                        g_synth.voices[voiceIdx].setUnisonSpreadIndex(vSpreadUi - 1);
                    }
                }

                // ADSR Controls
                ImGui::Text("ADSR Envelope");
                float attack = g_synth.voices[i].getAttackTime();
                if (ImGui::SliderFloat("Attack", &attack, 0.0f, 2.0f, "%.2f s")) {
                    for (size_t voiceIdx = 0; voiceIdx < g_synth.voices.size(); ++voiceIdx) {
                        g_synth.voices[voiceIdx].setAttackTime(attack);
                    }
                }
                float decay = g_synth.voices[i].getDecayTime();
                if (ImGui::SliderFloat("Decay", &decay, 0.0f, 2.0f, "%.2f s")) {
                    for (size_t voiceIdx = 0; voiceIdx < g_synth.voices.size(); ++voiceIdx) {
                        g_synth.voices[voiceIdx].setDecayTime(decay);
                    }
                }
                float sustain = g_synth.voices[i].getSustainLevel();
                if (ImGui::SliderFloat("Sustain", &sustain, 0.0f, 1.0f, "%.2f")) {
                    for (size_t voiceIdx = 0; voiceIdx < g_synth.voices.size(); ++voiceIdx) {
                        g_synth.voices[voiceIdx].setSustainLevel(sustain);
                    }
                }
                float release = g_synth.voices[i].getReleaseTime();
                if (ImGui::SliderFloat("Release", &release, 0.0f, 5.0f, "%.2f s")) {
                    for (size_t voiceIdx = 0; voiceIdx < g_synth.voices.size(); ++voiceIdx) {
                        g_synth.voices[voiceIdx].setReleaseTime(release);
                    }
                }

                ImGui::PopID();
            }

            // Unison
            ImGui::Separator();
            ImGui::Text("Unison");
            ImGui::SliderInt("Unison Voices", &g_synth.unisonCount, 1, 8);
            const char* spreadNames[] = {"Off","Tight","Medium","Wide","Extra Wide"};
            ImGui::Combo("Unison Spread", &g_synth.unisonSpreadIndex, spreadNames, IM_ARRAYSIZE(spreadNames));

#ifndef __EMSCRIPTEN__
            // Synchronization and Preset Save/Load
            ImGui::Separator();
            if (ImGui::Button("Copy Voice 1 Params to All")) {
                int nVoices = (int)g_synth.voices.size();
                if (nVoices > 1) { // Only copy if there are other voices
                    Voice& sourceVoice = g_synth.voices[0]; // Voice 1 (0-indexed)

                    for (int i = 1; i < nVoices; ++i) {
                        Voice& destVoice = g_synth.voices[i];

                        // Copy ADSR parameters
                        destVoice.setAttackTime(sourceVoice.getAttackTime());
                        destVoice.setDecayTime(sourceVoice.getDecayTime());
                        destVoice.setSustainLevel(sourceVoice.getSustainLevel());
                        destVoice.setReleaseTime(sourceVoice.getReleaseTime());
                        
                        // Copy Mix Level
                        destVoice.setMixLevel(sourceVoice.getMixLevel());

                        // Copy Unison parameters
                        destVoice.setUnisonCount(sourceVoice.getUnisonCount());
                        destVoice.setUnisonSpreadIndex(sourceVoice.getUnisonSpreadIndex());

                        // Copy VCO parameters (assuming 3 VCOs per voice)
                        for (int vco_idx = 0; vco_idx < 3; ++vco_idx) {
                            destVoice.setVcoWaveform(vco_idx, static_cast<Oscillator::WaveformType>(sourceVoice.getVcoWaveform(vco_idx)));
                            destVoice.setVcoMix(vco_idx, sourceVoice.getVcoMix(vco_idx));
                            destVoice.setVcoDetune(vco_idx, sourceVoice.getVcoDetune(vco_idx));
                            destVoice.setVcoPhaseMs(vco_idx, sourceVoice.getVcoPhaseMs(vco_idx));
                            destVoice.setVcoPulseWidth(vco_idx, sourceVoice.getVcoPulseWidth(vco_idx));
                            destVoice.setVcoPitchShift(vco_idx, sourceVoice.getVcoPitchShift(vco_idx));
                        }
                    }
                }
            }

            ImGui::Separator();
            ImGui::Text("Presets");
            static int currentPreset = 0;
            if (ImGui::Combo("Preset", &currentPreset, [](void* data, int idx, const char** out_text) {
                std::vector<std::string>* files = (std::vector<std::string>*)data;
                if (idx < 0 || idx >= (int)files->size()) return false;
                *out_text = (*files)[idx].c_str();
                return true;
            }, &presetFiles, presetFiles.size())) {
                strcpy(g_presetFilename, presetFiles[currentPreset].c_str());
            }
            if (ImGui::Button("Save")) {
                Preset::save(g_presetFilename);
                statusMessage = "Preset saved: " + std::string(g_presetFilename);
            }
            ImGui::SameLine();
            if (ImGui::Button("Load")) {
                Preset::load(g_presetFilename);
                statusMessage = "Preset loaded: " + std::string(g_presetFilename);
            }
            ImGui::SameLine();
            if (ImGui::Button("Save...")) {
                SDL_ShowSaveFileDialog(fileDialogCallback, (void*)2, g_window, filters, 1, cwd.c_str());
            }
            ImGui::SameLine();
            if (ImGui::Button("Load...")) {
                SDL_ShowOpenFileDialog(fileDialogCallback, (void*)1, g_window, filters, 1, cwd.c_str(), false);
            }
#endif
			
            // Modulation
            ImGui::Separator();
            ImGui::Text("Modulation");
            ImGui::SliderFloat("Pitch Bend Range (st)", &g_synth.pitchBendRange, 0.0f, 12.0f);
            ImGui::SliderFloat("Mod LFO Rate (Hz)", &g_synth.modLfoRate, 0.0f, 20.0f);

            // Arpeggiator
            ImGui::Separator();
            ImGui::Text("Arpeggiator");
            bool wasArpEnabled = g_synth.arpEnabled;
            if (ImGui::Checkbox("Enabled", &g_synth.arpEnabled) && wasArpEnabled != g_synth.arpEnabled) {
                // State changed, reset everything to avoid stuck notes
                for (auto& voice : g_synth.voices) {
                    voice.noteOff();
                }
                if (g_synth.arpActiveVoice != -1) {
                    g_synth.voices[g_synth.arpActiveVoice].noteOff();
                    g_synth.arpActiveVoice = -1;
                }
                g_synth.arpHeldNotes.clear();
                g_synth.noteToVoice.clear();
            }

            if (g_synth.arpEnabled) {
                ImGui::SliderFloat("BPM", &g_synth.arpBpm, 30.0f, 240.0f);
                ImGui::SliderFloat("Gate", &g_synth.arpGate, 0.01f, 1.0f);
                const char* arpDir[] = {"Up", "Down", "Up-Down", "Random"};
                ImGui::Combo("Direction", &g_synth.arpDirection, arpDir, IM_ARRAYSIZE(arpDir));
                ImGui::SliderInt("Range (Octaves)", &g_synth.arpRange, 1, 4);
                ImGui::Checkbox("Hold", &g_synth.arpHold);
            }

            // Effects
            ImGui::Separator();
            ImGui::Text("Effects");

            if (ImGui::Checkbox("Flanger", &g_synth.flangerEnabled)) {}
            if (g_synth.flangerEnabled) {
                ImGui::SliderFloat("Flanger Rate (Hz)", &g_synth.flangerRate, 0.01f, 10.0f);
                ImGui::SliderFloat("Flanger Depth (s)", &g_synth.flangerDepth, 0.0f, 0.02f);
                ImGui::SliderFloat("Flanger Mix", &g_synth.flangerMix, 0.0f, 1.0f);
            }

            if (ImGui::Checkbox("Delay", &g_synth.delayEnabled)) {}
            if (g_synth.delayEnabled) {
                ImGui::SliderFloat("Delay Time (s)", &g_synth.delayTimeSec, 0.01f, 2.0f);
                ImGui::SliderFloat("Delay Feedback", &g_synth.delayFeedback, 0.0f, 0.95f);
                ImGui::SliderFloat("Delay Mix", &g_synth.delayMix, 0.0f, 1.0f);
            }

            if (ImGui::Checkbox("Reverb", &g_synth.reverbEnabled)) {}
            if (g_synth.reverbEnabled) {
                ImGui::SliderFloat("Size", &g_synth.reverbSize, 0.0f, 1.0f);
                ImGui::SliderFloat("Damp", &g_synth.reverbDamp, 0.0f, 1.0f);
                ImGui::SliderFloat("Pre-Delay", &g_synth.reverbDelay, 0.0f, 0.2f, "%.3f");
                ImGui::SliderFloat("Diffuse", &g_synth.reverbDiffuse, 0.0f, 1.0f);
                ImGui::SliderFloat("Stereo", &g_synth.reverbStereo, 0.0f, 1.0f);
                ImGui::SliderFloat("Dry Mix", &g_synth.reverbDryMix, 0.0f, 1.0f);
                ImGui::SliderFloat("Wet Mix", &g_synth.reverbWetMix, 0.0f, 1.0f);
            }

             // Analog Filter
             ImGui::Separator();
             ImGui::Text("Analog Filter");
             if (ImGui::Checkbox("Filter Enabled", &g_synth.filterEnabled)) {}
             static float cutoff = g_synth.filter.getCutoff();
             if (ImGui::SliderFloat("Cutoff (Hz)", &cutoff, 20.0f, 20000.0f)) {
                 g_synth.filter.setCutoff(cutoff);
             }
             ImGui::Text("Cutoff: %.1f Hz", g_synth.filter.getCutoff());

             static float resonance = g_synth.filter.getResonance();
             if (ImGui::SliderFloat("Resonance (Q)", &resonance, 0.1f, 10.0f)) {
                 g_synth.filter.setResonance(resonance);
             }
             ImGui::Text("Resonance: %.2f", g_synth.filter.getResonance());

             static float drive = g_synth.filter.getDrive();
             if (ImGui::SliderFloat("Drive", &drive, 0.1f, 10.0f)) {
                 g_synth.filter.setDrive(drive);
             }
             ImGui::Text("Drive: %.2f", g_synth.filter.getDrive());

             static float inertial = g_synth.filter.getInertial();
             if (ImGui::SliderFloat("Inertial", &inertial, 0.0f, 0.99f)) {
                 g_synth.filter.setInertial(inertial);
			 }

             static int oversampling = g_synth.filter.getOversampling();
             if (ImGui::Combo("Oversampling", &oversampling, "0\0x2\0x4\0x8\0\0")) {
                 g_synth.filter.setOversampling(oversampling == 0 ? 0 : (1 << (oversampling))); // 0,2,4,8
             }
             ImGui::Text("Oversampling: x%d", g_synth.filter.getOversampling());
					 
              // Mixer / Bus compression
 			 ImGui::Separator();
 			 ImGui::Text("Mixer / Bus Compression");
 			 ImGui::Checkbox("Compressor", &g_synth.compressorEnabled);
 			 if (g_synth.compressorEnabled) {
 				 ImGui::SliderFloat("Threshold (dB)", &g_synth.compressorThresholdDb, -60.0f, 0.0f);
 				 ImGui::SliderFloat("Ratio", &g_synth.compressorRatio, 1.0f, 20.0f);
 				 ImGui::SliderFloat("Attack (ms)", &g_synth.compressorAttackMs, 0.1f, 200.0f);
 				 ImGui::SliderFloat("Release (ms)", &g_synth.compressorReleaseMs, 5.0f, 2000.0f);
 				 ImGui::SliderFloat("Makeup (dB)", &g_synth.compressorMakeupDb, -12.0f, 12.0f);
              }

              // DC Filter
              ImGui::Separator();
              ImGui::Text("DC Filter");
              ImGui::Checkbox("DC Filter", &g_synth.dcFilterEnabled);
              if (g_synth.dcFilterEnabled) {
                  ImGui::SliderFloat("DC Filter Alpha", &g_synth.dcFilterAlpha, 0.9f, 0.999f);
              }

              // Soft Clipping
              ImGui::Separator();
              ImGui::Text("Soft Clipping");
              ImGui::Checkbox("Soft Clipping", &g_synth.softClipEnabled);
              if (g_synth.softClipEnabled) {
                  ImGui::SliderFloat("Soft Clip Drive", &g_synth.softClipDrive, 1.0f, 10.0f);
              }

              // Auto Gain
              ImGui::Separator();
              ImGui::Text("Auto Gain");
              ImGui::Checkbox("Auto Gain", &g_synth.autoGainEnabled);
              if (g_synth.autoGainEnabled) {
                  ImGui::SliderFloat("Target RMS", &g_synth.autoGainTargetRMS, 0.1f, 0.8f);
                  ImGui::SliderFloat("Auto Gain Alpha", &g_synth.autoGainAlpha, 0.9f, 0.999f);
              }

  			 ImGui::End();
		}



         // Visualization update: compute FFT waterfall and upload texture (do this before ImGui draw calls)
        if (g_waterfallTex == 0) {
            glGenTextures(1, &g_waterfallTex);
            glBindTexture(GL_TEXTURE_2D, g_waterfallTex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, WATERFALL_WIDTH, WATERFALL_HEIGHT, 0, GL_RGB, GL_UNSIGNED_BYTE, g_waterfallPixels.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }

        int writeIdx = g_scopeWriteIndex.load();
        std::vector<std::complex<float>> fftIn(FFT_SIZE);
        for (int i = 0; i < FFT_SIZE; ++i) {
            int read = (writeIdx - FFT_SIZE + i + SCOPE_BUFFER) % SCOPE_BUFFER;
            float s = g_leftScopeBuffer[read]; // Use left channel for FFT
            float w = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (FFT_SIZE - 1)));
            fftIn[i] = std::complex<float>(s * w, 0.0f);
        }
        fft(fftIn);
        std::vector<float> mags(WATERFALL_WIDTH);
        for (int b = 0; b < WATERFALL_WIDTH; ++b) {
            int idxBin = 1 + (b * (FFT_SIZE/2 - 1)) / WATERFALL_WIDTH;
            float mag = std::abs(fftIn[idxBin]);
            mags[b] = 20.0f * std::log10(mag + 1e-6f);
        }
        memmove(g_waterfallPixels.data() + 3*WATERFALL_WIDTH, g_waterfallPixels.data(), (WATERFALL_HEIGHT-1)*3*WATERFALL_WIDTH);
        for (int x = 0; x < WATERFALL_WIDTH; ++x) {
            unsigned char r,g,b;
            magToColor(mags[x], r,g,b);
            g_waterfallPixels[x*3+0] = r;
            g_waterfallPixels[x*3+1] = g;
            g_waterfallPixels[x*3+2] = b;
        }
        glBindTexture(GL_TEXTURE_2D, g_waterfallTex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, WATERFALL_WIDTH, WATERFALL_HEIGHT, GL_RGB, GL_UNSIGNED_BYTE, g_waterfallPixels.data());

        // Draw oscilloscope, goniometer and waterfall in ImGui window
        ImGui::Begin("Scope & Spectrum");
        
        static std::vector<float> scopeSamplesL(1024);
        static std::vector<float> scopeSamplesR(1024);
        int len = (int)scopeSamplesL.size();
        
        int triggerRead = -1;
        int start = (writeIdx - len + SCOPE_BUFFER) % SCOPE_BUFFER;
        for (int i = 0; i < len - 1; ++i) {
            int a = (start + i) % SCOPE_BUFFER;
            int b = (start + i + 1) % SCOPE_BUFFER;
            float sa = g_leftScopeBuffer[a]; // Trigger on left channel
            float sb = g_leftScopeBuffer[b];
            if (g_triggerMode == 0) { if (sa < -0.001f && sb >= -0.001f) { triggerRead = b; break; } }
            else if (g_triggerMode == 1 || g_triggerMode == 2) {
                if (g_triggerEdge == 0) { if (sa < g_triggerLevel && sb >= g_triggerLevel) { triggerRead = b; break; } }
                    else { if (sa > g_triggerLevel && sb <= g_triggerLevel) { triggerRead = b; break; } }
            } else if (g_triggerMode == 3) {
                float low = g_triggerLevel - g_triggerHysteresis, high = g_triggerLevel + g_triggerHysteresis;
                if (g_triggerEdge == 0) { if (sa <= low && sb >= high) { triggerRead = b; break; } }
                else { if (sa >= high && sb <= low) { triggerRead = b; break; } }
            }
        }
        if (triggerRead == -1) triggerRead = start;

        for (int i = 0; i < len; ++i) {
            int read = (triggerRead + i) % SCOPE_BUFFER;
            scopeSamplesL[i] = g_leftScopeBuffer[read];
            scopeSamplesR[i] = g_rightScopeBuffer[read];
        }

        ImVec2 avail = ImGui::GetContentRegionAvail();
        float halfWidth = avail.x * 0.5f; // Half width for each channel
        ImVec2 plotSize(halfWidth - 4.0f, 120.0f); // Account for spacing

        // Use two columns for side-by-side layout
        ImGui::Columns(2, "ScopeColumns", false);

        // Left Channel
        ImGui::Text("Left Channel");
        ImVec2 leftScopePos = ImGui::GetCursorScreenPos();
        ImDrawList* leftDraw = ImGui::GetWindowDrawList();
        drawGrid(leftDraw, leftScopePos, plotSize, 10, 4, IM_COL32(80,80,80,80));

        ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.6f, 1.0f, 0.6f, 1.0f)); // Green for Left
        ImGui::PlotLines("##OscilloscopeL", scopeSamplesL.data(), len, 0, nullptr, -1.0f, 1.0f, plotSize);
        ImGui::PopStyleColor();

        ImGui::NextColumn();

        // Right Channel
        ImGui::Text("Right Channel");
        ImVec2 rightScopePos = ImGui::GetCursorScreenPos();
        ImDrawList* rightDraw = ImGui::GetWindowDrawList();
        drawGrid(rightDraw, rightScopePos, plotSize, 10, 4, IM_COL32(80,80,80,80));

        ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(1.0f, 0.6f, 0.6f, 1.0f)); // Red for Right
        ImGui::PlotLines("##OscilloscopeR", scopeSamplesR.data(), len, 0, nullptr, -1.0f, 1.0f, plotSize);
        ImGui::PopStyleColor();

        ImGui::Columns(1); // Reset to single column


        // Trigger Controls for scopes
        ImGui::Separator();
        ImGui::Text("Scope Trigger");
        const char* tModes[] = {"Zero","Level","Edge","Hysteresis"};
        ImGui::Combo("Trigger Mode", &g_triggerMode, tModes, IM_ARRAYSIZE(tModes));
        ImGui::SliderFloat("Trigger Level", &g_triggerLevel, -1.0f, 1.0f);
        ImGui::RadioButton("Rising", &g_triggerEdge, 0); ImGui::SameLine(); ImGui::RadioButton("Falling", &g_triggerEdge, 1);
        ImGui::SliderFloat("Hysteresis", &g_triggerHysteresis, 0.0f, 0.2f);

        // Per-voice oscilloscopes
        ImGui::Separator();
        ImGui::Text("Voice Oscilloscopes");
        int numVoices = (int)g_synth.voices.size();
        int showVoices = std::min(numVoices, MAX_VOICES);

        // Calculate dynamic grid layout
        float availWidth = ImGui::GetContentRegionAvail().x;
        int minCols = 1;
        int maxCols = 4; // Maximum 4 columns
        int optimalCols = std::min(maxCols, std::max(minCols, showVoices));
        float itemSpacing = ImGui::GetStyle().ItemSpacing.x;
        float plotWidth = (availWidth - (optimalCols - 1) * itemSpacing) / optimalCols;

        // Create grid columns
        ImGui::Columns(optimalCols, "VoiceScopeGrid", false);

        for (int vi = 0; vi < showVoices; ++vi) {
            ImGui::BeginGroup();

            std::vector<float> vbuf(SCOPE_VOICE_BUFFER);
            int w = g_voiceScopeWriteIdx[vi].load();
            int trigger = -1;
            int vstart = (w - SCOPE_VOICE_BUFFER + SCOPE_VOICE_BUFFER) % SCOPE_VOICE_BUFFER;
            for (int j = 0; j < SCOPE_VOICE_BUFFER - 1; ++j) {
                int a = (vstart + j) % SCOPE_VOICE_BUFFER;
                int b = (vstart + j + 1) % SCOPE_VOICE_BUFFER;
                float sa = g_voiceScopeBuffers[vi][a];
                float sb = g_voiceScopeBuffers[vi][b];
                if (g_triggerMode == 0) { if (sa < -0.001f && sb >= -0.001f) { trigger = b; break; } }
                else if (g_triggerMode == 1 || g_triggerMode == 2) {
                    if (g_triggerEdge == 0) { if (sa < g_triggerLevel && sb >= g_triggerLevel) { trigger = b; break; } }
                    else { if (sa > g_triggerLevel && sb <= g_triggerLevel) { trigger = b; break; } }
                } else if (g_triggerMode == 3) {
                    float low = g_triggerLevel - g_triggerHysteresis, high = g_triggerLevel + g_triggerHysteresis;
                    if (g_triggerEdge == 0) { if (sa <= low && sb >= high) { trigger = b; break; } }
                    else { if (sa >= high && sb <= low) { trigger = b; break; } }
                }
            }
            if (trigger == -1) trigger = vstart;
            for (int i = 0; i < SCOPE_VOICE_BUFFER; ++i) {
                int read = (trigger + i) % SCOPE_VOICE_BUFFER;
                vbuf[i] = g_voiceScopeBuffers[vi][read];
            }
            char oscId[32];
            snprintf(oscId, sizeof(oscId), "##VoiceOscilloscope%d", vi);
            ImGui::PlotLines(oscId, vbuf.data(), SCOPE_VOICE_BUFFER, 0, nullptr, -1.0f, 1.0f, ImVec2(plotWidth, 60));

            ImGui::EndGroup();

            // Move to next column
            ImGui::NextColumn();
        }

        // Reset to single column
        ImGui::Columns(1);

        // Goniometer (Lissajous)
        ImGui::BeginGroup();
        ImGui::Text("Goniometer");
        ImVec2 gonSz(240,240);
        ImGui::InvisibleButton("goniometer", gonSz);
        ImVec2 gonMin = ImGui::GetItemRectMin();
        ImVec2 gonMax = ImGui::GetItemRectMax();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRect(gonMin, gonMax, IM_COL32(200,200,200,100));
        const int gSamples = 512;
        ImVec2 center = ImVec2((gonMin.x + gonMax.x)*0.5f, (gonMin.y + gonMax.y)*0.5f);
        float radius = 0.45f * std::min(gonSz.x, gonSz.y);
        ImVec2 prevPt(0,0);
        bool havePrev = false;
        int goniometerWidx = g_scopeWriteIndex.load();
        for (int i = 0; i < gSamples; ++i) {
            int read = (goniometerWidx - gSamples + i + SCOPE_BUFFER) % SCOPE_BUFFER;
            float L = g_leftScopeBuffer[read];
            float R = g_rightScopeBuffer[read];
            float px = center.x + L * radius;
            float py = center.y - R * radius;
            ImVec2 pt(px, py);
            if (havePrev) draw->AddLine(prevPt, pt, IM_COL32(100,255,100,200), 1.0f);
            prevPt = pt; havePrev = true;
        }
        ImGui::EndGroup();

        ImGui::SameLine();

        // Spectrum (waterfall)
        ImGui::BeginGroup();
        ImGui::Text("Spectrum (waterfall)");
        ImGui::Image((void*)(intptr_t)g_waterfallTex, ImVec2((float)WATERFALL_WIDTH, (float)WATERFALL_HEIGHT));
        ImGui::EndGroup();

        ImGui::Separator();
        ImGui::Text("%s", statusMessage.c_str());

        ImGui::End();

        // Render
        uint64_t renderStart = SDL_GetPerformanceCounter();
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
        uint64_t renderEnd = SDL_GetPerformanceCounter();
        float renderTime = (renderEnd - renderStart) / (float)SDL_GetPerformanceFrequency() * 1000.0f;

        // Limit GUI to 60 FPS
        const float targetFrameTime = 1000.0f / 60.0f; // ~16.67ms
        float sleepTime = targetFrameTime - renderTime;
        if (sleepTime > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds((int)(sleepTime * 1000)));
        }
    }

    // Save application state on exit
    Preset::save("default_preset.json");

    // Cleanup - close all MIDI inputs
    for (auto& midi_input : g_midi_inputs) {
        if (midi_input) {
            midi_input->close_port();
        }
    }
    g_midi_inputs.clear();
    g_midi_port_data.clear();
    SDL_DestroyAudioStream(audioStream);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DestroyContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

#ifndef __EMSCRIPTEN__
    g_arpThreadShouldExit = true; // Signal arpeggiator thread to exit
    arpThread.join(); // Wait for arpeggiator thread to finish
#endif

    return 0;
}
