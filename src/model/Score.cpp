#include "model/Score.h"

#include <algorithm>

namespace pauta {

Frac durationOf(int durLog, int dots)
{
    Frac base = durLog >= 0 ? Frac(1, int64_t(1) << durLog) : Frac(2, 1);
    Frac total = base;
    Frac add = base;
    for (int i = 0; i < dots; ++i) {
        add = add / 2;
        total += add;
    }
    return total;
}

Frac Event::duration() const { return durationOf(durLog, dots); }

void Event::sortPitches()
{
    std::sort(pitches.begin(), pitches.end(),
              [](const Pitch& a, const Pitch& b) { return a.diatonic() < b.diatonic(); });
}

Frac Measure::filled() const
{
    Frac t;
    for (const Event& e : events)
        t += e.duration();
    return t;
}

void Score::appendMeasure()
{
    for (Staff& s : staves)
        s.measures.emplace_back();
}

void Score::setMeasureCount(int n)
{
    for (Staff& s : staves)
        s.measures.resize(size_t(std::max(n, 1)));
}

Score Score::makeEmpty(const QString& title, const std::vector<std::pair<QString, Clef>>& staves,
                       TimeSig time, int keyFifths, int measures)
{
    Score s;
    s.title = title;
    s.time = time;
    s.keyFifths = keyFifths;
    for (const auto& [name, clef] : staves) {
        Staff st;
        st.name = name;
        st.clef = clef;
        s.staves.push_back(st);
    }
    s.setMeasureCount(measures);
    return s;
}

Score Score::makeDemo()
{
    Score s = makeEmpty(QStringLiteral("Estudo em Sol"),
                        { { QStringLiteral("Piano"), Clef::Treble }, { QString(), Clef::Bass } },
                        TimeSig{ 3, 4 }, 1, 8);
    s.composer = QStringLiteral("Pauta (demonstração)");

    auto note = [](int durLog, std::vector<Pitch> p, int dots = 0) {
        Event e;
        e.durLog = durLog;
        e.dots = dots;
        e.pitches = std::move(p);
        e.sortPitches();
        return e;
    };
    auto rest = [](int durLog) { Event e; e.durLog = durLog; return e; };
    const Pitch G4{ 4, 4 }, A4{ 5, 4 }, B4{ 6, 4 }, C5{ 0, 5 }, D5{ 1, 5 }, E5{ 2, 5 }, Fs5{ 3, 5, 1 },
        G5{ 4, 5 }, G3{ 4, 3 }, D3{ 1, 3 }, B2{ 6, 2 }, C3{ 0, 3 }, A2{ 5, 2 }, Fs4{ 3, 4, 1 }, G2{ 4, 2 };

    auto& rh = s.staves[0].measures;
    auto& lh = s.staves[1].measures;
    rh[0].events = { note(2, { D5 }), note(3, { G4 }), note(3, { A4 }), note(3, { B4 }), note(3, { C5 }) };
    rh[1].events = { note(2, { D5 }), note(2, { G4 }), note(2, { G4 }) };
    rh[2].events = { note(2, { E5 }), note(3, { C5 }), note(3, { D5 }), note(3, { E5 }), note(3, { Fs5 }) };
    rh[3].events = { note(2, { G5 }), note(2, { G4 }), note(2, { G4 }) };
    rh[4].events = { note(2, { C5, E5 }), note(3, { D5 }), note(3, { C5 }), note(3, { B4 }), note(3, { A4 }) };
    rh[5].events = { note(2, { B4, D5 }), note(3, { C5 }), note(3, { B4 }), note(3, { A4 }), note(3, { G4 }) };
    rh[6].events = { note(2, { A4 }), note(3, { B4 }), note(3, { A4 }), note(3, { G4 }), note(3, { Fs4 }) };
    rh[7].events = { note(1, { G4, B4, D5 }, 1) };

    lh[0].events = { note(1, { G3, B2, D3 }), note(2, { A2 }) };
    lh[1].events = { note(1, { B2 }, 1) };
    lh[2].events = { note(1, { C3 }, 1) };
    lh[3].events = { note(1, { B2 }, 1) };
    lh[4].events = { note(1, { A2 }, 1) };
    lh[5].events = { note(1, { G2 }), rest(2) };
    lh[6].events = { note(1, { C3 }), note(2, { D3 }) };
    lh[7].events = { note(1, { G2, D3 }, 1) };
    return s;
}

int clefMiddleLine(Clef clef)
{
    switch (clef) {
    case Clef::Treble: return 4 * 7 + 6; // B4
    case Clef::Bass:   return 3 * 7 + 1; // D3
    case Clef::Alto:   return 4 * 7 + 0; // C4
    case Clef::Tenor:  return 3 * 7 + 5; // A3
    }
    return 4 * 7 + 6;
}

int staffPosition(int diatonic, Clef clef) { return diatonic - clefMiddleLine(clef); }

int keyAlterFor(int step, int keyFifths)
{
    static const int sharpOrder[7] = { 3, 0, 4, 1, 5, 2, 6 }; // F C G D A E B
    static const int flatOrder[7] = { 6, 2, 5, 1, 4, 0, 3 };  // B E A D G C F
    if (keyFifths > 0) {
        for (int i = 0; i < keyFifths && i < 7; ++i)
            if (sharpOrder[i] == step) return 1;
    } else if (keyFifths < 0) {
        for (int i = 0; i < -keyFifths && i < 7; ++i)
            if (flatOrder[i] == step) return -1;
    }
    return 0;
}

QString pitchName(const Pitch& p)
{
    static const char* names[7] = { "Dó", "Ré", "Mi", "Fá", "Sol", "Lá", "Si" };
    QString acc;
    switch (p.alter) {
    case -2: acc = QStringLiteral("𝄫"); break;
    case -1: acc = QStringLiteral("♭"); break;
    case 1:  acc = QStringLiteral("♯"); break;
    case 2:  acc = QStringLiteral("𝄪"); break;
    default: break;
    }
    return QString::fromUtf8(names[p.step]) + acc + QString::number(p.octave);
}

QString durationName(int durLog)
{
    switch (durLog) {
    case -1: return QStringLiteral("Breve");
    case 0:  return QStringLiteral("Semibreve");
    case 1:  return QStringLiteral("Mínima");
    case 2:  return QStringLiteral("Semínima");
    case 3:  return QStringLiteral("Colcheia");
    case 4:  return QStringLiteral("Semicolcheia");
    case 5:  return QStringLiteral("Fusa");
    case 6:  return QStringLiteral("Semifusa");
    }
    return QString();
}

QString keyName(int fifths)
{
    static const char* majors[15] = { "Dó♭", "Sol♭", "Ré♭", "Lá♭", "Mi♭", "Si♭", "Fá", "Dó", "Sol", "Ré", "Lá", "Mi", "Si", "Fá♯", "Dó♯" };
    static const char* minors[15] = { "lá♭", "mi♭", "si♭", "fá", "dó", "sol", "ré", "lá", "mi", "si", "fá♯", "dó♯", "sol♯", "ré♯", "lá♯" };
    fifths = std::clamp(fifths, -7, 7);
    const QString acc = fifths == 0 ? QStringLiteral("sem acidentes")
                                    : QStringLiteral("%1 %2").arg(std::abs(fifths)).arg(fifths > 0 ? QStringLiteral("♯") : QStringLiteral("♭"));
    return QStringLiteral("%1 maior / %2 menor (%3)")
        .arg(QString::fromUtf8(majors[fifths + 7]), QString::fromUtf8(minors[fifths + 7]), acc);
}

QString clefName(Clef clef)
{
    switch (clef) {
    case Clef::Treble: return QStringLiteral("Clave de Sol");
    case Clef::Bass:   return QStringLiteral("Clave de Fá");
    case Clef::Alto:   return QStringLiteral("Clave de Dó (3ª)");
    case Clef::Tenor:  return QStringLiteral("Clave de Dó (4ª)");
    }
    return QString();
}

} // namespace pauta
