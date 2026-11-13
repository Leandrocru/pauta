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

QStringView attr(const QXmlStreamAttributes& a, const char* name)
{
    // QLatin1String overload exists in every Qt 6 version (QStringView one only since 6.5).
    return a.value(QLatin1String(name));
}

int attrInt(const QXmlStreamAttributes& a, const char* name, int def)
{
    bool ok = false;
    const int v = attr(a, name).toInt(&ok);
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
    w.writeEmptyElement("tempo");
    w.writeAttribute("qpm", QString::number(score.tempo));
    for (const Staff& st : score.staves) {
        w.writeStartElement("staff");
        w.writeAttribute("name", st.name);
        w.writeAttribute("clef", clefKey(st.clef));
        for (const Measure& m : st.measures) {
            w.writeStartElement("measure");
            auto writeEvents = [&](const std::vector<Event>& events) {
                for (const Event& e : events) {
                    w.writeStartElement(e.isRest() ? "rest" : "chord");
                    w.writeAttribute("dur", QString::number(e.durLog));
                    if (e.dots) w.writeAttribute("dots", QString::number(e.dots));
                    if (e.articulations) {
                        QStringList names;
                        for (unsigned a : kArticAll)
                            if (e.articulations & a) names << articulationKey(a);
                        w.writeAttribute("artic", names.join(QLatin1Char(',')));
                    }
                    if (e.dynamic) w.writeAttribute("dyn", dynamicText(e.dynamic));
                    if (e.inTuplet()) {
                        w.writeAttribute("tuplet", QStringLiteral("%1:%2").arg(e.tupletActual).arg(e.tupletNormal));
                        if (e.tupletStart) w.writeAttribute("tupletStart", QStringLiteral("1"));
                    }
                    for (const Pitch& p : e.pitches) {
                        w.writeEmptyElement("note");
                        w.writeAttribute("step", QString(QChar::fromLatin1("CDEFGAB"[p.step])));
                        w.writeAttribute("oct", QString::number(p.octave));
                        if (p.alter) w.writeAttribute("alter", QString::number(p.alter));
                        if (p.tie) w.writeAttribute("tie", QStringLiteral("1"));
                    }
                    w.writeEndElement();
                }
            };
            writeEvents(m.events); // layer 1 directly in <measure> (as in version 1 files)
            for (int v = 1; v < kVoices; ++v) {
                if (m.voice(v).empty()) continue;
                w.writeStartElement("voice");
                w.writeAttribute("n", QString::number(v + 1));
                writeEvents(m.voice(v));
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
    int voiceIdx = 0;

    while (!r.atEnd()) {
        const auto tok = r.readNext();
        if (tok == QXmlStreamReader::StartElement) {
            const auto n = r.name();
            const auto a = r.attributes();
            if (n == u"pauta") {
                sawRoot = true;
                if (attrInt(a, "version", 1) > 1) {
                    if (error) *error = QStringLiteral("Arquivo criado por uma versão mais nova do programa.");
                    return false;
                }
            } else if (n == u"title") {
                s.title = r.readElementText();
            } else if (n == u"composer") {
                s.composer = r.readElementText();
            } else if (n == u"time") {
                s.time.beats = qBound(1, attrInt(a, "beats", 4), 64);
                s.time.beatType = qBound(1, attrInt(a, "beatType", 4), 64);
            } else if (n == u"tempo") {
                s.tempo = qBound(20, attrInt(a, "qpm", 96), 400);
            } else if (n == u"key") {
                s.keyFifths = qBound(-7, attrInt(a, "fifths", 0), 7);
            } else if (n == u"staff") {
                s.staves.emplace_back();
                staff = &s.staves.back();
                staff->name = attr(a, "name").toString();
                staff->clef = clefFromKey(attr(a, "clef"));
            } else if (n == u"measure" && staff) {
                staff->measures.emplace_back();
                measure = &staff->measures.back();
            } else if (n == u"voice" && measure) {
                voiceIdx = qBound(0, attrInt(a, "n", 1) - 1, kVoices - 1);
            } else if ((n == u"chord" || n == u"rest") && measure) {
                measure->voice(voiceIdx).emplace_back();
                event = &measure->voice(voiceIdx).back();
                event->durLog = qBound(-1, attrInt(a, "dur", 2), 6);
                event->dots = qBound(0, attrInt(a, "dots", 0), 3);
                const QStringList tup = attr(a, "tuplet").toString().split(QLatin1Char(':'));
                if (tup.size() == 2) {
                    event->tupletActual = qBound(0, tup[0].toInt(), 32);
                    event->tupletNormal = qBound(0, tup[1].toInt(), 32);
                    event->tupletStart = attrInt(a, "tupletStart", 0) != 0;
                }
                for (const QString& name : attr(a, "artic").toString().split(QLatin1Char(','), Qt::SkipEmptyParts))
                    for (unsigned art : kArticAll)
                        if (articulationKey(art) == name) event->articulations |= art;
                event->dynamic = dynamicFromText(attr(a, "dyn").toString());
            } else if (n == u"note" && event) {
                Pitch p;
                const QString step = attr(a, "step").toString();
                const int idx = step.isEmpty() ? -1 : QStringLiteral("CDEFGAB").indexOf(step.at(0));
                p.step = idx < 0 ? 0 : idx;
                p.octave = qBound(0, attrInt(a, "oct", 4), 9);
                p.alter = qBound(-2, attrInt(a, "alter", 0), 2);
                p.tie = attrInt(a, "tie", 0) != 0;
                event->pitches.push_back(p);
            }
        } else if (tok == QXmlStreamReader::EndElement) {
            const auto n = r.name();
            if (n == u"chord" || n == u"rest") {
                if (event) event->sortPitches();
                event = nullptr;
            } else if (n == u"voice") {
                voiceIdx = 0;
            } else if (n == u"measure") {
                measure = nullptr;
                voiceIdx = 0;
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
