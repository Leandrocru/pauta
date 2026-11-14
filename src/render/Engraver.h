#pragma once
#include "render/Layout.h"

namespace pauta {

struct EngraveOptions {
    qreal pageWidth = 595.28;  // A4 portrait, in points
    qreal pageHeight = 841.89;
    qreal margin = 42.5;       // 15 mm
    qreal sp = 5.0;            // staff space (staff height = 4 sp ≈ 7 mm)
    bool continuous = false;   // scroll view: one endless system instead of pages
    int part = -1;             // -1 = full score; otherwise the instrument (index into Score::parts())
    bool multiRests = false;   // join runs of empty measures into one multi-measure rest (on by default in parts)
    QString partName;          // printed at the top left of the first page (set for parts)
};

// Turns a Score into a page Layout. Pure function of its inputs (no global state besides the font).
Layout engrave(const Score& score, const EngraveOptions& opt = {});

} // namespace pauta
