#pragma once
// Native ".pauta" file format: plain, documented XML (see docs/FORMATO.md).
#include "model/Score.h"

#include <QString>

namespace pauta::io {

bool saveNative(const Score& score, const QString& path, QString* error = nullptr);
bool loadNative(Score& score, const QString& path, QString* error = nullptr);

} // namespace pauta::io
