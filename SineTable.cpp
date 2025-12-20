#include "SineTable.h"
#include <cmath>

// Sine lookup table for optimized sine wave generation
// 4096 samples for one full cycle (2π radians)
float g_sineTable[SINE_TABLE_SIZE];

// Initialize sine lookup table
void initSineTable() {
    for (int i = 0; i < SINE_TABLE_SIZE; ++i) {
        float phase = 2.0f * M_PI * static_cast<float>(i) / SINE_TABLE_SIZE_FLOAT;
        g_sineTable[i] = std::sin(phase);
    }
}

// Fast sine lookup with linear interpolation
float fastSin(float phase) {
    // Wrap phase to [0, 2π)
    phase = std::fmod(phase, 2.0f * M_PI);
    if (phase < 0.0f) phase += 2.0f * M_PI;

    // Convert to table index
    float index = phase * SINE_TABLE_SIZE_FLOAT / (2.0f * M_PI);

    // Get integer and fractional parts
    int idx1 = static_cast<int>(index) % SINE_TABLE_SIZE;
    int idx2 = (idx1 + 1) % SINE_TABLE_SIZE;
    float frac = index - std::floor(index);

    // Linear interpolation
    return g_sineTable[idx1] * (1.0f - frac) + g_sineTable[idx2] * frac;
}