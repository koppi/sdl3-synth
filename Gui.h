#pragma once

#include "Engine.h"

#include <SDL3/SDL.h>

#include <string>
#include <vector>

// Shared ImGui front-end used by the standalone application and the VST3
// editor. The owner supplies an already created SDL window with a current
// OpenGL context.
class SynthGui {
public:
    SynthGui(SynthEngine& engine, SDL_Window* window, bool standalone);
    ~SynthGui();

    bool init(const char* glslVersion = "#version 300 es");
    void shutdown();

    void newFrame();
    void draw();
    void render();
    void processEvent(const SDL_Event& event);

    void refreshPresetList(const std::string& directory = ".");
    void savePreset();
    void loadPreset();

    SDL_Window* getWindow() const { return window; }
    bool isInitialized() const { return initialized; }

    // Directory that holds runtime assets (fonts). The VST3 editor points this
    // at the plug-in's bundle resource directory before calling init().
    void setAssetDirectory(const std::string& directory) { assetDirectory = directory; }

    std::string statusMessage;
    std::string presetFilename = "default_preset.json";

private:
    void drawControlsWindow();
    void drawScopeWindow();
    void updateWaterfall();
    void savePresetDialog();
    void loadPresetDialog();

    SynthEngine& engine;
    SDL_Window* window = nullptr;
    struct ImGuiContext* imGuiContext = nullptr;
    bool standalone = false;
    bool initialized = false;
    bool layoutInitialized = false;
    bool layoutInitializedScope = false;

    std::vector<std::string> presetFiles;
    char presetFilenameBuffer[512] = "default_preset.json";
    std::string assetDirectory = "data";

    // Scope trigger settings.
    int triggerMode = 0;
    float triggerLevel = 0.0f;
    int triggerEdge = 0;
    float triggerHysteresis = 0.01f;

    // Waterfall texture.
    unsigned int waterfallTex = 0;
    std::vector<unsigned char> waterfallPixels;
};
