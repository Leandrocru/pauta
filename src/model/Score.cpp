#include "model/Score.h"

#include <QStringList>

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

Frac Event::baseDuration() const { return durationOf(durLog, dots); }

Frac Event::duration() const
{
    const Frac base = durationOf(durLog, dots);
    return inTuplet() ? base * Frac(tupletNormal, tupletActual) : base;
}

QString prettyChord(const QString& typed)
{
    // '#' always becomes a sharp; 'b' becomes a flat right after a root/bass letter or before a number.
    QString out;
    for (qsizetype i = 0; i < typed.size(); ++i) {
        const QChar c = typed[i];
        const bool afterRoot = i > 0 && QStringLiteral("ABCDEFG").contains(typed[i - 1]) && (i == 1 || typed[i - 2] == u'/');
        const bool beforeNumber = i + 1 < typed.size() && typed[i + 1].isDigit();
        if (c == u'#') out += QChar(0x266F);
        else if (c == u'b' && (afterRoot || beforeNumber)) out += QChar(0x266D);
        else out += c;
    }
    return out;
}

QString dynamicText(int d)
{
    static const char* t[DynCount] = { "", "ppp", "pp", "p", "mp", "mf", "f", "ff", "fff", "sfz", "sf", "fp" };
    return d > 0 && d < DynCount ? QString::fromLatin1(t[d]) : QString();
}

int dynamicFromText(const QString& text)
{
    for (int d = 1; d < DynCount; ++d)
        if (dynamicText(d) == text.trimmed().toLower()) return d;
    return 0;
}

int dynamicVelocity(int d)
{
    static const int v[DynCount] = { 80, 22, 36, 50, 64, 80, 96, 112, 126, 115, 110, 110 };
    return d >= 0 && d < DynCount ? v[d] : 80;
}

QString articulationName(unsigned a)
{
    switch (a) {
    case ArticStaccato: return QStringLiteral("Staccato");
    case ArticAccent: return QStringLiteral("Acento");
    case ArticTenuto: return QStringLiteral("Tenuto");
    case ArticMarcato: return QStringLiteral("Marcato");
    case ArticStaccatissimo: return QStringLiteral("Staccatissimo");
    case ArticFermata: return QStringLiteral("Fermata");
    default: return QString();
    }
}

QString articulationKey(unsigned a)
{
    switch (a) {
    case ArticStaccato: return QStringLiteral("staccato");
    case ArticAccent: return QStringLiteral("accent");
    case ArticTenuto: return QStringLiteral("tenuto");
    case ArticMarcato: return QStringLiteral("marcato");
    case ArticStaccatissimo: return QStringLiteral("staccatissimo");
    case ArticFermata: return QStringLiteral("fermata");
    default: return QString();
    }
}

int tupletNormalFor(int actual)
{
    if (actual == 2) return 3;   // duplet (compound meters)
    if (actual == 4) return 3;   // quadruplet in compound meters
    int n = 1;
    while (n * 2 < actual) n *= 2;
    return n;                    // 3->2, 5..7->4, 9..15->8
}

void Event::sortPitches()
{
    std::sort(pitches.begin(), pitches.end(),
              [](const Pitch& a, const Pitch& b) { return a.diatonic() < b.diatonic(); });
}

Frac Measure::filled(int v) const
{
    Frac t;
    for (const Event& e : voice(v))
        t += e.duration();
    return t;
}

bool Measure::isEmpty() const
{
    for (int v = 0; v < kVoices; ++v)
        if (!voice(v).empty()) return false;
    return true;
}

int Measure::usedVoices() const
{
    int n = 0;
    for (int v = 0; v < kVoices; ++v)
        if (std::any_of(voice(v).begin(), voice(v).end(), [](const Event& e) { return !e.isRest(); })) ++n;
    return n;
}

void Score::appendMeasure()
{
    for (Staff& s : staves)
        s.measures.emplace_back();
}

TimeSig Score::timeAt(int measure) const
{
    TimeSig t = time;
    for (const auto& [m, ts] : timeChanges) {
        if (m > measure) break;
        t = ts;
    }
    return t;
}

int Score::keyAt(int measure) const
{
    int k = keyFifths;
    for (const auto& [m, f] : keyChanges) {
        if (m > measure) break;
        k = f;
    }
    return k;
}

namespace {
// Shifts the keys of a change map: those >= at move by `delta`; those in [dropFrom, dropTo] go away.
template <typename T>
void shiftChanges(std::map<int, T>& changes, int at, int delta, int dropFrom = 1, int dropTo = 0, int minKey = 1)
{
    std::map<int, T> out;
    for (const auto& [m, v] : changes) {
        if (m >= dropFrom && m <= dropTo) continue;
        const int nm = m >= at ? m + delta : m;
        if (nm >= minKey) out[nm] = v;
    }
    changes.swap(out);
}
} // namespace

void Score::insertMeasures(int at, int n)
{
    if (n <= 0) return;
    for (Staff& s : staves) {
        s.measures.insert(s.measures.begin() + std::clamp(at, 0, int(s.measures.size())), size_t(n), Measure{});
        shiftChanges(s.clefChanges, at, n);
    }
    shiftChanges(timeChanges, at, n);
    shiftChanges(keyChanges, at, n);
    shiftChanges(marks, at, n, 1, 0, 0);
}

void Score::removeMeasures(int first, int last)
{
    if (last < first) return;
    const int count = last - first + 1;
    for (Staff& s : staves) {
        s.measures.erase(s.measures.begin() + first, s.measures.begin() + last + 1);
        shiftChanges(s.clefChanges, last + 1, -count, first, last);
    }
    shiftChanges(timeChanges, last + 1, -count, first, last);
    shiftChanges(keyChanges, last + 1, -count, first, last);
    shiftChanges(marks, last + 1, -count, first, last, 0);
}

std::vector<PartRange> Score::parts() const
{
    std::vector<PartRange> out;
    for (int i = 0; i < int(staves.size()); ++i) {
        if (out.empty() || !staves[size_t(i)].name.isEmpty()) {
            PartRange p;
            p.name = staves[size_t(i)].name; // may be empty (an unnamed first staff stays unnamed)
            p.firstStaff = i;
            out.push_back(p);
        } else {
            out.back().staffCount++;
        }
    }
    return out;
}

std::vector<int> Score::playOrder(int fromMeasure) const
{
    const int n = measureCount();
    std::vector<int> order;
    if (n == 0) return order;
    fromMeasure = std::clamp(fromMeasure, 0, n - 1);

    // Highest ending number in the run of bracketed measures around measure i.
    auto lastEndingOfRun = [&](int i) {
        int a = i, b = i;
        while (a > 0 && marksAt(a - 1).endings) --a;
        while (b + 1 < n && marksAt(b + 1).endings) ++b;
        unsigned all = 0;
        for (int k = a; k <= b; ++k) all |= marksAt(k).endings;
        int hi = 0;
        while (all >> hi) ++hi;
        return hi; // 1-based
    };

    std::map<int, int> played; // :| measure -> times its section has been played
    int repeatStart = fromMeasure;
    int pass = 1;
    bool looping = false;      // just jumped back to the start of a repeat
    int jumped = JumpNone;     // D.C./D.S. already taken (repeats are not taken again after it)
    int i = fromMeasure;
    while (i < n && order.size() < 100000) {
        const MeasureMarks mk = marksAt(i);
        if (mk.repeatStart && !looping && !jumped) {
            repeatStart = i;
            pass = 1;
        }
        looping = false;
        if (mk.endings) {
            const int wanted = jumped ? lastEndingOfRun(i) : pass;
            if (wanted < 1 || wanted > 32 || !(mk.endings & (1u << (wanted - 1)))) {
                ++i;
                continue;
            }
        }
        order.push_back(i);

        const bool alFine = jumped == JumpDCalFine || jumped == JumpDSalFine;
        const bool alCoda = jumped == JumpDCalCoda || jumped == JumpDSalCoda;
        if (alFine && mk.jump == JumpFine) break;
        if (alCoda && mk.jump == JumpToCoda) {
            int c = i + 1;
            while (c < n && !marksAt(c).coda) ++c;
            if (c >= n) break;
            i = c;
            continue;
        }
        if (mk.repeatEnd && !jumped) {
            int& times = played[i];
            ++times;
            if (times < std::max(2, mk.repeatTimes)) {
                pass = times + 1;
                i = repeatStart;
                looping = true;
                continue;
            }
            pass = 1;
            repeatStart = i + 1;
        }
        if (!jumped && mk.jump >= JumpDC && mk.jump <= JumpDSalCoda) {
            jumped = mk.jump;
            int target = 0;
            if (mk.jump >= JumpDS) {
                for (int k = 0; k < n; ++k)
                    if (marksAt(k).segno) { target = k; break; }
            }
            i = target;
            continue;
        }
        ++i;
    }
    return order;
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

namespace {
struct JumpInfo { const char* key; const char* text; };
const JumpInfo kJumps[JumpCount] = {
    { "", "" }, { "fine", "Fine" }, { "dc", "D.C." }, { "dcalfine", "D.C. al Fine" }, { "dcalcoda", "D.C. al Coda" },
    { "ds", "D.S." }, { "dsalfine", "D.S. al Fine" }, { "dsalcoda", "D.S. al Coda" }, { "tocoda", "To Coda" },
};
} // namespace

QString jumpText(int jump)
{
    return jump > 0 && jump < JumpCount ? QString::fromUtf8(kJumps[jump].text) : QString();
}

QString jumpKey(int jump)
{
    return jump > 0 && jump < JumpCount ? QString::fromLatin1(kJumps[jump].key) : QString();
}

int jumpFromKey(const QString& key)
{
    for (int j = 1; j < JumpCount; ++j)
        if (key == QLatin1String(kJumps[j].key)) return j;
    return JumpNone;
}

QString endingLabel(unsigned endings)
{
    QStringList parts;
    for (int k = 0; k < 32; ++k)
        if (endings & (1u << k)) parts << QStringLiteral("%1.").arg(k + 1);
    return parts.join(QStringLiteral(", "));
}

unsigned endingsFromLabel(const QString& s)
{
    unsigned out = 0;
    QString digits;
    auto flush = [&] {
        const int k = digits.toInt();
        if (k >= 1 && k <= 32) out |= 1u << (k - 1);
        digits.clear();
    };
    for (QChar c : s) {
        if (c.isDigit()) digits += c;
        else flush();
    }
    flush();
    return out;
}

} // namespace pauta
