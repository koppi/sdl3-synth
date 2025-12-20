#pragma once

#include "Voice.h"
#include "Filter.h"
#include <vector>
#include <map>
#include <cstdint>

struct Synthesizer {
    std::vector<Voice> voices;
    std::map<int,int> noteToVoice; // midiNote -> voice index
    float masterVolume;
    float pan; // -1.0 = full left, 0.0 = center, 1.0 = full right

    // Unison
    int unisonCount; // 1..8
    int unisonSpreadIndex; // 0..4

    // Pitch Bend & Modulation
    float pitchBend; // -1.0 to 1.0
    float pitchBendRange; // in semitones
    float modWheelValue; // 0 to 1.0
    float modLfoPhase;
    float modLfoRate;

    // Arpeggiator
    bool arpEnabled;
    float arpBpm;
    float arpGate; // 0..1
    int arpDirection; // 0=Up,1=Down,2=UpDown,3=Random
    int arpRange; // octaves
    bool arpHold;
    std::vector<int> arpHeldNotes; // notes currently held (MIDI)
    int arpStepIndex;
    uint64_t arpLastStepTime;
    int arpActiveVoice;
    int arpActiveMidi;
    uint64_t arpOffDeadline; // perf counter value when to turn off current arp note

    // Flanger
    bool flangerEnabled;
    float flangerRate;
    float flangerDepth; // seconds
    float flangerMix;
    std::vector<float> flangerBufferL;
    std::vector<float> flangerBufferR;
    int flangerIndexL;
    int flangerIndexR;
    float flangerPhase;

    // Delay
    bool delayEnabled;
    float delayTimeSec;
    float delayFeedback;
    float delayMix;
    std::vector<float> delayBufferL;
    std::vector<float> delayBufferR;
    int delayIndexL;
    int delayIndexR;
    int delayMaxSamples;

    // Reverb (enhanced multi-tap with stereo processing)
    bool reverbEnabled;
    float reverbSize;      // Overall reverb size (0.0-1.0)
    float reverbDamp;      // High frequency damping (0.0-1.0)
    float reverbDelay;     // Pre-delay time in seconds
    float reverbDiffuse;   // Diffusion amount (0.0-1.0)
    float reverbStereo;    // Stereo width (0.0-1.0)
    float reverbDryMix;    // Dry signal mix (0.0-1.0)
    float reverbWetMix;    // Wet signal mix (0.0-1.0)
    std::vector<float> reverbBufferL;
    std::vector<float> reverbBufferR;
    int reverbIndexL;
    int reverbIndexR;
    int reverbMaxSamples;

    // Mixer / Bus compression
    bool compressorEnabled;
    float compressorThresholdDb;
    float compressorRatio;
    float compressorAttackMs;
    float compressorReleaseMs;
    float compressorMakeupDb;
    float compressorGainL; // current smoothed gain L
    float compressorGainR; // current smoothed gain R

    // Filter
    bool filterEnabled;
    Filter filter;

    // DC Filter
    bool dcFilterEnabled;
    float dcFilterAlpha; // smoothing factor, typically ~0.995
    float dcFilterX1L, dcFilterX1R; // previous input samples
    float dcFilterY1L, dcFilterY1R; // previous output samples

    // Soft Clipping
    bool softClipEnabled;
    float softClipDrive; // amount of drive, 1.0 = no clipping, higher = more clipping

    // Auto Gain
    bool autoGainEnabled;
    float autoGainTargetRMS; // target RMS level, e.g. 0.3
    float autoGainAlpha; // smoothing for gain adjustment, e.g. 0.999
    float autoGainGainL, autoGainGainR; // current smoothed gain
    float autoGainRMSL, autoGainRMSR; // current smoothed RMS

    Synthesizer();
};