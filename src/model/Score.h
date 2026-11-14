#pragma once
// In-memory musical model: Score -> Staff -> Measure -> Event (note/chord/rest).
#include "model/Fraction.h"

#include <QString>
#include <algorithm>
#include <array>
#include <map>
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

// One syllable of lyrics under a note.
struct Syllable {
    QString text;
    bool hyphen = false;   // the word continues on the next syllable ("can-")
    bool extender = false; // melisma line after the syllable
    bool operator==(const Syllable&) const = default;
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
    // Smart shapes, stored on their first and last notes (they travel with the notes when
    // others are inserted or deleted). A start pairs with the next end in the same layer.
    bool slurStart = false;
    bool slurEnd = false;
    int hairpin = 0;             // Hairpin starting here: 1 = crescendo, 2 = diminuendo
    bool hairpinEnd = false;
    std::vector<Syllable> lyrics; // one per verse (empty text = no syllable in that verse)
    QString chordSymbol;          // e.g. "C7", "F#m7b5", "G/B" (as typed)

    const Syllable* lyric(int verse) const
    {
        return verse < int(lyrics.size()) && !lyrics[size_t(verse)].text.isEmpty() ? &lyrics[size_t(verse)] : nullptr;
    }

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

enum Hairpin : int { HairpinNone = 0, HairpinCresc = 1, HairpinDim = 2 };

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
    Clef clef = Clef::Treble;          // clef at the start of the piece
    std::map<int, Clef> clefChanges;   // measure index (> 0) -> clef from that measure on
    std::vector<Measure> measures;

    Clef clefAt(int measure) const
    {
        Clef c = clef;
        for (const auto& [m, cl] : clefChanges) {
            if (m > measure) break;
            c = cl;
        }
        return c;
    }
};

struct TimeSig {
    int beats = 4;
    int beatType = 4;
    Frac capacity() const { return Frac(beats, beatType); }
    bool operator==(const TimeSig&) const = default;
};

// Repeats, endings, jumps and special barlines of one measure (the same in every staff).
enum class BarStyle { Normal, Double, Final };
enum Jump : int {
    JumpNone = 0, JumpFine, JumpDC, JumpDCalFine, JumpDCalCoda, JumpDS, JumpDSalFine, JumpDSalCoda, JumpToCoda, JumpCount
};

struct MeasureMarks {
    bool repeatStart = false;   // |: at the start of the measure
    bool repeatEnd = false;     // :| at the end of the measure
    int repeatTimes = 2;        // total times the repeated section is played
    unsigned endings = 0;       // volta bracket over this measure: bit n-1 = ending n ("1.", "2.")
    BarStyle bar = BarStyle::Normal; // barline at the end of the measure (repeats take precedence)
    bool segno = false;         // 𝄋 at the start of the measure
    bool coda = false;          // 𝄌 at the start of the measure (target of "To Coda")
    int jump = JumpNone;        // instruction at the end of the measure (Fine, D.C., D.S., To Coda)

    bool isEmpty() const { return *this == MeasureMarks{}; }
    bool operator==(const MeasureMarks&) const = default;
};

// An instrument: a named staff plus the unnamed staves right after it (e.g. piano treble + bass).
struct PartRange {
    QString name;
    int firstStaff = 0;
    int staffCount = 1;
    bool contains(int staff) const { return staff >= firstStaff && staff < firstStaff + staffCount; }
};

struct Score {
    QString title;
    QString composer;
    TimeSig time;      // at the start of the piece
    int keyFifths = 0; // -7..+7, at the start of the piece
    // Changes in the middle of the piece: measure index (> 0) -> new value from that measure on.
    std::map<int, TimeSig> timeChanges;
    std::map<int, int> keyChanges;
    int tempo = 96;    // quarter notes per minute (playback)
    std::map<int, MeasureMarks> marks; // measure index -> repeats/endings/jumps (only non-empty ones)
    std::vector<Staff> staves;

    MeasureMarks marksAt(int measure) const
    {
        const auto it = marks.find(measure);
        return it != marks.end() ? it->second : MeasureMarks{};
    }
    void setMarks(int measure, const MeasureMarks& mk)
    {
        if (mk.isEmpty()) marks.erase(measure);
        else marks[measure] = mk;
    }
    // Measures in the order they are played (repeats, endings, D.C./D.S., Fine and Coda unrolled).
    std::vector<int> playOrder(int fromMeasure = 0) const;

    std::vector<PartRange> parts() const;
    int measureCount() const { return staves.empty() ? 0 : int(staves.front().measures.size()); }
    TimeSig timeAt(int measure) const;
    int keyAt(int measure) const;
    Frac capacityAt(int measure) const { return timeAt(measure).capacity(); }
    Clef clefAt(int staff, int measure) const { return staves[size_t(staff)].clefAt(measure); }
    bool timeChangesAt(int measure) const { return measure > 0 && timeChanges.count(measure); }
    bool keyChangesAt(int measure) const { return measure > 0 && keyChanges.count(measure); }
    bool clefChangesAt(int staff, int measure) const { return measure > 0 && staves[size_t(staff)].clefChanges.count(measure); }
    // Measure insertion/removal in every staff, keeping the changes on the right measures.
    void insertMeasures(int at, int n);
    void removeMeasures(int first, int last); // inclusive
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
QString pitchName(const Pitch& p);
Pitch pitchFromMidi(int midi, int keyFifths); // spelled with sharps or flats following the key           // e.g. "Fá♯4"
QString durationName(int durLog);            // e.g. "Semínima"
QString clefName(Clef clef);
QString keyName(int fifths);
int tupletNormalFor(int actual);             // 3 -> 2, 5 -> 4, 2 -> 3 (duplet), ...
QString dynamicText(int dynamic);            // "mf"
QString prettyChord(const QString& typed);   // "F#m7b5" -> "F♯m7♭5" for display
int dynamicFromText(const QString& text);    // "mf" -> DynMF (0 if unknown)
int dynamicVelocity(int dynamic);            // MIDI velocity for playback
QString articulationName(unsigned artic);    // "Staccato"
QString articulationKey(unsigned artic);     // "staccato" (file format)
QString jumpText(int jump);                  // "D.S. al Coda" (as printed)
QString jumpKey(int jump);                   // "dsalcoda" (file format)
int jumpFromKey(const QString& key);
QString endingLabel(unsigned endings);       // 0b011 -> "1., 2."
unsigned endingsFromLabel(const QString& s); // "1, 2" / "1.2." -> 0b011

} // namespace pauta
