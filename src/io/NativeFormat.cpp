#include "io/NativeFormat.h"

#include <QFile>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <algorithm>

namespace pauta::io {

namespace {

const char* clefKey(Clef c)
{
    switch (c) {
    case Clef::Treble: return "treble";
    case Clef::Bass:   return "bass";
    case Clef::Alto:   return "alto";
    case Clef::Tenor:  return "tenor";
    }
    return "treble";
}

Clef clefFromKey(QStringView s)
{
    if (s == u"bass") return Clef::Bass;
    if (s == u"alto") return Clef::Alto;
    if (s == u"tenor") return Clef::Tenor;
    return Clef::Treble;
}

int attrInt(const QXmlStreamAttributes& a, QStringView name, int def)
{
    bool ok = false;
    const int v = a.value(name).toInt(&ok);
    return ok ? v : def;
}

} // namespace

bool saveNative(const Score& score, const QString& path, QString* error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    QXmlStreamWriter w(&file);
    w.setAutoFormatting(true);
    w.setAutoFormattingIndent(1);
    w.writeStartDocument();
    w.writeStartElement("pauta");
    w.writeAttribute("version", "1");
    w.writeStartElement("score");
    w.writeTextElement("title", score.title);
    w.writeTextElement("composer", score.composer);
    w.writeEmptyElement("time");
    w.writeAttribute("beats", QString::number(score.time.beats));
    w.writeAttribute("beatType", QString::number(score.time.beatType));
    w.writeEmptyElement("key");
    w.writeAttribute("fifths", QString::number(score.keyFifths));
    for (const Staff& st : score.staves) {
        w.writeStartElement("staff");
        w.writeAttribute("name", st.name);
        w.writeAttribute("clef", clefKey(st.clef));
        for (const Measure& m : st.measures) {
            w.writeStartElement("measure");
            for (const Event& e : m.events) {
                w.writeStartElement(e.isRest() ? "rest" : "chord");
                w.writeAttribute("dur", QString::number(e.durLog));
                if (e.dots) w.writeAttribute("dots", QString::number(e.dots));
                for (const Pitch& p : e.pitches) {
                    w.writeEmptyElement("note");
                    w.writeAttribute("step", QString(QChar::fromLatin1("CDEFGAB"[p.step])));
                    w.writeAttribute("oct", QString::number(p.octave));
                    if (p.alter) w.writeAttribute("alter", QString::number(p.alter));
                }
                w.writeEndElement();
            }
            w.writeEndElement();
        }
        w.writeEndElement();
    }
    w.writeEndElement(); // score
    w.writeEndElement(); // pauta
    w.writeEndDocument();
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

bool loadNative(Score& out, const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    QXmlStreamReader r(&file);
    Score s;
    Staff* staff = nullptr;
    Measure* measure = nullptr;
    Event* event = nullptr;
    bool sawRoot = false;

    while (!r.atEnd()) {
        const auto tok = r.readNext();
        if (tok == QXmlStreamReader::StartElement) {
            const auto n = r.name();
            const auto a = r.attributes();
            if (n == u"pauta") {
                sawRoot = true;
                if (attrInt(a, u"version", 1) > 1) {
                    if (error) *error = QStringLiteral("Arquivo criado por uma versão mais nova do programa.");
                    return false;
                }
            } else if (n == u"title") {
                s.title = r.readElementText();
            } else if (n == u"composer") {
                s.composer = r.readElementText();
            } else if (n == u"time") {
                s.time.beats = qBound(1, attrInt(a, u"beats", 4), 64);
                s.time.beatType = qBound(1, attrInt(a, u"beatType", 4), 64);
            } else if (n == u"key") {
                s.keyFifths = qBound(-7, attrInt(a, u"fifths", 0), 7);
            } else if (n == u"staff") {
                s.staves.emplace_back();
                staff = &s.staves.back();
                staff->name = a.value(u"name").toString();
                staff->clef = clefFromKey(a.value(u"clef"));
            } else if (n == u"measure" && staff) {
                staff->measures.emplace_back();
                measure = &staff->measures.back();
            } else if ((n == u"chord" || n == u"rest") && measure) {
                measure->events.emplace_back();
                event = &measure->events.back();
                event->durLog = qBound(-1, attrInt(a, u"dur", 2), 6);
                event->dots = qBound(0, attrInt(a, u"dots", 0), 3);
            } else if (n == u"note" && event) {
                Pitch p;
                const QString step = a.value(u"step").toString();
                const int idx = step.isEmpty() ? -1 : QStringLiteral("CDEFGAB").indexOf(step.at(0));
                p.step = idx < 0 ? 0 : idx;
                p.octave = qBound(0, attrInt(a, u"oct", 4), 9);
                p.alter = qBound(-2, attrInt(a, u"alter", 0), 2);
                event->pitches.push_back(p);
            }
        } else if (tok == QXmlStreamReader::EndElement) {
            const auto n = r.name();
            if (n == u"chord" || n == u"rest") {
                if (event) event->sortPitches();
                event = nullptr;
            } else if (n == u"measure") {
                measure = nullptr;
            } else if (n == u"staff") {
                staff = nullptr;
            }
        }
    }
    if (r.hasError() || !sawRoot) {
        if (error)
            *error = r.hasError() ? QStringLiteral("XML inválido (linha %1): %2").arg(r.lineNumber()).arg(r.errorString())
                                  : QStringLiteral("Este não é um arquivo .pauta.");
        return false;
    }
    if (s.staves.empty()) {
        if (error) *error = QStringLiteral("O arquivo não contém pautas.");
        return false;
    }
    // Make every staff have the same number of measures.
    size_t maxM = 1;
    for (const Staff& st : s.staves) maxM = std::max(maxM, st.measures.size());
    s.setMeasureCount(int(maxM));
    out = std::move(s);
    return true;
}

} // namespace pauta::io
