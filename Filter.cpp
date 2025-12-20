#include "Filter.h"

Filter::Filter() : cutoff(1000.0f), resonance(0.707f), drive(1.0f), inertial(0.0f), oversampling(0), sampleRate(48000.0f), smoothedCutoff(1000.0f), smoothedResonance(0.707f), b0(1.0f), b1(0.0f), b2(0.0f), a1(0.0f), a2(0.0f), x1(0.0f), x2(0.0f), y1(0.0f), y2(0.0f) {
    updateCoefficients();
}

void Filter::setCutoff(float cutoffHz) {
    cutoff = cutoffHz;
}

void Filter::setResonance(float res) {
    resonance = res;
}

void Filter::setDrive(float d) {
    drive = d;
}

void Filter::setInertial(float i) {
    inertial = i;
}

void Filter::setOversampling(int os) {
    oversampling = os;
}

void Filter::setSampleRate(float sr) {
    sampleRate = sr;
    updateCoefficients();
}

float Filter::process(float input) {
    // Smooth parameters
    float alpha = inertial;
    smoothedCutoff = alpha * smoothedCutoff + (1.0f - alpha) * cutoff;
    smoothedResonance = alpha * smoothedResonance + (1.0f - alpha) * resonance;

    // Update coefficients if needed (only when parameters change significantly)
    static float lastCutoff = -1.0f;
    static float lastResonance = -1.0f;
    if (fabs(smoothedCutoff - lastCutoff) > 1.0f || fabs(smoothedResonance - lastResonance) > 0.01f) {
        lastCutoff = smoothedCutoff;
        lastResonance = smoothedResonance;
        float tempCutoff = smoothedCutoff;
        float tempResonance = smoothedResonance;
        // Temporarily set for coefficient calculation
        cutoff = tempCutoff;
        resonance = tempResonance;
        updateCoefficients();
    }

    // Apply drive
    input *= drive;
    input = tanh(input); // Soft clipping

    float output = input;

    if (oversampling > 0) {
        // Upsample
        int factor = oversampling;
        upsampled.resize(factor);
        upsampled[0] = input;
        for (int i = 1; i < factor; ++i) {
            upsampled[i] = 0.0f; // Zero stuffing or linear interp, simple zero for now
        }

        // Filter at high rate
        filtered.resize(factor);
        for (int i = 0; i < factor; ++i) {
            float highInput = upsampled[i];
            filtered[i] = b0 * highInput + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = highInput;
            y2 = y1;
            y1 = filtered[i];
        }

        // Downsample by averaging
        output = 0.0f;
        for (int i = 0; i < factor; ++i) {
            output += filtered[i];
        }
        output /= factor;
    } else {
        // No oversampling
        output = b0 * input + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = input;
        y2 = y1;
        y1 = output;
    }

    return output;
}

float Filter::getCutoff() const {
    return cutoff;
}

float Filter::getResonance() const {
    return resonance;
}

float Filter::getDrive() const {
    return drive;
}

float Filter::getInertial() const {
    return inertial;
}

int Filter::getOversampling() const {
    return oversampling;
}

void Filter::updateCoefficients() {
    if (resonance < 0.1f) resonance = 0.1f; // Avoid division by zero
    float omega = 2.0f * M_PI * cutoff / sampleRate;
    float k = tan(omega / 2.0f);
    float q = resonance;
    float norm = 1.0f / (1.0f + k / q + k * k);
    b0 = k * k * norm;
    b1 = 2.0f * b0;
    b2 = b0;
    a1 = 2.0f * (k * k - 1.0f) * norm;
    a2 = (1.0f - k / q + k * k) * norm;
}