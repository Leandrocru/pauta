#pragma once
// Small polyphonic wavetable synthesizer played through ALSA (Linux). It is meant for
// proof-listening without installing anything: piano-like, string-like and wind-like timbres.
#include "playback/MidiOut.h"

#include <array>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace pauta::playback {

class SoftSynth : public MidiOut {
public:
    SoftSynth();
    ~SoftSynth() override;
    bool ok() const override { return m_ok; }
    QString name() const override { return QStringLiteral("Sintetizador interno (ALSA)"); }
    void send(uint8_t status, uint8_t data1, uint8_t data2) override;

private:
    struct Voice {
        bool active = false;
        bool released = false;
        int channel = 0, note = 0, timbre = 0;
        double phase = 0, step = 0, amp = 0, level = 0, decay = 1, release = 1;
        double attackLeft = 0;
    };
    struct Msg { uint8_t st, d1, d2; };

    void run();
    void handle(const Msg& m);
    void render(int16_t* out, int frames);

    static constexpr int kRate = 44100;
    static constexpr int kTable = 2048;
    static constexpr int kTimbres = 4;   // 0 piano, 1 strings, 2 winds, 3 voice

    bool m_ok = false;
    void* m_pcm = nullptr; // snd_pcm_t*
    std::thread m_thread;
    std::atomic<bool> m_quit{ false };
    std::mutex m_mutex;
    std::vector<Msg> m_queue;
    std::array<Voice, 64> m_voices{};
    std::array<int, 16> m_program{};
    std::array<std::array<float, kTable>, kTimbres> m_tables{};
};

} // namespace pauta::playback
