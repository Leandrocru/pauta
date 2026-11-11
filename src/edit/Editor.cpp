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
    m_cursor.diatonic = clefMiddleLine(m_score.staves.empty() ? Clef::Treble : m_score.staves.front().clef);
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
    m_cursor.index = std::clamp(m_cursor.index, 0, int(measure().events.size()));
    m_cursor.diatonic = std::clamp(m_cursor.diatonic, 7, 7 * 8 + 6);
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
    return m_cursor.index < int(m.events.size()) ? &m.events[size_t(m_cursor.index)] : nullptr;
}

Event* Editor::currentEventMut()
{
    Measure& m = measure();
    return m_cursor.index < int(m.events.size()) ? &m.events[size_t(m_cursor.index)] : nullptr;
}

Event* Editor::targetEvent()
{
    if (Event* e = currentEventMut()) return e;
    Measure& m = measure();
    if (m_cursor.index > 0) return &m.events[size_t(m_cursor.index - 1)];
    // Cursor at the start of an empty/next measure: use the last event of the previous measure.
    if (m_cursor.measure > 0) {
        auto& prev = m_score.staves[size_t(m_cursor.staff)].measures[size_t(m_cursor.measure - 1)];
        if (!prev.events.empty()) return &prev.events.back();
    }
    return nullptr;
}

Pitch Editor::cursorPitch() const
{
    Pitch p = Pitch::fromDiatonic(m_cursor.diatonic);
    p.alter = keyAlterFor(p.step, m_score.keyFifths);
    return p;
}

void Editor::advanceIfFull()
{
    const Measure& m = measure();
    if (m_cursor.index == int(m.events.size()) && m.filled() >= m_score.time.capacity()) {
        if (m_cursor.measure == m_score.measureCount() - 1) m_score.appendMeasure();
        ++m_cursor.measure;
        m_cursor.index = 0;
    }
}

bool Editor::insertEvent(Event e)
{
    const Frac cap = m_score.time.capacity();
    if (measure().filled() + e.duration() > cap) {
        // A full measure with the cursor at its end: continue in the next one.
        if (m_cursor.index == int(measure().events.size()) && measure().filled() >= cap) {
            if (m_cursor.measure == m_score.measureCount() - 1) m_score.appendMeasure();
            ++m_cursor.measure;
            m_cursor.index = 0;
            if (measure().filled() + e.duration() > cap) { emit message(tooLong()); return false; }
        } else {
            emit message(tooLong());
            return false;
        }
    }
    auto& evs = measure().events;
    evs.insert(evs.begin() + m_cursor.index, std::move(e));
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
            const Frac newDur = durationOf(durLog, e->dots);
            if (measure().filled() - e->duration() + newDur > m_score.time.capacity()) {
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
        auto& evs = measure().events;
        evs.erase(evs.begin() + m_cursor.index);
        return true;
    });
}

void Editor::backspace()
{
    mutate(QStringLiteral("Apagar"), [&] {
        if (m_cursor.index == 0) {
            if (m_cursor.measure == 0) return false;
            --m_cursor.measure;
            m_cursor.index = int(measure().events.size());
            if (m_cursor.index == 0) return true; // just moved into an empty measure
        }
        auto& evs = measure().events;
        evs.erase(evs.begin() + (m_cursor.index - 1));
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
        for (Measure& m : m_score.staves[size_t(m_cursor.staff)].measures) {
            if (m.events.empty() || e < &m.events.front() || e > &m.events.back()) continue;
            if (m.filled() - e->duration() + durationOf(e->durLog, newDots) > m_score.time.capacity()) {
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
            p = Pitch::fromDiatonic(d, keyAlterFor(((d % 7) + 7) % 7, m_score.keyFifths));
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
    const int size = int(measure().events.size());
    if (d > 0) {
        if (m_cursor.index < size) ++m_cursor.index;
        else if (m_cursor.measure < m_score.measureCount() - 1) { ++m_cursor.measure; m_cursor.index = 0; }
    } else {
        if (m_cursor.index > 0) --m_cursor.index;
        else if (m_cursor.measure > 0) { --m_cursor.measure; m_cursor.index = int(measure().events.size()); }
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
    const Clef oldClef = m_score.staves[size_t(m_cursor.staff)].clef;
    m_cursor.staff += d;
    clampCursor();
    const Clef newClef = m_score.staves[size_t(m_cursor.staff)].clef;
    // Keep the cursor at the same visual height on the new staff.
    m_cursor.diatonic += clefMiddleLine(newClef) - clefMiddleLine(oldClef);
    clampCursor();
    emit cursorChanged();
}

void Editor::moveHome() { m_cursor.index = 0; emit cursorChanged(); }
void Editor::moveEnd() { m_cursor.index = int(measure().events.size()); emit cursorChanged(); }

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
        for (Staff& s : m_score.staves) s.measures.erase(s.measures.begin() + m_cursor.measure);
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
        for (Staff& s : m_score.staves) s.measures.erase(s.measures.begin() + a, s.measures.begin() + b + 1);
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
        for (Staff& s : m_score.staves) s.measures.insert(s.measures.begin() + at, size_t(n), Measure{});
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
                m_score.staves[size_t(s)].measures[size_t(m)].events.clear();
        return true;
    });
}

void Editor::setKeySignature(int fifths)
{
    mutate(QStringLiteral("Armadura de clave"), [&] {
        fifths = std::clamp(fifths, -7, 7);
        if (fifths == m_score.keyFifths) return false;
        // Keep the music diatonic: notes that followed the old key follow the new one.
        for (Staff& st : m_score.staves)
            for (Measure& m : st.measures)
                for (Event& e : m.events)
                    for (Pitch& p : e.pitches)
                        if (p.alter == keyAlterFor(p.step, m_score.keyFifths)) p.alter = keyAlterFor(p.step, fifths);
        m_score.keyFifths = fifths;
        return true;
    });
}

void Editor::setTimeSignature(TimeSig t)
{
    mutate(QStringLiteral("Fórmula de compasso"), [&] {
        if (t.beats == m_score.time.beats && t.beatType == m_score.time.beatType) return false;
        // Rebar: pour every staff's events into measures of the new length (events that do not
        // fit at the end of a measure move to the next one).
        const Frac cap = t.capacity();
        size_t maxMeasures = 1;
        std::vector<std::vector<Measure>> rebarred;
        for (Staff& st : m_score.staves) {
            std::vector<Measure> out(1);
            for (Measure& m : st.measures) {
                if (m.events.empty()) { // keep empty measures as empty measures (whole-measure rests)
                    if (!out.back().events.empty()) out.emplace_back();
                    out.emplace_back();
                    continue;
                }
                for (Event& e : m.events) {
                    if (out.back().filled() + e.duration() > cap && !out.back().events.empty()) out.emplace_back();
                    out.back().events.push_back(std::move(e));
                    if (out.back().filled() >= cap) out.emplace_back();
                }
            }
            while (out.size() > 1 && out.back().events.empty()) out.pop_back();
            maxMeasures = std::max(maxMeasures, out.size());
            rebarred.push_back(std::move(out));
        }
        maxMeasures = std::max(maxMeasures, size_t(m_score.measureCount()));
        for (size_t s = 0; s < m_score.staves.size(); ++s) {
            rebarred[s].resize(maxMeasures);
            m_score.staves[s].measures = std::move(rebarred[s]);
        }
        m_score.time = t;
        m_cursor.index = 0;
        return true;
    });
}

void Editor::setClef(int staff, Clef clef)
{
    mutate(QStringLiteral("Clave"), [&] {
        if (staff < 0 || staff >= int(m_score.staves.size()) || m_score.staves[size_t(staff)].clef == clef) return false;
        const int shift = clefMiddleLine(clef) - clefMiddleLine(m_score.staves[size_t(staff)].clef);
        m_score.staves[size_t(staff)].clef = clef;
        if (staff == m_cursor.staff) m_cursor.diatonic += shift;
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
