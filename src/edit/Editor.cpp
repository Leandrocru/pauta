#include "edit/Editor.h"

#include <QUndoCommand>

#include <algorithm>
#include <cstdlib>

namespace pauta {

namespace {

// Whole-document snapshot. Cheap for scores of normal size and impossible to get wrong;
// can be replaced by fine-grained commands later if profiling asks for it.
class SnapshotCommand : public QUndoCommand {
public:
    SnapshotCommand(Editor* ed, const QString& text, Score before, Cursor cb, Score after, Cursor ca)
        : QUndoCommand(text), m_ed(ed), m_before(std::move(before)), m_after(std::move(after)), m_cb(cb), m_ca(ca)
    {
    }
    void undo() override { m_ed->restore(m_before, m_cb); }
    void redo() override
    {
        if (m_first) { m_first = false; return; } // already applied
        m_ed->restore(m_after, m_ca);
    }

private:
    Editor* m_ed;
    Score m_before, m_after;
    Cursor m_cb, m_ca;
    bool m_first = true;
};

QString tooLong() { return QStringLiteral("Não cabe no compasso: a duração ultrapassa a fórmula de compasso."); }

} // namespace

Editor::Editor(QObject* parent) : QObject(parent)
{
    m_score = Score::makeDemo();
}

void Editor::setScore(Score s)
{
    m_score = std::move(s);
    m_undo.clear();
    m_sel = Selection{};
    m_cursor = Cursor{};
    m_cursor.diatonic = clefMiddleLine(m_score.staves.empty() ? Clef::Treble : m_score.staves.front().clefAt(0));
    emit scoreChanged();
    emit cursorChanged();
}

void Editor::restore(const Score& s, const Cursor& c)
{
    m_score = s;
    m_cursor = c;
    clampCursor();
    emit scoreChanged();
    emit cursorChanged();
}

void Editor::setCursor(const Cursor& c)
{
    m_cursor = c;
    clampCursor();
    emit cursorChanged();
}

void Editor::clampCursor()
{
    if (m_score.staves.empty()) { m_cursor = Cursor{}; return; }
    m_cursor.staff = std::clamp(m_cursor.staff, 0, int(m_score.staves.size()) - 1);
    m_cursor.measure = std::clamp(m_cursor.measure, 0, m_score.measureCount() - 1);
    m_cursor.index = std::clamp(m_cursor.index, 0, int(evs().size()));
    m_cursor.diatonic = std::clamp(m_cursor.diatonic, 7, 7 * 8 + 6);
    m_cursor.voice = std::clamp(m_cursor.voice, 0, kVoices - 1);
    if (m_sel.active && (m_sel.staff1 >= int(m_score.staves.size()) || m_sel.measure1 >= m_score.measureCount()))
        m_sel.active = false;
}

bool Editor::mutate(const QString& text, const std::function<bool()>& fn)
{
    Score before = m_score;
    const Cursor cb = m_cursor;
    if (!fn()) {
        m_score = std::move(before);
        m_cursor = cb;
        return false;
    }
    clampCursor();
    m_undo.push(new SnapshotCommand(this, text, std::move(before), cb, m_score, m_cursor));
    emit scoreChanged();
    emit cursorChanged();
    return true;
}

const Event* Editor::currentEvent() const
{
    const Measure& m = measure();
    const auto& ev = m.voice(m_cursor.voice);
    return m_cursor.index < int(ev.size()) ? &ev[size_t(m_cursor.index)] : nullptr;
}

Event* Editor::currentEventMut()
{
    Measure& m = measure();
    auto& ev = m.voice(m_cursor.voice);
    return m_cursor.index < int(ev.size()) ? &ev[size_t(m_cursor.index)] : nullptr;
}

Event* Editor::targetEvent()
{
    if (Event* e = currentEventMut()) return e;
    Measure& m = measure();
    if (m_cursor.index > 0) return &m.voice(m_cursor.voice)[size_t(m_cursor.index - 1)];
    // Cursor at the start of an empty/next measure: use the last event of the previous measure.
    if (m_cursor.measure > 0) {
        auto& prev = m_score.staves[size_t(m_cursor.staff)].measures[size_t(m_cursor.measure - 1)];
        if (!prev.voice(m_cursor.voice).empty()) return &prev.voice(m_cursor.voice).back();
    }
    return nullptr;
}

Pitch Editor::cursorPitch() const
{
    Pitch p = Pitch::fromDiatonic(m_cursor.diatonic);
    p.alter = keyAlterFor(p.step, m_score.keyAt(m_cursor.measure));
    return p;
}

void Editor::advanceIfFull()
{
    const Measure& m = measure();
    if (m_cursor.index == int(m.voice(m_cursor.voice).size()) && m.filled(m_cursor.voice) >= m_score.capacityAt(m_cursor.measure)) {
        if (m_cursor.measure == m_score.measureCount() - 1) m_score.appendMeasure();
        ++m_cursor.measure;
        m_cursor.index = 0;
    }
}

bool Editor::insertEvent(Event e)
{
    // Tuplet entry: the first note after Ctrl+N opens the group, later notes fill it.
    const int savedArm = m_tupArm, savedActual = m_tupActual, savedNormal = m_tupNormal;
    const Frac savedRemaining = m_tupRemaining;
    const Frac base = e.baseDuration();
    if (m_tupArm > 0) {
        m_tupActual = m_tupArm;
        m_tupNormal = tupletNormalFor(m_tupArm);
        m_tupArm = 0;
        e.tupletStart = true;
        m_tupRemaining = base * Frac(m_tupActual);
    }
    if (m_tupActual > 0 && m_tupRemaining > Frac(0)) {
        e.tupletActual = m_tupActual;
        e.tupletNormal = m_tupNormal;
        m_tupRemaining -= base;
        if (m_tupRemaining <= Frac(0)) m_tupActual = 0;
    }
    auto restore = [&] {
        m_tupArm = savedArm;
        m_tupActual = savedActual;
        m_tupNormal = savedNormal;
        m_tupRemaining = savedRemaining;
    };

    const Frac cap = m_score.capacityAt(m_cursor.measure);
    if (measure().filled(m_cursor.voice) + e.duration() > cap) {
        // A full measure with the cursor at its end: continue in the next one.
        if (m_cursor.index == int(evs().size()) && measure().filled(m_cursor.voice) >= cap) {
            if (m_cursor.measure == m_score.measureCount() - 1) m_score.appendMeasure();
            ++m_cursor.measure;
            m_cursor.index = 0;
            if (measure().filled(m_cursor.voice) + e.duration() > cap) { emit message(tooLong()); restore(); return false; }
        } else {
            emit message(tooLong());
            restore();
            return false;
        }
    }
    auto& list = evs();
    list.insert(list.begin() + m_cursor.index, std::move(e));
    ++m_cursor.index;
    advanceIfFull();
    return true;
}

void Editor::enterDuration(int durLog)
{
    m_durLog = durLog;
    if (currentEvent()) {
        mutate(QStringLiteral("Alterar duração"), [&] {
            Event* e = currentEventMut();
            Event changed = *e;
            changed.durLog = durLog;
            const Frac newDur = changed.duration();
            if (measure().filled(m_cursor.voice) - e->duration() + newDur > m_score.capacityAt(m_cursor.measure)) {
                emit message(tooLong());
                return false;
            }
            e->durLog = durLog;
            ++m_cursor.index;
            advanceIfFull();
            return true;
        });
        return;
    }
    mutate(QStringLiteral("Inserir nota"), [&] {
        Event e;
        e.durLog = durLog;
        e.pitches = { cursorPitch() };
        return insertEvent(std::move(e));
    });
}

void Editor::enterStep(int step)
{
    // Nearest pitch with this letter name to the cursor.
    const int cur = m_cursor.diatonic;
    const int base = cur - (((cur % 7) + 7) % 7) + step;
    int best = base;
    for (int c : { base - 7, base + 7 })
        if (std::abs(c - cur) < std::abs(best - cur)) best = c;
    m_cursor.diatonic = std::clamp(best, 7, 7 * 8 + 6);

    if (currentEvent()) {
        mutate(QStringLiteral("Alterar nota"), [&] {
            Event* e = currentEventMut();
            e->pitches = { cursorPitch() };
            ++m_cursor.index;
            advanceIfFull();
            return true;
        });
    } else {
        mutate(QStringLiteral("Inserir nota"), [&] {
            Event e;
            e.durLog = m_durLog;
            e.pitches = { cursorPitch() };
            return insertEvent(std::move(e));
        });
    }
    emit cursorChanged();
}

void Editor::changeDurationInPlace(int durLog)
{
    m_durLog = durLog;
    if (!currentEvent()) {
        emit cursorChanged(); // only the caret's duration changes
        return;
    }
    mutate(QStringLiteral("Alterar duração"), [&] {
        Event* e = currentEventMut();
        Event changed = *e;
        changed.durLog = durLog;
        if (measure().filled(m_cursor.voice) - e->duration() + changed.duration() > m_score.capacityAt(m_cursor.measure)) {
            emit message(tooLong());
            return false;
        }
        e->durLog = durLog;
        return true;
    });
}

void Editor::addStepToChord(int step)
{
    const int cur = m_cursor.diatonic;
    const int base = cur - (((cur % 7) + 7) % 7) + step;
    int best = base;
    for (int c : { base - 7, base + 7 })
        if (std::abs(c - cur) < std::abs(best - cur)) best = c;
    m_cursor.diatonic = std::clamp(best, 7, 7 * 8 + 6);
    mutate(QStringLiteral("Nota no acorde"), [&] {
        Event* e = targetEvent();
        if (!e || e->isRest()) return false;
        for (const Pitch& p : e->pitches)
            if (p.diatonic() == m_cursor.diatonic) return false;
        e->pitches.push_back(cursorPitch());
        e->sortPitches();
        return true;
    });
    emit cursorChanged();
}

void Editor::enterAtCursorPitch()
{
    if (!currentEvent()) {
        mutate(QStringLiteral("Inserir nota"), [&] {
            Event e;
            e.durLog = m_durLog;
            e.pitches = { cursorPitch() };
            return insertEvent(std::move(e));
        });
        return;
    }
    mutate(QStringLiteral("Nota no acorde"), [&] {
        Event* e = currentEventMut();
        auto it = std::find_if(e->pitches.begin(), e->pitches.end(),
                               [&](const Pitch& p) { return p.diatonic() == m_cursor.diatonic; });
        if (it != e->pitches.end()) e->pitches.erase(it);
        else {
            e->pitches.push_back(cursorPitch());
            e->sortPitches();
        }
        return true;
    });
}

void Editor::enterRest()
{
    if (!currentEvent()) {
        mutate(QStringLiteral("Inserir pausa"), [&] {
            Event e;
            e.durLog = m_durLog;
            return insertEvent(std::move(e));
        });
        return;
    }
    mutate(QStringLiteral("Nota/pausa"), [&] {
        Event* e = currentEventMut();
        if (e->isRest()) e->pitches = { cursorPitch() };
        else e->pitches.clear();
        return true;
    });
}

void Editor::deleteEvent()
{
    if (!currentEvent()) return;
    mutate(QStringLiteral("Apagar"), [&] {
        auto& list = evs();
        list.erase(list.begin() + m_cursor.index);
        return true;
    });
}

void Editor::backspace()
{
    mutate(QStringLiteral("Apagar"), [&] {
        if (m_cursor.index == 0) {
            if (m_cursor.measure == 0) return false;
            --m_cursor.measure;
            m_cursor.index = int(evs().size());
            if (m_cursor.index == 0) return true; // just moved into an empty measure
        }
        auto& list = evs();
        list.erase(list.begin() + (m_cursor.index - 1));
        --m_cursor.index;
        return true;
    });
}

void Editor::alter(int delta)
{
    mutate(delta > 0 ? QStringLiteral("Sustenido") : QStringLiteral("Bemol"), [&] {
        Event* e = targetEvent();
        if (!e || e->isRest()) return false;
        bool hit = false;
        for (Pitch& p : e->pitches)
            if (p.diatonic() == m_cursor.diatonic) { p.alter = std::clamp(p.alter + delta, -2, 2); hit = true; }
        if (!hit)
            for (Pitch& p : e->pitches) p.alter = std::clamp(p.alter + delta, -2, 2);
        return true;
    });
}

void Editor::toggleTie()
{
    mutate(QStringLiteral("Ligadura de valor"), [&] {
        Event* e = targetEvent();
        if (!e || e->isRest()) return false;
        // The pitch under the cursor if it is in the chord, otherwise the whole chord.
        bool hit = false;
        for (Pitch& p : e->pitches)
            if (p.diatonic() == m_cursor.diatonic) { p.tie = !p.tie; hit = true; }
        if (!hit) {
            const bool anyUntied = std::any_of(e->pitches.begin(), e->pitches.end(), [](const Pitch& p) { return !p.tie; });
            for (Pitch& p : e->pitches) p.tie = anyUntied;
        }
        return true;
    });
}

void Editor::toggleArticulation(unsigned artic)
{
    mutate(articulationName(artic), [&] {
        if (m_sel.active) {
            // On a region: add it everywhere, or remove it if every note already has it.
            std::vector<Event*> notes;
            for (int s = m_sel.staff0; s <= m_sel.staff1; ++s)
                for (int m = m_sel.measure0; m <= m_sel.measure1; ++m)
                    for (int v = 0; v < kVoices; ++v)
                        for (Event& e : m_score.staves[size_t(s)].measures[size_t(m)].voice(v))
                            if (!e.isRest() || artic == ArticFermata) notes.push_back(&e);
            if (notes.empty()) return false;
            const bool all = std::all_of(notes.begin(), notes.end(), [&](const Event* e) { return e->articulations & artic; });
            for (Event* e : notes) e->articulations = all ? (e->articulations & ~artic) : (e->articulations | artic);
            return true;
        }
        Event* e = targetEvent();
        if (!e || (e->isRest() && artic != ArticFermata)) return false;
        e->articulations ^= artic;
        return true;
    });
}

namespace {
const char* shapeName(int kind) { return kind == 0 ? "Ligadura de expressão" : kind == 1 ? "Crescendo" : "Diminuendo"; }
}

void Editor::addSmartShape(int kind, int staff, int voice, int m0, int i0, int m1, int i1)
{
    if (std::make_pair(m1, i1) < std::make_pair(m0, i0)) {
        std::swap(m0, m1);
        std::swap(i0, i1);
    }
    if (m0 == m1 && i0 == i1) {
        emit message(QStringLiteral("Escolha duas notas diferentes: a primeira e a última."));
        return;
    }
    mutate(QString::fromUtf8(shapeName(kind)), [&] {
        if (staff < 0 || staff >= int(m_score.staves.size())) return false;
        auto& ms = m_score.staves[size_t(staff)].measures;
        if (m1 >= int(ms.size())) return false;
        auto& a = ms[size_t(m0)].voice(voice);
        auto& b = ms[size_t(m1)].voice(voice);
        if (i0 >= int(a.size()) || i1 >= int(b.size())) return false;
        if (kind == 0) {
            a[size_t(i0)].slurStart = true;
            b[size_t(i1)].slurEnd = true;
        } else {
            a[size_t(i0)].hairpin = kind;
            b[size_t(i1)].hairpinEnd = true;
        }
        return true;
    });
}

void Editor::smartShapeOnSelection(int kind)
{
    if (!m_sel.active) {
        emit message(QStringLiteral("Selecione um trecho (ferramenta Seleção) ou use a ferramenta Linhas e ligaduras."));
        return;
    }
    const int v = m_cursor.voice;
    mutate(QString::fromUtf8(shapeName(kind)), [&] {
        bool any = false;
        for (int s = m_sel.staff0; s <= m_sel.staff1; ++s) {
            auto& ms = m_score.staves[size_t(s)].measures;
            Event* first = nullptr;
            Event* last = nullptr;
            for (int m = m_sel.measure0; m <= m_sel.measure1; ++m)
                for (Event& e : ms[size_t(m)].voice(v)) {
                    if (e.isRest() && kind == 0) continue; // slurs go from note to note
                    if (!first) first = &e;
                    last = &e;
                }
            if (!first || first == last) continue;
            if (kind == 0) { first->slurStart = true; last->slurEnd = true; }
            else { first->hairpin = kind; last->hairpinEnd = true; }
            any = true;
        }
        if (!any) emit message(QStringLiteral("A seleção precisa ter pelo menos duas notas na camada atual."));
        return any;
    });
}

void Editor::clearSmartShapes()
{
    mutate(QStringLiteral("Remover linhas e ligaduras"), [&] {
        auto clear = [](Event& e) {
            const bool had = e.slurStart || e.slurEnd || e.hairpin || e.hairpinEnd;
            e.slurStart = e.slurEnd = e.hairpinEnd = false;
            e.hairpin = 0;
            return had;
        };
        bool any = false;
        if (m_sel.active) {
            for (int s = m_sel.staff0; s <= m_sel.staff1; ++s)
                for (int m = m_sel.measure0; m <= m_sel.measure1; ++m)
                    for (int v = 0; v < kVoices; ++v)
                        for (Event& e : m_score.staves[size_t(s)].measures[size_t(m)].voice(v)) any |= clear(e);
            return any;
        }
        Event* e = targetEvent();
        return e && clear(*e);
    });
}

void Editor::setDynamic(int dynamic)
{
    mutate(dynamic ? dynamicText(dynamic) : QStringLiteral("Remover dinâmica"), [&] {
        if (m_sel.active) {
            bool any = false;
            for (int s = m_sel.staff0; s <= m_sel.staff1; ++s) {
                auto& meas = m_score.staves[size_t(s)].measures[size_t(m_sel.measure0)];
                for (int v = 0; v < kVoices && !any; ++v)
                    if (!meas.voice(v).empty()) { meas.voice(v).front().dynamic = dynamic; any = true; }
                any = false; // one per staff
            }
            return true;
        }
        Event* e = targetEvent();
        if (!e) return false;
        e->dynamic = (e->dynamic == dynamic) ? 0 : dynamic; // same mark again removes it
        return true;
    });
}

void Editor::naturalize()
{
    mutate(QStringLiteral("Bequadro"), [&] {
        Event* e = targetEvent();
        if (!e || e->isRest()) return false;
        for (Pitch& p : e->pitches) p.alter = 0;
        return true;
    });
}

void Editor::toggleDot()
{
    mutate(QStringLiteral("Ponto de aumento"), [&] {
        Event* e = targetEvent();
        if (!e) return false;
        const int newDots = e->dots ? 0 : 1;
        // Find the measure that holds e to check the capacity.
        for (int mi = 0; mi < m_score.measureCount(); ++mi) {
            Measure& m = m_score.staves[size_t(m_cursor.staff)].measures[size_t(mi)];
            const auto& ev = m.voice(m_cursor.voice);
            if (ev.empty() || e < &ev.front() || e > &ev.back()) continue;
            Event changed = *e;
            changed.dots = newDots;
            if (m.filled(m_cursor.voice) - e->duration() + changed.duration() > m_score.capacityAt(mi)) {
                emit message(tooLong());
                return false;
            }
        }
        e->dots = newDots;
        return true;
    });
}

void Editor::transpose(int steps)
{
    mutate(QStringLiteral("Transpor"), [&] {
        Event* e = targetEvent();
        if (!e || e->isRest()) return false;
        for (Pitch& p : e->pitches) {
            const int d = std::clamp(p.diatonic() + steps, 7, 7 * 8 + 6);
            const bool tied = p.tie;
            p = Pitch::fromDiatonic(d, keyAlterFor(((d % 7) + 7) % 7, m_score.keyAt(m_cursor.measure)));
            p.tie = tied;
        }
        e->sortPitches();
        m_cursor.diatonic = std::clamp(m_cursor.diatonic + steps, 7, 7 * 8 + 6);
        return true;
    });
}

void Editor::moveStep(int d)
{
    m_cursor.diatonic = std::clamp(m_cursor.diatonic + d, 7, 7 * 8 + 6);
    emit cursorChanged();
}

void Editor::moveEvent(int d)
{
    const int size = int(evs().size());
    if (d > 0) {
        if (m_cursor.index < size) ++m_cursor.index;
        else if (m_cursor.measure < m_score.measureCount() - 1) { ++m_cursor.measure; m_cursor.index = 0; }
    } else {
        if (m_cursor.index > 0) --m_cursor.index;
        else if (m_cursor.measure > 0) { --m_cursor.measure; m_cursor.index = int(evs().size()); }
    }
    emit cursorChanged();
}

void Editor::moveMeasure(int d)
{
    m_cursor.measure += d;
    m_cursor.index = 0;
    clampCursor();
    emit cursorChanged();
}

void Editor::moveStaff(int d)
{
    const Clef oldClef = m_score.clefAt(m_cursor.staff, m_cursor.measure);
    m_cursor.staff += d;
    clampCursor();
    const Clef newClef = m_score.clefAt(m_cursor.staff, m_cursor.measure);
    // Keep the cursor at the same visual height on the new staff.
    m_cursor.diatonic += clefMiddleLine(newClef) - clefMiddleLine(oldClef);
    clampCursor();
    emit cursorChanged();
}

void Editor::armTuplet(int actual)
{
    m_tupArm = std::clamp(actual, 2, 15);
    m_tupActual = 0;
    emit hint(QStringLiteral("Quiáltera de %1: digite a primeira nota (as próximas completam o grupo).").arg(m_tupArm));
    emit cursorChanged();
}

void Editor::setTempo(int qpm)
{
    mutate(QStringLiteral("Andamento"), [&] {
        qpm = std::clamp(qpm, 20, 400);
        if (qpm == m_score.tempo) return false;
        m_score.tempo = qpm;
        return true;
    });
}

void Editor::setVoice(int v)
{
    m_cursor.voice = std::clamp(v, 0, kVoices - 1);
    m_cursor.index = std::min(m_cursor.index, int(evs().size()));
    emit cursorChanged();
}

void Editor::moveHome() { m_cursor.index = 0; emit cursorChanged(); }
void Editor::moveEnd() { m_cursor.index = int(evs().size()); emit cursorChanged(); }

void Editor::addMeasures(int n)
{
    mutate(QStringLiteral("Adicionar compassos"), [&] {
        for (int i = 0; i < n; ++i) m_score.appendMeasure();
        return n > 0;
    });
}

void Editor::deleteMeasure()
{
    mutate(QStringLiteral("Excluir compasso"), [&] {
        if (m_score.measureCount() <= 1) return false;
        m_score.removeMeasures(m_cursor.measure, m_cursor.measure);
        m_cursor.index = 0;
        return true;
    });
}

void Editor::deleteSelectedMeasures()
{
    if (!m_sel.active) { deleteMeasure(); return; }
    const int a = m_sel.measure0, b = m_sel.measure1;
    mutate(QStringLiteral("Excluir compassos"), [&] {
        if (b - a + 1 >= m_score.measureCount()) {
            emit message(QStringLiteral("A partitura precisa de pelo menos um compasso."));
            return false;
        }
        m_score.removeMeasures(a, b);
        m_cursor.measure = std::min(a, m_score.measureCount() - 1);
        m_cursor.index = 0;
        m_sel.active = false;
        return true;
    });
    emit selectionChanged();
}

void Editor::insertMeasuresBefore(int n)
{
    const int at = m_sel.active ? m_sel.measure0 : m_cursor.measure;
    mutate(QStringLiteral("Inserir compassos"), [&] {
        if (n <= 0) return false;
        m_score.insertMeasures(at, n);
        return true;
    });
}

void Editor::setSelection(const Selection& s)
{
    m_sel = s;
    if (m_sel.active) {
        if (m_sel.staff0 > m_sel.staff1) std::swap(m_sel.staff0, m_sel.staff1);
        if (m_sel.measure0 > m_sel.measure1) std::swap(m_sel.measure0, m_sel.measure1);
        m_sel.staff0 = std::clamp(m_sel.staff0, 0, int(m_score.staves.size()) - 1);
        m_sel.staff1 = std::clamp(m_sel.staff1, 0, int(m_score.staves.size()) - 1);
        m_sel.measure0 = std::clamp(m_sel.measure0, 0, m_score.measureCount() - 1);
        m_sel.measure1 = std::clamp(m_sel.measure1, 0, m_score.measureCount() - 1);
    }
    emit selectionChanged();
}

void Editor::selectAll()
{
    setSelection(Selection{ true, 0, int(m_score.staves.size()) - 1, 0, m_score.measureCount() - 1 });
}

void Editor::clearSelection()
{
    if (!m_sel.active) return;
    m_sel.active = false;
    emit selectionChanged();
}

void Editor::clearSelectionContents()
{
    if (!m_sel.active) return;
    mutate(QStringLiteral("Limpar compassos"), [&] {
        for (int s = m_sel.staff0; s <= m_sel.staff1; ++s)
            for (int m = m_sel.measure0; m <= m_sel.measure1; ++m)
                for (int v = 0; v < kVoices; ++v) m_score.staves[size_t(s)].measures[size_t(m)].voice(v).clear();
        return true;
    });
}

namespace {
// End (exclusive) of the stretch that starts at `from` and runs until the next change in `changes`.
template <typename T>
int segmentEnd(const std::map<int, T>& changes, int from, int count)
{
    auto it = changes.upper_bound(from);
    return it == changes.end() ? count : std::min(it->first, count);
}
} // namespace

void Editor::setKeySignature(int fifths, int from)
{
    mutate(QStringLiteral("Armadura de clave"), [&] {
        fifths = std::clamp(fifths, -7, 7);
        from = std::clamp(from, 0, m_score.measureCount() - 1);
        const int old = m_score.keyAt(from);
        if (fifths == old) return false;
        const int end = segmentEnd(m_score.keyChanges, from, m_score.measureCount());
        // Keep the music diatonic in that stretch: notes that followed the old key follow the new one.
        for (Staff& st : m_score.staves)
            for (int m = from; m < end; ++m)
                for (int v = 0; v < kVoices; ++v)
                    for (Event& e : st.measures[size_t(m)].voice(v))
                        for (Pitch& p : e.pitches)
                            if (p.alter == keyAlterFor(p.step, old)) p.alter = keyAlterFor(p.step, fifths);
        if (from == 0) m_score.keyFifths = fifths;
        else if (fifths == m_score.keyAt(from - 1)) m_score.keyChanges.erase(from); // same as before: no change mark
        else m_score.keyChanges[from] = fifths;
        return true;
    });
}

void Editor::setTimeSignature(TimeSig t, int from)
{
    mutate(QStringLiteral("Fórmula de compasso"), [&] {
        from = std::clamp(from, 0, m_score.measureCount() - 1);
        if (t == m_score.timeAt(from)) return false;
        const int end = segmentEnd(m_score.timeChanges, from, m_score.measureCount());
        // Rebar the stretch [from, end): pour each layer's events into measures of the new length
        // (an event that does not fit moves to the next measure); empty measures stay empty.
        const Frac cap = t.capacity();
        size_t count = 1;
        std::vector<std::array<std::vector<std::vector<Event>>, kVoices>> rebarred(m_score.staves.size());
        auto fill = [](const std::vector<Event>& evs) {
            Frac f;
            for (const Event& e : evs) f += e.duration();
            return f;
        };
        for (size_t s = 0; s < m_score.staves.size(); ++s) {
            for (int v = 0; v < kVoices; ++v) {
                auto& out = rebarred[s][size_t(v)];
                out.emplace_back();
                for (int m = from; m < end; ++m) {
                    auto& src = m_score.staves[s].measures[size_t(m)].voice(v);
                    if (src.empty()) {
                        if (!out.back().empty()) out.emplace_back();
                        out.emplace_back();
                        continue;
                    }
                    for (Event& e : src) {
                        if (fill(out.back()) + e.duration() > cap && !out.back().empty()) out.emplace_back();
                        out.back().push_back(std::move(e));
                        if (fill(out.back()) >= cap) out.emplace_back();
                    }
                }
                while (out.size() > 1 && out.back().empty()) out.pop_back();
                if (v == 0 || std::any_of(out.begin(), out.end(), [](const auto& x) { return !x.empty(); }))
                    count = std::max(count, out.size());
            }
        }
        // Resize the stretch, keeping later changes on their measures.
        const int oldCount = end - from;
        if (int(count) > oldCount) m_score.insertMeasures(end, int(count) - oldCount);
        else if (int(count) < oldCount) m_score.removeMeasures(from + int(count), end - 1);
        for (size_t s = 0; s < m_score.staves.size(); ++s)
            for (int i = 0; i < int(count); ++i) {
                Measure& meas = m_score.staves[s].measures[size_t(from + i)];
                for (int v = 0; v < kVoices; ++v) {
                    auto& lists = rebarred[s][size_t(v)];
                    meas.voice(v) = size_t(i) < lists.size() ? std::move(lists[size_t(i)]) : std::vector<Event>{};
                }
            }
        if (from == 0) m_score.time = t;
        else if (t == m_score.timeAt(from - 1)) m_score.timeChanges.erase(from);
        else m_score.timeChanges[from] = t;
        m_cursor.index = 0;
        return true;
    });
}

void Editor::setClef(int staff, Clef clef, int from)
{
    mutate(QStringLiteral("Clave"), [&] {
        if (staff < 0 || staff >= int(m_score.staves.size())) return false;
        from = std::clamp(from, 0, m_score.measureCount() - 1);
        Staff& st = m_score.staves[size_t(staff)];
        const Clef old = st.clefAt(from);
        if (old == clef) return false;
        if (from == 0) st.clef = clef;
        else if (clef == st.clefAt(from - 1)) st.clefChanges.erase(from);
        else st.clefChanges[from] = clef;
        if (staff == m_cursor.staff && m_cursor.measure >= from) m_cursor.diatonic += clefMiddleLine(clef) - clefMiddleLine(old);
        return true;
    });
}

void Editor::setStaffName(int staff, const QString& name)
{
    mutate(QStringLiteral("Nome da pauta"), [&] {
        if (staff < 0 || staff >= int(m_score.staves.size())) return false;
        m_score.staves[size_t(staff)].name = name;
        return true;
    });
}

void Editor::addStaff(int index, const QString& name, Clef clef)
{
    mutate(QStringLiteral("Adicionar pauta"), [&] {
        Staff st;
        st.name = name;
        st.clef = clef;
        st.measures.resize(size_t(std::max(1, m_score.measureCount())));
        index = std::clamp(index, 0, int(m_score.staves.size()));
        m_score.staves.insert(m_score.staves.begin() + index, std::move(st));
        m_sel.active = false;
        return true;
    });
}

void Editor::removeStaff(int staff)
{
    mutate(QStringLiteral("Excluir pauta"), [&] {
        if (m_score.staves.size() <= 1 || staff < 0 || staff >= int(m_score.staves.size())) {
            emit message(QStringLiteral("A partitura precisa de pelo menos uma pauta."));
            return false;
        }
        m_score.staves.erase(m_score.staves.begin() + staff);
        m_sel.active = false;
        return true;
    });
}

void Editor::setTitleComposer(const QString& title, const QString& composer)
{
    mutate(QStringLiteral("Título e compositor"), [&] {
        if (title == m_score.title && composer == m_score.composer) return false;
        m_score.title = title;
        m_score.composer = composer;
        return true;
    });
}

} // namespace pauta
