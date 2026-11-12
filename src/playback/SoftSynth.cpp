#include "playback/SoftSynth.h"

#include <alsa/asoundlib.h>

#include <algorithm>
#include <cmath>

namespace pauta::playback {

namespace {

constexpr double kPi = 3.14159265358979323846;

int timbreForProgram(int program)
{
    if (program >= 40 && program <= 51) return 1; // strings
    if (program >= 56 && program <= 79) return 2; // brass / reeds / pipes
    if (program >= 52 && program <= 54) return 3; // choir / voice
    return 0;                                      // pianos, guitars, everything else
}

} // namespace

SoftSynth::SoftSynth()
{
    // Harmonic recipes for each timbre (relative amplitudes of partials 1..8).
    const double recipes[kTimbres][8] = {
        { 1.0, 0.55, 0.30, 0.18, 0.10, 0.06, 0.03, 0.02 }, // piano-like
        { 1.0, 0.70, 0.50, 0.35, 0.25, 0.18, 0.12, 0.08 }, // strings (sawtooth-ish)
        { 1.0, 0.10, 0.45, 0.05, 0.25, 0.03, 0.12, 0.02 }, // winds (odd partials)
        { 1.0, 0.40, 0.15, 0.08, 0.04, 0.02, 0.01, 0.00 }, // voice ("aah")
    };
    for (int t = 0; t < kTimbres; ++t) {
        double peak = 0;
        std::array<double, kTable> tmp{};
        for (int i = 0; i < kTable; ++i) {
            double v = 0;
            for (int h = 0; h < 8; ++h) v += recipes[t][h] * std::sin(2 * kPi * (h + 1) * i / kTable);
            tmp[size_t(i)] = v;
            peak = std::max(peak, std::abs(v));
        }
        for (int i = 0; i < kTable; ++i) m_tables[size_t(t)][size_t(i)] = float(tmp[size_t(i)] / peak);
    }

    snd_pcm_t* pcm = nullptr;
    if (snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) return;
    if (snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2, kRate, 1, 60000) < 0) {
        snd_pcm_close(pcm);
        return;
    }
    m_pcm = pcm;
    m_ok = true;
    m_thread = std::thread([this] { run(); });
}

SoftSynth::~SoftSynth()
{
    m_quit = true;
    if (m_thread.joinable()) m_thread.join();
    if (m_pcm) {
        snd_pcm_drop(static_cast<snd_pcm_t*>(m_pcm));
        snd_pcm_close(static_cast<snd_pcm_t*>(m_pcm));
    }
}

void SoftSynth::send(uint8_t status, uint8_t d1, uint8_t d2)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.push_back({ status, d1, d2 });
}

void SoftSynth::handle(const Msg& m)
{
    const int type = m.st & 0xF0;
    const int ch = m.st & 0x0F;
    if (type == 0xC0) {
        m_program[size_t(ch)] = m.d1;
    } else if (type == 0x90 && m.d2 > 0) {
        // Take a free voice, or steal the quietest one.
        Voice* v = nullptr;
        for (Voice& x : m_voices)
            if (!x.active) { v = &x; break; }
        if (!v) v = &*std::min_element(m_voices.begin(), m_voices.end(), [](const Voice& a, const Voice& b) { return a.level < b.level; });
        const double freq = 440.0 * std::pow(2.0, (m.d1 - 69) / 12.0);
        *v = Voice{};
        v->active = true;
        v->channel = ch;
        v->note = m.d1;
        v->timbre = timbreForProgram(m_program[size_t(ch)]);
        v->step = freq * kTable / kRate;
        v->amp = m.d2 / 127.0;
        v->attackLeft = 0.006 * kRate;
        // Piano-like notes fade by themselves (faster for high notes); others sustain.
        const double tau = v->timbre == 0 ? std::clamp(2.5 - (m.d1 - 40) * 0.03, 0.4, 3.0) : 30.0;
        v->decay = std::exp(-1.0 / (tau * kRate));
        v->release = std::exp(-1.0 / ((v->timbre == 0 ? 0.12 : 0.18) * kRate));
    } else if (type == 0x80 || (type == 0x90 && m.d2 == 0)) {
        for (Voice& x : m_voices)
            if (x.active && !x.released && x.channel == ch && x.note == m.d1) x.released = true;
    } else if (type == 0xB0 && m.d1 == 123) {
        for (Voice& x : m_voices)
            if (x.active && x.channel == ch) x.released = true;
    }
}

void SoftSynth::render(int16_t* out, int frames)
{
    for (int i = 0; i < frames; ++i) {
        double mix = 0;
        for (Voice& v : m_voices) {
            if (!v.active) continue;
            // Envelope: short linear attack, then decay; release on note off.
            if (v.attackLeft > 0) {
                v.level += (1.0 - v.level) / v.attackLeft;
                v.attackLeft -= 1;
            } else {
                v.level *= v.released ? v.release : v.decay;
            }
            if (v.released && v.level < 1e-4) { v.active = false; continue; }
            const auto& table = m_tables[size_t(v.timbre)];
            const int idx = int(v.phase);
            const double frac = v.phase - idx;
            const double s = table[size_t(idx)] + (table[size_t((idx + 1) % kTable)] - table[size_t(idx)]) * frac;
            mix += s * v.level * v.amp;
            v.phase += v.step;
            if (v.phase >= kTable) v.phase -= kTable;
        }
        const double y = std::tanh(mix * 0.25); // soft limiter
        const auto sample = int16_t(std::clamp(y, -1.0, 1.0) * 30000);
        out[2 * i] = sample;
        out[2 * i + 1] = sample;
    }
}

void SoftSynth::run()
{
    auto* pcm = static_cast<snd_pcm_t*>(m_pcm);
    constexpr int kFrames = 256;
    std::vector<int16_t> buf(kFrames * 2);
    std::vector<Msg> pending;
    while (!m_quit) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            pending.swap(m_queue);
        }
        for (const Msg& m : pending) handle(m);
        pending.clear();
        render(buf.data(), kFrames);
        snd_pcm_sframes_t n = snd_pcm_writei(pcm, buf.data(), kFrames);
        if (n < 0) snd_pcm_recover(pcm, int(n), 1);
    }
}

} // namespace pauta::playback
