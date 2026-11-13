#include "render/Renderer.h"
#include "render/Smufl.h"

#include <QFont>
#include <QFontMetricsF>
#include <QGlyphRun>
#include <QImage>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>

namespace pauta {

void paintPage(QPainter& p, const Layout& layout, int pageIdx)
{
    if (pageIdx < 0 || pageIdx >= int(layout.pages.size())) return;
    const PageLayout& pg = layout.pages[size_t(pageIdx)];

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    // Lines: grouped by width so each width is a single drawLines call.
    QPen pen(Qt::black);
    pen.setCapStyle(Qt::FlatCap);
    qreal currentW = -1;
    QList<QLineF> batch;
    auto flush = [&] {
        if (batch.isEmpty()) return;
        pen.setWidthF(currentW);
        p.setPen(pen);
        p.drawLines(batch);
        batch.clear();
    };
    for (const LineItem& l : pg.lines) {
        if (l.width != currentW) { flush(); currentW = l.width; }
        batch.append(l.line);
    }
    flush();

    // Filled shapes (beams, ties).
    if (!pg.fills.empty()) {
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::black);
        for (const QPainterPath& path : pg.fills) p.drawPath(path);
        p.setBrush(Qt::NoBrush);
    }

    // All music glyphs of the page in one draw call.
    if (!pg.glyphs.isEmpty()) {
        QGlyphRun run;
        run.setRawFont(smufl::Font::instance().rawFont(layout.sp));
        run.setGlyphIndexes(pg.glyphs);
        run.setPositions(pg.glyphPos);
        p.setPen(Qt::black);
        p.drawGlyphRun(QPointF(0, 0), run);
    }

    // Music glyphs drawn at a different size.
    if (!pg.scaledGlyphs.empty()) {
        const QRawFont raw = smufl::Font::instance().rawFont(layout.sp);
        p.setPen(Qt::black);
        for (const auto& g : pg.scaledGlyphs) {
            QGlyphRun run;
            run.setRawFont(raw);
            run.setGlyphIndexes({ g.glyph });
            run.setPositions({ QPointF(0, 0) });
            p.save();
            p.translate(g.pos);
            p.scale(g.scale, g.scale);
            p.drawGlyphRun(QPointF(0, 0), run);
            p.restore();
        }
    }

    // Text (title, names, measure numbers). Fonts are set up at 100 px and scaled, so that
    // sizes are exact page points on every device (screen DPI does not leak into the layout).
    for (const TextItem& t : pg.texts) {
        const qreal k = t.size / kTextRefPx;
        const QFont f = textFont(t.bold, t.italic);
        const qreal w = textAdvance(t.text, t.size, t.bold, t.italic);
        QPointF at = t.pos;
        if (t.align == 1) at.rx() -= w / 2;
        else if (t.align == -1) at.rx() -= w;
        p.save();
        p.translate(at);
        p.scale(k, k);
        p.setFont(f);
        p.drawText(QPointF(0, 0), t.text);
        p.restore();
    }
    p.restore();
}

bool exportPdf(const Layout& layout, const QString& path, const QString& title)
{
    QPdfWriter writer(path);
    writer.setTitle(title);
    writer.setCreator(QStringLiteral("Pauta"));
    writer.setPageSize(QPageSize(QSizeF(layout.pageWidth, layout.pageHeight), QPageSize::Point));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    writer.setResolution(1200);
    QPainter p;
    if (!p.begin(&writer)) return false;
    const qreal k = writer.resolution() / 72.0;
    for (int i = 0; i < int(layout.pages.size()); ++i) {
        if (i > 0) writer.newPage();
        p.save();
        p.scale(k, k);
        paintPage(p, layout, i);
        p.restore();
    }
    return p.end();
}

bool exportPng(const Layout& layout, int page, const QString& path, qreal dpi)
{
    const qreal k = dpi / 72.0;
    QImage img(QSize(int(layout.pageWidth * k), int(layout.pageHeight * k)), QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    p.scale(k, k);
    paintPage(p, layout, page);
    p.end();
    return img.save(path);
}

QFont textFont(bool bold, bool italic)
{
    QFont f(QStringLiteral("Times New Roman"));
    f.setPixelSize(int(kTextRefPx));
    f.setBold(bold);
    f.setItalic(italic);
    return f;
}

qreal textAdvance(const QString& text, qreal size, bool bold, bool italic)
{
    return QFontMetricsF(textFont(bold, italic)).horizontalAdvance(text) * size / kTextRefPx;
}

} // namespace pauta
