#pragma once
// Result of engraving: positioned drawing primitives per page plus hit-test data.
// All coordinates are in page units (1 unit = 1 typographic point = 1/72 inch).
#include "model/Score.h"

#include <QLineF>
#include <QPainterPath>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <array>
#include <vector>

namespace pauta {

struct LineItem {
    QLineF line;
    qreal width = 1;
};

struct TextItem {
    QString text;
    QPointF pos;       // anchor point (baseline)
    qreal size = 10;   // point size
    bool bold = false;
    bool italic = false;
    int align = 0;     // -1 = right-aligned at pos, 0 = left, 1 = centered
};

struct PageLayout {
    // Music glyphs are stored as one batch (glyph index + position) for a single draw call.
    QList<quint32> glyphs;
    QList<QPointF> glyphPos;
    std::vector<LineItem> lines;
    std::vector<QPainterPath> fills; // filled shapes: beams, ties
    std::vector<TextItem> texts;
};

// Where one staff of one measure ended up; used for the cursor and mouse hit-testing.
struct MeasureSlot {
    int page = 0;
    int staff = 0;
    int measure = 0;
    qreal x0 = 0, x1 = 0;       // horizontal extent of the measure
    qreal staffTop = 0;         // y of the top staff line
    Clef clef = Clef::Treble;
    std::array<std::vector<qreal>, kVoices> eventX{}; // x of each event's notehead, per layer
    std::array<qreal, kVoices> appendX{};             // x where a new event would go, per layer
};

struct Layout {
    qreal pageWidth = 595.28;
    qreal pageHeight = 841.89;
    qreal sp = 5.0;             // staff space
    std::vector<PageLayout> pages;
    std::vector<MeasureSlot> measureSlots;

    const MeasureSlot* slot(int staff, int measure) const
    {
        for (const MeasureSlot& s : measureSlots)
            if (s.staff == staff && s.measure == measure) return &s;
        return nullptr;
    }
    qreal yForPosition(const MeasureSlot& s, int pos) const { return s.staffTop + (4 - pos) * sp / 2; }
};

} // namespace pauta
