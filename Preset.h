#pragma once

#include <string>

class SynthEngine;

class Preset {
public:
    static void save(SynthEngine& engine, const std::string& filename);
    static void load(SynthEngine& engine, const std::string& filename);
    static void saveToString(SynthEngine& engine, std::string& outJson);
    static void loadFromString(SynthEngine& engine, const std::string& jsonStr);
};