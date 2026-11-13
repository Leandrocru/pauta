#pragma once
// Original icons for the palettes, drawn at runtime (music symbols come from the SMuFL font).
#include "ui/Tools.h"

#include <QIcon>

namespace pauta::icons {

QIcon tool(Tool t);
QIcon glyph(char16_t smuflCodepoint);
QIcon dynamic(const QString& text); // "mf" drawn with the music font

enum class Transport { Rewind, Play, Stop, Record };
QIcon transport(Transport t);

} // namespace pauta::icons
