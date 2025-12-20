#pragma once

// Sine lookup table for optimized sine wave generation
// 4096 samples for one full cycle (2π radians)
const int SINE_TABLE_SIZE = 4096;
const float SINE_TABLE_SIZE_FLOAT = static_cast<float>(SINE_TABLE_SIZE);
extern float g_sineTable[SINE_TABLE_SIZE];

// Initialize sine lookup table
void initSineTable();

// Fast sine lookup with linear interpolation
float fastSin(float phase);