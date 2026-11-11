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
    m_hover = Hit{}; // slot pointers die with the old layout
    QElapsedTimer t;
    t.start();
    EngraveOptions opt;
    opt.continuous = m_continuous;
    m_layout = engrave(m_ed->score(), opt);
    m_layoutMs = double(t.nsecsElapsed()) / 1e6;
    emit layoutTimed(m_layoutMs);
    updateGeometry();
    resize(sizeHint());
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

void ScoreView::setTool(Tool t)
{
    m_tool = t;
    m_hover = Hit{};
    updateToolCursor();
    update();
}

void ScoreView::updateToolCursor()
{
    switch (m_tool) {
    case Tool::Hand: setCursor(Qt::OpenHandCursor); break;
    case Tool::Zoom: setCursor(QCursor(icons::tool(Tool::Zoom).pixmap(24, 24), 8, 8)); break;
    case Tool::SimpleEntry: setCursor(Qt::CrossCursor); break;
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
        paintSelection(p, i);
        paintPage(p, m_layout, i);
        if (m_tool == Tool::SpeedyEntry) paintSpeedy(p, i);
        if (m_tool == Tool::SimpleEntry) paintGhost(p, i);
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
    const qreal x = c.index < int(s->eventX.size()) ? s->eventX[size_t(c.index)] : s->appendX;
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
    const bool onEvent = c.index < int(s->eventX.size());
    const qreal x = onEvent ? s->eventX[size_t(c.index)] : s->appendX;
    const int pos = staffPosition(c.diatonic, s->clef);
    const qreal y = m_layout.yForPosition(*s, pos);

    p.save();
    // Editing frame around the active measure (Speedy Entry style).
    QPen frame(kAccent);
    frame.setWidthF(0.18 * sp);
    p.setPen(frame);
    p.setBrush(QColor(30, 111, 217, 14));
    p.drawRect(QRectF(s->x0 + 0.15 * sp, s->staffTop - 2.5 * sp, s->x1 - s->x0 - 0.3 * sp, 9 * sp));

    // Vertical insertion bar.
    QPen bar(QColor(30, 111, 217, onEvent ? 70 : 170));
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
    p.setPen(QColor(30, 111, 217, 200));
    p.drawGlyphRun(QPointF(0, 0), run);
    p.restore();
}

void ScoreView::paintGhost(QPainter& p, int page)
{
    if (!m_hover.slot || m_hover.page != page) return;
    const MeasureSlot& s = *m_hover.slot;
    const qreal sp = m_layout.sp;
    const int pos = staffPosition(m_hover.diatonic, s.clef);
    const qreal x = m_hover.pagePt.x() - 0.6 * sp;
    const qreal y = m_simpleRest ? m_layout.yForPosition(s, 0) : m_layout.yForPosition(s, pos);
    p.save();
    QPen ledger(kAccent);
    ledger.setWidthF(0.16 * sp);
    p.setPen(ledger);
    if (!m_simpleRest) {
        const qreal headW = smufl::Font::instance().advance(smufl::NoteheadBlack, sp);
        for (int lp = 6; lp <= pos; lp += 2)
            p.drawLine(QPointF(x - 0.35 * sp, m_layout.yForPosition(s, lp)), QPointF(x + headW + 0.35 * sp, m_layout.yForPosition(s, lp)));
        for (int lp = -6; lp >= pos; lp -= 2)
            p.drawLine(QPointF(x - 0.35 * sp, m_layout.yForPosition(s, lp)), QPointF(x + headW + 0.35 * sp, m_layout.yForPosition(s, lp)));
    }
    QGlyphRun run;
    run.setRawFont(smufl::Font::instance().rawFont(sp));
    const char16_t g = m_simpleRest ? smufl::restFor(m_ed->durLog()) : smufl::noteheadFor(m_ed->durLog());
    run.setGlyphIndexes({ smufl::Font::instance().glyphIndex(g) });
    run.setPositions({ QPointF(x, y) });
    p.setPen(QColor(30, 111, 217, 150));
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
    case Qt::Key_Equal: ed.naturalize(); return true;
    case Qt::Key_Period: ed.toggleDot(); return true;
    case Qt::Key_R: if (!ctrl) { ed.enterRest(); return true; } break;
    default: break;
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

void ScoreView::keyPressEvent(QKeyEvent* e)
{
    switch (m_tool) {
    case Tool::SpeedyEntry:
    case Tool::SimpleEntry:
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
    const MeasureSlot& s = *h.slot;
    const qreal sp = m_layout.sp;
    Cursor c = m_ed->cursor();
    c.staff = s.staff;
    c.measure = s.measure;
    c.diatonic = h.diatonic;
    // Clicking on an existing note column adds/removes that pitch (chord); elsewhere inserts.
    for (size_t k = 0; k < s.eventX.size(); ++k) {
        if (std::abs(h.pagePt.x() - (s.eventX[k] + 0.6 * sp)) < 1.0 * sp) {
            c.index = int(k);
            m_ed->setCursor(c);
            if (m_simpleRest) m_ed->enterRest();
            else m_ed->enterAtCursorPitch();
            return;
        }
    }
    c.index = 0;
    while (c.index < int(s.eventX.size()) && s.eventX[size_t(c.index)] < h.pagePt.x()) ++c.index;
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
        c.index = int(h.slot->eventX.size());
        for (size_t k = 0; k < h.slot->eventX.size(); ++k)
            if (h.pagePt.x() < h.slot->eventX[k] + 1.6 * sp) { c.index = int(k); break; }
        c.diatonic = h.diatonic;
        m_ed->setCursor(c);
        return;
    }
    case Tool::Staff:
    case Tool::Clef:
    case Tool::KeySig:
    case Tool::TimeSig:
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
