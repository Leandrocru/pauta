#pragma once
// Editing controller: owns the Score, the cursor and the undo history.
// Implements "Speedy Entry"-style keyboard note entry.
#include "model/Score.h"

#include <QObject>
#include <QUndoStack>

#include <functional>

namespace pauta {

struct Cursor {
    int staff = 0;
    int measure = 0;
    int index = 0;      // event index inside the measure; == size means "append"
    int diatonic = 34;  // pitch under the cursor (B4)
    int voice = 0;      // active layer (0..3)
    bool operator==(const Cursor&) const = default;
};

// Rectangular region of measures across a range of staves (inclusive).
struct Selection {
    bool active = false;
    int staff0 = 0, staff1 = 0;
    int measure0 = 0, measure1 = 0;
    bool contains(int staff, int measure) const
    {
        return active && staff >= staff0 && staff <= staff1 && measure >= measure0 && measure <= measure1;
    }
};

class Editor : public QObject {
    Q_OBJECT
public:
    explicit Editor(QObject* parent = nullptr);

    const Score& score() const { return m_score; }
    const Cursor& cursor() const { return m_cursor; }
    QUndoStack* undoStack() { return &m_undo; }
    int durLog() const { return m_durLog; }

    void setScore(Score s);                // replaces the document, clears history
    void setCursor(const Cursor& c);
    void setDurLog(int durLog) { m_durLog = durLog; emit cursorChanged(); }
    void setVoice(int v);                  // active layer 0..3 (Finale: layers 1-4)
    void armTuplet(int actual);            // Ctrl+3 etc.: the next notes form a tuplet
    int armedTuplet() const { return m_tupArm ? m_tupArm : m_tupActual; }
    void setTempo(int qpm);
    void restore(const Score& s, const Cursor& c); // used by undo/redo

    // Note entry
    void enterDuration(int durLog);  // number keys: insert note / change duration
    void enterStep(int step);        // letter keys C..B
    void enterAtCursorPitch();       // Enter: insert note / toggle pitch in chord
    void enterRest();                // R: insert rest / toggle note<->rest
    void deleteEvent();              // Delete
    void backspace();                // Backspace
    void alter(int delta);           // + / -
    void naturalize();               // N
    void toggleTie();                // = or L (Finale-style: tie to the next note)
    void toggleDot();                // .
    void transpose(int steps);       // Shift+Up/Down on the current event

    // Navigation
    void moveStep(int d);
    void moveEvent(int d);
    void moveMeasure(int d);
    void moveStaff(int d);
    void moveHome();
    void moveEnd();

    // Structure
    void addMeasures(int n);
    void deleteMeasure();
    void deleteSelectedMeasures();
    void insertMeasuresBefore(int n);

    // Region selection (Selection / Measure tools)
    const Selection& selection() const { return m_sel; }
    void setSelection(const Selection& s);
    void selectAll();
    void clearSelection();
    void clearSelectionContents(); // Backspace/Delete on a selected region

    // Document-level changes (all undoable)
    void setKeySignature(int fifths);
    void setTimeSignature(TimeSig t);
    void setClef(int staff, Clef clef);
    void setStaffName(int staff, const QString& name);
    void addStaff(int index, const QString& name, Clef clef);
    void removeStaff(int staff);
    void setTitleComposer(const QString& title, const QString& composer);

    const Event* currentEvent() const;

signals:
    void scoreChanged();
    void cursorChanged();
    void selectionChanged();
    void message(const QString& text); // problem (the UI beeps)
    void hint(const QString& text);    // information only

private:
    bool mutate(const QString& text, const std::function<bool()>& fn);
    Measure& measure() { return m_score.staves[size_t(m_cursor.staff)].measures[size_t(m_cursor.measure)]; }
    const Measure& measure() const { return m_score.staves[size_t(m_cursor.staff)].measures[size_t(m_cursor.measure)]; }
    std::vector<Event>& evs() { return measure().voice(m_cursor.voice); }
    const std::vector<Event>& evs() const { return measure().voice(m_cursor.voice); }
    Event* currentEventMut();
    Event* targetEvent(); // current event, or the one just before the cursor
    Pitch cursorPitch() const;
    bool insertEvent(Event e);
    void advanceIfFull();
    void clampCursor();

    Score m_score;
    Cursor m_cursor;
    QUndoStack m_undo;
    int m_durLog = 2;
    Selection m_sel;
    // Tuplet being entered: armed (next note starts it) or active (filling it).
    int m_tupArm = 0;
    int m_tupActual = 0, m_tupNormal = 0;
    Frac m_tupRemaining; // written (base) time still to fill
};

} // namespace pauta
