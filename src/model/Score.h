#pragma once
// In-memory musical model: Score -> Staff -> Measure -> Event (note/chord/rest).
#include "model/Fraction.h"

#include <QString>
#include <algorithm>
#include <array>
#include <vector>

namespace pauta {

enum class Clef { Treble, Bass, Alto, Tenor };

struct Pitch {
    int step = 0;   // 0=C, 1=D, 2=E, 3=F, 4=G, 5=A, 6=B
    int octave = 4; // scientific octave (C4 = middle C)
    int alter = 0;  // -2..+2 semitones
    bool tie = false; // tied to the same pitch in the next event of the staff

    int diatonic() const { return octave * 7 + step; }
    static Pitch fromDiatonic(int d, int alter = 0) { return Pitch{ ((d % 7) + 7) % 7, (d - (((d % 7) + 7) % 7)) / 7, alter }; }
    bool operator==(const Pitch&) const = default;
};

struct Event {
    int durLog = 2;              // -1=breve, 0=whole, 1=half, 2=quarter, 3=eighth, 4=16th, 5=32nd, 6=64th
    int dots = 0;
    std::vector<Pitch> pitches;  // ascending by diatonic; empty = rest
    // Tuplet: `tupletActual` notes in the time of `tupletNormal` (3:2 = triplet). 0 = none.
    int tupletActual = 0;
    int tupletNormal = 0;
    bool tupletStart = false;    // first event of a tuplet group
    unsigned articulations = 0;  // Artic bit flags
    int dynamic = 0;             // Dynamic value; 0 = none

    bool isRest() const { return pitches.empty(); }
    bool inTuplet() const { return tupletActual > 0 && tupletNormal > 0; }
    Frac duration() const;       // real duration (tuplet ratio applied)
    Frac baseDuration() const;   // written value (without the tuplet ratio)
    void sortPitches();
};

// Articulations (bit flags on an Event).
enum Artic : unsigned {
    ArticStaccato = 1u << 0,
    ArticAccent = 1u << 1,
    ArticTenuto = 1u << 2,
    ArticMarcato = 1u << 3,
    ArticStaccatissimo = 1u << 4,
    ArticFermata = 1u << 5,
};
constexpr unsigned kArticAll[6] = { ArticStaccato, ArticAccent, ArticTenuto, ArticMarcato, ArticStaccatissimo, ArticFermata };

// Dynamic marks attached to an event (shown below the staff, used for playback volume).
enum Dynamic : int { DynNone = 0, DynPPP, DynPP, DynP, DynMP, DynMF, DynF, DynFF, DynFFF, DynSFZ, DynSF, DynFP, DynCount };

constexpr int kVoices = 4; // "layers" in Finale terms

struct Measure {
    std::vector<Event> events;                    // layer 1
    std::array<std::vector<Event>, kVoices - 1> more; // layers 2..4

    std::vector<Event>& voice(int v) { return v <= 0 ? events : more[size_t(std::min(v, kVoices - 1) - 1)]; }
    const std::vector<Event>& voice(int v) const { return v <= 0 ? events : more[size_t(std::min(v, kVoices - 1) - 1)]; }
    Frac filled(int v = 0) const;
    bool isEmpty() const; // no events in any layer
    int usedVoices() const; // how many layers hold at least one note (not only rests)
};

struct Staff {
    QString name;
    Clef clef = Clef::Treble;
    std::vector<Measure> measures;
};

struct TimeSig {
    int beats = 4;
    int beatType = 4;
    Frac capacity() const { return Frac(beats, beatType); }
};

struct Score {
    QString title;
    QString composer;
    TimeSig time;
    int keyFifths = 0; // -7..+7
    int tempo = 96;    // quarter notes per minute (playback)
    std::vector<Staff> staves;

    int measureCount() const { return staves.empty() ? 0 : int(staves.front().measures.size()); }
    void appendMeasure();
    void setMeasureCount(int n);

    static Score makeEmpty(const QString& title, const std::vector<std::pair<QString, Clef>>& staves,
                           TimeSig time, int keyFifths, int measures);
    static Score makeDemo();
};

Frac durationOf(int durLog, int dots);
int clefMiddleLine(Clef clef);              // diatonic index of the middle staff line
int staffPosition(int diatonic, Clef clef);  // 0 = middle line, +1 per diatonic step up
int keyAlterFor(int step, int keyFifths);    // alteration implied by the key signature
QString pitchName(const Pitch& p);           // e.g. "Fá♯4"
QString durationName(int durLog);            // e.g. "Semínima"
QString clefName(Clef clef);
QString keyName(int fifths);
int tupletNormalFor(int actual);             // 3 -> 2, 5 -> 4, 2 -> 3 (duplet), ...
QString dynamicText(int dynamic);            // "mf"
int dynamicFromText(const QString& text);    // "mf" -> DynMF (0 if unknown)
int dynamicVelocity(int dynamic);            // MIDI velocity for playback
QString articulationName(unsigned artic);    // "Staccato"
QString articulationKey(unsigned artic);     // "staccato" (file format)                 // e.g. "Sol maior / mi menor (1 ♯)"

} // namespace pauta
