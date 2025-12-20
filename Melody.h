#pragma once

#include <vector>
#include <cstdint>
#include <atomic>

// Forward declaration for Synthesizer (avoid circular include)
struct Synthesizer;

// Melody-related structs
struct NoteOnEvent {
    int midiNote;
    float velocity;
    int voiceIndex; // Which voice is playing this note
};

struct ScheduledNote {
    int midiNote;
    float velocity;
    uint64_t noteOffTime; // SDL_GetPerformanceCounter() value
    int voiceIndex; // The voice that played this note

    // Explicit constructor
    ScheduledNote(int note, float vel, uint64_t offTime, int vIdx)
        : midiNote(note), velocity(vel), noteOffTime(offTime), voiceIndex(vIdx) {}
};

struct MelodyEvent {
    std::vector<int> midiNotes; // MIDI notes to play
    float durationSeconds; // Duration of chord/notes
    float delayToNextSeconds; // Delay until the next MelodyEvent
};

// Melody class to handle startup melody playback
class Melody {
public:
    Melody();
    ~Melody() = default;

    // Melody data
    std::vector<MelodyEvent> startupMelody;

    // Playback state
    int currentMelodyEventIndex = 0;
    uint64_t nextMelodyEventTime = 0; // SDL_GetPerformanceCounter() value
    bool melodyPlaying = false;
    int melodyLoopCount = 0; // Current loop iteration
    const int melodyMaxLoops = 2; // Maximum number of loops (reduced since melody is much longer)

    // Scheduled notes
    std::vector<ScheduledNote> playingScheduledNotes;

    // Methods
    void startMelody();
    void stopMelody();
    void updateMelodyPlayback(uint64_t currentTime, float perfFreq, Synthesizer& synth, std::atomic<int>& roundRobinIndex);
    void scheduleNoteOff(int midiNote, float velocity, uint64_t noteOffTime, int voiceIndex);
    void processScheduledNoteOffs(uint64_t currentTime, Synthesizer& synth);

private:
    // Helper methods
    void resetMelody();
};