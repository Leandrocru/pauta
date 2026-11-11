#include "ui/ScoreView.h"

#include "edit/Editor.h"
#include "render/Engraver.h"
#include "render/Renderer.h"
#include "render/Smufl.h"

#include <QElapsedTimer>
#include <QGlyphRun>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace pauta {

ScoreView::ScoreView(Editor* editor, QWidget* parent) : QWidget(parent), m_ed(editor)
{
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    connect(m_ed, &Editor::scoreChanged, this, &ScoreView::relayout);
    connect(m_ed, &Editor::cursorChanged, this, &ScoreView::onCursorChanged);
    relayout();
}

void ScoreView::relayout()
{
    QElapsedTimer t;
    t.start();
    m_layout = engrave(m_ed->score());
    m_layoutMs = double(t.nsecsElapsed()) / 1e6;
    emit layoutTimed(m_layoutMs);
    updateGeometry();
    resize(sizeHint());
    update();
}

void ScoreView::setZoom(qreal z)
{
    z = std::clamp(z, 0.4, 5.0);
    if (qFuzzyCompare(z, m_zoom)) return;
    m_zoom = z;
    updateGeometry();
    resize(sizeHint());
    update();
    emit zoomChanged(m_zoom);
    onCursorChanged();
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
        paintPage(p, m_layout, i);
        paintCursor(p, i);
        p.restore();
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
    const qreal top = std::min(s->staffTop - 2 * sp, y - sp);
    const qreal bottom = std::max(s->staffTop + 6 * sp, y + sp);
    return QRectF(x - sp, top, 3.5 * sp, bottom - top);
}

void ScoreView::paintCursor(QPainter& p, int page)
{
    const Cursor& c = m_ed->cursor();
    const MeasureSlot* s = m_layout.slot(c.staff, c.measure);
    if (!s || s->page != page) return;
    const qreal sp = m_layout.sp;
    const QColor accent(0x1e, 0x6f, 0xd9);
    const bool onEvent = c.index < int(s->eventX.size());
    const qreal x = onEvent ? s->eventX[size_t(c.index)] : s->appendX;
    const int pos = staffPosition(c.diatonic, s->clef);
    const qreal y = m_layout.yForPosition(*s, pos);

    p.save();
    // Highlight the current measure on this staff.
    p.fillRect(QRectF(s->x0, s->staffTop - 0.5 * sp, s->x1 - s->x0, 5 * sp), QColor(30, 111, 217, 18));
    // Vertical insertion bar.
    QPen bar(QColor(30, 111, 217, onEvent ? 70 : 150));
    bar.setWidthF(onEvent ? 2.6 * sp : 0.25 * sp);
    bar.setCapStyle(Qt::FlatCap);
    p.setPen(bar);
    const qreal bx = onEvent ? x + 0.6 * sp : x - 0.6 * sp;
    p.drawLine(QPointF(bx, s->staffTop - 1.5 * sp), QPointF(bx, s->staffTop + 5.5 * sp));

    // Ghost ledger lines + notehead at the cursor pitch.
    QPen ledger(accent);
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

void ScoreView::onCursorChanged()
{
    int page = 0;
    const QRectF r = cursorRectOnPage(&page);
    if (!r.isNull()) {
        const QRectF pr = pageRect(page);
        const QRectF wr(pr.topLeft() + r.topLeft() * m_zoom, r.size() * m_zoom);
        emit ensureVisibleRequested(wr.toAlignedRect());
    }
    update();
}

void ScoreView::keyPressEvent(QKeyEvent* e)
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
        return;
    case Qt::Key_Down:
        if (alt) ed.moveStaff(1);
        else if (shift) ed.transpose(ctrl ? -7 : -1);
        else ed.moveStep(ctrl ? -7 : -1);
        return;
    case Qt::Key_Left: ctrl ? ed.moveMeasure(-1) : ed.moveEvent(-1); return;
    case Qt::Key_Right: ctrl ? ed.moveMeasure(1) : ed.moveEvent(1); return;
    case Qt::Key_Home: ed.moveHome(); return;
    case Qt::Key_End: ed.moveEnd(); return;
    case Qt::Key_Return:
    case Qt::Key_Enter: ed.enterAtCursorPitch(); return;
    case Qt::Key_Delete: ed.deleteEvent(); return;
    case Qt::Key_Backspace: ed.backspace(); return;
    case Qt::Key_Plus: ed.alter(1); return;
    case Qt::Key_Minus: ed.alter(-1); return;
    case Qt::Key_Equal: ed.naturalize(); return;
    case Qt::Key_Period: ed.toggleDot(); return;
    case Qt::Key_R: if (!ctrl) { ed.enterRest(); return; } break;
    default: break;
    }

    if (!ctrl && !alt) {
        // Number keys: duration (Finale-style numbering: 5 = quarter note).
        const int k = e->key();
        if (k >= Qt::Key_1 && k <= Qt::Key_8) {
            static const int map[8] = { 6, 5, 4, 3, 2, 1, 0, -1 }; // 1=64th .. 8=breve
            ed.enterDuration(map[k - Qt::Key_1]);
            return;
        }
        // Letter keys: note names (C D E F G A B).
        static const QString letters = QStringLiteral("CDEFGAB");
        const QString t = e->text().toUpper();
        if (t.size() == 1) {
            const int step = letters.indexOf(t.at(0));
            if (step >= 0) { ed.enterStep(step); return; }
        }
    }
    QWidget::keyPressEvent(e);
}

void ScoreView::mousePressEvent(QMouseEvent* e)
{
    setFocus();
    const QPointF wp = e->position();
    for (int i = 0; i < int(m_layout.pages.size()); ++i) {
        const QRectF pr = pageRect(i);
        if (!pr.contains(wp)) continue;
        const QPointF pp = (wp - pr.topLeft()) / m_zoom;
        const qreal sp = m_layout.sp;
        const MeasureSlot* best = nullptr;
        qreal bestDist = 1e9;
        for (const MeasureSlot& s : m_layout.measureSlots) {
            if (s.page != i || pp.x() < s.x0 || pp.x() > s.x1) continue;
            const qreal mid = s.staffTop + 2 * sp;
            const qreal d = std::abs(pp.y() - mid);
            if (d < 8 * sp && d < bestDist) { bestDist = d; best = &s; }
        }
        if (!best) return;
        Cursor c = m_ed->cursor();
        c.staff = best->staff;
        c.measure = best->measure;
        c.index = int(best->eventX.size());
        for (size_t k = 0; k < best->eventX.size(); ++k) {
            if (pp.x() < best->eventX[k] + 1.6 * sp) { c.index = int(k); break; }
        }
        const int pos = int(std::lround((best->staffTop + 2 * sp - pp.y()) / (sp / 2)));
        c.diatonic = clefMiddleLine(best->clef) + pos;
        m_ed->setCursor(c);
        return;
    }
}

void ScoreView::wheelEvent(QWheelEvent* e)
{
    if (e->modifiers() & Qt::ControlModifier) {
        const qreal steps = e->angleDelta().y() / 120.0;
        setZoom(m_zoom * std::pow(1.15, steps));
        e->accept();
        return;
    }
    QWidget::wheelEvent(e);
}

} // namespace pauta
