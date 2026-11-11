#pragma once
#include "render/Layout.h"

namespace pauta {

struct EngraveOptions {
    qreal pageWidth = 595.28;  // A4 portrait, in points
    qreal pageHeight = 841.89;
    qreal margin = 42.5;       // 15 mm
    qreal sp = 5.0;            // staff space (staff height = 4 sp ≈ 7 mm)
};

// Turns a Score into a page Layout. Pure function of its inputs (no global state besides the font).
Layout engrave(const Score& score, const EngraveOptions& opt = {});

} // namespace pauta
