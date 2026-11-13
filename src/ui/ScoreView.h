#pragma once
// Score canvas: page view or scroll view, paints only visible pages, and interprets mouse and
// keyboard according to the active tool of the main tool palette.
#include "render/Layout.h"
#include "ui/Tools.h"

#include <QWidget>

namespace pauta {

class Editor;

class ScoreView : public QWidget {
    Q_OBJECT
public:
    explicit ScoreView(Editor* editor, QWidget* parent = nullptr);

    const Layout& layout() const { return m_layout; }
    double lastLayoutMs() const { return m_layoutMs; }

    qreal zoom() const { return m_zoom; }
    void setZoom(qreal z);
    Tool tool() const { return m_tool; }
    void setTool(Tool t);
    bool continuous() const { return m_continuous; }
    void setContinuous(bool c);
    void setSimpleRestMode(bool rest) { m_simpleRest = rest; update(); }
    void setPlaybackMeasure(int measure); // -1 = none
    void setArmedArticulation(unsigned a) { m_armedArtic = a; }
    void setArmedDynamic(int d) { m_armedDyn = d; }
    void setArmedShape(int kind) { m_armedShape = kind; m_shapeStart = {}; update(); }
    void setVerse(int verse) { m_text.verse = std::clamp(verse, 0, 9); update(); }
    int verse() const { return m_text.verse; }

    QRectF pageRect(int page) const; // in widget pixels
    int pageAt(qreal y) const;       // page under a widget y coordinate
    QSize sizeHint() const override;

signals:
    void ensureVisibleRequested(const QRect& r);
    void scrollByRequested(int dx, int dy);
    void zoomChanged(qreal zoom);
    void layoutTimed(double ms);
    void toolClicked(pauta::Tool tool, int staff, int measure); // Staff/Clef/Key/Time tools

protected:
    void paintEvent(QPaintEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    bool event(QEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void leaveEvent(QEvent* e) override;

private:
    struct Hit {
        const MeasureSlot* slot = nullptr;
        QPointF pagePt;
        int page = -1;
        int diatonic = 0;
    };
    Hit hitTest(const QPointF& widgetPos) const;
    void relayout();
    void onCursorChanged();
    QRectF cursorRectOnPage(int* page) const;
    void paintSelection(QPainter& p, int page);
    void paintSpeedy(QPainter& p, int page);
    void paintGhost(QPainter& p, int page);
    // Where a Simple Entry click at this hit would put the note (same rule for preview and click).
    struct InsertPoint {
        const MeasureSlot* slot = nullptr;
        qreal x = 0;
        int index = 0;
        bool onEvent = false; // adds to / removes from the existing chord
    };
    InsertPoint insertPointFor(const Hit& h) const;
    void drawNoteFigure(QPainter& p, const MeasureSlot& s, qreal x, int diatonic, bool rest, const QColor& color) const;
    void zoomAt(const QPointF& widgetPos, qreal factor);
    void simpleEntryClick(const Hit& h);
    void updateToolCursor();
    bool entryKeys(QKeyEvent* e);

    Editor* m_ed;
    Layout m_layout;
    qreal m_zoom = 1.5; // widget pixels per page point
    double m_layoutMs = 0;
    Tool m_tool = Tool::SpeedyEntry;
    bool m_continuous = false;
    bool m_simpleRest = false;
    int m_playMeasure = -1;
    unsigned m_armedArtic = 1; // ArticStaccato
    int m_armedDyn = 5;        // DynMF
    int m_armedShape = 0;      // 0 slur, 1 crescendo, 2 diminuendo
    struct ShapeStart { bool valid = false; int staff = 0, voice = 0, measure = 0, index = 0; } m_shapeStart;
    // Typing lyrics / chord symbols into the score.
    struct TextEntry { bool active = false; int staff = 0, measure = 0, voice = 0, index = 0, verse = 0; } m_text;
    bool textKeys(QKeyEvent* e);
    bool stepText(int dir); // to the next/previous note (lyrics skip rests)

    // Mouse interaction state
    bool m_selecting = false;
    int m_anchorStaff = 0, m_anchorMeasure = 0;
    bool m_panning = false;
    QPoint m_lastGlobal;
    Hit m_hover;

    static constexpr int kGap = 24;
};

} // namespace pauta
