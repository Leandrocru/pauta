// MusicXML 4.0 export (score-partwise), plain .musicxml or compressed .mxl.
#include "io/MusicXml.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QFileInfo>
#include <QSaveFile>
#include <QXmlStreamWriter>

#ifndef PAUTA_NO_MXL
#include <private/qzipwriter_p.h>
#endif

#include <algorithm>
#include <map>
#include <numeric>
#include <set>

namespace pauta::io {

namespace {

const char* typeName(int durLog)
{
    switch (durLog) {
    case -1: return "breve";
    case 0: return "whole";
    case 1: return "half";
    case 2: return "quarter";
    case 3: return "eighth";
    case 4: return "16th";
    case 5: return "32nd";
    default: return "64th";
    }
}

// Chord-symbol suffix <-> MusicXML <kind> value (the typed suffix also travels in kind@text).
const std::pair<const char*, const char*> kChordKinds[] = {
    { "", "major" }, { "m", "minor" }, { "7", "dominant" }, { "maj7", "major-seventh" }, { "m7", "minor-seventh" },
    { "dim", "diminished" }, { "dim7", "diminished-seventh" }, { "aug", "augmented" }, { "+", "augmented" },
    { "m7b5", "half-diminished" }, { "sus4", "suspended-fourth" }, { "sus2", "suspended-second" },
    { "6", "major-sixth" }, { "m6", "minor-sixth" }, { "9", "dominant-ninth" }, { "maj9", "major-ninth" },
    { "m9", "minor-ninth" }, { "11", "dominant-11th" }, { "13", "dominant-13th" }, { "5", "power" },
    { "mmaj7", "major-minor" }, { "7+", "augmented-seventh" },
};

// "F#m7b5" / "G/B" -> <harmony>. Returns false for text that is not a chord (nothing written).
bool writeHarmony(QXmlStreamWriter& w, const QString& chord, const QString& staffNo, bool multiStaff)
{
    auto parseNote = [](const QString& t, qsizetype& i, QString& step, int& alter) {
        if (i >= t.size() || !QStringLiteral("ABCDEFG").contains(t[i])) return false;
        step = t[i++];
        alter = 0;
        while (i < t.size() && (t[i] == u'#' || t[i] == u'b')) alter += t[i++] == u'#' ? 1 : -1;
        return true;
    };
    const qsizetype slash = chord.indexOf(QLatin1Char('/'));
    const QString main = slash >= 0 ? chord.left(slash) : chord;
    qsizetype i = 0;
    QString step;
    int alter = 0;
    if (!parseNote(main, i, step, alter)) return false;
    const QString suffix = main.mid(i);
    QString bassStep;
    int bassAlter = 0;
    if (slash >= 0) {
        qsizetype j = 0;
        if (!parseNote(chord.mid(slash + 1), j, bassStep, bassAlter)) return false;
    }
    QString kind = QStringLiteral("other");
    for (const auto& [suf, name] : kChordKinds)
        if (suffix == QLatin1String(suf)) { kind = QLatin1String(name); break; }

    w.writeStartElement("harmony");
    w.writeStartElement("root");
    w.writeTextElement("root-step", step);
    if (alter) w.writeTextElement("root-alter", QString::number(alter));
    w.writeEndElement();
    w.writeStartElement("kind");
    w.writeAttribute("text", suffix);
    w.writeCharacters(kind);
    w.writeEndElement();
    if (!bassStep.isEmpty()) {
        w.writeStartElement("bass");
        w.writeTextElement("bass-step", bassStep);
        if (bassAlter) w.writeTextElement("bass-alter", QString::number(bassAlter));
        w.writeEndElement();
    }
    if (multiStaff) w.writeTextElement("staff", staffNo);
    w.writeEndElement();
    return true;
}

// A part = a named staff plus the unnamed staves right after it (e.g. piano treble + bass).
struct Part {
    QString name;
    int firstStaff = 0;
    int staffCount = 1;
};

std::vector<Part> groupParts(const Score& s)
{
    std::vector<Part> parts;
    for (int i = 0; i < int(s.staves.size()); ++i) {
        if (parts.empty() || !s.staves[size_t(i)].name.isEmpty()) {
            Part p;
            p.name = s.staves[size_t(i)].name; // may be empty (an unnamed staff stays unnamed)
            p.firstStaff = i;
            parts.push_back(p);
        } else {
            parts.back().staffCount++;
        }
    }
    return parts;
}

// Divisions per quarter note so that every duration is a whole number.
int64_t divisionsFor(const Score& s)
{
    int64_t div = 1;
    auto need = [&](Frac whole) {
        const Frac q = whole * Frac(4);
        div = std::lcm(div, q.den);
    };
    need(s.time.capacity());
    for (const auto& [m, t] : s.timeChanges) need(t.capacity());
    for (const Staff& st : s.staves)
        for (const Measure& m : st.measures)
            for (int v = 0; v < kVoices; ++v)
                for (const Event& e : m.voice(v)) need(e.duration());
    return std::min<int64_t>(div, 1 << 20);
}

void clefElement(QXmlStreamWriter& w, Clef c, int number)
{
    w.writeStartElement("clef");
    w.writeAttribute("number", QString::number(number));
    switch (c) {
    case Clef::Treble: w.writeTextElement("sign", "G"); w.writeTextElement("line", "2"); break;
    case Clef::Bass: w.writeTextElement("sign", "F"); w.writeTextElement("line", "4"); break;
    case Clef::Alto: w.writeTextElement("sign", "C"); w.writeTextElement("line", "3"); break;
    case Clef::Tenor: w.writeTextElement("sign", "C"); w.writeTextElement("line", "4"); break;
    }
    w.writeEndElement();
}

// Beam level of an event: 0 (none), 1 eighth, 2 sixteenth, ...
int beamLevels(const Event& e) { return e.isRest() ? 0 : std::max(0, e.durLog - 2); }

// Beam groups with the same rule as the engraver: consecutive eighth-or-shorter notes inside one
// beaming unit (dotted quarter in compound meters, quarter otherwise).
std::vector<int> beamGroups(const std::vector<Event>& evs, const TimeSig& t)
{
    const Frac unit = (t.beatType >= 8 && t.beats % 3 == 0) ? Frac(3, t.beatType) : Frac(1, 4);
    std::vector<int> group(evs.size(), -1);
    Frac pos;
    int g = 0;
    int64_t curUnit = -1;
    std::vector<size_t> cur;
    auto flush = [&] {
        if (cur.size() >= 2)
            for (size_t i : cur) group[i] = g;
        if (!cur.empty()) ++g;
        cur.clear();
    };
    for (size_t k = 0; k < evs.size(); ++k) {
        const Event& e = evs[k];
        const int64_t u = (pos.num * unit.den) / (pos.den * unit.num);
        const Frac end = pos + e.duration();
        const bool beamable = beamLevels(e) > 0;
        const bool fits = end <= Frac(u + 1) * unit;
        if (!beamable || !fits || (u != curUnit && !cur.empty())) flush();
        if (beamable && fits) {
            curUnit = u;
            cur.push_back(k);
        }
        pos = end;
    }
    flush();
    return group;
}

class Writer {
public:
    Writer(const Score& s, QXmlStreamWriter& w) : m_s(s), m_w(w), m_div(divisionsFor(s)) {}
    void write();

private:
    void writePart(const Part& p, int partIndex);
    void writeLayer(const Part& p, int staffInPart, int voice, int measure, int64_t& written);
    int64_t dur(Frac whole) const { return (whole * Frac(4 * m_div)).num; }

    const Score& m_s;
    QXmlStreamWriter& m_w;
    int64_t m_div;
    // Per staff and layer: pitches of the previous event that carried a tie (to write the "stop").
    std::map<std::pair<int, int>, std::set<int>> m_tiedFrom;
    std::map<std::pair<std::pair<int, int>, int>, bool> m_prevHyphen; // (staff, layer), verse -> last syllable had a hyphen
};

void Writer::write()
{
    m_w.writeStartElement("score-partwise");
    m_w.writeAttribute("version", "4.0");
    if (!m_s.title.isEmpty()) {
        m_w.writeStartElement("work");
        m_w.writeTextElement("work-title", m_s.title);
        m_w.writeEndElement();
    }
    m_w.writeStartElement("identification");
    if (!m_s.composer.isEmpty()) {
        m_w.writeStartElement("creator");
        m_w.writeAttribute("type", "composer");
        m_w.writeCharacters(m_s.composer);
        m_w.writeEndElement();
    }
    m_w.writeStartElement("encoding");
    m_w.writeTextElement("software", QStringLiteral("Pauta %1").arg(QCoreApplication::applicationVersion()));
    m_w.writeEndElement();
    m_w.writeEndElement(); // identification

    const std::vector<Part> parts = groupParts(m_s);
    m_w.writeStartElement("part-list");
    for (size_t i = 0; i < parts.size(); ++i) {
        m_w.writeStartElement("score-part");
        m_w.writeAttribute("id", QStringLiteral("P%1").arg(i + 1));
        m_w.writeTextElement("part-name", parts[i].name);
        m_w.writeEndElement();
    }
    m_w.writeEndElement();
    for (size_t i = 0; i < parts.size(); ++i) writePart(parts[i], int(i));
    m_w.writeEndElement(); // score-partwise
}

void Writer::writePart(const Part& p, int partIndex)
{
    m_w.writeStartElement("part");
    m_w.writeAttribute("id", QStringLiteral("P%1").arg(partIndex + 1));
    for (int m = 0; m < m_s.measureCount(); ++m) {
        m_w.writeStartElement("measure");
        m_w.writeAttribute("number", QString::number(m + 1));
        if (m > 0) {
            // Changes in the middle of the piece.
            bool clefChange = false;
            for (int k = 0; k < p.staffCount; ++k) clefChange |= m_s.clefChangesAt(p.firstStaff + k, m);
            if (m_s.keyChangesAt(m) || m_s.timeChangesAt(m) || clefChange) {
                m_w.writeStartElement("attributes");
                if (m_s.keyChangesAt(m)) {
                    m_w.writeStartElement("key");
                    m_w.writeTextElement("fifths", QString::number(m_s.keyAt(m)));
                    m_w.writeEndElement();
                }
                if (m_s.timeChangesAt(m)) {
                    m_w.writeStartElement("time");
                    m_w.writeTextElement("beats", QString::number(m_s.timeAt(m).beats));
                    m_w.writeTextElement("beat-type", QString::number(m_s.timeAt(m).beatType));
                    m_w.writeEndElement();
                }
                for (int k = 0; k < p.staffCount; ++k)
                    if (m_s.clefChangesAt(p.firstStaff + k, m)) clefElement(m_w, m_s.clefAt(p.firstStaff + k, m), k + 1);
                m_w.writeEndElement();
            }
        }
        if (m == 0) {
            m_w.writeStartElement("attributes");
            m_w.writeTextElement("divisions", QString::number(m_div));
            m_w.writeStartElement("key");
            m_w.writeTextElement("fifths", QString::number(m_s.keyFifths));
            m_w.writeEndElement();
            m_w.writeStartElement("time");
            m_w.writeTextElement("beats", QString::number(m_s.time.beats));
            m_w.writeTextElement("beat-type", QString::number(m_s.time.beatType));
            m_w.writeEndElement();
            if (p.staffCount > 1) m_w.writeTextElement("staves", QString::number(p.staffCount));
            for (int k = 0; k < p.staffCount; ++k) clefElement(m_w, m_s.staves[size_t(p.firstStaff + k)].clef, k + 1);
            m_w.writeEndElement(); // attributes
            if (partIndex == 0) {
                // Tempo (shown as a metronome mark, used for playback by the importer).
                m_w.writeStartElement("direction");
                m_w.writeAttribute("placement", "above");
                m_w.writeStartElement("direction-type");
                m_w.writeStartElement("metronome");
                m_w.writeTextElement("beat-unit", "quarter");
                m_w.writeTextElement("per-minute", QString::number(m_s.tempo));
                m_w.writeEndElement();
                m_w.writeEndElement();
                m_w.writeEmptyElement("sound");
                m_w.writeAttribute("tempo", QString::number(m_s.tempo));
                m_w.writeEndElement(); // direction
            }
        }
        // Every staff and layer of the part, going back in time (<backup>) between them.
        int64_t written = 0;
        for (int k = 0; k < p.staffCount; ++k)
            for (int v = 0; v < kVoices; ++v) writeLayer(p, k, v, m, written);
        m_w.writeEndElement(); // measure
    }
    m_w.writeEndElement(); // part
}

void Writer::writeLayer(const Part& p, int staffInPart, int voice, int m, int64_t& written)
{
    const int staffIdx = p.firstStaff + staffInPart;
    const Measure& meas = m_s.staves[size_t(staffIdx)].measures[size_t(m)];
    const std::vector<Event>& evs = meas.voice(voice);
    const bool wholeRest = voice == 0 && meas.isEmpty();
    if (evs.empty() && !wholeRest) return;

    if (written > 0) {
        m_w.writeStartElement("backup");
        m_w.writeTextElement("duration", QString::number(written));
        m_w.writeEndElement();
    }
    written = 0;
    const QString voiceNo = QString::number(staffInPart * kVoices + voice + 1);
    const QString staffNo = QString::number(staffInPart + 1);

    auto direction = [&](auto body) {
        m_w.writeStartElement("direction");
        m_w.writeAttribute("placement", "below");
        m_w.writeStartElement("direction-type");
        body();
        m_w.writeEndElement();
        m_w.writeTextElement("voice", voiceNo);
        if (p.staffCount > 1) m_w.writeTextElement("staff", staffNo);
        m_w.writeEndElement();
    };

    if (wholeRest) {
        m_w.writeStartElement("note");
        m_w.writeEmptyElement("rest");
        m_w.writeAttribute("measure", "yes");
        const int64_t d = dur(m_s.capacityAt(m));
        m_w.writeTextElement("duration", QString::number(d));
        m_w.writeTextElement("voice", voiceNo);
        if (p.staffCount > 1) m_w.writeTextElement("staff", staffNo);
        m_w.writeEndElement();
        written = d;
        m_tiedFrom[{ staffIdx, voice }].clear();
        return;
    }

    const std::vector<int> beams = beamGroups(evs, m_s.timeAt(m));
    auto& tiedFrom = m_tiedFrom[{ staffIdx, voice }];
    for (size_t i = 0; i < evs.size(); ++i) {
        const Event& e = evs[i];
        const bool tupEnd = e.inTuplet() && (i + 1 == evs.size() || !evs[i + 1].inTuplet() || evs[i + 1].tupletStart
                                             || evs[i + 1].tupletActual != e.tupletActual);
        if (e.dynamic)
            direction([&] {
                m_w.writeStartElement("dynamics");
                m_w.writeEmptyElement(dynamicText(e.dynamic));
                m_w.writeEndElement();
            });
        if (!e.chordSymbol.isEmpty()) writeHarmony(m_w, e.chordSymbol, staffNo, p.staffCount > 1);
        if (e.hairpin)
            direction([&] {
                m_w.writeEmptyElement("wedge");
                m_w.writeAttribute("type", e.hairpin == HairpinCresc ? "crescendo" : "diminuendo");
            });

        const int64_t d = dur(e.duration());
        std::set<int> nextTied;
        const size_t n = std::max<size_t>(1, e.pitches.size());
        for (size_t k = 0; k < n; ++k) {
            const bool rest = e.isRest();
            const Pitch* pt = rest ? nullptr : &e.pitches[k];
            const bool tieStop = pt && tiedFrom.count(pt->diatonic());
            const bool tieStart = pt && pt->tie;
            if (tieStart) nextTied.insert(pt->diatonic());

            m_w.writeStartElement("note");
            if (k > 0) m_w.writeEmptyElement("chord");
            if (rest) {
                m_w.writeEmptyElement("rest");
            } else {
                m_w.writeStartElement("pitch");
                m_w.writeTextElement("step", QString(QChar::fromLatin1("CDEFGAB"[pt->step])));
                if (pt->alter) m_w.writeTextElement("alter", QString::number(pt->alter));
                m_w.writeTextElement("octave", QString::number(pt->octave));
                m_w.writeEndElement();
            }
            m_w.writeTextElement("duration", QString::number(d));
            if (tieStop) { m_w.writeEmptyElement("tie"); m_w.writeAttribute("type", "stop"); }
            if (tieStart) { m_w.writeEmptyElement("tie"); m_w.writeAttribute("type", "start"); }
            m_w.writeTextElement("voice", voiceNo);
            m_w.writeTextElement("type", typeName(e.durLog));
            for (int dt = 0; dt < e.dots; ++dt) m_w.writeEmptyElement("dot");
            if (e.inTuplet()) {
                m_w.writeStartElement("time-modification");
                m_w.writeTextElement("actual-notes", QString::number(e.tupletActual));
                m_w.writeTextElement("normal-notes", QString::number(e.tupletNormal));
                m_w.writeEndElement();
            }
            if (p.staffCount > 1) m_w.writeTextElement("staff", staffNo);

            // Beams (written on the first note of a chord only).
            if (k == 0 && beams[i] >= 0) {
                const int lv = beamLevels(e);
                const bool prevIn = i > 0 && beams[i - 1] == beams[i];
                const bool nextIn = i + 1 < evs.size() && beams[i + 1] == beams[i];
                for (int L = 1; L <= lv; ++L) {
                    const bool prevL = prevIn && beamLevels(evs[i - 1]) >= L;
                    const bool nextL = nextIn && beamLevels(evs[i + 1]) >= L;
                    const char* kind = prevL && nextL ? "continue" : prevL ? "end" : nextL ? "begin"
                                     : (nextIn ? "forward hook" : "backward hook");
                    if (L == 1) kind = !prevIn ? "begin" : !nextIn ? "end" : "continue";
                    m_w.writeStartElement("beam");
                    m_w.writeAttribute("number", QString::number(L));
                    m_w.writeCharacters(QString::fromLatin1(kind));
                    m_w.writeEndElement();
                }
            }

            // Notations.
            const bool first = k == 0;
            const unsigned artics = first ? (e.articulations & ~unsigned(ArticFermata)) : 0;
            const bool any = tieStop || tieStart || (first && (e.slurStart || e.slurEnd || e.tupletStart || tupEnd
                                                               || artics || (e.articulations & ArticFermata)));
            if (any) {
                m_w.writeStartElement("notations");
                if (tieStop) { m_w.writeEmptyElement("tied"); m_w.writeAttribute("type", "stop"); }
                if (tieStart) { m_w.writeEmptyElement("tied"); m_w.writeAttribute("type", "start"); }
                if (first && e.slurEnd) { m_w.writeEmptyElement("slur"); m_w.writeAttribute("type", "stop"); m_w.writeAttribute("number", "1"); }
                if (first && e.slurStart) { m_w.writeEmptyElement("slur"); m_w.writeAttribute("type", "start"); m_w.writeAttribute("number", "1"); }
                if (first && e.tupletStart) { m_w.writeEmptyElement("tuplet"); m_w.writeAttribute("type", "start"); }
                if (first && tupEnd) { m_w.writeEmptyElement("tuplet"); m_w.writeAttribute("type", "stop"); }
                if (artics) {
                    m_w.writeStartElement("articulations");
                    if (artics & ArticAccent) m_w.writeEmptyElement("accent");
                    if (artics & ArticMarcato) m_w.writeEmptyElement("strong-accent");
                    if (artics & ArticStaccato) m_w.writeEmptyElement("staccato");
                    if (artics & ArticTenuto) m_w.writeEmptyElement("tenuto");
                    if (artics & ArticStaccatissimo) m_w.writeEmptyElement("staccatissimo");
                    m_w.writeEndElement();
                }
                if (first && (e.articulations & ArticFermata)) m_w.writeEmptyElement("fermata");
                m_w.writeEndElement();
            }
            // Lyrics (on the first note of a chord). syllabic follows from the hyphens.
            if (k == 0 && !rest) {
                for (size_t verse = 0; verse < e.lyrics.size(); ++verse) {
                    const Syllable& sy = e.lyrics[verse];
                    if (sy.text.isEmpty()) continue;
                    bool& prevHyphen = m_prevHyphen[{ { staffIdx, voice }, int(verse) }];
                    const char* syl = prevHyphen ? (sy.hyphen ? "middle" : "end") : (sy.hyphen ? "begin" : "single");
                    prevHyphen = sy.hyphen;
                    m_w.writeStartElement("lyric");
                    m_w.writeAttribute("number", QString::number(verse + 1));
                    m_w.writeTextElement("syllabic", QString::fromLatin1(syl));
                    m_w.writeTextElement("text", sy.text);
                    if (sy.extender) m_w.writeEmptyElement("extend");
                    m_w.writeEndElement();
                }
            }
            m_w.writeEndElement(); // note
        }
        tiedFrom = nextTied;
        written += d;

        if (e.hairpinEnd)
            direction([&] {
                m_w.writeEmptyElement("wedge");
                m_w.writeAttribute("type", "stop");
            });
    }
}

} // namespace

bool exportMusicXml(const Score& score, const QString& path, QString* error)
{
    QByteArray xml;
    {
        QBuffer buf(&xml);
        buf.open(QIODevice::WriteOnly);
        QXmlStreamWriter w(&buf);
        w.setAutoFormatting(true);
        w.setAutoFormattingIndent(2);
        w.writeStartDocument();
        w.writeDTD(QStringLiteral("<!DOCTYPE score-partwise PUBLIC \"-//Recordare//DTD MusicXML 4.0 Partwise//EN\" "
                                  "\"http://www.musicxml.org/dtds/partwise.dtd\">"));
        Writer(score, w).write();
        w.writeEndDocument();
    }

    if (path.endsWith(QStringLiteral(".mxl"), Qt::CaseInsensitive)) {
#ifdef PAUTA_NO_MXL
        if (error) *error = QStringLiteral("Esta versão não grava .mxl; use .musicxml.");
        return false;
#else
        // Compressed MusicXML: a zip with the score and META-INF/container.xml pointing to it.
        const QString inner = QFileInfo(path).completeBaseName() + QStringLiteral(".musicxml");
        QByteArray zipped;
        {
            QBuffer zbuf(&zipped);
            zbuf.open(QIODevice::WriteOnly);
            QZipWriter zip(&zbuf);
            zip.setCompressionPolicy(QZipWriter::NeverCompress);
            zip.addFile(QStringLiteral("mimetype"), QByteArrayLiteral("application/vnd.recordare.musicxml"));
            zip.setCompressionPolicy(QZipWriter::AlwaysCompress);
            zip.addFile(QStringLiteral("META-INF/container.xml"),
                        QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<container><rootfiles>"
                                       "<rootfile full-path=\"%1\" media-type=\"application/vnd.recordare.musicxml+xml\"/>"
                                       "</rootfiles></container>\n").arg(inner).toUtf8());
            zip.addFile(inner, xml);
            zip.close();
        }
        xml = zipped;
#endif
    }

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(xml) != xml.size() || !f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

} // namespace pauta::io
