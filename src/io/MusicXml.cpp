#include "io/MusicXml.h"

#include "io/NativeFormat.h"

#include <QDomDocument>
#include <QFile>
#include <QFileInfo>

#ifndef PAUTA_NO_MXL
#include <QtCore/private/qzipreader_p.h>
#endif

#include <algorithm>
#include <cmath>
#include <map>

namespace pauta::io {

namespace {

int typeToLog(const QString& t)
{
    static const std::map<QString, int> m = {
        { QStringLiteral("breve"), -1 }, { QStringLiteral("whole"), 0 }, { QStringLiteral("half"), 1 },
        { QStringLiteral("quarter"), 2 }, { QStringLiteral("eighth"), 3 }, { QStringLiteral("16th"), 4 },
        { QStringLiteral("32nd"), 5 }, { QStringLiteral("64th"), 6 }, { QStringLiteral("128th"), 6 },
        { QStringLiteral("256th"), 6 },
    };
    auto it = m.find(t.trimmed());
    return it == m.end() ? -99 : it->second;
}

// Exact note value for a time span, if one exists (e.g. 3/8 = dotted quarter).
bool fracToDuration(Frac f, int& durLog, int& dots)
{
    for (int l = -1; l <= 6; ++l)
        for (int d = 0; d <= 3; ++d)
            if (durationOf(l, d) == f) { durLog = l; dots = d; return true; }
    return false;
}

// Fills a gap with the fewest rests (largest values first).
void appendRests(Measure& m, Frac gap)
{
    while (gap > Frac(0)) {
        int l = -1;
        while (l < 6 && durationOf(l, 0) > gap) ++l;
        if (durationOf(l, 0) > gap) break; // smaller than a 64th: drop
        Event e;
        e.durLog = l;
        m.events.push_back(e);
        gap -= durationOf(l, 0);
    }
}

Clef clefFrom(const QString& sign, int line)
{
    if (sign == u"F") return Clef::Bass;
    if (sign == u"C") return line >= 4 ? Clef::Tenor : Clef::Alto;
    return Clef::Treble; // G, percussion, TAB: closest we have
}

QString childText(const QDomElement& e, const QString& name) { return e.firstChildElement(name).text().trimmed(); }

QByteArray readScoreBytes(const QString& path, QString* error)
{
    if (path.endsWith(QStringLiteral(".mxl"), Qt::CaseInsensitive)) {
#ifdef PAUTA_NO_MXL
        if (error) *error = QStringLiteral("Esta versão não abre .mxl compactado. Exporte como MusicXML não compactado (.musicxml).");
        return {};
#else
        QZipReader zip(path);
        if (!zip.isReadable()) {
            if (error) *error = QStringLiteral("Não foi possível abrir o arquivo .mxl (ZIP inválido).");
            return {};
        }
        // META-INF/container.xml names the main score; otherwise take the first XML file.
        QString main;
        QDomDocument container;
        const QByteArray cont = zip.fileData(QStringLiteral("META-INF/container.xml"));
        if (!cont.isEmpty() && container.setContent(cont)) {
            const QDomElement rf = container.documentElement().firstChildElement(QStringLiteral("rootfiles")).firstChildElement(QStringLiteral("rootfile"));
            main = rf.attribute(QStringLiteral("full-path"));
        }
        if (main.isEmpty()) {
            for (const QZipReader::FileInfo& fi : zip.fileInfoList()) {
                if (fi.isFile && !fi.filePath.startsWith(QStringLiteral("META-INF"))
                    && (fi.filePath.endsWith(QStringLiteral(".xml")) || fi.filePath.endsWith(QStringLiteral(".musicxml")))) {
                    main = fi.filePath;
                    break;
                }
            }
        }
        const QByteArray data = main.isEmpty() ? QByteArray() : zip.fileData(main);
        if (data.isEmpty() && error) *error = QStringLiteral("O arquivo .mxl não contém uma partitura MusicXML.");
        return data;
#endif
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return {};
    }
    return f.readAll();
}

// Per-staff state while reading one measure.
struct StaffState {
    Measure measure;
    QString mainVoice;      // only one voice per staff is kept (the first one seen)
    Frac filled;            // time reached by the kept voice
    bool lastWasRest = true;
    bool wholeMeasureRest = false;
};

} // namespace

bool isMusicXmlPath(const QString& path)
{
    const QString s = QFileInfo(path).suffix().toLower();
    return s == u"musicxml" || s == u"mxl" || s == u"xml";
}

QString openFileFilter()
{
    return QStringLiteral("Partituras (*.pauta *.musicxml *.mxl *.xml);;Pauta (*.pauta);;MusicXML (*.musicxml *.mxl *.xml)");
}

bool loadAnyScore(Score& score, const QString& path, QString* error, QStringList* warnings)
{
    if (isMusicXmlPath(path)) return importMusicXml(score, path, error, warnings);
    return loadNative(score, path, error);
}

bool importMusicXml(Score& out, const QString& path, QString* error, QStringList* warnings)
{
    const QByteArray data = readScoreBytes(path, error);
    if (data.isEmpty()) {
        if (error && error->isEmpty()) *error = QStringLiteral("Arquivo vazio.");
        return false;
    }

    QDomDocument doc;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    const QDomDocument::ParseResult res = doc.setContent(data);
    if (!res) {
        if (error) *error = QStringLiteral("XML inválido (linha %1): %2").arg(res.errorLine).arg(res.errorMessage);
        return false;
    }
#else
    QString msg;
    int line = 0;
    if (!doc.setContent(data, &msg, &line)) {
        if (error) *error = QStringLiteral("XML inválido (linha %1): %2").arg(line).arg(msg);
        return false;
    }
#endif

    const QDomElement root = doc.documentElement();
    if (root.tagName() == u"score-timewise") {
        if (error) *error = QStringLiteral("MusicXML no formato \"timewise\" ainda não é suportado. Exporte como \"partwise\" (o padrão).");
        return false;
    }
    if (root.tagName() != u"score-partwise") {
        if (error) *error = QStringLiteral("Este arquivo não é uma partitura MusicXML.");
        return false;
    }

    Score s;
    s.title = childText(root.firstChildElement(QStringLiteral("work")), QStringLiteral("work-title"));
    if (s.title.isEmpty()) s.title = childText(root, QStringLiteral("movement-title"));
    for (QDomElement c = root.firstChildElement(QStringLiteral("identification")).firstChildElement(QStringLiteral("creator"));
         !c.isNull(); c = c.nextSiblingElement(QStringLiteral("creator"))) {
        if (c.attribute(QStringLiteral("type")) == u"composer") { s.composer = c.text().trimmed(); break; }
    }

    std::map<QString, QString> partNames;
    for (QDomElement sp = root.firstChildElement(QStringLiteral("part-list")).firstChildElement(QStringLiteral("score-part"));
         !sp.isNull(); sp = sp.nextSiblingElement(QStringLiteral("score-part")))
        partNames[sp.attribute(QStringLiteral("id"))] = childText(sp, QStringLiteral("part-name"));

    bool timeSet = false, keySet = false;
    bool timeChanged = false, keyChanged = false, clefChanged = false, tuplets = false;
    int droppedNotes = 0, graceNotes = 0;

    for (QDomElement part = root.firstChildElement(QStringLiteral("part")); !part.isNull();
         part = part.nextSiblingElement(QStringLiteral("part"))) {
        std::vector<Staff> staves(1);
        int divisions = 1;
        bool firstMeasure = true;

        for (QDomElement me = part.firstChildElement(QStringLiteral("measure")); !me.isNull();
             me = me.nextSiblingElement(QStringLiteral("measure"))) {
            std::vector<StaffState> st(staves.size());
            Frac pos, lastStart;
            auto staffIndex = [&](const QDomElement& e) {
                const int n = e.firstChildElement(QStringLiteral("staff")).text().toInt();
                return std::clamp(n <= 0 ? 0 : n - 1, 0, int(st.size()) - 1);
            };
            auto toFrac = [&](int dur) { return Frac(dur, int64_t(std::max(divisions, 1)) * 4); };

            for (QDomElement el = me.firstChildElement(); !el.isNull(); el = el.nextSiblingElement()) {
                const QString tag = el.tagName();
                if (tag == u"attributes") {
                    if (!el.firstChildElement(QStringLiteral("divisions")).isNull())
                        divisions = std::max(1, el.firstChildElement(QStringLiteral("divisions")).text().toInt());
                    const QDomElement nst = el.firstChildElement(QStringLiteral("staves"));
                    if (!nst.isNull() && firstMeasure) {
                        const int n = std::clamp(nst.text().toInt(), 1, 8);
                        staves.resize(size_t(n));
                        st.resize(size_t(n));
                    }
                    const QDomElement key = el.firstChildElement(QStringLiteral("key"));
                    if (!key.isNull() && !key.firstChildElement(QStringLiteral("fifths")).isNull()) {
                        const int f = std::clamp(key.firstChildElement(QStringLiteral("fifths")).text().toInt(), -7, 7);
                        if (!keySet) { s.keyFifths = f; keySet = true; }
                        else if (f != s.keyFifths) keyChanged = true;
                    }
                    const QDomElement time = el.firstChildElement(QStringLiteral("time"));
                    if (!time.isNull() && !time.firstChildElement(QStringLiteral("beats")).isNull()) {
                        TimeSig t{ std::clamp(time.firstChildElement(QStringLiteral("beats")).text().toInt(), 1, 64),
                                   std::clamp(time.firstChildElement(QStringLiteral("beat-type")).text().toInt(), 1, 64) };
                        if (!timeSet) { s.time = t; timeSet = true; }
                        else if (t.beats != s.time.beats || t.beatType != s.time.beatType) timeChanged = true;
                    }
                    for (QDomElement c = el.firstChildElement(QStringLiteral("clef")); !c.isNull(); c = c.nextSiblingElement(QStringLiteral("clef"))) {
                        const int n = std::clamp(c.attribute(QStringLiteral("number"), QStringLiteral("1")).toInt(), 1, int(staves.size())) - 1;
                        const Clef cl = clefFrom(childText(c, QStringLiteral("sign")), childText(c, QStringLiteral("line")).toInt());
                        if (firstMeasure) staves[size_t(n)].clef = cl;
                        else if (cl != staves[size_t(n)].clef) clefChanged = true;
                    }
                } else if (tag == u"backup") {
                    pos -= toFrac(childText(el, QStringLiteral("duration")).toInt());
                    if (pos < Frac(0)) pos = Frac(0);
                } else if (tag == u"forward") {
                    pos += toFrac(childText(el, QStringLiteral("duration")).toInt());
                } else if (tag == u"note") {
                    if (!el.firstChildElement(QStringLiteral("grace")).isNull()) { ++graceNotes; continue; }
                    if (!el.firstChildElement(QStringLiteral("cue")).isNull()) continue;
                    const bool chord = !el.firstChildElement(QStringLiteral("chord")).isNull();
                    const Frac dur = toFrac(childText(el, QStringLiteral("duration")).toInt());
                    const Frac start = chord ? lastStart : pos;
                    if (!chord) {
                        lastStart = pos;
                        pos += dur;
                    }
                    StaffState& ss = st[size_t(staffIndex(el))];
                    QString voice = childText(el, QStringLiteral("voice"));
                    if (voice.isEmpty()) voice = QStringLiteral("1");
                    if (ss.mainVoice.isEmpty()) ss.mainVoice = voice;
                    if (voice != ss.mainVoice) { ++droppedNotes; continue; }

                    const QDomElement restEl = el.firstChildElement(QStringLiteral("rest"));
                    const bool isRest = !restEl.isNull();
                    Pitch p;
                    if (!isRest) {
                        const QDomElement pe = el.firstChildElement(QStringLiteral("pitch"));
                        const QString step = childText(pe, QStringLiteral("step"));
                        const int idx = step.isEmpty() ? -1 : QStringLiteral("CDEFGAB").indexOf(step.at(0).toUpper());
                        if (idx < 0) continue; // unpitched/percussion note: not supported yet
                        p.step = idx;
                        p.octave = std::clamp(childText(pe, QStringLiteral("octave")).toInt(), 0, 9);
                        p.alter = std::clamp(int(std::lround(childText(pe, QStringLiteral("alter")).toDouble())), -2, 2);
                        for (QDomElement t = el.firstChildElement(QStringLiteral("tie")); !t.isNull(); t = t.nextSiblingElement(QStringLiteral("tie")))
                            if (t.attribute(QStringLiteral("type")) == u"start") p.tie = true;
                    }

                    if (chord) {
                        if (!isRest && !ss.measure.events.empty() && !ss.measure.events.back().isRest()) {
                            ss.measure.events.back().pitches.push_back(p);
                            ss.measure.events.back().sortPitches();
                        }
                        continue;
                    }

                    // Gap before this note in the kept voice (e.g. after <forward>): fill with rests.
                    if (start > ss.filled) appendRests(ss.measure, start - ss.filled);

                    Event ev;
                    const int tl = typeToLog(childText(el, QStringLiteral("type")));
                    int dots = 0;
                    for (QDomElement d = el.firstChildElement(QStringLiteral("dot")); !d.isNull(); d = d.nextSiblingElement(QStringLiteral("dot"))) ++dots;
                    if (!el.firstChildElement(QStringLiteral("time-modification")).isNull()) tuplets = true;
                    if (tl != -99) {
                        ev.durLog = tl;
                        ev.dots = std::min(dots, 3);
                    } else if (isRest && restEl.attribute(QStringLiteral("measure")) == u"yes") {
                        ss.wholeMeasureRest = true;
                        ss.filled = std::max(ss.filled, start + dur);
                        continue;
                    } else if (!fracToDuration(dur, ev.durLog, ev.dots)) {
                        appendRests(ss.measure, dur); // odd length rest/note: approximate with rests
                        ss.filled = start + dur;
                        continue;
                    }
                    if (!isRest) ev.pitches = { p };
                    ss.measure.events.push_back(std::move(ev));
                    ss.filled = start + dur;
                }
            }

            for (size_t k = 0; k < staves.size(); ++k) {
                Measure m = k < st.size() ? std::move(st[k].measure) : Measure{};
                // A measure that only holds rests filling it is shown as the usual whole-measure rest.
                const bool allRests = std::all_of(m.events.begin(), m.events.end(), [](const Event& e) { return e.isRest(); });
                if (allRests) m.events.clear();
                staves[k].measures.push_back(std::move(m));
            }
            firstMeasure = false;
        }

        const QString name = partNames[part.attribute(QStringLiteral("id"))];
        staves.front().name = name;
        for (Staff& stf : staves) s.staves.push_back(std::move(stf));
    }

    if (s.staves.empty()) {
        if (error) *error = QStringLiteral("A partitura não tem partes.");
        return false;
    }
    size_t maxM = 1;
    for (const Staff& stf : s.staves) maxM = std::max(maxM, stf.measures.size());
    s.setMeasureCount(int(maxM));
    if (s.title.isEmpty()) s.title = QFileInfo(path).completeBaseName();

    if (warnings) {
        if (droppedNotes) warnings->append(QStringLiteral("%1 nota(s) de vozes secundárias foram omitidas (o Pauta ainda tem uma voz por pauta).").arg(droppedNotes));
        if (graceNotes) warnings->append(QStringLiteral("%1 apojatura(s) foram omitidas.").arg(graceNotes));
        if (tuplets) warnings->append(QStringLiteral("Há quiálteras: as notas foram mantidas, mas ainda sem o número/colchete de quiáltera."));
        if (keyChanged) warnings->append(QStringLiteral("Mudanças de armadura no meio da peça foram ignoradas."));
        if (timeChanged) warnings->append(QStringLiteral("Mudanças de fórmula de compasso no meio da peça foram ignoradas."));
        if (clefChanged) warnings->append(QStringLiteral("Mudanças de clave no meio da peça foram ignoradas."));
    }
    out = std::move(s);
    return true;
}

} // namespace pauta::io
