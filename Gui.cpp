#include "Gui.h"
#include "Preset.h"
#include "SineTable.h"
#include "Utils.h"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"

#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <numeric>
#include <vector>

namespace {

constexpr int kFftSize = 1024;
constexpr int kWaterfallWidth = 512;
constexpr int kWaterfallHeight = 256;

void drawGrid(ImDrawList* draw, const ImVec2& pos, const ImVec2& size,
              int cols, int rows, ImU32 color) {
    if (!draw) return;
    for (int c = 0; c <= cols; ++c) {
        float x = pos.x + (size.x * ((float)c / (float)cols));
        draw->AddLine(ImVec2(x, pos.y), ImVec2(x, pos.y + size.y), color, 1.0f);
    }
    for (int r = 0; r <= rows; ++r) {
        float y = pos.y + (size.y * ((float)r / (float)rows));
        draw->AddLine(ImVec2(pos.x, y), ImVec2(pos.x + size.x, y), color, 1.0f);
    }
}

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
        float ang = 2.0f * (float)M_PI / len * -1.0f;
        std::complex<float> wlen(std::cos(ang), fastSin(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (int k = 0; k < len / 2; ++k) {
                std::complex<float> u = a[i + k];
                std::complex<float> v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

void magToColor(float db, unsigned char& r, unsigned char& g, unsigned char& b) {
    float t = std::clamp((db + 100.0f) / 100.0f, 0.0f, 1.0f);
    if (t < 0.33f) {
        float u = t / 0.33f;
        r = 0;
        g = static_cast<unsigned char>(u * 255.0f);
        b = static_cast<unsigned char>(128.0f + u * 127.0f);
    } else if (t < 0.66f) {
        float u = (t - 0.33f) / 0.33f;
        r = static_cast<unsigned char>(u * 255.0f);
        g = 255;
        b = static_cast<unsigned char>(255.0f - u * 255.0f);
    } else {
        float u = (t - 0.66f) / 0.34f;
        r = 255;
        g = static_cast<unsigned char>(255.0f - u * 255.0f);
        b = 0;
    }
}

void findTriggerIndex(const float* buffer, int bufferSize, int writeIndex,
                      int triggerMode, float triggerLevel, int triggerEdge,
                      float triggerHysteresis, int readLength, int& outStart) {
    int start = (writeIndex - readLength + bufferSize * 2) % bufferSize;
    outStart = start;
    for (int i = 0; i < readLength - 1; ++i) {
        float sa = buffer[(start + i) % bufferSize];
        float sb = buffer[(start + i + 1) % bufferSize];
        if (triggerMode == 0) {
            if (sa < -0.001f && sb >= -0.001f) { outStart = (start + i + 1) % bufferSize; return; }
        } else if (triggerMode == 1 || triggerMode == 2) {
            if (triggerEdge == 0) {
                if (sa < triggerLevel && sb >= triggerLevel) { outStart = (start + i + 1) % bufferSize; return; }
            } else {
                if (sa > triggerLevel && sb <= triggerLevel) { outStart = (start + i + 1) % bufferSize; return; }
            }
        } else {
            float low = triggerLevel - triggerHysteresis;
            float high = triggerLevel + triggerHysteresis;
            if (triggerEdge == 0) {
                if (sa <= low && sb >= high) { outStart = (start + i + 1) % bufferSize; return; }
            } else {
                if (sa >= high && sb <= low) { outStart = (start + i + 1) % bufferSize; return; }
            }
        }
    }
}

} // namespace

SynthGui::SynthGui(SynthEngine& eng, SDL_Window* win, bool isStandalone)
    : engine(eng), window(win), standalone(isStandalone) {
    waterfallPixels.assign(kWaterfallWidth * kWaterfallHeight * 3, 0);
}

SynthGui::~SynthGui() {
    shutdown();
}

bool SynthGui::init(const char* glslVersion) {
    if (initialized) return true;

    IMGUI_CHECKVERSION();
    imGuiContext = ImGui::CreateContext();
    if (!imGuiContext) return false;
    ImGui::SetCurrentContext(imGuiContext);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    if (!standalone) {
        io.IniFilename = nullptr;
    }

    const std::string fontPath = assetDirectory + "/xkcd-script.ttf";
    ImFont* font = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 23.0f);
    if (!font) io.Fonts->AddFontDefault();

    if (!ImGui_ImplSDL3_InitForOpenGL(window, SDL_GL_GetCurrentContext())) {
        ImGui::DestroyContext(imGuiContext);
        imGuiContext = nullptr;
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init(glslVersion)) {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext(imGuiContext);
        imGuiContext = nullptr;
        return false;
    }

    glGenTextures(1, &waterfallTex);
    glBindTexture(GL_TEXTURE_2D, waterfallTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, kWaterfallWidth, kWaterfallHeight, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, waterfallPixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    initialized = true;
    return true;
}

void SynthGui::shutdown() {
    if (!initialized) return;

    ImGui::SetCurrentContext(imGuiContext);

    // OpenGL objects can only be deleted while a context is current on this
    // thread. When a plug-in is torn down by its host this is not always the
    // case, in which case the driver frees the objects together with the
    // context.
    const bool glCurrent = SDL_GL_GetCurrentContext() != nullptr;
    if (glCurrent && waterfallTex != 0) {
        glDeleteTextures(1, &waterfallTex);
    }
    waterfallTex = 0;

    if (glCurrent) {
        ImGui_ImplOpenGL3_Shutdown();
    }
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(imGuiContext);
    imGuiContext = nullptr;
    initialized = false;
}

void SynthGui::processEvent(const SDL_Event& event) {
    if (!initialized) return;
    ImGui::SetCurrentContext(imGuiContext);
    ImGui_ImplSDL3_ProcessEvent(&event);
}

void SynthGui::newFrame() {
    if (!initialized) return;
    ImGui::SetCurrentContext(imGuiContext);
    ImGui_ImplSDL3_NewFrame();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
}

void SynthGui::draw() {
    if (!initialized) return;
    ImGui::SetCurrentContext(imGuiContext);

    if (!layoutInitialized) {
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(560, 620), ImGuiCond_FirstUseEver);
        layoutInitialized = true;
    }

    {
        std::lock_guard<std::recursive_mutex> lock(engine.mutex);
        drawControlsWindow();
    }

    updateWaterfall();

    {
        std::lock_guard<std::recursive_mutex> lock(engine.mutex);
        drawScopeWindow();
    }
}

void SynthGui::drawControlsWindow() {
    Synthesizer& synth = engine.synth;
    auto& knobs = engine.knobs.params;

    ImGui::Begin("Synthesizer Controls");

    auto knobAt = [&](int index) -> KnobParam& {
        static float dummyValue = 0.0f;
        static KnobParam dummy(&dummyValue, 0.0f, 1.0f, -1, -1, KnobParam::DIRECT, -1);
        if (index < 0 || index >= (int)knobs.size()) return dummy;
        return knobs[index];
    };

    ImGui::PushID("master");
    ImGui::Text("Master");
    ImGui::SameLine();
    DrawKnob(engine.knobs, "Vol", knobAt(0));
    ImGui::SameLine();
    DrawKnob(engine.knobs, "Pan", knobAt(1));
    ImGui::SameLine();
    {
        const float presetValues[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
        const char* presetLabels[] = {"0%", "25%", "50%", "75%", "100%"};
        for (int pi = 0; pi < 5; ++pi) {
            if (ImGui::SmallButton(presetLabels[pi])) {
                engine.setParameterNormalized(0, presetValues[pi]);
            }
            if (pi < 4) ImGui::SameLine(0, 2);
        }
    }
    ImGui::PopID();

    ImGui::Separator();
    ImGui::PushID("voice1");
    ImGui::Text("Voice 1");
    ImGui::SameLine();
    DrawKnob(engine.knobs, "Freq", knobAt(2));
    ImGui::SameLine();
    DrawKnob(engine.knobs, "Gain", knobAt(3));
    ImGui::SameLine();
    DrawKnob(engine.knobs, "Mix", knobAt(4));
    ImGui::PopID();

    const char* vcoWaveNames[] = {"Sine", "Square", "Saw", "Triangle",
                                  "Saw Up", "Saw Down", "Pulse", "Random"};
    const char* spreadNames[] = {"Off", "Tight", "Medium", "Wide", "Extra Wide"};

    for (int vcoIndex = 0; vcoIndex < 3; ++vcoIndex) {
        ImGui::PushID(vcoIndex);
        ImGui::Separator();
        char title[32];
        snprintf(title, sizeof(title), "VCO %d", vcoIndex + 1);
        ImGui::Text("%s", title);
        int widx = synth.voices.empty() ? 0 : synth.voices[0].getVcoWaveform(vcoIndex);
        if (ImGui::Combo("W", &widx, vcoWaveNames, IM_ARRAYSIZE(vcoWaveNames))) {
            for (auto& voice : synth.voices)
                voice.setVcoWaveform(vcoIndex, static_cast<Oscillator::WaveformType>(widx));
        }
        ImGui::SameLine();
        int knobBase = 5 + vcoIndex * 6;
        DrawKnob(engine.knobs, "Gain", knobAt(knobBase + 0));
        ImGui::SameLine();
        DrawKnob(engine.knobs, "Pitch", knobAt(knobBase + 1));
        ImGui::SameLine();
        DrawKnob(engine.knobs, "Detune", knobAt(knobBase + 2));
        DrawKnob(engine.knobs, "Phase", knobAt(knobBase + 3));
        ImGui::SameLine();
        DrawKnob(engine.knobs, "PW", knobAt(knobBase + 4));
        ImGui::SameLine();
        DrawKnob(engine.knobs, "Pan", knobAt(knobBase + 5));

        if (!synth.voices.empty()) {
            int vUnison = synth.voices[0].getUnisonCount();
            if (ImGui::SliderInt("Unison (per voice, 0=global)", &vUnison, 0, 8)) {
                for (auto& voice : synth.voices) voice.setUnisonCount(vUnison);
            }
            int vSpreadUi = synth.voices[0].getUnisonSpreadIndex() + 1;
            const char* perVoiceSpread[] = {"Global", "Off", "Tight", "Medium", "Wide", "Extra Wide"};
            if (ImGui::Combo("Spread (per voice)", &vSpreadUi, perVoiceSpread,
                             IM_ARRAYSIZE(perVoiceSpread))) {
                for (auto& voice : synth.voices) voice.setUnisonSpreadIndex(vSpreadUi - 1);
            }
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    ImGui::PushID("adsr");
    ImGui::Text("ADSR");
    ImGui::SameLine();
    DrawKnob(engine.knobs, "Atk", knobAt(23));
    ImGui::SameLine();
    DrawKnob(engine.knobs, "Dec", knobAt(24));
    ImGui::SameLine();
    DrawKnob(engine.knobs, "Sus", knobAt(25));
    ImGui::SameLine();
    DrawKnob(engine.knobs, "Rel", knobAt(26));
    ImGui::PopID();

    ImGui::Separator();
    ImGui::Text("Unison");
    ImGui::SliderInt("Voices", &synth.unisonCount, 1, 8);
    ImGui::Combo("Spread", &synth.unisonSpreadIndex, spreadNames, IM_ARRAYSIZE(spreadNames));

    ImGui::Separator();
    if (ImGui::Button("Copy Voice 1 Params to All")) {
        int nVoices = (int)synth.voices.size();
        if (nVoices > 1) {
            Voice& sourceVoice = synth.voices[0];
            for (int i = 1; i < nVoices; ++i) {
                Voice& destVoice = synth.voices[i];
                destVoice.setAttackTime(sourceVoice.getAttackTime());
                destVoice.setDecayTime(sourceVoice.getDecayTime());
                destVoice.setSustainLevel(sourceVoice.getSustainLevel());
                destVoice.setReleaseTime(sourceVoice.getReleaseTime());
                destVoice.setMixLevel(sourceVoice.getMixLevel());
                destVoice.setUnisonCount(sourceVoice.getUnisonCount());
                destVoice.setUnisonSpreadIndex(sourceVoice.getUnisonSpreadIndex());
                for (int vcoIdx = 0; vcoIdx < 3; ++vcoIdx) {
                    destVoice.setVcoWaveform(vcoIdx, static_cast<Oscillator::WaveformType>(
                                                         sourceVoice.getVcoWaveform(vcoIdx)));
                    destVoice.setVcoMix(vcoIdx, sourceVoice.getVcoMix(vcoIdx));
                    destVoice.setVcoDetune(vcoIdx, sourceVoice.getVcoDetune(vcoIdx));
                    destVoice.setVcoPhaseMs(vcoIdx, sourceVoice.getVcoPhaseMs(vcoIdx));
                    destVoice.setVcoPulseWidth(vcoIdx, sourceVoice.getVcoPulseWidth(vcoIdx));
                    destVoice.setVcoPitchShift(vcoIdx, sourceVoice.getVcoPitchShift(vcoIdx));
                    destVoice.setVcoPan(vcoIdx, sourceVoice.getVcoPan(vcoIdx));
                }
            }
        }
    }

    ImGui::Separator();
    ImGui::Text("Presets");
    if (standalone) {
        if (presetFiles.empty()) refreshPresetList();
        {
            static int currentPreset = 0;
            if (currentPreset >= (int)presetFiles.size()) currentPreset = 0;
            if (!presetFiles.empty()) {
                std::vector<const char*> names;
                names.reserve(presetFiles.size());
                for (const auto& file : presetFiles) names.push_back(file.c_str());
                if (ImGui::Combo("Preset", &currentPreset,
                                 [](void* data, int idx) -> const char* {
                                     auto* files = static_cast<std::vector<const char*>*>(data);
                                     if (idx < 0 || idx >= (int)files->size()) return nullptr;
                                     return (*files)[idx];
                                 },
                                 &names, (int)names.size())) {
                    presetFilename = presetFiles[currentPreset];
                    snprintf(presetFilenameBuffer, sizeof(presetFilenameBuffer), "%s",
                             presetFilename.c_str());
                }
            }
        }
        if (ImGui::Button("Save")) {
            savePreset();
        }
        ImGui::SameLine();
        if (ImGui::Button("Load")) {
            loadPreset();
        }
        ImGui::SameLine();
        if (ImGui::Button("Save...")) savePresetDialog();
        ImGui::SameLine();
        if (ImGui::Button("Load...")) loadPresetDialog();
    } else {
        ImGui::TextDisabled("Preset stored with the plug-in state.");
    }

    ImGui::Separator();
    ImGui::PushID("mod");
    ImGui::Text("Modulation");
    ImGui::SameLine();
    DrawKnob(engine.knobs, "Bend", knobAt(27));
    ImGui::SameLine();
    DrawKnob(engine.knobs, "LFO", knobAt(28));
    ImGui::PopID();

    ImGui::Separator();
    ImGui::PushID("arp");
    ImGui::Text("Arpeggiator");
    bool wasArpEnabled = synth.arpEnabled;
    if (ImGui::Checkbox("Enabled", &synth.arpEnabled) && wasArpEnabled != synth.arpEnabled) {
        engine.allNotesOff();
    }
    if (synth.arpEnabled) {
        ImGui::SameLine();
        DrawKnob(engine.knobs, "BPM", knobAt(29));
        ImGui::SameLine();
        DrawKnob(engine.knobs, "Gate", knobAt(30));
        const char* arpDirections[] = {"Up", "Down", "Up-Down", "Random"};
        ImGui::Combo("Dir", &synth.arpDirection, arpDirections, IM_ARRAYSIZE(arpDirections));
        ImGui::SliderInt("Range", &synth.arpRange, 1, 4);
        ImGui::SameLine();
        ImGui::Checkbox("Hold", &synth.arpHold);
    }
    ImGui::PopID();

    ImGui::Separator();
    ImGui::Text("Effects");

    ImGui::PushID("flanger");
    ImGui::Checkbox("Flanger", &synth.flangerEnabled);
    if (synth.flangerEnabled) {
        ImGui::SameLine();
        DrawKnob(engine.knobs, "Rate", knobAt(31));
        ImGui::SameLine();
        DrawKnob(engine.knobs, "Depth", knobAt(32));
        ImGui::SameLine();
        DrawKnob(engine.knobs, "Mix", knobAt(33));
    }
    ImGui::PopID();

    ImGui::PushID("delay");
    ImGui::Checkbox("Delay", &synth.delayEnabled);
    if (synth.delayEnabled) {
        ImGui::SameLine();
        DrawKnob(engine.knobs, "Time", knobAt(34));
        ImGui::SameLine();
        DrawKnob(engine.knobs, "Fdbk", knobAt(35));
        ImGui::SameLine();
        DrawKnob(engine.knobs, "Mix", knobAt(36));
    }
    ImGui::PopID();

    ImGui::PushID("reverb");
    ImGui::Checkbox("Reverb", &synth.reverbEnabled);
    if (synth.reverbEnabled) {
        DrawKnob(engine.knobs, "Size", knobAt(37));
        ImGui::SameLine();
        DrawKnob(engine.knobs, "Damp", knobAt(38));
        ImGui::SameLine();
        DrawKnob(engine.knobs, "Delay", knobAt(39));
        DrawKnob(engine.knobs, "Diffuse", knobAt(40));
        ImGui::SameLine();
        DrawKnob(engine.knobs, "Stereo", knobAt(41));
        ImGui::SameLine();
        DrawKnob(engine.knobs, "Dry", knobAt(42));
        ImGui::SameLine();
        DrawKnob(engine.knobs, "Wet", knobAt(43));
    }
    ImGui::PopID();

    ImGui::Separator();
    ImGui::PushID("filter");
    ImGui::Text("Analog Filter");
    ImGui::Checkbox("Enabled", &synth.filterEnabled);
    ImGui::SameLine();
    DrawKnob(engine.knobs, "Cutoff", knobAt(44));
    ImGui::SameLine();
    DrawKnob(engine.knobs, "Res", knobAt(45));
    ImGui::SameLine();
    DrawKnob(engine.knobs, "Drive", knobAt(46));
    DrawKnob(engine.knobs, "Inertial", knobAt(47));
    ImGui::SameLine();
    {
        static int oversamplingIndex = 0;
        if (ImGui::Combo("OS", &oversamplingIndex, "0\0x2\0x4\0x8\0\0")) {
            synth.filter.setOversampling(oversamplingIndex == 0 ? 0 : (1 << oversamplingIndex));
        }
    }
    ImGui::PopID();

    ImGui::Separator();
    ImGui::PushID("comp");
    ImGui::Text("Compressor");
    ImGui::Checkbox("On", &synth.compressorEnabled);
    if (synth.compressorEnabled) {
        ImGui::SameLine();
        DrawKnob(engine.knobs, "Thresh", knobAt(48));
        ImGui::SameLine();
        DrawKnob(engine.knobs, "Ratio", knobAt(49));
        DrawKnob(engine.knobs, "Atk", knobAt(50));
        ImGui::SameLine();
        DrawKnob(engine.knobs, "Rel", knobAt(51));
        ImGui::SameLine();
        DrawKnob(engine.knobs, "Makeup", knobAt(52));
    }
    ImGui::PopID();

    ImGui::Separator();
    ImGui::PushID("dc");
    ImGui::Text("DC Filter");
    ImGui::Checkbox("On", &synth.dcFilterEnabled);
    if (synth.dcFilterEnabled) {
        ImGui::SameLine();
        DrawKnob(engine.knobs, "Alpha", knobAt(53));
    }
    ImGui::PopID();

    ImGui::Separator();
    ImGui::PushID("softclip");
    ImGui::Text("Soft Clip");
    ImGui::Checkbox("On", &synth.softClipEnabled);
    if (synth.softClipEnabled) {
        ImGui::SameLine();
        DrawKnob(engine.knobs, "Drive", knobAt(54));
    }
    ImGui::PopID();

    ImGui::Separator();
    ImGui::PushID("autogain");
    ImGui::Text("Auto Gain");
    ImGui::Checkbox("On", &synth.autoGainEnabled);
    if (synth.autoGainEnabled) {
        ImGui::SameLine();
        DrawKnob(engine.knobs, "RMS", knobAt(55));
        ImGui::SameLine();
        DrawKnob(engine.knobs, "Alpha", knobAt(56));
    }
    ImGui::PopID();

    ImGui::Separator();
    ImGui::Text("MIDI Learn");
    if (engine.knobs.midiLearnMode) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.6f, 0.0f, 1.0f));
        if (ImGui::Button("Listening... (Esc to cancel)")) {
            engine.knobs.midiLearnMode = false;
            engine.knobs.midiLearnKnobIdx = -1;
        }
        ImGui::PopStyleColor();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            engine.knobs.midiLearnMode = false;
            engine.knobs.midiLearnKnobIdx = -1;
        }
    } else {
        if (ImGui::Button("Learn")) {
            engine.knobs.midiLearnMode = true;
            statusMessage = "MIDI Learn: click a knob, then move a MIDI CC";
        }
    }

    ImGui::Separator();
    ImGui::Text("%s", statusMessage.c_str());

    ImGui::End();
}

void SynthGui::updateWaterfall() {
    if (!initialized || waterfallTex == 0) return;

    float left[SYNTH_SCOPE_BUFFER];
    float right[SYNTH_SCOPE_BUFFER];
    engine.copyScope(left, right, SYNTH_SCOPE_BUFFER);

    std::vector<std::complex<float>> fftIn(kFftSize);
    for (int i = 0; i < kFftSize; ++i) {
        float s = left[i];
        float w = 0.5f * (1.0f - std::cos(2.0f * (float)M_PI * i / (kFftSize - 1)));
        fftIn[i] = std::complex<float>(s * w, 0.0f);
    }
    fft(fftIn);

    std::vector<float> mags(kWaterfallWidth);
    for (int b = 0; b < kWaterfallWidth; ++b) {
        int idxBin = 1 + (b * (kFftSize / 2 - 1)) / kWaterfallWidth;
        float mag = std::abs(fftIn[idxBin]);
        mags[b] = 20.0f * std::log10(mag + 1e-6f);
    }

    memmove(waterfallPixels.data() + 3 * kWaterfallWidth, waterfallPixels.data(),
            (kWaterfallHeight - 1) * 3 * kWaterfallWidth);
    for (int x = 0; x < kWaterfallWidth; ++x) {
        unsigned char r, g, b;
        magToColor(mags[x], r, g, b);
        waterfallPixels[x * 3 + 0] = r;
        waterfallPixels[x * 3 + 1] = g;
        waterfallPixels[x * 3 + 2] = b;
    }

    glBindTexture(GL_TEXTURE_2D, waterfallTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kWaterfallWidth, kWaterfallHeight,
                    GL_RGB, GL_UNSIGNED_BYTE, waterfallPixels.data());
}

void SynthGui::drawScopeWindow() {
    Synthesizer& synth = engine.synth;

    if (!layoutInitializedScope) {
        ImGui::SetNextWindowPos(ImVec2(580, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(500, 420), ImGuiCond_FirstUseEver);
        layoutInitializedScope = true;
    }

    ImGui::Begin("Scope & Spectrum");

    constexpr int len = 1024;
    float scopeSamplesL[len];
    float scopeSamplesR[len];

    int writeIdx = engine.scopeWriteIndex.load(std::memory_order_relaxed);
    int start = 0;
    findTriggerIndex(engine.leftScopeBuffer, SYNTH_SCOPE_BUFFER, writeIdx,
                     triggerMode, triggerLevel, triggerEdge, triggerHysteresis, len, start);

    for (int i = 0; i < len; ++i) {
        int read = (start + i) % SYNTH_SCOPE_BUFFER;
        scopeSamplesL[i] = engine.leftScopeBuffer[read];
        scopeSamplesR[i] = engine.rightScopeBuffer[read];
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float halfWidth = avail.x * 0.5f;
    ImVec2 plotSize(halfWidth - 4.0f, 120.0f);

    ImGui::Columns(2, "ScopeColumns", false);

    ImGui::Text("Left Channel");
    ImVec2 leftScopePos = ImGui::GetCursorScreenPos();
    drawGrid(ImGui::GetWindowDrawList(), leftScopePos, plotSize, 10, 4, IM_COL32(80, 80, 80, 80));
    ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.6f, 1.0f, 0.6f, 1.0f));
    ImGui::PlotLines("##OscilloscopeL", scopeSamplesL, len, 0, nullptr, -1.0f, 1.0f, plotSize);
    ImGui::PopStyleColor();

    ImGui::NextColumn();

    ImGui::Text("Right Channel");
    ImVec2 rightScopePos = ImGui::GetCursorScreenPos();
    drawGrid(ImGui::GetWindowDrawList(), rightScopePos, plotSize, 10, 4, IM_COL32(80, 80, 80, 80));
    ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(1.0f, 0.6f, 0.6f, 1.0f));
    ImGui::PlotLines("##OscilloscopeR", scopeSamplesR, len, 0, nullptr, -1.0f, 1.0f, plotSize);
    ImGui::PopStyleColor();

    ImGui::Columns(1);

    ImGui::Separator();
    ImGui::Text("Scope Trigger");
    const char* triggerModes[] = {"Zero", "Level", "Edge", "Hysteresis"};
    ImGui::Combo("Trigger Mode", &triggerMode, triggerModes, IM_ARRAYSIZE(triggerModes));
    {
        KnobContext triggerContext;
        KnobParam trigLevel(&triggerLevel, -1.0f, 1.0f, -1, -1, KnobParam::DIRECT, -1);
        KnobParam trigHyst(&triggerHysteresis, 0.0f, 0.2f, -1, -1, KnobParam::DIRECT, -1);
        DrawKnob(triggerContext, "Level", trigLevel);
        ImGui::SameLine();
        DrawKnob(triggerContext, "Hyst", trigHyst);
    }
    ImGui::RadioButton("Rising", &triggerEdge, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Falling", &triggerEdge, 1);

    ImGui::Separator();
    ImGui::Text("Voice Oscilloscopes");
    int numVoices = (int)synth.voices.size();
    int showVoices = std::min(numVoices, SYNTH_MAX_VOICES);
    float availWidth = ImGui::GetContentRegionAvail().x;
    int optimalCols = std::min(4, std::max(1, showVoices));
    float itemSpacing = ImGui::GetStyle().ItemSpacing.x;
    float plotWidth = (availWidth - (optimalCols - 1) * itemSpacing) / optimalCols;

    ImGui::Columns(optimalCols, "VoiceScopeGrid", false);
    std::vector<float> voiceBuffer(SYNTH_SCOPE_VOICE_BUFFER);
    for (int vi = 0; vi < showVoices; ++vi) {
        ImGui::BeginGroup();
        int voiceStart = 0;
        findTriggerIndex(engine.voiceScopeBuffers[vi], SYNTH_SCOPE_VOICE_BUFFER,
                         engine.voiceScopeWriteIdx[vi].load(std::memory_order_relaxed),
                         triggerMode, triggerLevel, triggerEdge, triggerHysteresis,
                         SYNTH_SCOPE_VOICE_BUFFER, voiceStart);
        for (int i = 0; i < SYNTH_SCOPE_VOICE_BUFFER; ++i) {
            int read = (voiceStart + i) % SYNTH_SCOPE_VOICE_BUFFER;
            voiceBuffer[i] = engine.voiceScopeBuffers[vi][read];
        }
        char oscId[32];
        snprintf(oscId, sizeof(oscId), "##VoiceOscilloscope%d", vi);
        ImGui::PlotLines(oscId, voiceBuffer.data(), SYNTH_SCOPE_VOICE_BUFFER, 0, nullptr,
                         -1.0f, 1.0f, ImVec2(plotWidth, 60));
        ImGui::EndGroup();
        ImGui::NextColumn();
    }
    ImGui::Columns(1);

    ImGui::BeginGroup();
    ImGui::Text("Goniometer");
    ImVec2 gonSz(240, 240);
    ImGui::InvisibleButton("goniometer", gonSz);
    ImVec2 gonMin = ImGui::GetItemRectMin();
    ImVec2 gonMax = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRect(gonMin, gonMax, IM_COL32(200, 200, 200, 100));
    constexpr int gSamples = 512;
    ImVec2 center((gonMin.x + gonMax.x) * 0.5f, (gonMin.y + gonMax.y) * 0.5f);
    float radius = 0.45f * std::min(gonSz.x, gonSz.y);
    ImVec2 prevPt(0, 0);
    bool havePrev = false;
    int gonioWidx = engine.scopeWriteIndex.load(std::memory_order_relaxed);
    for (int i = 0; i < gSamples; ++i) {
        int read = (gonioWidx - gSamples + i + SYNTH_SCOPE_BUFFER * 2) % SYNTH_SCOPE_BUFFER;
        float l = engine.leftScopeBuffer[read];
        float r = engine.rightScopeBuffer[read];
        ImVec2 pt(center.x + l * radius, center.y - r * radius);
        if (havePrev) draw->AddLine(prevPt, pt, IM_COL32(100, 255, 100, 200), 1.0f);
        prevPt = pt;
        havePrev = true;
    }
    ImGui::EndGroup();

    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::Text("Spectrum (waterfall)");
    ImGui::Image((ImTextureID)(intptr_t)waterfallTex,
                 ImVec2((float)kWaterfallWidth, (float)kWaterfallHeight));
    ImGui::EndGroup();

    ImGui::Separator();
    ImGui::Text("%s", statusMessage.c_str());
    ImGui::End();
}

void SynthGui::render() {
    if (!initialized) return;
    ImGui::SetCurrentContext(imGuiContext);
    ImGui::Render();

    ImGuiIO& io = ImGui::GetIO();
    glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
    glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window);
}

void SynthGui::refreshPresetList(const std::string& directory) {
    presetFiles.clear();
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (entry.path().extension() == ".json") {
            presetFiles.push_back(entry.path().filename().string());
        }
    }
    std::sort(presetFiles.begin(), presetFiles.end());
}

void SynthGui::savePreset() {
    Preset::save(engine, presetFilename);
    statusMessage = "Preset saved: " + presetFilename;
    refreshPresetList();
}

void SynthGui::loadPreset() {
    Preset::load(engine, presetFilename);
    statusMessage = "Preset loaded: " + presetFilename;
}

namespace {
struct DialogAction {
    SynthGui* gui;
    int action; // 1 = load, 2 = save
};

void SDLCALL presetDialogCallback(void* userdata, const char* const* filelist, int /*filter*/) {
    auto* action = static_cast<DialogAction*>(userdata);
    if (!action || !filelist || !filelist[0]) {
        delete action;
        return;
    }
    action->gui->presetFilename = filelist[0];
    if (action->action == 1) action->gui->loadPreset();
    else action->gui->savePreset();
    delete action;
}
} // namespace

void SynthGui::savePresetDialog() {
    static const SDL_DialogFileFilter filters[] = {{"JSON files", "json"}};
    auto* action = new DialogAction{this, 2};
    SDL_ShowSaveFileDialog(presetDialogCallback, action, window, filters, 1, ".");
}

void SynthGui::loadPresetDialog() {
    static const SDL_DialogFileFilter filters[] = {{"JSON files", "json"}};
    auto* action = new DialogAction{this, 1};
    SDL_ShowOpenFileDialog(presetDialogCallback, action, window, filters, 1, ".", false);
}
