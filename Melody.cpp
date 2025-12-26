#include "Melody.h"
#include "Synthesizer.h"
#include "Voice.h"
#include <algorithm>
#include <atomic>
#include <iostream>



Melody::Melody() {
    // Initialize Mozart-inspired melody with variations
    // Inspired by Mozart's elegant style: graceful melodies, alberti bass, classical harmonies
    startupMelody = {
        // ===== MAIN THEME (A Section) =====
        // Opening motif - rich chord progressions with extended harmonies
        {{48, 55, 60, 64, 67, 72}, 0.15f, 0.02f}, // C3-G3-C4-E4-G4-C5 (I6 chord)
        {{50, 57, 62, 66, 69, 74}, 0.12f, 0.02f}, // D3-A3-D4-F#4-A4-D5 (V7 chord)
        {{48, 52, 60, 64, 67, 72}, 0.12f, 0.02f}, // C3-E3-C4-E4-G4-C5 (I chord with added 6th)
        {{43, 55, 59, 62, 67, 71}, 0.15f, 0.02f}, // G2-B2-D3-D4-G4-B4 (ii7 chord in G)

        // Graceful melody line with rich alberti bass accompaniment
        {{60, 48, 52, 55, 60, 67}, 0.10f, 0.02f}, // C4 melody, C3-E3-G3-C4-G4 bass (I chord)
        {{62, 50, 54, 57, 62, 69}, 0.10f, 0.02f}, // D4 melody, D3-F#3-A3-D4-A4 bass (V chord)
        {{64, 52, 55, 59, 64, 71}, 0.10f, 0.02f}, // E4 melody, E3-G3-B3-E4-B4 bass (vi chord)
        {{65, 53, 57, 60, 65, 72}, 0.10f, 0.02f}, // F4 melody, F3-A3-C4-F4-C5 bass (IV chord)

        // Ornamented passage with rich trills and suspensions
        {{67, 55, 59, 62, 67, 74}, 0.08f, 0.02f}, // G4 melody, G3-B3-D4-G4-D5 bass (V9 chord)
        {{69, 57, 60, 64, 69, 76}, 0.08f, 0.02f}, // A4 melody, A3-C4-E4-A4-E5 bass (vi7 chord)
        {{67, 55, 59, 62, 67, 74}, 0.08f, 0.02f}, // G4 melody (trill resolution)
        {{65, 53, 57, 60, 65, 72}, 0.10f, 0.02f}, // F4 melody, F3-A3-C4-F4-C5 bass (IV6 chord)

        // Crescendo to cadence with polychords
        {{64, 52, 55, 59, 64, 71, 76}, 0.12f, 0.02f}, // E4 melody, E3-G3-B3-E4-B4-E5 bass (vi9 chord)
        {{62, 50, 54, 57, 62, 69, 74}, 0.12f, 0.02f}, // D4 melody, D3-F#3-A3-D4-A4-D5 bass (V7add9)
        {{60, 48, 52, 55, 60, 67, 72}, 0.18f, 0.02f}, // C4 melody, C3-E3-G3-C4-G4-C5 bass (I6/4 cadence)

        // ===== DEVELOPMENT SECTION (B Section) =====
        // Modulation to G Major - complex extended harmonies
        {{55, 59, 62, 67, 71, 74}, 0.12f, 0.02f}, // G3-B3-D4-G4-B4-D5 (I7 in G)
        {{57, 60, 64, 69, 73, 76}, 0.12f, 0.02f}, // A3-C4-E4-A4-C#5-E5 (ii7#5 in G)
        {{59, 62, 65, 71, 74, 78}, 0.12f, 0.02f}, // B3-D4-F#4-B4-D5-F#5 (iii9 in G)
        {{60, 64, 67, 72, 76, 79}, 0.12f, 0.02f}, // C4-E4-G4-C5-E5-G5 (IV7 in G)

        // Chromatic passage with rich suspensions and polychords
        {{62, 53, 57, 62, 65, 69}, 0.10f, 0.02f}, // D4 melody, F3-A3-D4-F4-A4 bass (suspension)
        {{63, 55, 58, 63, 67, 70}, 0.10f, 0.02f}, // D#4 melody, G3-A#3-D#4-G4-A#4 bass (chromatic)
        {{62, 53, 57, 62, 65, 69}, 0.10f, 0.02f}, // D4 melody (resolution)
        {{60, 52, 55, 60, 64, 67}, 0.10f, 0.02f}, // C4 melody, E3-G3-C4-E4-G4 bass (6/4 suspension)

        // Sequence with varied rhythms
        {{64, 55, 59, 64}, 0.08f, 0.02f}, // E4 melody, G3-B3-E4 bass
        {{65, 57, 60, 65}, 0.08f, 0.02f}, // F4 melody, A3-C4-F4 bass
        {{67, 59, 62, 67}, 0.06f, 0.02f}, // G4 melody, B3-D4-G4 bass
        {{69, 60, 64, 69}, 0.06f, 0.02f}, // A4 melody, C4-E4-A4 bass

        // Diminished seventh chord sequence with full voicings (Mozart loves these)
        {{60, 63, 66, 69, 72, 75}, 0.12f, 0.02f}, // C4-Eb4-Gb4-Bb4-C5-Eb5 (full dim7)
        {{62, 65, 68, 71, 74, 77}, 0.12f, 0.02f}, // D4-F4-Ab4-B4-D5-F5 (full dim7)
        {{60, 63, 66, 69, 72, 75}, 0.12f, 0.02f}, // C4-Eb4-Gb4-Bb4-C5-Eb5 (resolution)

        // ===== RECAPITULATION WITH VARIATIONS =====
        // Return to C Major with ornamented main theme and rich harmonies
        {{48, 55, 60, 64, 67, 72}, 0.12f, 0.02f}, // C3-G3-C4-E4-G4-C5 (I7 chord - return)
        {{50, 57, 62, 66, 69, 74}, 0.12f, 0.02f}, // D3-A3-D4-F#4-A4-D5 (V9 chord)
        {{48, 52, 60, 64, 67, 72}, 0.12f, 0.02f}, // C3-E3-C4-E4-G4-C5 (I6add9 chord)
        {{43, 55, 59, 62, 67, 71}, 0.12f, 0.02f}, // G2-B2-D3-D4-G4-B4 (ii11 chord)

        // Varied melody with appoggiaturas and passing tones
        {{60, 48, 52, 55}, 0.08f, 0.02f}, // C4 melody, C3-D3-G3 bass
        {{61, 48, 52, 55}, 0.04f, 0.01f}, // C#4 appoggiatura
        {{60, 48, 52, 55}, 0.08f, 0.02f}, // C4 resolution
        {{62, 50, 54, 57}, 0.08f, 0.02f}, // D4 melody, D3-E3-A3 bass
        {{63, 50, 54, 57}, 0.04f, 0.01f}, // D#4 appoggiatura
        {{62, 50, 54, 57}, 0.08f, 0.02f}, // D4 resolution

        // More ornamented passage
        {{64, 52, 55, 59}, 0.06f, 0.02f}, // E4 melody, E3-G3-B3 bass
        {{66, 52, 55, 59}, 0.03f, 0.01f}, // F#4 grace note
        {{64, 52, 55, 59}, 0.06f, 0.02f}, // E4 resolution
        {{65, 53, 57, 60}, 0.10f, 0.02f}, // F4 melody, F3-A3-C4 bass

        // Crescendo with octave leaps (very Mozart-like)
        {{67, 55, 59, 62}, 0.08f, 0.02f}, // G4 melody, G3-B3-D4 bass
        {{72, 55, 59, 62}, 0.04f, 0.01f}, // C5 octave leap
        {{67, 55, 59, 62}, 0.08f, 0.02f}, // G4 return
        {{69, 57, 60, 64}, 0.08f, 0.02f}, // A4 melody, A3-C4-E4 bass
        {{74, 57, 60, 64}, 0.04f, 0.01f}, // D5 octave leap
        {{69, 57, 60, 64}, 0.08f, 0.02f}, // A4 return

        // Final flourish with arpeggios
        {{72, 60, 64, 67}, 0.06f, 0.02f}, // C5, C4, E4, G4 (arpeggio up)
        {{74, 62, 65, 69}, 0.06f, 0.02f}, // D5, D4, F4, A4 (arpeggio up)
        {{76, 64, 67, 71}, 0.06f, 0.02f}, // E5, E4, G4, B4 (arpeggio up)
        {{77, 65, 69, 72}, 0.06f, 0.02f}, // F5, F4, A4, C5 (arpeggio up)

        // Grand finale - full rich chordal cadence with polychords
        {{72, 60, 64, 67, 72, 76, 79}, 0.20f, 0.05f}, // C5 octave, C4-E4-G4-C5-E5-G5 (I7add11)
        {{71, 59, 62, 66, 71, 74, 78}, 0.15f, 0.05f}, // B4 octave, B3-D4-F#4-B4-D5-F#5 (V9b5)
        {{72, 60, 64, 67, 72, 76, 79, 84}, 0.30f, 0.10f}, // C5 octave, C4-E4-G4-C5-E5-G5-C6 (I13 - final)
    };
}

void Melody::startMelody() {
    melodyPlaying = true;
    currentMelodyEventIndex = 0;
    nextMelodyEventTime = 0; // Will be set immediately
    melodyLoopCount = 0;
}

void Melody::stopMelody() {
    melodyPlaying = false;
    currentMelodyEventIndex = 0;
    melodyLoopCount = 0;
    playingScheduledNotes.clear();
}

void Melody::resetMelody() {
    currentMelodyEventIndex = 0;
    melodyLoopCount = 0;
    nextMelodyEventTime = 0;
}

void Melody::updateMelodyPlayback(uint64_t currentTime, float perfFreq, Synthesizer& synth, std::atomic<int>& roundRobinIndex) {
    if (!melodyPlaying) {
        return;
    }

    // Handle scheduled note-offs
    processScheduledNoteOffs(currentTime, synth);

    // Schedule next melody event
    if (currentMelodyEventIndex < startupMelody.size()) {
        if (currentTime >= nextMelodyEventTime) {
            uint64_t timeOfThisEvent = currentTime;

            const MelodyEvent& event = startupMelody[currentMelodyEventIndex];

            // Play notes in the current event

            for (int midiNote : event.midiNotes) {
                float velocity = 0.8f; // Fixed velocity for melody

                // Allocate a voice using simple round-robin for each note in the chord.
                // This ensures each note of a chord gets a distinct voice.
                int nVoices = (int)synth.voices.size();
                int voiceIndex = roundRobinIndex.fetch_add(1) % nVoices;

                // Before assigning a new note, check if the chosen voice was previously playing any other note
                // and remove that mapping from noteToVoice.
                // This is crucial for correct cleanup when a voice is stolen.
                int oldMidiNoteOnStolenVoice = synth.voices[voiceIndex].getMidiNote();
                if (oldMidiNoteOnStolenVoice != -1) { // -1 means voice was truly inactive or never played
                    auto it = synth.noteToVoice.find(oldMidiNoteOnStolenVoice);
                    if (it != synth.noteToVoice.end() && it->second == voiceIndex) {
                        synth.noteToVoice.erase(it);
                    }
                }

                // Ensure any note currently on this voice is explicitly turned off.
                if (synth.voices[voiceIndex].getMidiNote() != -1) {
                    synth.voices[voiceIndex].noteOff(); // Initiate release for the stolen voice's previous note
                }

                synth.noteToVoice[midiNote] = voiceIndex;
                synth.voices[voiceIndex].noteOn(midiNote, velocity);

                // Schedule note off
                uint64_t noteOffTime = currentTime + (uint64_t)(event.durationSeconds * perfFreq);
                scheduleNoteOff(midiNote, velocity, noteOffTime, voiceIndex);
            }

            // Schedule the next event time based on timeOfThisEvent
            nextMelodyEventTime = timeOfThisEvent + (uint64_t)((event.durationSeconds + event.delayToNextSeconds) * perfFreq);
            currentMelodyEventIndex++;
        }
    } else { // Melody finished one pass, check for looping
        melodyLoopCount++;
        if (melodyLoopCount < melodyMaxLoops) {
            resetMelody(); // Restart melody
            nextMelodyEventTime = currentTime; // Play immediately
            // Do not clear playingScheduledNotes here, let them decay naturally based on their noteOffTime
        } else {
            // Melody finished all loops, ensure all playing notes are off
            if (!playingScheduledNotes.empty()) {
                for (const auto& sn : playingScheduledNotes) {
                    if (sn.voiceIndex >= 0 && sn.voiceIndex < (int)synth.voices.size()) {
                        synth.voices[sn.voiceIndex].noteOff();
                    }
                }
                playingScheduledNotes.clear();
            }
            melodyPlaying = false; // Stop melody playback
            resetMelody(); // Reset for potential replay
        }
    }
}

void Melody::scheduleNoteOff(int midiNote, float velocity, uint64_t noteOffTime, int voiceIndex) {
    playingScheduledNotes.push_back({midiNote, velocity, noteOffTime, voiceIndex});
}

void Melody::processScheduledNoteOffs(uint64_t currentTime, Synthesizer& synth) {
    playingScheduledNotes.erase(
        std::remove_if(playingScheduledNotes.begin(), playingScheduledNotes.end(),
            [&](const ScheduledNote& sn) {
                if (currentTime >= sn.noteOffTime) {
                    if (sn.voiceIndex >= 0 && sn.voiceIndex < (int)synth.voices.size()) {
                        synth.voices[sn.voiceIndex].noteOff();
                    }
                    return true; // Remove from scheduled notes
                }
                return false;
            }),
        playingScheduledNotes.end());
}