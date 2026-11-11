#pragma once
// SMuFL code points and glyph metrics for the music font (Bravura).
#include <QHash>
#include <QRawFont>
#include <QRectF>

namespace pauta::smufl {

enum Glyph : char16_t {
    Brace = 0xE000,
    GClef = 0xE050,
    CClef = 0xE05C,
    FClef = 0xE062,
    TimeSig0 = 0xE080,
    TimeSigCommon = 0xE08A,
    NoteheadDoubleWhole = 0xE0A0,
    NoteheadWhole = 0xE0A2,
    NoteheadHalf = 0xE0A3,
    NoteheadBlack = 0xE0A4,
    AugmentationDot = 0xE1E7,
    Flag8thUp = 0xE240, // +2 per smaller value, +1 for "down"
    AccFlat = 0xE260,
    AccNatural = 0xE261,
    AccSharp = 0xE262,
    AccDoubleSharp = 0xE263,
    AccDoubleFlat = 0xE264,
    RestDoubleWhole = 0xE4E2,
    RestWhole = 0xE4E3,
    RestHalf = 0xE4E4,
    RestQuarter = 0xE4E5,
    Rest8th = 0xE4E6, // +1 per smaller value
    MetNoteDoubleWhole = 0xECA0,
    MetNoteWhole = 0xECA2,
    MetNoteHalfUp = 0xECA3,
    MetNoteQuarterUp = 0xECA5,
    MetNote8thUp = 0xECA7, // +2 per smaller value
};

char16_t noteheadFor(int durLog);
char16_t restFor(int durLog);
char16_t flagFor(int durLog, bool stemUp); // 0 if none
char16_t accidentalFor(int alter);
char16_t metronomeNoteFor(int durLog);

// Loads the embedded music font once; glyph metrics in page units for a given staff space.
class Font {
public:
    static Font& instance();

    bool isValid() const { return m_valid; }
    QString familyName() const { return m_family; }
    const QByteArray& data() const { return m_data; }

    // Raw font sized so that 1 em = 4 staff spaces of size `sp`.
    QRawFont rawFont(qreal sp) const;
    quint32 glyphIndex(char16_t cp) const;
    qreal advance(char16_t cp, qreal sp) const;  // horizontal advance in page units
    QRectF bbox(char16_t cp, qreal sp) const;    // tight bounds relative to origin

private:
    Font();
    bool m_valid = false;
    QString m_family;
    QByteArray m_data;
    QRawFont m_unitFont; // pixel size 100 (= 25 staff spaces)
    mutable QHash<char16_t, quint32> m_index;
};

} // namespace pauta::smufl
