#include "ui/Icons.h"

#include "render/Smufl.h"

#include <QGlyphRun>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

#include <algorithm>

namespace pauta::icons {

namespace {

constexpr int kSize = 48;
const QColor kInk(0x2b, 0x2d, 0x31);
const QColor kAccent(0x1e, 0x6f, 0xd9);

// Draws a SMuFL glyph scaled to fit `target`, centred on its tight bounds.
void drawGlyphFit(QPainter& p, char16_t cp, const QRectF& target, const QColor& color = kInk)
{
    const auto& font = smufl::Font::instance();
    const qreal sp = 10;
    const QRectF bb = font.bbox(cp, sp);
    if (bb.isEmpty()) return;
    // Fit the box, but do not blow up tiny symbols (a staccato dot must stay a dot).
    const qreal k = std::min({ target.width() / bb.width(), target.height() / bb.height(), 2.2 });
    p.save();
    p.translate(target.center());
    p.scale(k, k);
    QGlyphRun run;
    run.setRawFont(font.rawFont(sp));
    run.setGlyphIndexes({ font.glyphIndex(cp) });
    run.setPositions({ QPointF(-bb.center().x(), -bb.center().y()) });
    p.setPen(color);
    p.drawGlyphRun(QPointF(0, 0), run);
    p.restore();
}

void drawText(QPainter& p, const QString& s, const QRectF& r, const QString& family, int px, bool bold, bool italic)
{
    QFont f(family);
    f.setPixelSize(px);
    f.setBold(bold);
    f.setItalic(italic);
    p.setFont(f);
    p.setPen(kInk);
    p.drawText(r, Qt::AlignCenter, s);
}

void staffLines(QPainter& p, qreal x0, qreal x1, qreal top, qreal gap, int n = 5)
{
    p.setPen(QPen(kInk, 2));
    for (int i = 0; i < n; ++i) p.drawLine(QPointF(x0, top + i * gap), QPointF(x1, top + i * gap));
}

QIcon finish(QPixmap& pm) { return QIcon(pm); }

} // namespace

QIcon hairpin(bool crescendo)
{
    QPixmap pm(kSize, kSize);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(kInk, 3, Qt::SolidLine, Qt::RoundCap));
    const qreal a = 6, b = kSize - 6, c = kSize / 2.0, o = 11;
    if (crescendo) {
        p.drawLine(QPointF(a, c), QPointF(b, c - o));
        p.drawLine(QPointF(a, c), QPointF(b, c + o));
    } else {
        p.drawLine(QPointF(a, c - o), QPointF(b, c));
        p.drawLine(QPointF(a, c + o), QPointF(b, c));
    }
    p.end();
    return QIcon(pm);
}

QIcon dynamic(const QString& text)
{
    const auto& font = smufl::Font::instance();
    static const QString letters = QStringLiteral("pmfrsz");
    static const char16_t cps[6] = { 0xE520, 0xE521, 0xE522, 0xE523, 0xE524, 0xE525 };
    const qreal sp = 7;
    QList<quint32> idx;
    QList<QPointF> pos;
    qreal x = 0;
    QRectF bounds;
    for (QChar c : text) {
        const int i = letters.indexOf(c);
        if (i < 0) continue;
        idx << font.glyphIndex(cps[i]);
        pos << QPointF(x, 0);
        bounds = bounds.united(font.bbox(cps[i], sp).translated(x, 0));
        x += font.advance(cps[i], sp) - 0.08 * sp;
    }
    QPixmap pm(kSize, kSize);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal k = std::min((kSize - 6) / bounds.width(), (kSize - 10) / bounds.height());
    p.translate(kSize / 2.0, kSize / 2.0);
    p.scale(k, k);
    p.translate(-bounds.center());
    QGlyphRun run;
    run.setRawFont(font.rawFont(sp));
    run.setGlyphIndexes(idx);
    run.setPositions(pos);
    p.setPen(kInk);
    p.drawGlyphRun(QPointF(0, 0), run);
    p.end();
    return QIcon(pm);
}

QIcon glyph(char16_t cp)
{
    QPixmap pm(kSize, kSize);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    drawGlyphFit(p, cp, QRectF(6, 4, kSize - 12, kSize - 8));
    p.end();
    return finish(pm);
}

QIcon tool(Tool t)
{
    QPixmap pm(kSize, kSize);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    const QRectF full(4, 4, kSize - 8, kSize - 8);

    switch (t) {
    case Tool::Selection: {
        QPainterPath arrow;
        arrow.moveTo(14, 6);
        arrow.lineTo(14, 38);
        arrow.lineTo(22, 30);
        arrow.lineTo(28, 43);
        arrow.lineTo(33, 41);
        arrow.lineTo(27, 28);
        arrow.lineTo(38, 28);
        arrow.closeSubpath();
        p.setPen(QPen(kInk, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::white);
        p.drawPath(arrow);
        break;
    }
    case Tool::Zoom:
        p.setPen(QPen(kInk, 4, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(20, 20), 12, 12);
        p.drawLine(QPointF(29, 29), QPointF(41, 41));
        p.setPen(QPen(kInk, 3, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(14, 20), QPointF(26, 20));
        p.drawLine(QPointF(20, 14), QPointF(20, 26));
        break;
    case Tool::Hand: {
        p.setPen(QPen(kInk, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::white);
        const qreal fx[4] = { 13, 19.5, 26, 32.5 };
        const qreal ftop[4] = { 12, 7, 8, 13 };
        p.drawRoundedRect(QRectF(11, 22, 27, 21), 8, 8);
        for (int i = 0; i < 4; ++i) p.drawRoundedRect(QRectF(fx[i], ftop[i], 6, 22 - ftop[i] + 8), 3, 3);
        p.drawRoundedRect(QRectF(4, 25, 14, 6).translated(0, 0), 3, 3);
        p.setPen(Qt::NoPen);
        p.drawRect(QRectF(12, 23, 25, 12));
        break;
    }
    case Tool::Staff:
        staffLines(p, 6, 42, 12, 6);
        break;
    case Tool::Clef:
        drawGlyphFit(p, smufl::GClef, QRectF(12, 2, 24, 44));
        break;
    case Tool::KeySig:
        drawGlyphFit(p, smufl::AccSharp, QRectF(6, 6, 16, 32));
        drawGlyphFit(p, smufl::AccFlat, QRectF(26, 10, 16, 30));
        break;
    case Tool::TimeSig:
        drawGlyphFit(p, char16_t(smufl::TimeSig0 + 4), QRectF(14, 3, 20, 20));
        drawGlyphFit(p, char16_t(smufl::TimeSig0 + 4), QRectF(14, 25, 20, 20));
        break;
    case Tool::Measure:
        staffLines(p, 4, 44, 12, 6);
        p.setPen(QPen(kInk, 3));
        p.drawLine(QPointF(24, 12), QPointF(24, 36));
        p.setPen(QPen(kAccent, 2));
        p.setBrush(QColor(30, 111, 217, 50));
        p.drawRect(QRectF(26, 9, 16, 30));
        break;
    case Tool::SimpleEntry:
        drawGlyphFit(p, smufl::MetNote8thUp, QRectF(10, 3, 26, 42));
        break;
    case Tool::SpeedyEntry: {
        drawGlyphFit(p, char16_t(smufl::MetNote8thUp + 2), QRectF(4, 3, 24, 42));
        QPainterPath bolt;
        bolt.moveTo(38, 4);
        bolt.lineTo(29, 24);
        bolt.lineTo(36, 24);
        bolt.lineTo(31, 44);
        bolt.lineTo(45, 19);
        bolt.lineTo(38, 19);
        bolt.lineTo(43, 4);
        bolt.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xe0, 0x9a, 0x10));
        p.drawPath(bolt);
        break;
    }
    case Tool::HyperScribe:
        p.setPen(QPen(kInk, 2));
        p.setBrush(Qt::white);
        p.drawRect(QRectF(5, 10, 38, 28));
        for (int i = 1; i < 5; ++i) p.drawLine(QPointF(5 + i * 7.6, 10), QPointF(5 + i * 7.6, 38));
        p.setBrush(kInk);
        for (int i : { 1, 2, 4 }) p.drawRect(QRectF(5 + i * 7.6 - 2.5, 10, 5, 16));
        break;
    case Tool::Tuplet:
        drawText(p, QStringLiteral("3"), QRectF(0, 2, kSize, 26), QStringLiteral("Times New Roman"), 24, true, true);
        p.setPen(QPen(kInk, 2.5));
        p.drawPolyline(QPolygonF({ QPointF(6, 40), QPointF(6, 32), QPointF(42, 32), QPointF(42, 40) }));
        break;
    case Tool::SmartShape: {
        QPainterPath slur;
        slur.moveTo(5, 34);
        slur.cubicTo(14, 10, 34, 10, 43, 34);
        slur.cubicTo(34, 16, 14, 16, 5, 34);
        p.setPen(QPen(kInk, 1.5));
        p.setBrush(kInk);
        p.drawPath(slur);
        break;
    }
    case Tool::Articulation:
        drawGlyphFit(p, char16_t(0xE4A0), QRectF(8, 14, 32, 20)); // articAccentAbove
        break;
    case Tool::Expression:
        drawGlyphFit(p, char16_t(0xE522), QRectF(8, 6, 32, 36)); // dynamicForte
        break;
    case Tool::Repeat:
        drawGlyphFit(p, char16_t(0xE041), QRectF(10, 4, 28, 40)); // repeatRight
        break;
    case Tool::Chord:
        drawText(p, QStringLiteral("C7"), full, QStringLiteral("Arial"), 22, true, false);
        break;
    case Tool::Lyrics:
        drawText(p, QStringLiteral("lá"), QRectF(0, 0, kSize, 40), QStringLiteral("Times New Roman"), 28, false, true);
        p.setPen(QPen(kInk, 2));
        p.drawLine(QPointF(8, 40), QPointF(40, 40));
        break;
    case Tool::Text:
        drawText(p, QStringLiteral("A"), full, QStringLiteral("Times New Roman"), 36, true, false);
        break;
    case Tool::PageLayout:
        p.setPen(QPen(kInk, 2));
        p.setBrush(Qt::white);
        p.drawRect(QRectF(10, 4, 28, 40));
        p.setPen(QPen(kInk, 1.4));
        for (int i = 0; i < 3; ++i)
            for (int l = 0; l < 3; ++l) p.drawLine(QPointF(14, 10 + i * 11 + l * 2.5), QPointF(34, 10 + i * 11 + l * 2.5));
        break;
    }
    p.end();
    return finish(pm);
}

QIcon transport(Transport t)
{
    QPixmap pm(kSize, kSize);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(kInk);
    switch (t) {
    case Transport::Rewind:
        p.drawRect(QRectF(10, 12, 5, 24));
        p.drawPolygon(QPolygonF({ QPointF(38, 12), QPointF(38, 36), QPointF(16, 24) }));
        break;
    case Transport::Play:
        p.setBrush(QColor(0x2a, 0x8a, 0x3e));
        p.drawPolygon(QPolygonF({ QPointF(14, 10), QPointF(14, 38), QPointF(38, 24) }));
        break;
    case Transport::Stop:
        p.drawRect(QRectF(13, 13, 22, 22));
        break;
    case Transport::Record:
        p.setBrush(QColor(0xc8, 0x2a, 0x2a));
        p.drawEllipse(QPointF(24, 24), 11, 11);
        break;
    }
    p.end();
    return QIcon(pm);
}

} // namespace pauta::icons
