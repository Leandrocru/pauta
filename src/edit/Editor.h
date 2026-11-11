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
    bool operator==(const Cursor&) const = default;
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
    void restore(const Score& s, const Cursor& c); // used by undo/redo

    // Note entry
    void enterDuration(int durLog);  // number keys: insert note / change duration
    void enterStep(int step);        // letter keys C..B
    void enterAtCursorPitch();       // Enter: insert note / toggle pitch in chord
    void enterRest();                // R: insert rest / toggle note<->rest
    void deleteEvent();              // Delete
    void backspace();                // Backspace
    void alter(int delta);           // + / -
    void naturalize();               // =
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

    const Event* currentEvent() const;

signals:
    void scoreChanged();
    void cursorChanged();
    void message(const QString& text);

private:
    bool mutate(const QString& text, const std::function<bool()>& fn);
    Measure& measure() { return m_score.staves[size_t(m_cursor.staff)].measures[size_t(m_cursor.measure)]; }
    const Measure& measure() const { return m_score.staves[size_t(m_cursor.staff)].measures[size_t(m_cursor.measure)]; }
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
};

} // namespace pauta
