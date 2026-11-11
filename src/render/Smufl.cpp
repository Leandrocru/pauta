#include "render/Smufl.h"

#include <QFile>
#include <QFontDatabase>
#include <QVector>

namespace pauta::smufl {

namespace {
constexpr qreal kUnitPixel = 100.0; // pixel size of the metrics font
}

char16_t noteheadFor(int durLog)
{
    if (durLog < 0) return NoteheadDoubleWhole;
    if (durLog == 0) return NoteheadWhole;
    if (durLog == 1) return NoteheadHalf;
    return NoteheadBlack;
}

char16_t restFor(int durLog)
{
    switch (durLog) {
    case -1: return RestDoubleWhole;
    case 0:  return RestWhole;
    case 1:  return RestHalf;
    case 2:  return RestQuarter;
    default: return char16_t(Rest8th + (durLog - 3));
    }
}

char16_t flagFor(int durLog, bool stemUp)
{
    if (durLog < 3) return 0;
    return char16_t(Flag8thUp + 2 * (durLog - 3) + (stemUp ? 0 : 1));
}

char16_t accidentalFor(int alter)
{
    switch (alter) {
    case -2: return AccDoubleFlat;
    case -1: return AccFlat;
    case 1:  return AccSharp;
    case 2:  return AccDoubleSharp;
    default: return AccNatural;
    }
}

char16_t metronomeNoteFor(int durLog)
{
    switch (durLog) {
    case -1: return MetNoteDoubleWhole;
    case 0:  return MetNoteWhole;
    case 1:  return MetNoteHalfUp;
    case 2:  return MetNoteQuarterUp;
    default: return char16_t(MetNote8thUp + 2 * (durLog - 3));
    }
}

Font& Font::instance()
{
    static Font f;
    return f;
}

Font::Font()
{
    QFile file(QStringLiteral(":/resources/fonts/Bravura.otf"));
    if (!file.open(QIODevice::ReadOnly))
        return;
    m_data = file.readAll();
    const int id = QFontDatabase::addApplicationFontFromData(m_data);
    if (id >= 0) {
        const QStringList fams = QFontDatabase::applicationFontFamilies(id);
        if (!fams.isEmpty()) m_family = fams.first();
    }
    m_unitFont.loadFromData(m_data, kUnitPixel, QFont::PreferNoHinting);
    m_valid = m_unitFont.isValid();
}

QRawFont Font::rawFont(qreal sp) const
{
    QRawFont f(m_unitFont);
    f.setPixelSize(4.0 * sp);
    return f;
}

quint32 Font::glyphIndex(char16_t cp) const
{
    auto it = m_index.constFind(cp);
    if (it != m_index.constEnd()) return *it;
    const QList<quint32> idx = m_unitFont.glyphIndexesForString(QString(QChar(cp)));
    const quint32 g = idx.isEmpty() ? 0 : idx.first();
    m_index.insert(cp, g);
    return g;
}

qreal Font::advance(char16_t cp, qreal sp) const
{
    const quint32 g = glyphIndex(cp);
    const QList<QPointF> adv = m_unitFont.advancesForGlyphIndexes(QList<quint32>{ g });
    if (adv.isEmpty()) return 0;
    return adv.first().x() * (4.0 * sp / kUnitPixel);
}

QRectF Font::bbox(char16_t cp, qreal sp) const
{
    const QRectF r = m_unitFont.boundingRect(glyphIndex(cp));
    const qreal k = 4.0 * sp / kUnitPixel;
    return QRectF(r.x() * k, r.y() * k, r.width() * k, r.height() * k);
}

} // namespace pauta::smufl
