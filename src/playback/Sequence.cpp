#include "playback/Sequence.h"

#include <algorithm>
#include <map>

namespace pauta::playback {

int midiPitch(const Pitch& p)
{
    static const int semis[7] = { 0, 2, 4, 5, 7, 9, 11 };
    return std::clamp(12 * (p.octave + 1) + semis[p.step] + p.alter, 0, 127);
}

int gmProgramFor(const QString& staffName)
{
    const QString n = staffName.toLower();
    struct M { const char* key; int program; };
    static const M table[] = {
        { "violino", 40 }, { "violin", 40 }, { "viola", 41 }, { "violoncelo", 42 }, { "cello", 42 },
        { "contrabaixo", 43 }, { "flauta", 73 }, { "flute", 73 }, { "oboé", 68 }, { "oboe", 68 },
        { "clarinete", 71 }, { "clarinet", 71 }, { "fagote", 70 }, { "trompete", 56 }, { "trompa", 60 },
        { "trombone", 57 }, { "tuba", 58 }, { "saxofone", 65 }, { "sax", 65 }, { "violão", 24 },
        { "guitarra", 25 }, { "órgão", 19 }, { "orgao", 19 }, { "cravo", 6 }, { "harpa", 46 },
        { "soprano", 52 }, { "contralto", 52 }, { "tenor", 52 }, { "baixo", 52 }, { "voz", 52 },
        { "coro", 52 }, { "choir", 52 }, { "voice", 52 },
    };
    for (const M& m : table)
        if (n.contains(QString::fromUtf8(m.key))) return m.program;
    return 0; // acoustic grand piano
}

Sequence buildSequence(const Score& score, int fromMeasure)
{
    Sequence seq;
    const int nMeasures = score.measureCount();
    fromMeasure = std::clamp(fromMeasure, 0, std::max(0, nMeasures - 1));
    seq.fromMeasure = fromMeasure;
    const double secPerWhole = 4.0 * 60.0 / std::max(score.tempo, 1);
    const Frac capacity = score.time.capacity();

    // Measure start times (a measure lasts its time signature, or longer if overfilled).
    double t = 0;
    std::vector<double> starts;
    for (int m = fromMeasure; m < nMeasures; ++m) {
        starts.push_back(t);
        Frac len = capacity;
        for (const Staff& st : score.staves)
            for (int v = 0; v < kVoices; ++v) len = std::max(len, st.measures[size_t(m)].filled(v));
        t += len.toDouble() * secPerWhole;
    }
    seq.measureStart = starts;
    seq.length = t;

    struct Note { double start, end; int pitch, channel, velocity; };
    std::vector<Note> notes;

    for (size_t s = 0; s < score.staves.size(); ++s) {
        const Staff& st = score.staves[s];
        // Channel per staff (channel 10 is percussion in General MIDI).
        int channel = int(s) < 9 ? int(s) : int(s) + 1;
        channel = std::min(channel, 15);
        seq.messages.push_back({ 0.0, uint8_t(0xC0 | channel), uint8_t(gmProgramFor(st.name)), 0 });

        // Dynamic level along this staff (marks in any layer, in time order). One-off accents
        // (sfz, sf) do not change the level; fp is loud once, then piano.
        std::vector<std::pair<double, int>> levels; // (time, velocity)
        for (int v = 0; v < kVoices; ++v)
            for (int m = fromMeasure; m < nMeasures; ++m) {
                Frac p;
                for (const Event& e : st.measures[size_t(m)].voice(v)) {
                    const double t0 = starts[size_t(m - fromMeasure)] + p.toDouble() * secPerWhole;
                    p += e.duration();
                    if (!e.dynamic || e.dynamic == DynSFZ || e.dynamic == DynSF) continue;
                    levels.push_back({ t0, dynamicVelocity(e.dynamic == DynFP ? int(DynP) : e.dynamic) });
                }
            }
        // Marks placed before the playback start still set the level.
        int initial = 80;
        for (int m = 0; m < fromMeasure; ++m)
            for (int v = 0; v < kVoices; ++v)
                for (const Event& e : st.measures[size_t(m)].voice(v))
                    if (e.dynamic && e.dynamic != DynSFZ && e.dynamic != DynSF)
                        initial = dynamicVelocity(e.dynamic == DynFP ? int(DynP) : e.dynamic);
        std::stable_sort(levels.begin(), levels.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        auto levelAt = [&](double t) {
            int vel = initial;
            for (const auto& [time, v] : levels) {
                if (time > t + 1e-6) break;
                vel = v;
            }
            return vel;
        };

        for (int v = 0; v < kVoices; ++v) {
            std::map<int, size_t> tied; // diatonic -> note still sounding through a tie
            for (int m = fromMeasure; m < nMeasures; ++m) {
                Frac pos;
                for (const Event& e : st.measures[size_t(m)].voice(v)) {
                    const double start = starts[size_t(m - fromMeasure)] + pos.toDouble() * secPerWhole;
                    const double dur = e.duration().toDouble() * secPerWhole;
                    pos += e.duration();
                    std::map<int, size_t> stillTied;
                    // Articulation: length and accent.
                    double len = 0.92;
                    if (e.articulations & ArticTenuto) len = 1.0;
                    if (e.articulations & ArticStaccato) len = 0.5;
                    if (e.articulations & ArticStaccatissimo) len = 0.3;
                    int vel = (e.dynamic == DynSFZ || e.dynamic == DynSF || e.dynamic == DynFP) ? dynamicVelocity(e.dynamic) : levelAt(start);
                    if (e.articulations & ArticAccent) vel += 18;
                    if (e.articulations & ArticMarcato) vel += 25;
                    vel = std::clamp(vel, 1, 127);
                    for (const Pitch& p : e.pitches) {
                        auto it = tied.find(p.diatonic());
                        size_t idx;
                        if (it != tied.end()) {
                            idx = it->second;                 // continuation of a tie: no new attack
                            notes[idx].end = start + dur * (p.tie ? 1.0 : len);
                        } else {
                            idx = notes.size();
                            notes.push_back({ start, start + dur * (p.tie ? 1.0 : len), midiPitch(p), channel, vel });
                        }
                        if (p.tie) stillTied[p.diatonic()] = idx;
                    }
                    tied.swap(stillTied);
                }
            }
        }
    }

    seq.noteCount = int(notes.size());
    for (const Note& n : notes) {
        seq.messages.push_back({ n.start, uint8_t(0x90 | n.channel), uint8_t(n.pitch), uint8_t(n.velocity) });
        seq.messages.push_back({ std::max(n.end, n.start + 0.02), uint8_t(0x80 | n.channel), uint8_t(n.pitch), 0 });
    }
    std::stable_sort(seq.messages.begin(), seq.messages.end(), [](const MidiMsg& a, const MidiMsg& b) {
        if (a.time != b.time) return a.time < b.time;
        auto rank = [](uint8_t st) { return (st & 0xF0) == 0xC0 ? 0 : (st & 0xF0) == 0x80 ? 1 : 2; };
        return rank(a.status) < rank(b.status);
    });
    return seq;
}

} // namespace pauta::playback
