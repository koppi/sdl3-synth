#pragma once

#include <cmath>
#include <vector>

class Filter {
public:
    Filter();

    void setCutoff(float cutoffHz);
    void setResonance(float resonance); // Q factor, higher = more resonance
    void setDrive(float drive); // Input gain, causes saturation
    void setInertial(float inertial); // Smoothing factor for parameter changes (0-1)
    void setOversampling(int oversampling); // 0, 2, 4, 8 times sample rate
    void setSampleRate(float sampleRate);

    float process(float input);

    float getCutoff() const;
    float getResonance() const;
    float getDrive() const;
    float getInertial() const;
    int getOversampling() const;

private:
    void updateCoefficients();

    float cutoff;
    float resonance;
    float drive;
    float inertial;
    int oversampling;
    float sampleRate;

    // Smoothing state
    float smoothedCutoff;
    float smoothedResonance;

    // Filter coefficients
    float b0, b1, b2, a1, a2;

    // Filter state
    float x1, x2; // Previous inputs
    float y1, y2; // Previous outputs

    // Oversampling buffers
    std::vector<float> upsampled;
    std::vector<float> filtered;
};