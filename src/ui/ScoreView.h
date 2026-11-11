#pragma once
// Page view of the score: paints only visible pages, handles Speedy Entry keys and mouse.
#include "render/Layout.h"

#include <QWidget>

namespace pauta {

class Editor;

class ScoreView : public QWidget {
    Q_OBJECT
public:
    explicit ScoreView(Editor* editor, QWidget* parent = nullptr);

    const Layout& layout() const { return m_layout; }
    qreal zoom() const { return m_zoom; }
    void setZoom(qreal z);
    double lastLayoutMs() const { return m_layoutMs; }

    QSize sizeHint() const override;

signals:
    void ensureVisibleRequested(const QRect& r);
    void zoomChanged(qreal zoom);
    void layoutTimed(double ms);

protected:
    void paintEvent(QPaintEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    void relayout();
    void onCursorChanged();
    QRectF pageRect(int page) const;            // in widget pixels
    QRectF cursorRectOnPage(int* page) const;   // in page units
    void paintCursor(QPainter& p, int page);

    Editor* m_ed;
    Layout m_layout;
    qreal m_zoom = 1.5;  // widget pixels per page point
    double m_layoutMs = 0;
    static constexpr int kGap = 24;
};

} // namespace pauta
