#pragma once

#include <cmath>
#include <utility>

// Audio parameters
const int SAMPLE_RATE = 44100;
const int BUFFER_SIZE = 1024; // Number of samples per buffer

// MIDI to Frequency conversion
inline float midiNoteToFrequency(int midiNote) {
    return 440.0f * std::pow(2.0f, (midiNote - 69) / 12.0f);
}

// Time functions
double getCurrentTime();
double getCurrentTimeNative();

// CPU times function
std::pair<long long, long long> getCpuTimes();
