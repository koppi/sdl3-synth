#include "Synthesizer.h"
#include "Utils.h"

Synthesizer::Synthesizer() : masterVolume(1.0f), pan(0.0f),
                             unisonCount(1), unisonSpreadIndex(0),
                              pitchBend(0.0f), pitchBendRange(2.0f), modWheelValue(0.0f), modLfoPhase(0.0f), modLfoRate(5.0f),
                              filterEnabled(true),
                             arpEnabled(false), arpBpm(120.0f), arpGate(0.5f), arpDirection(0), arpRange(4), arpHold(false),
                             arpStepIndex(0), arpLastStepTime(0), arpActiveVoice(-1), arpActiveMidi(-1), arpOffDeadline(0),
                             flangerEnabled(false), flangerRate(0.5f), flangerDepth(0.003f), flangerMix(0.5f), flangerIndexL(0), flangerIndexR(0), flangerPhase(0.0f),
                             delayEnabled(true), delayTimeSec(0.3f), delayFeedback(0.3f), delayMix(0.4f), delayIndexL(0), delayIndexR(0), delayMaxSamples(0),
                              reverbEnabled(true), reverbSize(0.5f), reverbDamp(0.2f), reverbDelay(0.02f), reverbDiffuse(0.7f), reverbStereo(0.8f), reverbDryMix(0.7f), reverbWetMix(0.3f), reverbIndexL(0), reverbIndexR(0), reverbMaxSamples(0),
                              compressorEnabled(true), compressorThresholdDb(-6.0f), compressorRatio(4.0f), compressorAttackMs(10.0f), compressorReleaseMs(100.0f), compressorMakeupDb(0.0f), compressorGainL(1.0f), compressorGainR(1.0f),
                              dcFilterEnabled(false), dcFilterAlpha(0.995f), dcFilterX1L(0.0f), dcFilterX1R(0.0f), dcFilterY1L(0.0f), dcFilterY1R(0.0f),
                              softClipEnabled(false), softClipDrive(1.0f),
                              autoGainEnabled(false), autoGainTargetRMS(0.3f), autoGainAlpha(0.999f), autoGainGainL(1.0f), autoGainGainR(1.0f), autoGainRMSL(0.0f), autoGainRMSR(0.0f)
{
    const int NUM_VOICES = 8; // Set to 8 voices
    voices.resize(NUM_VOICES);

    // allocate delay buffer (max 3s)
    int maxDelaySec = 3;
    delayMaxSamples = SAMPLE_RATE * maxDelaySec;
    delayBufferL.assign(delayMaxSamples, 0.0f);
    delayBufferR.assign(delayMaxSamples, 0.0f);

    // flanger small buffer (100ms)
    flangerBufferL.assign(SAMPLE_RATE / 10, 0.0f);
    flangerBufferR.assign(SAMPLE_RATE / 10, 0.0f);

    // reverb buffer (2s)
    reverbMaxSamples = SAMPLE_RATE * 2;
    reverbBufferL.assign(reverbMaxSamples, 0.0f);
    reverbBufferR.assign(reverbMaxSamples, 0.0f);

    // filter
    filter.setSampleRate(SAMPLE_RATE);
}