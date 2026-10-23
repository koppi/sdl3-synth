#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"

#include "libremidi/libremidi.hpp"

#include "Engine.h"
#include "Gui.h"
#include "Melody.h"
#include "Preset.h"
#include "SineTable.h"
#include "Utils.h"

#ifndef SAMPLE_RATE
#define SAMPLE_RATE 44100
#endif

namespace {

struct PortData {
    unsigned int portNumber = 0;
    std::string portName;
};

std::vector<std::unique_ptr<libremidi::midi_in>> g_midi_inputs;
std::vector<PortData> g_midi_port_data;
SynthEngine* g_engine = nullptr;
Melody g_melody;

void updateMelody(SynthEngine& engine) {
    std::lock_guard<std::recursive_mutex> lock(engine.mutex);
    const uint64_t now = SDL_GetPerformanceCounter();
    const float perfFreq = (float)SDL_GetPerformanceFrequency();
    static std::atomic<int> roundRobinIndex{0};
    g_melody.updateMelodyPlayback(now, perfFreq, engine.synth, roundRobinIndex);
}

} // namespace

void SDLCALL audioCallback(void* userdata, SDL_AudioStream* stream,
                           int additional_amount, int total_amount) {
    auto* engine = static_cast<SynthEngine*>(userdata);
    (void)additional_amount;
    if (!engine) return;

    const int channels = 2;
    const int numSamples = total_amount / (int)sizeof(Sint16);
    const int numFrames = numSamples / channels;
    if (numFrames <= 0) return;

    std::vector<float> left((size_t)numFrames);
    std::vector<float> right((size_t)numFrames);
    engine->process(left.data(), right.data(), numFrames);

    std::vector<Sint16> buffer((size_t)numSamples);
    for (int i = 0; i < numFrames; ++i) {
        buffer[(size_t)i * 2 + 0] = (Sint16)(std::clamp(left[i], -1.0f, 1.0f) * 32767.0f);
        buffer[(size_t)i * 2 + 1] = (Sint16)(std::clamp(right[i], -1.0f, 1.0f) * 32767.0f);
    }
    SDL_PutAudioStreamData(stream, buffer.data(), (int)(buffer.size() * sizeof(Sint16)));
}

#ifdef EMSCRIPTEN
extern "C" void midiCallbackFromJS(unsigned char* data, int length, int inputIndex) {
    (void)inputIndex;
    if (!g_engine || !data || length <= 0) return;
    g_engine->handleMidiMessage(data, length);
}
#endif

static void midiCallback(const libremidi::message& message, void* /*userData*/) {
    if (!g_engine) return;
    const auto& bytes = message.bytes;
    if (!bytes.empty()) {
        g_engine->handleMidiMessage(bytes.data(), (int)bytes.size());
    }
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    std::srand((unsigned int)std::time(nullptr));
    initSineTable();

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        std::cerr << "SDL could not initialize! SDL_Error: " << SDL_GetError() << std::endl;
        return 1;
    }

    int displayCount = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&displayCount);
    if (displayCount == 0) {
        std::cerr << "No video display available!" << std::endl;
        SDL_Quit();
        return 1;
    }
    SDL_free(displays);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_Window* window = SDL_CreateWindow("SDL3 Synthesizer", 1100, 760,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::cerr << "Window could not be created! SDL_Error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        std::cerr << "OpenGL context could not be created! SDL_Error: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_GL_SetSwapInterval(1);
    SDL_GL_MakeCurrent(window, glContext);

    SynthEngine engine;
    g_engine = &engine;
    engine.setSampleRate((float)SAMPLE_RATE);

    SynthGui gui(engine, window, true);
    if (!gui.init("#version 130")) {
        std::cerr << "Failed to initialize the GUI" << std::endl;
        SDL_GL_DestroyContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    gui.refreshPresetList();

#ifndef __EMSCRIPTEN__
    Preset::load(engine, "default_preset.json");
    gui.statusMessage = "Loaded default_preset.json";
#else
    gui.statusMessage = "SDL3 Synthesizer";
#endif

    SDL_AudioSpec desiredSpec;
    SDL_zero(desiredSpec);
    desiredSpec.freq = SAMPLE_RATE;
    desiredSpec.format = SDL_AUDIO_S16LE;
    desiredSpec.channels = 2;

    SDL_AudioStream* audioStream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desiredSpec, audioCallback, &engine);
    if (!audioStream) {
        std::cerr << "Failed to open audio device stream! SDL_Error: " << SDL_GetError() << std::endl;
        gui.shutdown();
        SDL_GL_DestroyContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_ResumeAudioStreamDevice(audioStream);

    try {
#ifdef __EMSCRIPTEN__
        libremidi::observer_configuration callbacks;
        libremidi::observer obs{std::move(callbacks),
                                libremidi::observer_configuration_for(libremidi::API::WEBMIDI)};
#else
        libremidi::observer_configuration callbacks{
            .input_added = [](const libremidi::input_port& id) {
                std::cout << "MIDI Input connected: " << id.port_name << std::endl;
                g_midi_port_data.push_back({(unsigned int)g_midi_port_data.size(), id.port_name});
                libremidi::input_configuration conf{
                    .on_message = [](const libremidi::message& msg) { midiCallback(msg, nullptr); }};
                auto& input = g_midi_inputs.emplace_back(std::make_unique<libremidi::midi_in>(conf));
                input->open_port(id);
            },
            .input_removed = [](const libremidi::input_port& id) {
                std::cout << "MIDI Input removed: " << id.port_name << std::endl;
                auto it = std::find_if(g_midi_port_data.begin(), g_midi_port_data.end(),
                                       [&id](const PortData& pd) { return pd.portName == id.port_name; });
                if (it != g_midi_port_data.end()) {
                    auto index = std::distance(g_midi_port_data.begin(), it);
                    g_midi_inputs.erase(g_midi_inputs.begin() + index);
                    g_midi_port_data.erase(it);
                }
            }};
        libremidi::observer obs{std::move(callbacks),
                                libremidi::observer_configuration_for(libremidi::API::UNSPECIFIED)};
#endif
        for (const auto& port : obs.get_input_ports()) {
            std::cout << "Opening MIDI Input: " << port.port_name << std::endl;
            g_midi_port_data.push_back({(unsigned int)g_midi_port_data.size(), port.port_name});
            libremidi::input_configuration conf{
                .on_message = [](const libremidi::message& msg) { midiCallback(msg, nullptr); }};
            auto& input = g_midi_inputs.emplace_back(std::make_unique<libremidi::midi_in>(conf));
            input->open_port(port);
        }
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize MIDI: " << e.what() << std::endl;
    }

#ifndef __EMSCRIPTEN__
    g_melody.startMelody();
#endif

    bool quit = false;
    SDL_Event event;

    long long lastTotalCpuTime = 0;
    long long lastIdleCpuTime = 0;
    uint64_t lastCpuUpdateTime = SDL_GetPerformanceCounter();
    const uint64_t cpuUpdateInterval = SDL_GetPerformanceFrequency() / 2;
    {
        auto initialCpuTimes = getCpuTimes();
        lastTotalCpuTime = initialCpuTimes.first;
        lastIdleCpuTime = initialCpuTimes.second;
    }

    while (!quit) {
        const uint64_t currentTime = SDL_GetPerformanceCounter();

        if (currentTime - lastCpuUpdateTime >= cpuUpdateInterval) {
            auto cpuTimes = getCpuTimes();
            const long long totalDelta = cpuTimes.first - lastTotalCpuTime;
            const long long idleDelta = cpuTimes.second - lastIdleCpuTime;
            float cpuUsage = 0.0f;
            if (totalDelta > 0) {
                cpuUsage = 100.0f * (1.0f - (float)idleDelta / (float)totalDelta);
            }
            char title[256];
            snprintf(title, sizeof(title), "SDL3 Synthesizer | CPU: %.1f%%", cpuUsage);
            SDL_SetWindowTitle(window, title);
            lastTotalCpuTime = cpuTimes.first;
            lastIdleCpuTime = cpuTimes.second;
            lastCpuUpdateTime = currentTime;
        }

        updateMelody(engine);

        while (SDL_PollEvent(&event) != 0) {
            gui.processEvent(event);
            if (event.type == SDL_EVENT_QUIT) {
                quit = true;
            } else if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                       event.window.windowID == SDL_GetWindowID(window)) {
                quit = true;
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.key == SDLK_ESCAPE) {
                    quit = true;
                } else if (event.key.key == SDLK_SPACE) {
                    std::lock_guard<std::recursive_mutex> lock(engine.mutex);
                    for (auto& voice : engine.synth.voices) voice.noteOff();
                    engine.synth.noteToVoice.clear();
                    engine.synth.arpHeldNotes.clear();
                    engine.synth.arpActiveVoice = -1;
                    engine.synth.arpActiveMidi = -1;
                    if (g_melody.melodyPlaying) {
                        g_melody.stopMelody();
                    } else {
                        g_melody.startMelody();
                    }
                } else if (event.key.key == SDLK_F1) {
                    g_melody.stopMelody();
                    engine.allNotesOff();
                }
            }
        }

        gui.newFrame();
        gui.draw();
        gui.render();

        constexpr float kTargetFrameMs = 1000.0f / 60.0f;
        const uint64_t frameEnd = SDL_GetPerformanceCounter();
        const float frameMs = (frameEnd - currentTime) /
                              (float)SDL_GetPerformanceFrequency() * 1000.0f;
        if (frameMs < kTargetFrameMs) {
            SDL_Delay((Uint32)(kTargetFrameMs - frameMs));
        }
    }

#ifndef __EMSCRIPTEN__
    Preset::save(engine, "default_preset.json");
#endif

    for (auto& midiInput : g_midi_inputs) {
        if (midiInput) midiInput->close_port();
    }
    g_midi_inputs.clear();
    g_midi_port_data.clear();

    SDL_DestroyAudioStream(audioStream);
    gui.shutdown();
    SDL_GL_DestroyContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    g_engine = nullptr;
    return 0;
}

#ifdef _WIN32
#include <windows.h>
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return main(__argc, __argv);
}
#endif
