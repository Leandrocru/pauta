#pragma once
// MusicXML import (score-partwise, .musicxml / .xml / compressed .mxl).
#include "model/Score.h"

#include <QString>
#include <QStringList>

namespace pauta::io {

// Fills `score` from a MusicXML file. Returns false with `error` set when the file cannot be
// read at all; `warnings` lists what was simplified (e.g. extra voices, grace notes).
bool importMusicXml(Score& score, const QString& path, QString* error = nullptr, QStringList* warnings = nullptr);

// Opens any supported score file, choosing the reader by extension.
bool loadAnyScore(Score& score, const QString& path, QString* error = nullptr, QStringList* warnings = nullptr);

bool isMusicXmlPath(const QString& path);
QString openFileFilter(); // for QFileDialog

} // namespace pauta::io
