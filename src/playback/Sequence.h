#pragma once
// Turns a Score into timed MIDI messages for playback.
#include "model/Score.h"

#include <cstdint>
#include <vector>

namespace pauta::playback {

struct MidiMsg {
    double time = 0;     // seconds from the start of playback
    uint8_t status = 0;  // 0x90 note on, 0x80 note off, 0xC0 program change (low nibble = channel)
    uint8_t data1 = 0;
    uint8_t data2 = 0;
};

struct Sequence {
    std::vector<MidiMsg> messages;     // sorted by time (offs before ons at the same time)
    std::vector<int> order;            // measures in playing order (repeats unrolled)
    std::vector<double> measureStart;  // seconds, one per entry of `order`
    int fromMeasure = 0;
    double length = 0;                 // seconds
    int noteCount = 0;
};

// Playback from `fromMeasure` to the end, following repeats, endings and jumps.
// Ties are joined into one sounding note.
Sequence buildSequence(const Score& score, int fromMeasure = 0);

int midiPitch(const Pitch& p);
int gmProgramFor(const QString& staffName); // General MIDI program guessed from the name

} // namespace pauta::playback
