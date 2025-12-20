#pragma once

#include <string>

class Preset {
public:
    static void save(const std::string& filename);
    static void load(const std::string& filename);
};