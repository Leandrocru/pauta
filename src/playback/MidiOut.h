#pragma once
// Sound output: receives MIDI messages.
//  - Windows: the system's built-in General MIDI synthesizer (WinMM / Microsoft GS Wavetable Synth).
//  - Linux: a small built-in synthesizer played through ALSA (no extra software needed).
#include <QString>

#include <cstdint>
#include <memory>

namespace pauta::playback {

class MidiOut {
public:
    virtual ~MidiOut() = default;
    virtual bool ok() const = 0;
    virtual QString name() const = 0;
    virtual void send(uint8_t status, uint8_t data1, uint8_t data2) = 0;
    void allNotesOff()
    {
        for (uint8_t ch = 0; ch < 16; ++ch) send(uint8_t(0xB0 | ch), 123, 0); // All Notes Off
    }
};

// Best available output on this system (never null; may report !ok()).
std::unique_ptr<MidiOut> createMidiOut();

} // namespace pauta::playback
