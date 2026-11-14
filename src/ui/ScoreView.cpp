#include "ui/ScoreView.h"

#include "edit/Editor.h"
#include "render/Engraver.h"
#include "render/Renderer.h"
#include "render/Smufl.h"
#include "ui/Icons.h"

#include <QElapsedTimer>
#include <QGlyphRun>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace pauta {

namespace {
const QColor kAccent(30, 111, 217);

// Layer colours, as Finale users know them: 1 black-ish blue, 2 red, 3 green, 4 blue.
QColor layerColor(int v, int alpha = 255)
{
    static const QColor c[4] = { QColor(30, 111, 217), QColor(214, 40, 40), QColor(30, 150, 60), QColor(40, 70, 200) };
    QColor k = c[std::clamp(v, 0, 3)];
    k.setAlpha(alpha);
    return k;
}

// Colour of the Simple Entry note caret (layer 1: a strong purple, as Finale users expect).
QColor caretColor(int v, int alpha = 255)
{
    QColor k = v == 0 ? QColor(150, 40, 200) : layerColor(v);
    k.setAlpha(alpha);
    return k;
}
}

ScoreView::ScoreView(Editor* editor, QWidget* parent) : QWidget(parent), m_ed(editor)
{
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);
    connect(m_ed, &Editor::scoreChanged, this, &ScoreView::relayout);
    connect(m_ed, &Editor::cursorChanged, this, &ScoreView::onCursorChanged);
    connect(m_ed, &Editor::selectionChanged, this, qOverload<>(&QWidget::update));
    relayout();
    updateToolCursor();
}

// ---------------------------------------------------------------------------------------------
// Layout, geometry

void ScoreView::relayout()
{
    m_hover = Hit{}; // slot pointers die with the old layout (recomputed below)
    QElapsedTimer t;
    t.start();
    EngraveOptions opt;
    opt.continuous = m_continuous;
    m_layout = engrave(m_ed->score(), opt);
    m_layoutMs = double(t.nsecsElapsed()) / 1e6;
    emit layoutTimed(m_layoutMs);
    updateGeometry();
    resize(sizeHint());
    // Keep the Simple Entry preview under the mouse after the score changed (e.g. after a click).
    if (m_tool == Tool::SimpleEntry && underMouse()) m_hover = hitTest(mapFromGlobal(QCursor::pos()));
    update();
}

void ScoreView::setContinuous(bool c)
{
    if (c == m_continuous) return;
    m_continuous = c;
    relayout();
    onCursorChanged();
}

void ScoreView::setZoom(qreal z)
{
    z = std::clamp(z, 0.3, 6.0);
    if (qFuzzyCompare(z, m_zoom)) return;
    m_zoom = z;
    updateGeometry();
    resize(sizeHint());
    update();
    emit zoomChanged(m_zoom);
}

void ScoreView::zoomAt(const QPointF& widgetPos, qreal factor)
{
    const int page = std::max(0, pageAt(widgetPos.y()));
    const QRectF before = pageRect(page);
    const QPointF pagePt = (widgetPos - before.topLeft()) / m_zoom;
    setZoom(m_zoom * factor);
    const QRectF after = pageRect(page);
    const QPointF w = after.topLeft() + pagePt * m_zoom;
    emit ensureVisibleRequested(QRect(w.toPoint() - QPoint(1, 1), QSize(2, 2)));
}

void ScoreView::setPlaybackMeasure(int measure)
{
    m_playMeasure = measure;
    if (measure >= 0) {
        // Keep the sounding measure (all its staves) on screen.
        QRectF r;
        int page = -1;
        for (const MeasureSlot& s : m_layout.measureSlots) {
            if (s.measure != measure) continue;
            const QRectF sr(s.x0, s.staffTop - 2 * m_layout.sp, s.x1 - s.x0, 8 * m_layout.sp);
            r = r.isNull() ? sr : r.united(sr);
            page = s.page;
        }
        if (page >= 0) {
            const QRectF pr = pageRect(page);
            emit ensureVisibleRequested(QRectF(pr.topLeft() + r.topLeft() * m_zoom, r.size() * m_zoom).toAlignedRect());
        }
    }
    update();
}

void ScoreView::setTool(Tool t)
{
    m_tool = t;
    m_text.active = false;
    m_hover = Hit{};
    updateToolCursor();
    update();
}

void ScoreView::updateToolCursor()
{
    switch (m_tool) {
    case Tool::Hand: setCursor(Qt::OpenHandCursor); break;
    case Tool::Zoom: setCursor(QCursor(icons::tool(Tool::Zoom).pixmap(24, 24), 8, 8)); break;
    case Tool::SimpleEntry: setCursor(Qt::ArrowCursor); break;
    default: setCursor(Qt::ArrowCursor); break;
    }
}

QSize ScoreView::sizeHint() const
{
    const int n = std::max<int>(1, int(m_layout.pages.size()));
    const int w = int(std::ceil(m_layout.pageWidth * m_zoom)) + 2 * kGap;
    const int h = n * (int(std::ceil(m_layout.pageHeight * m_zoom)) + kGap) + kGap;
    return QSize(w, h);
}

QRectF ScoreView::pageRect(int page) const
{
    const qreal pw = m_layout.pageWidth * m_zoom;
    const qreal ph = m_layout.pageHeight * m_zoom;
    const qreal x = std::max<qreal>(kGap, (width() - pw) / 2);
    return QRectF(x, kGap + page * (std::ceil(ph) + kGap), pw, ph);
}

int ScoreView::pageAt(qreal y) const
{
    const qreal ph = std::ceil(m_layout.pageHeight * m_zoom) + kGap;
    return std::clamp(int((y - kGap / 2.0) / ph), 0, std::max(0, int(m_layout.pages.size()) - 1));
}

ScoreView::Hit ScoreView::hitTest(const QPointF& wp) const
{
    Hit h;
    for (int i = 0; i < int(m_layout.pages.size()); ++i) {
        const QRectF pr = pageRect(i);
        if (!pr.contains(wp)) continue;
        h.page = i;
        h.pagePt = (wp - pr.topLeft()) / m_zoom;
        const qreal sp = m_layout.sp;
        qreal best = 1e9;
        for (const MeasureSlot& s : m_layout.measureSlots) {
            if (s.page != i || h.pagePt.x() < s.x0 || h.pagePt.x() > s.x1) continue;
            const qreal d = std::abs(h.pagePt.y() - (s.staffTop + 2 * sp));
            if (d < 8 * sp && d < best) { best = d; h.slot = &s; }
        }
        if (h.slot) {
            const int pos = int(std::lround((h.slot->staffTop + 2 * sp - h.pagePt.y()) / (sp / 2)));
            h.diatonic = std::clamp(clefMiddleLine(h.slot->clef) + pos, 7, 7 * 8 + 6);
        }
        break;
    }
    return h;
}

// ---------------------------------------------------------------------------------------------
// Painting

void ScoreView::paintEvent(QPaintEvent* e)
{
    QPainter p(this);
    p.fillRect(e->rect(), QColor(0x6b, 0x6f, 0x76));
    for (int i = 0; i < int(m_layout.pages.size()); ++i) {
        const QRectF r = pageRect(i);
        if (!r.intersects(e->rect())) continue; // only visible pages are painted
        p.fillRect(r.translated(3, 3), QColor(0, 0, 0, 60));
        p.fillRect(r, Qt::white);
        p.save();
        p.translate(r.topLeft());
        p.scale(m_zoom, m_zoom);
        if (m_playMeasure >= 0) {
            for (const MeasureSlot& s : m_layout.measureSlots)
                if (s.page == i && s.measure == m_playMeasure)
                    p.fillRect(QRectF(s.x0, s.staffTop - 1.5 * m_layout.sp, s.x1 - s.x0, 7 * m_layout.sp), QColor(40, 170, 80, 45));
        }
        paintSelection(p, i);
        paintPage(p, m_layout, i);
        if (m_tool == Tool::SpeedyEntry) paintSpeedy(p, i);
        if (m_tool == Tool::SimpleEntry) paintGhost(p, i);
        if ((m_tool == Tool::Lyrics || m_tool == Tool::Chord) && m_text.active) {
            // Where the typed text goes: a box under (lyrics) or over (chords) the note.
            if (const MeasureSlot* s = m_layout.slot(m_text.staff, m_text.measure); s && s->page == i) {
                const auto& xs = s->eventX[size_t(m_text.voice)];
                if (m_text.index < int(xs.size())) {
                    const qreal sp = m_layout.sp;
                    const qreal x = xs[size_t(m_text.index)];
                    const qreal base = m_tool == Tool::Lyrics ? s->lyricBase + m_text.verse * 2.4 * sp : s->chordBase;
                    p.fillRect(QRectF(x - 0.5 * sp, s->staffTop - 1.5 * sp, 2.2 * sp, 7 * sp), QColor(30, 150, 60, 40));
                    QPen frame(QColor(30, 150, 60));
                    frame.setWidthF(0.12 * sp);
                    p.setPen(frame);
                    p.setBrush(Qt::NoBrush);
                    p.drawRect(QRectF(x - 1.6 * sp, base - 2.4 * sp, 4.4 * sp, 3.0 * sp));
                }
            }
        }
        if (m_tool == Tool::SmartShape && m_shapeStart.valid) {
            // Mark the first note of the shape being drawn.
            if (const MeasureSlot* s = m_layout.slot(m_shapeStart.staff, m_shapeStart.measure); s && s->page == i) {
                const auto& xs = s->eventX[size_t(m_shapeStart.voice)];
                if (m_shapeStart.index < int(xs.size()))
                    p.fillRect(QRectF(xs[size_t(m_shapeStart.index)] - 0.5 * m_layout.sp, s->staffTop - 1.5 * m_layout.sp,
                                      2.2 * m_layout.sp, 7 * m_layout.sp), QColor(30, 150, 60, 70));
            }
        }
        p.restore();
    }
}

void ScoreView::paintSelection(QPainter& p, int page)
{
    const Selection& sel = m_ed->selection();
    if (!sel.active) return;
    const qreal sp = m_layout.sp;
    const QColor fill(30, 111, 217, 48);
    for (const MeasureSlot& s : m_layout.measureSlots) {
        if (s.page != page || !sel.contains(s.staff, s.measure)) continue;
        qreal top = s.staffTop - 1.0 * sp;
        qreal bottom = s.staffTop + 5.0 * sp;
        if (s.staff < sel.staff1) {
            // extend down to the next staff so the selection reads as one block
            if (const MeasureSlot* next = m_layout.slot(s.staff + 1, s.measure)) bottom = next->staffTop - 1.0 * sp;
        }
        p.fillRect(QRectF(s.x0, top, s.x1 - s.x0, bottom - top), fill);
    }
}

QRectF ScoreView::cursorRectOnPage(int* page) const
{
    const Cursor& c = m_ed->cursor();
    const MeasureSlot* s = m_layout.slot(c.staff, c.measure);
    if (!s) return {};
    if (page) *page = s->page;
    const qreal sp = m_layout.sp;
    const qreal x = c.index < int(s->eventX[size_t(c.voice)].size()) ? s->eventX[size_t(c.voice)][size_t(c.index)] : s->appendX[size_t(c.voice)];
    const int pos = staffPosition(c.diatonic, s->clef);
    const qreal y = m_layout.yForPosition(*s, pos);
    const qreal top = std::min(s->staffTop - 3 * sp, y - sp);
    const qreal bottom = std::max(s->staffTop + 7 * sp, y + sp);
    return QRectF(std::min(x, s->x0) - sp, top, std::max(s->x1, x) - std::min(x, s->x0) + 2 * sp, bottom - top);
}

void ScoreView::paintSpeedy(QPainter& p, int page)
{
    const Cursor& c = m_ed->cursor();
    const MeasureSlot* s = m_layout.slot(c.staff, c.measure);
    if (!s || s->page != page) return;
    const qreal sp = m_layout.sp;
    const bool onEvent = c.index < int(s->eventX[size_t(c.voice)].size());
    const qreal x = onEvent ? s->eventX[size_t(c.voice)][size_t(c.index)] : s->appendX[size_t(c.voice)];
    const int pos = staffPosition(c.diatonic, s->clef);
    const qreal y = m_layout.yForPosition(*s, pos);

    p.save();
    // Editing frame around the active measure (Speedy Entry style).
    QPen frame(layerColor(c.voice));
    frame.setWidthF(0.18 * sp);
    p.setPen(frame);
    p.setBrush(layerColor(c.voice, 14));
    p.drawRect(QRectF(s->x0 + 0.15 * sp, s->staffTop - 2.5 * sp, s->x1 - s->x0 - 0.3 * sp, 9 * sp));

    // Vertical insertion bar.
    QPen bar(layerColor(c.voice, onEvent ? 70 : 170));
    bar.setWidthF(onEvent ? 2.6 * sp : 0.25 * sp);
    bar.setCapStyle(Qt::FlatCap);
    p.setPen(bar);
    const qreal bx = onEvent ? x + 0.6 * sp : x - 0.6 * sp;
    p.drawLine(QPointF(bx, s->staffTop - 2 * sp), QPointF(bx, s->staffTop + 6 * sp));

    // Ghost ledger lines + notehead at the cursor pitch.
    QPen ledger(kAccent);
    ledger.setWidthF(0.16 * sp);
    p.setPen(ledger);
    const qreal headW = smufl::Font::instance().advance(smufl::NoteheadBlack, sp);
    for (int lp = 6; lp <= pos; lp += 2)
        p.drawLine(QPointF(x - 0.35 * sp, m_layout.yForPosition(*s, lp)), QPointF(x + headW + 0.35 * sp, m_layout.yForPosition(*s, lp)));
    for (int lp = -6; lp >= pos; lp -= 2)
        p.drawLine(QPointF(x - 0.35 * sp, m_layout.yForPosition(*s, lp)), QPointF(x + headW + 0.35 * sp, m_layout.yForPosition(*s, lp)));
    QGlyphRun run;
    run.setRawFont(smufl::Font::instance().rawFont(sp));
    run.setGlyphIndexes({ smufl::Font::instance().glyphIndex(smufl::noteheadFor(m_ed->durLog())) });
    run.setPositions({ QPointF(x, y) });
    p.setPen(layerColor(c.voice, 200));
    p.drawGlyphRun(QPointF(0, 0), run);
    p.restore();
}

void ScoreView::paintGhost(QPainter& p, int page)
{
    const int v = m_ed->cursor().voice;
    if (m_hover.slot) {
        // Mouse over a staff: the note shows exactly where the click will put it.
        if (m_hover.page != page) return;
        const InsertPoint ip = insertPointFor(m_hover);
        if (!ip.slot) return;
        if (!ip.onEvent && ip.index < int(ip.slot->eventX[size_t(v)].size())) {
            // Inserting between notes: a thin bar marks the gap.
            QPen bar(caretColor(v, 120));
            bar.setWidthF(0.15 * m_layout.sp);
            p.setPen(bar);
            const qreal bx = ip.slot->eventX[size_t(v)][size_t(ip.index)] - 0.4 * m_layout.sp;
            p.drawLine(QPointF(bx, ip.slot->staffTop - m_layout.sp), QPointF(bx, ip.slot->staffTop + 5 * m_layout.sp));
        }
        drawNoteFigure(p, *ip.slot, ip.x, m_hover.diatonic, m_simpleRest, caretColor(v, 220));
        return;
    }
    // No mouse over the score: show the keyboard caret at the insertion point.
    const Cursor& c = m_ed->cursor();
    const MeasureSlot* s = m_layout.slot(c.staff, c.measure);
    if (!s || s->page != page) return;
    const std::vector<qreal>& xs = s->eventX[size_t(c.voice)];
    const bool onEvent = c.index < int(xs.size());
    const qreal x = onEvent ? xs[size_t(c.index)] : s->appendX[size_t(c.voice)];
    if (onEvent) {
        // The note under the caret is selected; the purple figure shows where Enter would add a chord note.
        p.fillRect(QRectF(x - 0.5 * m_layout.sp, s->staffTop - 1.5 * m_layout.sp, 2.2 * m_layout.sp, 7 * m_layout.sp), caretColor(v, 35));
        drawNoteFigure(p, *s, x, c.diatonic, m_simpleRest, caretColor(v, 150));
        return;
    }
    drawNoteFigure(p, *s, x, c.diatonic, m_simpleRest, caretColor(v, 220));
}

ScoreView::InsertPoint ScoreView::insertPointFor(const Hit& h) const
{
    InsertPoint ip;
    if (!h.slot) return ip;
    const MeasureSlot& s = *h.slot;
    const qreal sp = m_layout.sp;
    const int v = m_ed->cursor().voice;
    const std::vector<qreal>& xs = s.eventX[size_t(v)];
    ip.slot = h.slot;
    // On an existing note column: the click adds (or removes) a note in that chord.
    for (size_t k = 0; k < xs.size(); ++k) {
        if (std::abs(h.pagePt.x() - (xs[k] + 0.6 * sp)) < 1.0 * sp) {
            ip.index = int(k);
            ip.x = xs[k];
            ip.onEvent = true;
            return ip;
        }
    }
    // Elsewhere: inserted before the first note to the right (or at the end of the layer).
    ip.index = 0;
    while (ip.index < int(xs.size()) && xs[size_t(ip.index)] < h.pagePt.x()) ++ip.index;
    ip.x = ip.index < int(xs.size()) ? xs[size_t(ip.index)] - 1.4 * sp : s.appendX[size_t(v)];
    return ip;
}

// A complete note (head, stem, flags, ledger lines) or rest of the current duration.
void ScoreView::drawNoteFigure(QPainter& p, const MeasureSlot& s, qreal x, int diatonic, bool rest, const QColor& color) const
{
    const qreal sp = m_layout.sp;
    const auto& font = smufl::Font::instance();
    const int durLog = m_ed->durLog();
    QGlyphRun run;
    run.setRawFont(font.rawFont(sp));
    p.save();
    p.setPen(color);
    if (rest) {
        const int pos = durLog <= 0 ? 2 : 0;
        run.setGlyphIndexes({ font.glyphIndex(smufl::restFor(durLog)) });
        run.setPositions({ QPointF(x, m_layout.yForPosition(s, pos)) });
        p.drawGlyphRun(QPointF(0, 0), run);
        p.restore();
        return;
    }
    const int pos = staffPosition(diatonic, s.clef);
    const qreal y = m_layout.yForPosition(s, pos);
    const char16_t head = smufl::noteheadFor(durLog);
    const qreal headW = font.advance(head, sp);

    QPen thin(color);
    thin.setWidthF(0.16 * sp);
    thin.setCapStyle(Qt::FlatCap);
    p.setPen(thin);
    for (int lp = 6; lp <= pos; lp += 2)
        p.drawLine(QPointF(x - 0.35 * sp, m_layout.yForPosition(s, lp)), QPointF(x + headW + 0.35 * sp, m_layout.yForPosition(s, lp)));
    for (int lp = -6; lp >= pos; lp -= 2)
        p.drawLine(QPointF(x - 0.35 * sp, m_layout.yForPosition(s, lp)), QPointF(x + headW + 0.35 * sp, m_layout.yForPosition(s, lp)));

    std::vector<quint32> glyphs{ font.glyphIndex(head) };
    std::vector<QPointF> positions{ QPointF(x, y) };
    if (durLog >= 1) {
        const bool up = pos < 0;
        const qreal stemW = 0.12 * sp;
        const qreal len = (3.5 + 0.5 * std::max(0, durLog - 3)) * sp;
        const qreal sx = up ? x + headW - stemW / 2 : x + stemW / 2;
        const qreal tip = up ? std::min(y - len, m_layout.yForPosition(s, 0)) : std::max(y + len, m_layout.yForPosition(s, 0));
        QPen stem(color);
        stem.setWidthF(stemW);
        stem.setCapStyle(Qt::FlatCap);
        p.setPen(stem);
        p.drawLine(QPointF(sx, y + (up ? -0.17 : 0.17) * sp), QPointF(sx, tip));
        if (const char16_t f = smufl::flagFor(durLog, up)) {
            glyphs.push_back(font.glyphIndex(f));
            positions.push_back(QPointF(sx - stemW / 2, tip));
        }
    }
    run.setGlyphIndexes(QList<quint32>(glyphs.begin(), glyphs.end()));
    run.setPositions(QList<QPointF>(positions.begin(), positions.end()));
    p.setPen(color);
    p.drawGlyphRun(QPointF(0, 0), run);
    p.restore();
}

void ScoreView::onCursorChanged()
{
    if (m_tool == Tool::SpeedyEntry || m_tool == Tool::SimpleEntry) {
        int page = 0;
        const QRectF r = cursorRectOnPage(&page);
        if (!r.isNull()) {
            const QRectF pr = pageRect(page);
            const QRectF wr(pr.topLeft() + r.topLeft() * m_zoom, r.size() * m_zoom);
            emit ensureVisibleRequested(wr.toAlignedRect());
        }
    }
    update();
}

// ---------------------------------------------------------------------------------------------
// Keyboard

bool ScoreView::entryKeys(QKeyEvent* e)
{
    const auto mods = e->modifiers();
    const bool ctrl = mods & Qt::ControlModifier;
    const bool shift = mods & Qt::ShiftModifier;
    const bool alt = mods & Qt::AltModifier;
    Editor& ed = *m_ed;

    switch (e->key()) {
    case Qt::Key_Up:
        if (alt) ed.moveStaff(-1);
        else if (shift) ed.transpose(ctrl ? 7 : 1);
        else ed.moveStep(ctrl ? 7 : 1);
        return true;
    case Qt::Key_Down:
        if (alt) ed.moveStaff(1);
        else if (shift) ed.transpose(ctrl ? -7 : -1);
        else ed.moveStep(ctrl ? -7 : -1);
        return true;
    case Qt::Key_Left: ctrl ? ed.moveMeasure(-1) : ed.moveEvent(-1); return true;
    case Qt::Key_Right: ctrl ? ed.moveMeasure(1) : ed.moveEvent(1); return true;
    case Qt::Key_Home: ed.moveHome(); return true;
    case Qt::Key_End: ed.moveEnd(); return true;
    case Qt::Key_Return:
    case Qt::Key_Enter: ed.enterAtCursorPitch(); return true;
    case Qt::Key_Delete: ed.deleteEvent(); return true;
    case Qt::Key_Backspace: ed.backspace(); return true;
    case Qt::Key_Plus: ed.alter(1); return true;
    case Qt::Key_Minus: ed.alter(-1); return true;
    case Qt::Key_Equal: ed.toggleTie(); return true;
    case Qt::Key_L: if (!ctrl) { ed.toggleTie(); return true; } break;
    case Qt::Key_N: if (!ctrl) { ed.naturalize(); return true; } break;
    case Qt::Key_Period: ed.toggleDot(); return true;
    case Qt::Key_R: if (!ctrl) { ed.enterRest(); return true; } break;
    default: break;
    }
    if (ctrl && !alt && e->key() >= Qt::Key_2 && e->key() <= Qt::Key_9) {
        ed.armTuplet(e->key() - Qt::Key_0);
        return true;
    }
    if (!ctrl && !alt) {
        const int k = e->key();
        if (k >= Qt::Key_1 && k <= Qt::Key_8) {
            static const int map[8] = { 6, 5, 4, 3, 2, 1, 0, -1 }; // Finale-style: 5 = quarter
            ed.enterDuration(map[k - Qt::Key_1]);
            return true;
        }
        static const QString letters = QStringLiteral("CDEFGAB");
        const QString t = e->text().toUpper();
        if (t.size() == 1) {
            const int step = letters.indexOf(t.at(0));
            if (step >= 0) { ed.enterStep(step); return true; }
        }
    }
    return false;
}

// While typing lyrics or chord symbols, keys that are also shortcuts (Space = play, letters,
// Backspace, arrows) must reach the score instead of triggering the menu actions.
bool ScoreView::event(QEvent* e)
{
    if (e->type() == QEvent::ShortcutOverride && m_text.active && (m_tool == Tool::Lyrics || m_tool == Tool::Chord)) {
        auto* ke = static_cast<QKeyEvent*>(e);
        const bool plain = !(ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier));
        if (plain) {
            e->accept();
            return true;
        }
    }
    return QWidget::event(e);
}

bool ScoreView::stepText(int dir)
{
    const bool lyrics = m_tool == Tool::Lyrics;
    int m = m_text.measure, i = m_text.index;
    const int count = m_ed->score().measureCount();
    while (true) {
        i += dir;
        while (true) {
            const auto& evs = m_ed->score().staves[size_t(m_text.staff)].measures[size_t(m)].voice(m_text.voice);
            if (i >= 0 && i < int(evs.size())) break;
            if (dir > 0) { if (++m >= count) return false; i = 0; }
            else {
                if (--m < 0) return false;
                i = int(m_ed->score().staves[size_t(m_text.staff)].measures[size_t(m)].voice(m_text.voice).size()) - 1;
            }
        }
        const Event* ev = m_ed->eventAt(m_text.staff, m, m_text.voice, i);
        if (ev && (!lyrics || !ev->isRest())) break; // syllables go on notes, chord symbols on any event
    }
    m_text.measure = m;
    m_text.index = i;
    Cursor c = m_ed->cursor();
    c.staff = m_text.staff;
    c.measure = m;
    c.voice = m_text.voice;
    c.index = i;
    m_ed->setCursor(c);
    update();
    return true;
}

bool ScoreView::textKeys(QKeyEvent* e)
{
    if (!m_text.active) return false;
    const bool lyrics = m_tool == Tool::Lyrics;
    const Event* ev = m_ed->eventAt(m_text.staff, m_text.measure, m_text.voice, m_text.index);
    if (!ev) { m_text.active = false; return false; }
    Syllable sy = lyrics && m_text.verse < int(ev->lyrics.size()) ? ev->lyrics[size_t(m_text.verse)] : Syllable{};
    QString chord = ev->chordSymbol;
    auto store = [&] {
        if (lyrics) m_ed->setSyllable(m_text.staff, m_text.measure, m_text.voice, m_text.index, m_text.verse, sy);
        else m_ed->setChordSymbol(m_text.staff, m_text.measure, m_text.voice, m_text.index, chord);
    };
    switch (e->key()) {
    case Qt::Key_Escape: m_text.active = false; update(); return true;
    case Qt::Key_Left: stepText(-1); return true;
    case Qt::Key_Right:
    case Qt::Key_Space:
    case Qt::Key_Tab:
    case Qt::Key_Return:
    case Qt::Key_Enter: stepText(1); return true;
    case Qt::Key_Up: if (lyrics) setVerse(m_text.verse - 1); return true;
    case Qt::Key_Down: if (lyrics) setVerse(m_text.verse + 1); return true;
    case Qt::Key_Backspace:
        if (lyrics ? sy.text.isEmpty() : chord.isEmpty()) { stepText(-1); return true; }
        if (lyrics) { sy.text.chop(1); sy.hyphen = sy.extender = false; } else chord.chop(1);
        store();
        return true;
    default: break;
    }
    const QString t = e->text();
    if (t.isEmpty() || !t.at(0).isPrint()) return false;
    if (lyrics && t == u"-") { // end of syllable, the word goes on
        if (sy.text.isEmpty()) return true;
        sy.hyphen = true;
        sy.extender = false;
        store();
        stepText(1);
        return true;
    }
    if (lyrics && t == u"_") { // melisma line
        if (sy.text.isEmpty()) return true;
        sy.extender = true;
        sy.hyphen = false;
        store();
        stepText(1);
        return true;
    }
    if (lyrics) sy.text += t; else chord += t;
    store();
    return true;
}

void ScoreView::keyPressEvent(QKeyEvent* e)
{
    if ((m_tool == Tool::Lyrics || m_tool == Tool::Chord) && textKeys(e)) return;
    // Alt+1..4 chooses the active layer in every tool (as Finale's layer selector).
    if ((e->modifiers() & Qt::AltModifier) && e->key() >= Qt::Key_1 && e->key() <= Qt::Key_4) {
        m_ed->setVoice(e->key() - Qt::Key_1);
        return;
    }
    switch (m_tool) {
    case Tool::SimpleEntry: {
        // The keyboard takes over the caret: the preview leaves the mouse and follows the keys.
        if (m_hover.slot) {
            m_hover = Hit{};
            setCursor(Qt::ArrowCursor);
            update();
        }
        const auto mods = e->modifiers();
        const bool ctrl = mods & Qt::ControlModifier, alt = mods & Qt::AltModifier, shift = mods & Qt::ShiftModifier;
        const int k = e->key();
        if (!ctrl && !alt && k >= Qt::Key_1 && k <= Qt::Key_8) {
            static const int map[8] = { 6, 5, 4, 3, 2, 1, 0, -1 }; // 5 = quarter, as in Finale
            m_ed->changeDurationInPlace(map[k - Qt::Key_1]);
            return;
        }
        if (!ctrl && !alt && k == Qt::Key_0) { m_ed->enterRest(); return; }
        if (shift && !ctrl && !alt && k >= Qt::Key_A && k <= Qt::Key_G) {
            static const QString letters = QStringLiteral("CDEFGAB");
            m_ed->addStepToChord(letters.indexOf(QChar(char16_t(k))));
            return;
        }
        if (entryKeys(e)) return;
        break;
    }
    case Tool::SpeedyEntry:
        if (entryKeys(e)) return;
        break;
    case Tool::Selection:
    case Tool::Measure:
        if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) { m_ed->clearSelectionContents(); return; }
        if (e->key() == Qt::Key_Escape) { m_ed->clearSelection(); return; }
        break;
    default:
        break;
    }
    QWidget::keyPressEvent(e);
}

// ---------------------------------------------------------------------------------------------
// Mouse

void ScoreView::simpleEntryClick(const Hit& h)
{
    const InsertPoint ip = insertPointFor(h);
    if (!ip.slot) return;
    Cursor c = m_ed->cursor();
    c.staff = ip.slot->staff;
    c.measure = ip.slot->measure;
    c.diatonic = h.diatonic;
    c.index = ip.index;
    m_ed->setCursor(c);
    if (m_simpleRest) m_ed->enterRest();
    else m_ed->enterAtCursorPitch();
}

void ScoreView::mousePressEvent(QMouseEvent* e)
{
    setFocus();
    const Hit h = hitTest(e->position());
    switch (m_tool) {
    case Tool::Hand:
        m_panning = true;
        m_lastGlobal = e->globalPosition().toPoint();
        setCursor(Qt::ClosedHandCursor);
        return;
    case Tool::Zoom: {
        const bool out = e->button() == Qt::RightButton || (e->modifiers() & Qt::ControlModifier);
        zoomAt(e->position(), out ? 1 / 1.25 : 1.25);
        return;
    }
    case Tool::Selection:
    case Tool::Measure:
        if (!h.slot) { m_ed->clearSelection(); return; }
        if ((e->modifiers() & Qt::ShiftModifier) && m_ed->selection().active) {
            m_ed->setSelection(Selection{ true, m_anchorStaff, h.slot->staff, m_anchorMeasure, h.slot->measure });
        } else {
            m_anchorStaff = h.slot->staff;
            m_anchorMeasure = h.slot->measure;
            m_ed->setSelection(Selection{ true, m_anchorStaff, m_anchorStaff, m_anchorMeasure, m_anchorMeasure });
        }
        m_selecting = true;
        return;
    case Tool::SimpleEntry:
        if (h.slot && e->button() == Qt::LeftButton) simpleEntryClick(h);
        return;
    case Tool::SpeedyEntry: {
        if (!h.slot) return;
        const qreal sp = m_layout.sp;
        Cursor c = m_ed->cursor();
        c.staff = h.slot->staff;
        c.measure = h.slot->measure;
        c.index = int(h.slot->eventX[size_t(c.voice)].size());
        for (size_t k = 0; k < h.slot->eventX[size_t(c.voice)].size(); ++k)
            if (h.pagePt.x() < h.slot->eventX[size_t(c.voice)][k] + 1.6 * sp) { c.index = int(k); break; }
        c.diatonic = h.diatonic;
        m_ed->setCursor(c);
        return;
    }
    case Tool::Lyrics:
    case Tool::Chord: {
        // Click a note to start typing its syllable / chord symbol.
        if (!h.slot) { m_text.active = false; update(); return; }
        const qreal sp = m_layout.sp;
        int bestV = -1, bestK = -1;
        qreal bestD = 1.8 * sp;
        for (int v = 0; v < kVoices; ++v)
            for (size_t k = 0; k < h.slot->eventX[size_t(v)].size(); ++k) {
                const qreal d = std::abs(h.pagePt.x() - (h.slot->eventX[size_t(v)][k] + 0.6 * sp));
                if (d < bestD) { bestD = d; bestV = v; bestK = int(k); }
            }
        if (bestK < 0) return;
        m_text.active = true;
        m_text.staff = h.slot->staff;
        m_text.measure = h.slot->measure;
        m_text.voice = bestV;
        m_text.index = bestK;
        Cursor c = m_ed->cursor();
        c.staff = m_text.staff;
        c.measure = m_text.measure;
        c.voice = bestV;
        c.index = bestK;
        m_ed->setCursor(c);
        update();
        return;
    }
    case Tool::SmartShape: {
        // First click: the first note; second click: the last note.
        if (!h.slot) { m_shapeStart = {}; update(); return; }
        const qreal sp = m_layout.sp;
        int bestV = -1, bestK = -1;
        qreal bestD = 1.6 * sp;
        for (int v = 0; v < kVoices; ++v)
            for (size_t k = 0; k < h.slot->eventX[size_t(v)].size(); ++k) {
                const qreal d = std::abs(h.pagePt.x() - (h.slot->eventX[size_t(v)][k] + 0.6 * sp));
                if (d < bestD) { bestD = d; bestV = v; bestK = int(k); }
            }
        if (bestK < 0) return;
        if (!m_shapeStart.valid || m_shapeStart.staff != h.slot->staff || m_shapeStart.voice != bestV) {
            m_shapeStart = { true, h.slot->staff, bestV, h.slot->measure, bestK };
        } else {
            m_ed->addSmartShape(m_armedShape, m_shapeStart.staff, m_shapeStart.voice, m_shapeStart.measure,
                                m_shapeStart.index, h.slot->measure, bestK);
            m_shapeStart = {};
        }
        update();
        return;
    }
    case Tool::Articulation:
    case Tool::Expression: {
        // Click on a note: it gets the articulation / dynamic chosen in the palette.
        if (!h.slot) return;
        const qreal sp = m_layout.sp;
        Cursor c = m_ed->cursor();
        int bestV = -1, bestK = -1;
        qreal bestD = 1.6 * sp;
        for (int v = 0; v < kVoices; ++v)
            for (size_t k = 0; k < h.slot->eventX[size_t(v)].size(); ++k) {
                const qreal d = std::abs(h.pagePt.x() - (h.slot->eventX[size_t(v)][k] + 0.6 * sp));
                if (d < bestD) { bestD = d; bestV = v; bestK = int(k); }
            }
        if (bestK < 0) return;
        m_ed->clearSelection();
        c.staff = h.slot->staff;
        c.measure = h.slot->measure;
        c.voice = bestV;
        c.index = bestK;
        m_ed->setCursor(c);
        if (m_tool == Tool::Articulation) m_ed->toggleArticulation(m_armedArtic);
        else m_ed->setDynamic(m_armedDyn);
        return;
    }
    case Tool::Staff:
    case Tool::Clef:
    case Tool::KeySig:
    case Tool::TimeSig:
    case Tool::Repeat:
        if (h.slot) emit toolClicked(m_tool, h.slot->staff, h.slot->measure);
        return;
    default:
        return;
    }
}

void ScoreView::mouseMoveEvent(QMouseEvent* e)
{
    if (m_panning) {
        const QPoint g = e->globalPosition().toPoint();
        emit scrollByRequested(m_lastGlobal.x() - g.x(), m_lastGlobal.y() - g.y());
        m_lastGlobal = g;
        return;
    }
    if (m_selecting && (e->buttons() & Qt::LeftButton)) {
        const Hit h = hitTest(e->position());
        if (h.slot) m_ed->setSelection(Selection{ true, m_anchorStaff, h.slot->staff, m_anchorMeasure, h.slot->measure });
        return;
    }
    if (m_tool == Tool::SimpleEntry) {
        const Hit h = hitTest(e->position());
        // Over a staff the note figure itself is the pointer (as in Finale's Simple Entry).
        setCursor(h.slot ? Qt::BlankCursor : Qt::ArrowCursor);
        if (h.slot != m_hover.slot || h.diatonic != m_hover.diatonic || h.pagePt != m_hover.pagePt) {
            m_hover = h;
            update();
        }
    }
}

void ScoreView::mouseReleaseEvent(QMouseEvent*)
{
    if (m_panning) {
        m_panning = false;
        setCursor(Qt::OpenHandCursor);
    }
    m_selecting = false;
}

void ScoreView::leaveEvent(QEvent*)
{
    updateToolCursor();
    if (m_hover.slot) {
        m_hover = Hit{};
        update();
    }
}

void ScoreView::wheelEvent(QWheelEvent* e)
{
    if (e->modifiers() & Qt::ControlModifier) {
        zoomAt(e->position(), std::pow(1.15, e->angleDelta().y() / 120.0));
        e->accept();
        return;
    }
    QWidget::wheelEvent(e);
}

} // namespace pauta
