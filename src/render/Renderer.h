#pragma once
#include "render/Layout.h"

#include <QFont>

class QPainter;

namespace pauta {

constexpr qreal kTextRefPx = 100.0;

// Paints one engraved page in page units (the caller sets up the painter transform).
void paintPage(QPainter& p, const Layout& layout, int page);

// Exports (vector PDF with embedded fonts; raster PNG of one page).
bool exportPdf(const Layout& layout, const QString& path, const QString& title = {});
bool exportPng(const Layout& layout, int page, const QString& path, qreal dpi = 150);

// Text helpers in page units (points), independent of screen DPI.
QFont textFont(bool bold, bool italic);
qreal textAdvance(const QString& text, qreal size, bool bold = false, bool italic = false);

} // namespace pauta
