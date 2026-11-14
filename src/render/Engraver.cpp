#include "render/Engraver.h"
#include "render/Renderer.h"
#include "render/Smufl.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace pauta {

namespace {

using smufl::Font;
constexpr int kNoAcc = 99;

// ---- Engraving constants (in staff spaces) -------------------------------------------------
constexpr qreal kStaffLineW = 0.13;
constexpr qreal kStemW = 0.12;
constexpr qreal kLedgerW = 0.16;
constexpr qreal kLedgerExt = 0.35;
constexpr qreal kThinBarW = 0.16;
constexpr qreal kThickBarW = 0.5;
constexpr qreal kStemLen = 3.5;
constexpr qreal kAccColW = 1.15;
constexpr qreal kMeasureLeftPad = 1.0;
constexpr qreal kMeasureRightPad = 0.8;
constexpr qreal kRepeatStartW = 1.9; // room taken by |: at the start of a measure
constexpr qreal kRepeatEndW = 1.5;   // room taken by :| at the end of a measure
constexpr qreal kRepeatDotR = 0.22;
constexpr qreal kStaffGap = 7.0;   // between bottom line of one staff and top line of the next
constexpr qreal kSystemGap = 9.0;  // between systems

// Ideal space following a time segment of length d (logarithmic, like traditional engraving).
qreal idealSpace(Frac d)
{
    const double v = std::max(d.toDouble(), 1.0 / 64.0);
    return 1.6 + 1.0 * std::log2(v * 32.0);
}

struct EventInfo {
    std::vector<int> pos;     // staff position per pitch (ascending)
    std::vector<int> acc;     // accidental to print per pitch (kNoAcc = none)
    std::vector<int> accCol;  // accidental column per pitch
    std::vector<bool> offset; // notehead on the "wrong" side of the stem
    int accCols = 0;
    bool stemUp = true;
    bool hasSecond = false;
    bool shiftRight = false;  // moved right to avoid another layer's notehead
    int restPos = 0;          // staff position of a rest
    int beamGroup = -1;       // index into StaffMeasureInfo::beams, -1 = flagged/unbeamed
    Frac onset;
};

// One layer of one staff in one measure.
struct StaffMeasureInfo {
    std::vector<EventInfo> events;
    std::vector<std::vector<int>> beams; // event indices of each beam group (>= 2 notes)
    Frac filled;
};

// All layers of one staff in one measure.
struct StaffInfo {
    std::array<StaffMeasureInfo, kVoices> v;
    bool multi = false; // more than one layer has notes: stems are fixed per layer
};

// Beaming unit: dotted quarter (or 3 x beat) in compound meters, quarter note otherwise.
Frac beamUnit(const TimeSig& t)
{
    if (t.beatType >= 8 && t.beats % 3 == 0) return Frac(3, t.beatType);
    return Frac(1, 4);
}

int64_t unitIndex(Frac t, Frac unit) { return (t.num * unit.den) / (t.den * unit.num); }

struct MeasureSpacing {
    std::vector<Frac> onsets;     // sorted unique; last = end of measure
    std::vector<qreal> fixedAt;   // fixed space before onset i
    std::vector<qreal> stretchAt; // stretchable space before onset i
    qreal fixedTotal = 0;
    qreal prefix = 0; // room for clef/key/time changes when the measure is not at a system start
    qreal stretchTotal = 0;
    std::vector<StaffInfo> staves;

    qreal width(qreal f) const { return fixedTotal + stretchTotal * f; }
    qreal xAt(size_t i, qreal f) const { return fixedAt[i] + stretchAt[i] * f; }
    size_t indexOf(Frac t) const
    {
        auto it = std::lower_bound(onsets.begin(), onsets.end(), t);
        return size_t(it - onsets.begin());
    }
};

void analyseEvents(const Score& score, int mi, const Staff& staff, const Measure& m, StaffInfo& out)
{
    out.multi = m.usedVoices() > 1;

    // 1) Onsets, positions and default stem directions, layer by layer.
    for (int v = 0; v < kVoices; ++v) {
        StaffMeasureInfo& vi = out.v[size_t(v)];
        Frac t;
        for (const Event& e : m.voice(v)) {
            EventInfo info;
            info.onset = t;
            t += e.duration();
            const size_t n = e.pitches.size();
            info.pos.resize(n);
            info.acc.assign(n, kNoAcc);
            info.accCol.assign(n, 0);
            info.offset.assign(n, false);
            for (size_t k = 0; k < n; ++k) info.pos[k] = staffPosition(e.pitches[k].diatonic(), staff.clefAt(mi));
            if (out.multi) info.stemUp = v % 2 == 0; // layers 1/3 up, 2/4 down
            else if (n > 0) info.stemUp = info.pos.front() + info.pos.back() < 0;
            const int base = e.durLog <= 0 ? 2 : 0;   // whole rest hangs from line 4
            info.restPos = !out.multi ? base : (v % 2 == 0 ? base + 4 : base - 4);
            vi.events.push_back(std::move(info));
        }
        vi.filled = t;
    }

    // 2) Accidentals: one state per staff, shared by all layers, applied in time order.
    struct Ref { Frac onset; int v; size_t k; };
    std::vector<Ref> order;
    for (int v = 0; v < kVoices; ++v)
        for (size_t k = 0; k < out.v[size_t(v)].events.size(); ++k) order.push_back({ out.v[size_t(v)].events[k].onset, v, k });
    std::stable_sort(order.begin(), order.end(), [](const Ref& a, const Ref& b) { return a.onset < b.onset; });
    std::map<int, int> state; // diatonic -> current alteration within the measure
    for (const Ref& r : order) {
        const Event& e = m.voice(r.v)[r.k];
        EventInfo& info = out.v[size_t(r.v)].events[r.k];
        for (size_t k = 0; k < e.pitches.size(); ++k) {
            const Pitch& p = e.pitches[k];
            auto it = state.find(p.diatonic());
            const int current = it != state.end() ? it->second : keyAlterFor(p.step, score.keyAt(mi));
            if (p.alter != current) info.acc[k] = p.alter;
            state[p.diatonic()] = p.alter;
        }
    }

    // 3) Beam groups per layer: consecutive eighth-or-shorter notes inside one beaming unit;
    //    rests, longer notes and unit boundaries break the beam.
    const Frac unit = beamUnit(score.timeAt(mi));
    for (int v = 0; v < kVoices; ++v) {
        StaffMeasureInfo& vi = out.v[size_t(v)];
        const std::vector<Event>& evs = m.voice(v);
        std::vector<int> cur;
        int64_t curUnit = -1;
        auto flush = [&] {
            if (cur.size() >= 2) {
                const int g = int(vi.beams.size());
                int top = -1000, bottom = 1000;
                for (int i : cur) {
                    top = std::max(top, vi.events[size_t(i)].pos.back());
                    bottom = std::min(bottom, vi.events[size_t(i)].pos.front());
                }
                const bool up = out.multi ? v % 2 == 0 : top + bottom < 0; // farther extreme decides
                for (int i : cur) {
                    vi.events[size_t(i)].beamGroup = g;
                    vi.events[size_t(i)].stemUp = up;
                }
                vi.beams.push_back(cur);
            }
            cur.clear();
        };
        for (size_t k = 0; k < evs.size(); ++k) {
            const Event& e = evs[k];
            const EventInfo& info = vi.events[k];
            const bool beamable = !e.isRest() && e.durLog >= 3;
            const Frac end = info.onset + e.duration();
            const int64_t u = unitIndex(info.onset, unit);
            const bool fitsUnit = end <= Frac(u + 1) * unit;
            if (!beamable || !fitsUnit || (u != curUnit && !cur.empty())) flush();
            if (beamable && fitsUnit) {
                curUnit = u;
                cur.push_back(int(k));
            }
        }
        flush();
    }

    // 4) Seconds inside chords and accidental columns.
    for (StaffMeasureInfo& vi : out.v) {
        for (EventInfo& info : vi.events) {
            const size_t n = info.pos.size();
            if (n == 0) continue;
            if (info.stemUp) {
                for (size_t k = 1; k < n; ++k)
                    if (info.pos[k] - info.pos[k - 1] == 1 && !info.offset[k - 1]) info.offset[k] = true;
            } else {
                for (size_t k = n - 1; k-- > 0;)
                    if (info.pos[k + 1] - info.pos[k] == 1 && !info.offset[k + 1]) info.offset[k] = true;
            }
            info.hasSecond = std::find(info.offset.begin(), info.offset.end(), true) != info.offset.end();
            // Accidental columns, assigned top-down: a column may be reused once notes are a 6th apart.
            std::vector<std::vector<int>> cols;
            for (size_t k = n; k-- > 0;) {
                if (info.acc[k] == kNoAcc) continue;
                size_t c = 0;
                for (; c < cols.size(); ++c) {
                    const bool clash = std::any_of(cols[c].begin(), cols[c].end(),
                                                   [&](int p) { return std::abs(p - info.pos[k]) < 6; });
                    if (!clash) break;
                }
                if (c == cols.size()) cols.emplace_back();
                cols[c].push_back(info.pos[k]);
                info.accCol[k] = int(c);
            }
            info.accCols = int(cols.size());
        }
    }

    // 5) Down-stem layers whose notes would touch an up-stem layer at the same time
    //    (unison or second) are moved to the right, as in standard engraving.
    if (out.multi) {
        for (int v = 1; v < kVoices; v += 2) {
            for (EventInfo& low : out.v[size_t(v)].events) {
                if (low.pos.empty()) continue;
                for (int u = 0; u < kVoices; u += 2)
                    for (const EventInfo& high : out.v[size_t(u)].events)
                        if (high.onset == low.onset && !high.pos.empty() && high.pos.front() - low.pos.back() <= 1)
                            low.shiftRight = true;
            }
        }
    }
}

MeasureSpacing computeSpacing(const Score& score, int mi, qreal sp, qreal headW)
{
    MeasureSpacing ms;
    const Frac capacity = score.capacityAt(mi);
    std::set<Frac> onsetSet{ Frac(0) };
    Frac end = capacity;
    ms.staves.resize(score.staves.size());
    for (size_t s = 0; s < score.staves.size(); ++s) {
        const Staff& st = score.staves[s];
        analyseEvents(score, mi, st, st.measures[size_t(mi)], ms.staves[s]);
        for (const StaffMeasureInfo& vi : ms.staves[s].v) {
            for (const EventInfo& e : vi.events) onsetSet.insert(e.onset);
            onsetSet.insert(vi.filled);
            end = std::max(end, vi.filled);
        }
    }
    onsetSet.insert(end);
    ms.onsets.assign(onsetSet.begin(), onsetSet.end());
    while (!ms.onsets.empty() && ms.onsets.back() > end) ms.onsets.pop_back();

    const size_t n = ms.onsets.size();
    std::vector<qreal> extraLeft(n, 0), extraRight(n, 0);
    for (size_t s = 0; s < score.staves.size(); ++s) {
        const Measure& m = score.staves[s].measures[size_t(mi)];
        for (int v = 0; v < kVoices; ++v) {
            const std::vector<Event>& evs = m.voice(v);
            for (size_t k = 0; k < evs.size(); ++k) {
                const EventInfo& info = ms.staves[s].v[size_t(v)].events[k];
                const size_t i = ms.indexOf(info.onset);
                qreal left = info.accCols > 0 ? info.accCols * kAccColW * sp + 0.2 * sp : 0;
                qreal right = evs[k].dots > 0 ? (0.4 + 0.5 * evs[k].dots) * sp : 0;
                if (info.hasSecond) (info.stemUp ? right : left) += headW;
                if (info.shiftRight) right += 1.1 * headW;
                extraLeft[i] = std::max(extraLeft[i], left);
                extraRight[i] = std::max(extraRight[i], right);
            }
        }
    }

    qreal fixed = kMeasureLeftPad * sp;
    qreal stretch = 0;
    ms.fixedAt.resize(n);
    ms.stretchAt.resize(n);
    for (size_t i = 0; i < n; ++i) {
        fixed += extraLeft[i];
        ms.fixedAt[i] = fixed;
        ms.stretchAt[i] = stretch;
        fixed += extraRight[i];
        if (i + 1 < n) stretch += idealSpace(ms.onsets[i + 1] - ms.onsets[i]) * sp;
    }
    if (n == 1) stretch += idealSpace(capacity) * sp; // degenerate (zero-length) measure
    ms.fixedTotal = fixed + kMeasureRightPad * sp;
    ms.stretchTotal = stretch;
    return ms;
}

// Key signature positions on the treble staff (0 = middle line).
const int kSharpPos[7] = { 4, 1, 5, 2, -1, 3, 0 };
const int kFlatPos[7] = { 0, 3, -1, 2, -2, 1, -3 };

int keySigPos(int i, bool sharps, Clef clef)
{
    int p = sharps ? kSharpPos[i] : kFlatPos[i];
    switch (clef) {
    case Clef::Treble: break;
    case Clef::Bass: p -= 2; break;
    case Clef::Alto: p -= 1; break;
    case Clef::Tenor: p += 1; if (sharps && p > 4) p -= 7; break;
    }
    return p;
}

char16_t clefGlyph(Clef c)
{
    switch (c) {
    case Clef::Treble: return smufl::GClef;
    case Clef::Bass: return smufl::FClef;
    case Clef::Alto:
    case Clef::Tenor: return smufl::CClef;
    }
    return smufl::GClef;
}

int clefPos(Clef c)
{
    switch (c) {
    case Clef::Treble: return -2; // G line
    case Clef::Bass: return 2;    // F line
    case Clef::Alto: return 0;
    case Clef::Tenor: return 2;
    }
    return -2;
}

class Builder {
public:
    Builder(const Score& score, const EngraveOptions& opt)
        : m_score(score), m_opt(opt), m_sp(opt.sp), m_font(Font::instance())
    {
        m_layout.pageWidth = opt.pageWidth;
        m_layout.pageHeight = opt.pageHeight;
        m_layout.sp = opt.sp;
        m_headW = m_font.advance(smufl::NoteheadBlack, m_sp);
        m_pendingTies.resize(score.staves.size() * kVoices);
        m_slurs.resize(score.staves.size() * kVoices);
        m_hairpins.resize(score.staves.size() * kVoices);
    }

    Layout run();

private:
    struct System {
        int first = 0, last = 0; // measure range [first, last]
        qreal stretch = 1;
        qreal headerW = 0;
        bool showTime = false;
    };

    qreal headerWidth(int measure, bool showTime) const;
    qreal clefColumnWidth(int measure) const;
    qreal keyWidth(int fifths, int previous) const;
    qreal timeWidth(const TimeSig& t) const;
    qreal drawKey(PageLayout& pg, qreal x, qreal staffTop, Clef clef, int fifths, int previous);
    qreal drawTime(PageLayout& pg, qreal x, qreal staffTop, const TimeSig& t);
    qreal changePrefix(int measure) const;
    void drawChanges(PageLayout& pg, int staff, int measure, qreal x, qreal staffTop);
    void drawSystem(const System& sys, int page, qreal left, qreal top, bool firstSystem);
    void drawHeader(PageLayout& pg, int staff, qreal x, qreal staffTop, int measure, bool showTime);
    void drawEvent(PageLayout& pg, const Event& e, const EventInfo& info, qreal x, qreal staffTop, bool beamed);
    void drawBeamGroup(PageLayout& pg, const std::vector<Event>& evs, const StaffMeasureInfo& smi, const std::vector<int>& group,
                       const std::vector<qreal>& xs, qreal staffTop);
    void drawTie(int page, qreal x0, qreal x1, qreal y, bool above);
    void drawMarks(PageLayout& pg, const Event& e, const EventInfo& info, qreal x, qreal staffTop);
    void glyphCentered(PageLayout& pg, char16_t cp, qreal cx, qreal cy);
    void drawTuplets(PageLayout& pg, const std::vector<Event>& evs, const StaffMeasureInfo& vi,
                     const std::vector<qreal>& xs, qreal staffTop);
    void handleTies(int page, int staff, int voice, const Event& e, const EventInfo& info, qreal x, qreal staffTop,
                    qreal systemStartX, qreal systemEndX);

    struct PendingTie {
        int diatonic = 0;
        qreal x = 0, y = 0;
        bool above = true;
        int page = 0;
        qreal staffTop = 0;
        qreal systemEndX = 0;
    };
    std::vector<std::vector<PendingTie>> m_pendingTies; // per staff and layer

    // Smart shapes waiting for their last note (per staff and layer).
    struct PendingSlur {
        bool active = false;
        qreal x = 0, y = 0;
        bool above = true;
        int page = 0;
        qreal staffTop = 0, systemEndX = 0;
        qreal extreme = 0; // most outward point of the notes it spans (on its side)
    };
    struct PendingHairpin {
        bool active = false;
        int type = 0;
        qreal x = 0;
        int page = 0;
        qreal staffTop = 0, systemEndX = 0;
    };
    std::vector<PendingSlur> m_slurs;
    std::vector<PendingHairpin> m_hairpins;
    void handleShapes(int page, int staff, int voice, int measure, int index, const Event& e, const EventInfo& info,
                      qreal x, qreal staffTop, qreal systemStartX, qreal systemEndX);
    bool slurGoesAbove(int staff, int voice, int measure, int index) const;
    void drawSlur(int page, qreal x0, qreal y0, qreal x1, qreal y1, bool above, qreal extreme);
    void drawHairpin(int page, int type, qreal x0, qreal x1, qreal staffTop);
    qreal dynamicWidth(int dynamic) const;
    void drawMeasureRest(PageLayout& pg, qreal x0, qreal x1, qreal staffTop);

    void glyph(PageLayout& pg, char16_t cp, qreal x, qreal y)
    {
        pg.glyphs.append(m_font.glyphIndex(cp));
        pg.glyphPos.append(QPointF(x, y));
    }
    void line(PageLayout& pg, qreal x1, qreal y1, qreal x2, qreal y2, qreal w)
    {
        pg.lines.push_back({ QLineF(x1, y1, x2, y2), w * m_sp });
    }
    qreal y(qreal staffTop, int pos) const { return staffTop + (4 - pos) * m_sp / 2; }
    // Vertical room of each staff in a system: lyrics below, chord symbols above.
    struct StaffBand {
        int verses = 0;
        bool chords = false;
        qreal lyricBase = 0; // baseline of verse 1, relative to the staff's top line
        qreal chordBase = 0; // baseline of chord symbols, relative to the staff's top line (negative = above)
        qreal below = 0;     // extra room needed under the bottom line
        qreal above = 0;     // extra room needed over the top line
        qreal markBase = 0;  // top staff: baseline of endings/segno/jump texts (negative = above)
    };
    bool hasMarksAbove(int first, int last) const
    {
        for (int m = first; m <= last; ++m) {
            const MeasureMarks mk = m_score.marksAt(m);
            if (mk.endings || mk.segno || mk.coda || mk.jump) return true;
        }
        return false;
    }
    // Barlines and repeat signs.
    void repeatDots(PageLayout& pg, qreal cx, qreal staffTop);
    void drawRepeatMarks(PageLayout& pg, const System& sys, const std::vector<qreal>& x0s, const std::vector<qreal>& x1s, qreal sysTop);
    std::vector<StaffBand> bandsFor(int first, int last) const;
    std::vector<qreal> offsetsFor(const std::vector<StaffBand>& bands) const;
    qreal systemHeightFor(int first, int last) const { return offsetsFor(bandsFor(first, last)).back() + 4 * m_sp; }
    std::vector<StaffBand> m_bands;   // of the system being drawn
    std::vector<qreal> m_offsets;     // staff top offsets of the system being drawn

    struct PendingLyric {
        bool hyphen = false, extender = false;
        qreal xEnd = 0, y = 0, lastNoteEnd = 0;
        int page = 0;
        qreal staffTop = 0, systemEndX = 0;
    };
    std::map<long, PendingLyric> m_lyrics; // key: (staff * kVoices + layer) * 16 + verse
    void drawWords(PageLayout& pg, int page, int staff, int voice, const Event& e, qreal x, qreal staffTop, qreal systemEndX);
    void flushMelismas()
    {
        for (auto& [key, pl] : m_lyrics)
            if (pl.extender) {
                if (pl.lastNoteEnd > pl.xEnd + 0.6 * m_sp)
                    page(pl.page).lines.push_back({ QLineF(pl.xEnd + 0.2 * m_sp, pl.y, pl.lastNoteEnd, pl.y), 0.12 * m_sp });
                pl.extender = false;
            }
    }
    PageLayout& page(int i)
    {
        while (int(m_layout.pages.size()) <= i) m_layout.pages.emplace_back();
        return m_layout.pages[size_t(i)];
    }

    const Score& m_score;
    EngraveOptions m_opt;
    qreal m_sp;
    const Font& m_font;
    qreal m_headW = 0;
    std::vector<MeasureSpacing> m_spacing;
    Layout m_layout;
};

qreal Builder::clefColumnWidth(int measure) const
{
    qreal w = 0;
    for (int st = 0; st < int(m_score.staves.size()); ++st)
        w = std::max(w, m_font.advance(clefGlyph(m_score.clefAt(st, measure)), m_sp));
    return w;
}

// Width of a key signature; `previous` != fifths adds the naturals that cancel the old key.
qreal Builder::keyWidth(int fifths, int previous) const
{
    int naturals = 0;
    if (previous != fifths && previous != 0) {
        if (fifths == 0 || (fifths > 0) != (previous > 0)) naturals = std::abs(previous);
        else if (std::abs(fifths) < std::abs(previous)) naturals = std::abs(previous) - std::abs(fifths);
    }
    auto step = [&](char16_t g) { return m_font.advance(g, m_sp) + 0.1 * m_sp; };
    const qreal w = naturals * step(smufl::AccNatural) + std::abs(fifths) * step(fifths > 0 ? smufl::AccSharp : smufl::AccFlat);
    return naturals + std::abs(fifths) > 0 ? w + 0.6 * m_sp : 0;
}

qreal Builder::timeWidth(const TimeSig& t) const
{
    auto numW = [&](const QString& str) {
        qreal w = 0;
        for (QChar c : str) w += m_font.advance(char16_t(smufl::TimeSig0 + (c.unicode() - '0')), m_sp);
        return w;
    };
    return std::max(numW(QString::number(t.beats)), numW(QString::number(t.beatType))) + 1.0 * m_sp;
}

qreal Builder::headerWidth(int measure, bool showTime) const
{
    qreal w = 0.7 * m_sp + clefColumnWidth(measure) + 0.8 * m_sp;
    w += keyWidth(m_score.keyAt(measure), m_score.keyAt(measure));
    if (showTime) w += timeWidth(m_score.timeAt(measure));
    return w;
}

qreal Builder::drawKey(PageLayout& pg, qreal x, qreal staffTop, Clef clef, int fifths, int previous)
{
    auto step = [&](char16_t g) { return m_font.advance(g, m_sp) + 0.1 * m_sp; };
    // Naturals for the accidentals of the old key that are no longer there.
    if (previous != fifths && previous != 0) {
        const bool prevSharps = previous > 0;
        int from = 0;
        if (fifths != 0 && (fifths > 0) == prevSharps) from = std::abs(fifths);
        for (int i = from; i < std::abs(previous); ++i) {
            glyph(pg, smufl::AccNatural, x, y(staffTop, keySigPos(i, prevSharps, clef)));
            x += step(smufl::AccNatural);
        }
    }
    const bool sharps = fifths > 0;
    const char16_t acc = sharps ? smufl::AccSharp : smufl::AccFlat;
    for (int i = 0; i < std::abs(fifths); ++i) {
        glyph(pg, acc, x, y(staffTop, keySigPos(i, sharps, clef)));
        x += step(acc);
    }
    return x;
}

qreal Builder::drawTime(PageLayout& pg, qreal x, qreal staffTop, const TimeSig& t)
{
    auto digitsW = [&](const QString& str) {
        qreal w = 0;
        for (QChar c : str) w += m_font.advance(char16_t(smufl::TimeSig0 + (c.unicode() - '0')), m_sp);
        return w;
    };
    const QString top = QString::number(t.beats);
    const QString bot = QString::number(t.beatType);
    const qreal w = std::max(digitsW(top), digitsW(bot));
    auto drawNum = [&](const QString& str, int pos) {
        qreal xx = x + (w - digitsW(str)) / 2;
        for (QChar c : str) {
            const char16_t cp = char16_t(smufl::TimeSig0 + (c.unicode() - '0'));
            glyph(pg, cp, xx, y(staffTop, pos));
            xx += m_font.advance(cp, m_sp);
        }
    };
    drawNum(top, 2);
    drawNum(bot, -2);
    return x + w + 1.0 * m_sp;
}

void Builder::drawHeader(PageLayout& pg, int staffIdx, qreal x, qreal staffTop, int measure, bool showTime)
{
    const Clef clef = m_score.clefAt(staffIdx, measure);
    x += 0.7 * m_sp;
    glyph(pg, clefGlyph(clef), x, y(staffTop, clefPos(clef)));
    x += clefColumnWidth(measure) + 0.8 * m_sp;
    const int key = m_score.keyAt(measure);
    x = drawKey(pg, x, staffTop, clef, key, key);
    if (key != 0) x += 0.6 * m_sp;
    if (showTime) drawTime(pg, x, staffTop, m_score.timeAt(measure));
}

// Room taken by changes at the start of a measure that is not the first of its system.
qreal Builder::changePrefix(int measure) const
{
    if (measure <= 0) return 0;
    qreal w = 0;
    bool clef = false;
    for (int st = 0; st < int(m_score.staves.size()); ++st) clef |= m_score.clefChangesAt(st, measure);
    if (clef) w += clefColumnWidth(measure) + 0.6 * m_sp;
    if (m_score.keyChangesAt(measure)) w += keyWidth(m_score.keyAt(measure), m_score.keyAt(measure - 1)) + 0.2 * m_sp;
    if (m_score.timeChangesAt(measure)) w += timeWidth(m_score.timeAt(measure));
    return w > 0 ? w + 0.4 * m_sp : 0;
}

void Builder::drawChanges(PageLayout& pg, int staffIdx, int measure, qreal x, qreal staffTop)
{
    x += 0.6 * m_sp;
    bool anyClef = false;
    for (int st = 0; st < int(m_score.staves.size()); ++st) anyClef |= m_score.clefChangesAt(st, measure);
    const Clef clef = m_score.clefAt(staffIdx, measure);
    if (anyClef) {
        if (m_score.clefChangesAt(staffIdx, measure)) glyph(pg, clefGlyph(clef), x, y(staffTop, clefPos(clef)));
        x += clefColumnWidth(measure) + 0.6 * m_sp;
    }
    if (m_score.keyChangesAt(measure)) x = drawKey(pg, x, staffTop, clef, m_score.keyAt(measure), m_score.keyAt(measure - 1)) + 0.8 * m_sp;
    if (m_score.timeChangesAt(measure)) drawTime(pg, x, staffTop, m_score.timeAt(measure));
}

void Builder::drawMeasureRest(PageLayout& pg, qreal x0, qreal x1, qreal staffTop)
{
    const qreal w = m_font.advance(smufl::RestWhole, m_sp);
    glyph(pg, smufl::RestWhole, (x0 + x1) / 2 - w / 2, y(staffTop, 2));
}

void Builder::drawEvent(PageLayout& pg, const Event& e, const EventInfo& info, qreal x, qreal staffTop, bool beamed)
{
    if (e.isRest()) {
        const char16_t cp = smufl::restFor(e.durLog);
        const int pos = info.restPos;
        glyph(pg, cp, x, y(staffTop, pos));
        const qreal w = m_font.advance(cp, m_sp);
        for (int d = 0; d < e.dots; ++d)
            glyph(pg, smufl::AugmentationDot, x + w + (0.4 + 0.5 * d) * m_sp, y(staffTop, pos + 1 - (pos % 2 != 0 ? 1 : 0)));
        return;
    }

    const char16_t head = smufl::noteheadFor(e.durLog);
    const qreal headW = m_font.advance(head, m_sp);
    const size_t n = e.pitches.size();
    const qreal stemW = kStemW * m_sp;

    // Noteheads (with seconds offset to the other side of the stem).
    qreal minX = x, maxX = x + headW;
    for (size_t k = 0; k < n; ++k) {
        qreal hx = x;
        if (info.offset[k]) hx = info.stemUp ? x + headW - stemW : x - headW + stemW;
        glyph(pg, head, hx, y(staffTop, info.pos[k]));
        minX = std::min(minX, hx);
        maxX = std::max(maxX, hx + headW);
    }

    // Ledger lines.
    const int top = info.pos.back(), bottom = info.pos.front();
    for (int p = 6; p <= top; p += 2) line(pg, minX - kLedgerExt * m_sp, y(staffTop, p), maxX + kLedgerExt * m_sp, y(staffTop, p), kLedgerW);
    for (int p = -6; p >= bottom; p -= 2) line(pg, minX - kLedgerExt * m_sp, y(staffTop, p), maxX + kLedgerExt * m_sp, y(staffTop, p), kLedgerW);

    // Accidentals.
    const qreal accRight = minX - 0.2 * m_sp;
    for (size_t k = 0; k < n; ++k) {
        if (info.acc[k] == kNoAcc) continue;
        const char16_t cp = smufl::accidentalFor(info.acc[k]);
        const qreal ax = accRight - info.accCol[k] * kAccColW * m_sp - m_font.advance(cp, m_sp);
        glyph(pg, cp, ax, y(staffTop, info.pos[k]));
    }

    // Augmentation dots (always in a space).
    if (e.dots > 0) {
        std::set<int> dotPos;
        for (int p : info.pos) dotPos.insert((p % 2 == 0) ? p + 1 : p);
        for (int p : dotPos)
            for (int d = 0; d < e.dots; ++d)
                glyph(pg, smufl::AugmentationDot, maxX + (0.4 + 0.5 * d) * m_sp, y(staffTop, p));
    }

    // Stem and flag (beamed notes get their stems from drawBeamGroup).
    if (e.durLog >= 1 && !beamed) {
        const int extraFlags = std::max(0, e.durLog - 3);
        const qreal len = (kStemLen + 0.5 * extraFlags) * m_sp;
        const qreal mid = y(staffTop, 0);
        if (info.stemUp) {
            const qreal sx = x + headW - stemW / 2;
            const qreal y0 = y(staffTop, bottom) - 0.168 * m_sp;
            const qreal tip = std::min(y(staffTop, top) - len, mid);
            line(pg, sx, y0, sx, tip, kStemW);
            if (char16_t f = smufl::flagFor(e.durLog, true)) glyph(pg, f, sx - stemW / 2, tip);
        } else {
            const qreal sx = x + stemW / 2;
            const qreal y0 = y(staffTop, top) + 0.168 * m_sp;
            const qreal tip = std::max(y(staffTop, bottom) + len, mid);
            line(pg, sx, y0, sx, tip, kStemW);
            if (char16_t f = smufl::flagFor(e.durLog, false)) glyph(pg, f, sx - stemW / 2, tip);
        }
    }
}

void Builder::drawBeamGroup(PageLayout& pg, const std::vector<Event>& evs, const StaffMeasureInfo& smi,
                            const std::vector<int>& group, const std::vector<qreal>& xs, qreal staffTop)
{
    const qreal sp = m_sp;
    const qreal stemW = kStemW * sp;
    const qreal headW = m_font.advance(smufl::NoteheadBlack, sp);
    const bool up = smi.events[size_t(group.front())].stemUp;
    const qreal dir = up ? -1.0 : 1.0; // direction from notes towards the beam
    const qreal thick = 0.5 * sp;
    const qreal levelStep = 0.75 * sp;

    const size_t n = group.size();
    std::vector<qreal> sx(n), endY(n), baseY(n);
    int levels = 1;
    for (size_t i = 0; i < n; ++i) {
        const EventInfo& info = smi.events[size_t(group[i])];
        const qreal topY = y(staffTop, info.pos.back());
        const qreal botY = y(staffTop, info.pos.front());
        sx[i] = up ? xs[size_t(group[i])] + headW - stemW / 2 : xs[size_t(group[i])] + stemW / 2;
        endY[i] = up ? topY : botY;                       // note nearest to the beam
        baseY[i] = up ? botY - 0.168 * sp : topY + 0.168 * sp; // stem starts at the far note
        levels = std::max(levels, evs[size_t(group[i])].durLog - 2);
    }

    // Beam line through the stem tips of the outer notes, slope limited to one staff space.
    qreal y0 = endY.front() + dir * kStemLen * sp;
    qreal y1 = endY.back() + dir * kStemLen * sp;
    const qreal span = std::max(sx.back() - sx.front(), 1e-3);
    const qreal dy = std::clamp(y1 - y0, -1.0 * sp, 1.0 * sp);
    const qreal slope = dy / span;
    auto beamAt = [&](qreal x) { return y0 + slope * (x - sx.front()); };

    // Every stem must be at least this long (more beams need longer stems).
    const qreal minLen = (2.5 + 0.75 * (levels - 1)) * sp;
    qreal shift = 0;
    for (size_t i = 0; i < n; ++i) {
        const qreal need = endY[i] + dir * minLen; // beam must be at least this far out
        const qreal have = beamAt(sx[i]);
        shift = std::max(shift, up ? have - need : need - have);
    }
    y0 += dir * shift; // move the beam outwards (away from the notes)
    // Notes far outside the staff: the beam must at least reach the middle line.
    const qreal mid = y(staffTop, 0);
    const qreal nearEnd = up ? std::min(beamAt(sx.front()), beamAt(sx.back())) : std::max(beamAt(sx.front()), beamAt(sx.back()));
    if (up && nearEnd > mid) y0 -= nearEnd - mid;
    if (!up && nearEnd < mid) y0 += mid - nearEnd;

    // Stems.
    for (size_t i = 0; i < n; ++i) line(pg, sx[i], baseY[i], sx[i], beamAt(sx[i]), kStemW);

    // Beams: a parallelogram per segment, inner levels stacked towards the notes.
    auto quad = [&](qreal xa, qreal xb, int level) {
        const qreal off = -dir * levelStep * (level - 1);
        const qreal ya = beamAt(xa) + off, yb = beamAt(xb) + off;
        QPainterPath path;
        path.moveTo(xa, ya);
        path.lineTo(xb, yb);
        path.lineTo(xb, yb - dir * thick);
        path.lineTo(xa, ya - dir * thick);
        path.closeSubpath();
        pg.fills.push_back(path);
    };
    quad(sx.front() - stemW / 2, sx.back() + stemW / 2, 1);
    for (int level = 2; level <= levels; ++level) {
        auto has = [&](size_t i) { return evs[size_t(group[i])].durLog - 2 >= level; };
        for (size_t i = 0; i < n; ++i) {
            if (!has(i)) continue;
            const bool prev = i > 0 && has(i - 1);
            const bool next = i + 1 < n && has(i + 1);
            if (next) quad(sx[i] - stemW / 2, sx[i + 1] + stemW / 2, level);
            else if (!prev) {
                // Fractional beam (stub) pointing into the group.
                const qreal len = std::min(1.1 * sp, span / 2);
                if (i + 1 < n) quad(sx[i] - stemW / 2, sx[i] + len, level);
                else quad(sx[i] - len, sx[i] + stemW / 2, level);
            }
        }
    }
}

qreal Builder::dynamicWidth(int dynamic) const
{
    static const QString letters = QStringLiteral("pmfrsz");
    static const char16_t cps[6] = { 0xE520, 0xE521, 0xE522, 0xE523, 0xE524, 0xE525 };
    qreal w = 0;
    for (QChar c : dynamicText(dynamic)) {
        const int i = letters.indexOf(c);
        if (i >= 0) w += m_font.advance(cps[i], m_sp) - 0.08 * m_sp;
    }
    return w;
}

void Builder::drawSlur(int pageIdx, qreal x0, qreal y0, qreal x1, qreal y1, bool above, qreal extreme)
{
    const qreal sp = m_sp;
    if (x1 - x0 < sp) x1 = x0 + sp;
    const qreal len = x1 - x0;
    const qreal dir = above ? -1.0 : 1.0;
    // Height: grows with the length, and is enough to clear the notes in between.
    qreal h = std::clamp(len * 0.1, 1.2 * sp, 3.5 * sp);
    const qreal mid = (y0 + y1) / 2;
    const qreal need = above ? (mid - extreme) + 0.6 * sp : (extreme - mid) + 0.6 * sp;
    h = std::max(h, need / 0.75);
    h = std::min(h, 6.0 * sp);
    const qreal t = 0.22 * sp; // thickness in the middle
    QPainterPath path;
    path.moveTo(x0, y0);
    path.cubicTo(x0 + len * 0.25, y0 + dir * h, x1 - len * 0.25, y1 + dir * h, x1, y1);
    path.cubicTo(x1 - len * 0.25, y1 + dir * (h - t), x0 + len * 0.25, y0 + dir * (h - t), x0, y0);
    path.closeSubpath();
    page(pageIdx).fills.push_back(path);
}

void Builder::drawHairpin(int pageIdx, int type, qreal x0, qreal x1, qreal staffTop)
{
    const qreal sp = m_sp;
    if (x1 - x0 < 1.5 * sp) x1 = x0 + 1.5 * sp;
    const qreal yc = staffTop + 4 * sp + 2.6 * sp; // level with the dynamics
    const qreal open = 0.65 * sp;
    PageLayout& pg = page(pageIdx);
    if (type == HairpinCresc) {
        line(pg, x0, yc, x1, yc - open, 0.12);
        line(pg, x0, yc, x1, yc + open, 0.12);
    } else {
        line(pg, x0, yc - open, x1, yc, 0.12);
        line(pg, x0, yc + open, x1, yc, 0.12);
    }
}

// A slur goes above unless every stemmed note under it has its stem up (then it goes below).
bool Builder::slurGoesAbove(int staff, int voice, int measure, int index) const
{
    bool sawStem = false;
    for (int mm = measure; mm < m_score.measureCount(); ++mm) {
        const auto& evs = m_score.staves[size_t(staff)].measures[size_t(mm)].voice(voice);
        const auto& infos = m_spacing[size_t(mm)].staves[size_t(staff)].v[size_t(voice)].events;
        for (size_t i = (mm == measure ? size_t(index) : 0); i < evs.size(); ++i) {
            if (!evs[i].isRest() && evs[i].durLog >= 1) {
                if (!infos[i].stemUp) return true;
                sawStem = true;
            }
            if (evs[i].slurEnd && !(mm == measure && int(i) == index)) return !sawStem;
        }
    }
    return !sawStem;
}

void Builder::handleShapes(int pageIdx, int staff, int voice, int measure, int index, const Event& e, const EventInfo& info,
                           qreal x, qreal staffTop, qreal systemStartX, qreal systemEndX)
{
    const qreal sp = m_sp;
    const qreal headW = m_font.advance(e.isRest() ? smufl::restFor(e.durLog) : smufl::noteheadFor(e.durLog), sp);
    const size_t key = size_t(staff * kVoices + voice);
    const bool stem = !e.isRest() && e.durLog >= 1;

    // Where a slur attaches to this note: next to the notehead, or past the stem when the
    // stem is on the slur's side.
    auto attachY = [&](bool above) {
        if (e.isRest()) return y(staffTop, above ? 4 : -4) + (above ? -sp : sp);
        if (above) {
            const qreal top = y(staffTop, info.pos.back());
            return stem && info.stemUp ? top - kStemLen * sp - 0.4 * sp : top - 1.0 * sp;
        }
        const qreal bottom = y(staffTop, info.pos.front());
        return stem && !info.stemUp ? bottom + kStemLen * sp + 0.4 * sp : bottom + 1.0 * sp;
    };

    // ---- slur ----
    PendingSlur& sl = m_slurs[key];
    if (sl.active && e.slurEnd) {
        const qreal ex = x + headW / 2;
        const qreal ey = attachY(sl.above);
        if (sl.page == pageIdx && qFuzzyCompare(sl.staffTop, staffTop)) {
            drawSlur(pageIdx, sl.x, sl.y, ex, ey, sl.above, sl.extreme);
        } else {
            // Broken by a system break: one piece to the end of the first system, one from the start of the next.
            drawSlur(sl.page, sl.x, sl.y, sl.systemEndX - 0.5 * sp, sl.y, sl.above, sl.y);
            drawSlur(pageIdx, systemStartX - 0.5 * sp, ey, ex, ey, sl.above, ey);
        }
        sl.active = false;
    } else if (sl.active) {
        // A note under the slur pushes it outward.
        if (sl.page == pageIdx && qFuzzyCompare(sl.staffTop, staffTop)) {
            const qreal yy = attachY(sl.above);
            sl.extreme = sl.above ? std::min(sl.extreme, yy) : std::max(sl.extreme, yy);
        }
    }
    if (e.slurStart && !sl.active) {
        sl.active = true;
        sl.above = slurGoesAbove(staff, voice, measure, index);
        sl.x = x + headW / 2;
        sl.y = attachY(sl.above);
        sl.extreme = sl.y;
        sl.page = pageIdx;
        sl.staffTop = staffTop;
        sl.systemEndX = systemEndX;
    }

    // ---- hairpin ----
    PendingHairpin& hp = m_hairpins[key];
    if (hp.active && e.hairpinEnd) {
        const qreal endX = e.dynamic ? x - 0.8 * sp : x + headW;
        if (hp.page == pageIdx && qFuzzyCompare(hp.staffTop, staffTop)) {
            drawHairpin(pageIdx, hp.type, hp.x, endX, staffTop);
        } else {
            drawHairpin(hp.page, hp.type, hp.x, hp.systemEndX - 1.0 * sp, hp.staffTop);
            drawHairpin(pageIdx, hp.type, systemStartX, endX, staffTop);
        }
        hp.active = false;
    }
    if (e.hairpin && !hp.active) {
        hp.active = true;
        hp.type = e.hairpin;
        // Start after this note's dynamic mark, if it has one.
        hp.x = e.dynamic ? x - 0.4 * sp + dynamicWidth(e.dynamic) + 0.6 * sp : x;
        hp.page = pageIdx;
        hp.staffTop = staffTop;
        hp.systemEndX = systemEndX;
    }
}

std::vector<Builder::StaffBand> Builder::bandsFor(int first, int last) const
{
    const qreal sp = m_sp;
    std::vector<StaffBand> bands(m_score.staves.size());
    for (size_t s = 0; s < bands.size(); ++s) {
        StaffBand& b = bands[s];
        int top = 4, bottom = -4;
        bool dyn = false;
        for (int mm = first; mm <= last; ++mm) {
            const Measure& meas = m_score.staves[s].measures[size_t(mm)];
            const StaffInfo& si = m_spacing[size_t(mm)].staves[s];
            for (int v = 0; v < kVoices; ++v) {
                const auto& evs = meas.voice(v);
                for (size_t k = 0; k < evs.size(); ++k) {
                    const Event& e = evs[k];
                    const EventInfo& inf = si.v[size_t(v)].events[k];
                    for (size_t verse = 0; verse < e.lyrics.size(); ++verse)
                        if (!e.lyrics[verse].text.isEmpty()) b.verses = std::max(b.verses, int(verse) + 1);
                    b.chords |= !e.chordSymbol.isEmpty();
                    dyn |= e.dynamic || e.hairpin;
                    if (inf.pos.empty()) continue;
                    const bool stem = e.durLog >= 1;
                    top = std::max(top, inf.pos.back() + (stem && inf.stemUp ? 7 : 1));
                    bottom = std::min(bottom, inf.pos.front() - (stem && !inf.stemUp ? 7 : 1));
                }
            }
        }
        // Lyrics: under the notes (and under dynamics/hairpins, which sit at ~7 spaces).
        b.lyricBase = std::max(7.6 * sp, (4 - bottom) * sp / 2 + 2.8 * sp);
        if (dyn) b.lyricBase = std::max(b.lyricBase, 9.6 * sp);
        if (b.verses > 0) b.below = b.lyricBase + (b.verses - 1) * 2.4 * sp + 0.8 * sp - 4 * sp;
        // Chord symbols: over the staff and over high notes.
        b.chordBase = std::min(-2.0 * sp, (4 - top) * sp / 2 - 1.4 * sp);
        if (b.chords) b.above = -b.chordBase + 1.8 * sp;
        // Endings, segno/coda and jump texts: over the top staff, above chord symbols.
        b.markBase = std::min(-3.0 * sp, b.chords ? b.chordBase - 2.4 * sp : b.chordBase - 0.6 * sp);
        if (s == 0 && hasMarksAbove(first, last)) {
            bool stacked = false; // segno/jump over a measure that also has an ending bracket
            for (int mm = first; mm <= last; ++mm) {
                const MeasureMarks mk = m_score.marksAt(mm);
                stacked |= mk.endings && (mk.segno || mk.coda || mk.jump);
            }
            b.above = std::max(b.above, -b.markBase + (stacked ? 6.0 : 3.6) * sp);
        }
    }
    return bands;
}

std::vector<qreal> Builder::offsetsFor(const std::vector<StaffBand>& bands) const
{
    std::vector<qreal> off(bands.size(), 0);
    for (size_t s = 1; s < bands.size(); ++s)
        off[s] = off[s - 1] + 4 * m_sp + std::max(kStaffGap * m_sp, bands[s - 1].below + bands[s].above + 1.0 * m_sp);
    return off;
}

void Builder::drawWords(PageLayout& pg, int pageIdx, int staff, int voice, const Event& e, qreal x, qreal staffTop, qreal systemEndX)
{
    const qreal sp = m_sp;
    const StaffBand& band = m_bands[size_t(staff)];
    const qreal headW = m_font.advance(e.isRest() ? smufl::restFor(e.durLog) : smufl::noteheadFor(e.durLog), sp);

    // Chord symbol, left-aligned with the notehead.
    if (!e.chordSymbol.isEmpty()) {
        // Letters as text; sharps and flats from the music font (chord-symbol glyphs), so they
        // look right whatever text font is installed.
        const qreal yb = staffTop + band.chordBase;
        qreal cx = x;
        QString chunk;
        auto flush = [&] {
            if (chunk.isEmpty()) return;
            pg.texts.push_back({ chunk, QPointF(cx, yb), 11, true, false, 0 });
            cx += textAdvance(chunk, 11, true);
            chunk.clear();
        };
        for (QChar c : prettyChord(e.chordSymbol)) {
            if (c.unicode() == 0x266F || c.unicode() == 0x266D) {
                flush();
                const char16_t g = c.unicode() == 0x266F ? char16_t(0xED62) : char16_t(0xED60); // csymAccidentalSharp/Flat
                const qreal k = 0.55; // chord-symbol size relative to the staff glyphs
                pg.scaledGlyphs.push_back({ m_font.glyphIndex(g), QPointF(cx + 0.05 * sp, yb - 0.15 * sp), k });
                cx += m_font.advance(g, sp) * k + 0.1 * sp;
            } else {
                chunk += c;
            }
        }
        flush();
    }

    // Lyrics: one line per verse, syllables centred under the notehead.
    const qreal lyricSize = 10;
    for (int verse = 0; verse < band.verses; ++verse) {
        const long key = long(staff * kVoices + voice) * 16 + verse;
        const qreal yy = staffTop + band.lyricBase + verse * 2.4 * sp;
        const Syllable* sy = e.isRest() ? nullptr : e.lyric(verse);
        auto it = m_lyrics.find(key);
        if (it != m_lyrics.end()) {
            PendingLyric& pl = it->second;
            const bool sameLine = pl.page == pageIdx && qFuzzyCompare(pl.staffTop, staffTop);
            if (sy) {
                const qreal w = textAdvance(sy->text, lyricSize);
                const qreal xStart = x + headW / 2 - w / 2;
                PageLayout& prevPg = page(pl.page);
                if (pl.extender && pl.lastNoteEnd > pl.xEnd + 0.6 * sp)
                    prevPg.lines.push_back({ QLineF(pl.xEnd + 0.2 * sp, pl.y, pl.lastNoteEnd, pl.y), 0.12 * sp });
                if (pl.hyphen) {
                    // A short dash halfway between the syllables (or at the end of the line before a break).
                    const qreal hx = sameLine ? (pl.xEnd + xStart) / 2 : std::min(pl.xEnd + 1.2 * sp, pl.systemEndX - 0.4 * sp);
                    const qreal hy = pl.y - lyricSize * 0.3;
                    if (!sameLine || xStart - pl.xEnd > 0.9 * sp)
                        prevPg.lines.push_back({ QLineF(hx - 0.4 * sp, hy, hx + 0.4 * sp, hy), 0.11 * sp });
                }
                m_lyrics.erase(it);
            } else if (!e.isRest() && pl.extender && sameLine) {
                pl.lastNoteEnd = x + headW; // the melisma line runs under this note too
            }
        }
        if (sy) {
            pg.texts.push_back({ sy->text, QPointF(x + headW / 2, yy), lyricSize, false, false, 1 });
            if (sy->hyphen || sy->extender) {
                PendingLyric pl;
                pl.hyphen = sy->hyphen;
                pl.extender = sy->extender && !sy->hyphen;
                pl.xEnd = x + headW / 2 + textAdvance(sy->text, lyricSize) / 2;
                pl.y = yy;
                pl.lastNoteEnd = x + headW;
                pl.page = pageIdx;
                pl.staffTop = staffTop;
                pl.systemEndX = systemEndX;
                m_lyrics[key] = pl;
            }
        }
    }
}

void Builder::glyphCentered(PageLayout& pg, char16_t cp, qreal cx, qreal cy)
{
    const QRectF bb = m_font.bbox(cp, m_sp);
    glyph(pg, cp, cx - bb.center().x(), cy - bb.center().y());
}

// Articulations next to the notehead (away from the stem), fermata above the staff,
// dynamics below the staff.
void Builder::drawMarks(PageLayout& pg, const Event& e, const EventInfo& info, qreal x, qreal staffTop)
{
    const qreal sp = m_sp;
    const char16_t head = e.isRest() ? smufl::restFor(e.durLog) : smufl::noteheadFor(e.durLog);
    const qreal cx = x + m_font.advance(head, sp) / 2;

    qreal lowestMark = 0; // bottom of marks drawn under the notes (keeps dynamics clear of them)
    if (!e.isRest() && (e.articulations & ~unsigned(ArticFermata | ArticMarcato))) {
        // Notehead side: below for up-stems, above for down-stems (whole notes: below).
        const bool below = e.durLog <= 0 ? true : info.stemUp;
        int p = below ? info.pos.front() - 2 : info.pos.back() + 2;
        auto inSpace = [&](int q) {
            if (q >= -4 && q <= 4 && q % 2 == 0) return below ? q - 1 : q + 1; // never on a staff line
            return q;
        };
        // Order outward from the note: staccato/staccatissimo, tenuto, accent/marcato.
        static const std::pair<unsigned, std::pair<char16_t, char16_t>> order[] = {
            { ArticStaccato, { 0xE4A2, 0xE4A3 } }, { ArticStaccatissimo, { 0xE4A6, 0xE4A7 } },
            { ArticTenuto, { 0xE4A4, 0xE4A5 } }, { ArticAccent, { 0xE4A0, 0xE4A1 } },
        };
        for (const auto& [bit, glyphs] : order) {
            if (!(e.articulations & bit)) continue;
            const int q = inSpace(p);
            glyphCentered(pg, below ? glyphs.second : glyphs.first, cx, y(staffTop, q));
            if (below) lowestMark = std::max(lowestMark, y(staffTop, q) + 0.6 * sp);
            p = below ? q - 2 : q + 2;
        }
    }

    if (!e.isRest() && (e.articulations & ArticMarcato)) {
        // Marcato always goes above (clear of the staff and of an up-stem).
        int q = info.pos.back() + 2;
        if (e.durLog >= 1 && info.stemUp) q = info.pos.back() + 9;
        q = std::max(q, 6);
        glyphCentered(pg, 0xE4AC, cx, y(staffTop, q));
    }

    if (e.articulations & ArticFermata) {
        // Above everything: staff, notes and an up-stem.
        qreal top = y(staffTop, 4) - 1.2 * sp;
        if (!e.isRest()) {
            const qreal noteTop = y(staffTop, info.pos.back()) - (e.durLog >= 1 && info.stemUp ? kStemLen * sp : 0.8 * sp);
            top = std::min(top, noteTop - 0.6 * sp);
        }
        const QRectF bb = m_font.bbox(0xE4C0, sp);
        glyph(pg, 0xE4C0, cx - bb.center().x(), top - bb.bottom());
    }

    if (e.dynamic) {
        // Below the staff (and below low notes / down-stems), left-aligned with the notehead.
        qreal base = staffTop + 4 * sp + 3.0 * sp;
        if (!e.isRest()) {
            const qreal noteBottom = y(staffTop, info.pos.front()) + (e.durLog >= 1 && !info.stemUp ? kStemLen * sp : 0.6 * sp);
            base = std::max(base, noteBottom + 2.2 * sp);
        }
        if (lowestMark > 0) base = std::max(base, lowestMark + 2.0 * sp);
        const QString text = dynamicText(e.dynamic);
        static const QString letters = QStringLiteral("pmfrsz");
        static const char16_t cps[6] = { 0xE520, 0xE521, 0xE522, 0xE523, 0xE524, 0xE525 };
        qreal dx = x - 0.4 * sp;
        for (QChar c : text) {
            const int i = letters.indexOf(c);
            if (i < 0) continue;
            glyph(pg, cps[i], dx, base);
            dx += m_font.advance(cps[i], sp) - 0.08 * sp; // dynamics letters sit a little tighter
        }
    }
}

void Builder::drawTuplets(PageLayout& pg, const std::vector<Event>& evs, const StaffMeasureInfo& vi,
                          const std::vector<qreal>& xs, qreal staffTop)
{
    const qreal sp = m_sp;
    const qreal headW = m_font.advance(smufl::NoteheadBlack, sp);
    size_t k = 0;
    while (k < evs.size()) {
        if (!evs[k].inTuplet()) { ++k; continue; }
        // A group: from a tuplet start to the next start / ratio change / non-tuplet event.
        const size_t k0 = k;
        ++k;
        while (k < evs.size() && evs[k].inTuplet() && !evs[k].tupletStart && evs[k].tupletActual == evs[k0].tupletActual) ++k;
        const size_t k1 = k - 1;

        // Side: where the stems are (above for up-stems), like the number of a beamed triplet.
        bool above = true;
        for (size_t i = k0; i <= k1; ++i)
            if (!evs[i].isRest()) { above = vi.events[i].stemUp; break; }
        const bool allBeamed = vi.events[k0].beamGroup >= 0 &&
            std::all_of(vi.events.begin() + long(k0), vi.events.begin() + long(k1) + 1,
                        [&](const EventInfo& e) { return e.beamGroup == vi.events[k0].beamGroup; });

        // Clear all noteheads, stems and the staff itself.
        qreal edge = above ? y(staffTop, 4) - 0.5 * sp : y(staffTop, -4) + 0.5 * sp;
        for (size_t i = k0; i <= k1; ++i) {
            const EventInfo& info = vi.events[i];
            if (evs[i].isRest()) {
                const qreal ry = y(staffTop, info.restPos);
                edge = above ? std::min(edge, ry - 1.5 * sp) : std::max(edge, ry + 1.5 * sp);
                continue;
            }
            const bool stem = evs[i].durLog >= 1;
            const qreal stemLen = (kStemLen + 0.5 * std::max(0, evs[i].durLog - 3)) * sp;
            if (above) edge = std::min(edge, y(staffTop, info.pos.back()) - (stem && info.stemUp ? stemLen : 0.8 * sp));
            else edge = std::max(edge, y(staffTop, info.pos.front()) + (stem && !info.stemUp ? stemLen : 0.8 * sp));
        }
        const qreal by = edge + (above ? -0.9 : 0.9) * sp; // bracket line
        const qreal x0 = xs[k0] - 0.3 * sp;
        const qreal x1 = xs[k1] + headW + 0.3 * sp;
        const qreal cx = (x0 + x1) / 2;

        // Number (SMuFL tuplet digits), centred on the bracket line.
        const QString num = QString::number(evs[k0].tupletActual);
        qreal w = 0;
        for (QChar c : num) w += m_font.advance(char16_t(0xE880 + (c.unicode() - '0')), sp);
        qreal nx = cx - w / 2;
        const qreal ny = by + 0.75 * sp;
        for (QChar c : num) {
            const char16_t cp = char16_t(0xE880 + (c.unicode() - '0'));
            glyph(pg, cp, nx, ny);
            nx += m_font.advance(cp, sp);
        }
        if (allBeamed) continue; // a beamed group shows just the number

        const qreal hook = (above ? 0.8 : -0.8) * sp;
        const qreal gap = w / 2 + 0.4 * sp;
        line(pg, x0, by, cx - gap, by, kThinBarW * 0.8);
        line(pg, cx + gap, by, x1, by, kThinBarW * 0.8);
        line(pg, x0, by, x0, by + hook, kThinBarW * 0.8);
        line(pg, x1, by, x1, by + hook, kThinBarW * 0.8);
    }
}

void Builder::drawTie(int pageIdx, qreal x0, qreal x1, qreal yy, bool above)
{
    if (x1 - x0 < 0.5 * m_sp) x1 = x0 + 0.5 * m_sp;
    const qreal len = x1 - x0;
    const qreal h = std::clamp(len * 0.12, 0.45 * m_sp, 1.3 * m_sp);
    const qreal s = above ? -1.0 : 1.0;
    const qreal t = 0.17 * m_sp; // thickness in the middle
    QPainterPath p;
    p.moveTo(x0, yy);
    p.cubicTo(x0 + len * 0.25, yy + s * h, x1 - len * 0.25, yy + s * h, x1, yy);
    p.cubicTo(x1 - len * 0.25, yy + s * (h - t), x0 + len * 0.25, yy + s * (h - t), x0, yy);
    p.closeSubpath();
    page(pageIdx).fills.push_back(p);
}

void Builder::handleTies(int pageIdx, int staff, int voice, const Event& e, const EventInfo& info, qreal x, qreal staffTop,
                         qreal systemStartX, qreal systemEndX)
{
    const qreal headW = m_font.advance(smufl::noteheadFor(e.durLog), m_sp);
    auto& pending = m_pendingTies[size_t(staff * kVoices + voice)];

    // Finish ties coming from the previous event of this staff.
    for (const PendingTie& t : pending) {
        auto it = std::find_if(e.pitches.begin(), e.pitches.end(), [&](const Pitch& p) { return p.diatonic() == t.diatonic; });
        if (it == e.pitches.end()) continue;
        const qreal endX = x + 0.15 * headW;
        if (t.page == pageIdx && qFuzzyCompare(t.staffTop, staffTop)) {
            drawTie(pageIdx, t.x, endX, t.y, t.above);
        } else {
            // Tie broken by a system (or page) break: two halves.
            drawTie(t.page, t.x, t.systemEndX - 0.5 * m_sp, t.y, t.above);
            drawTie(pageIdx, systemStartX - 0.5 * m_sp, endX, t.y - t.staffTop + staffTop, t.above);
        }
    }
    pending.clear();

    // Start new ties.
    const size_t n = e.pitches.size();
    for (size_t k = 0; k < n; ++k) {
        if (!e.pitches[k].tie) continue;
        PendingTie t;
        t.diatonic = e.pitches[k].diatonic();
        t.above = n == 1 ? !info.stemUp : (k >= n / 2);
        t.x = x + (info.offset[k] && info.stemUp ? 1.85 : 0.85) * headW;
        t.y = y(staffTop, info.pos[k]) + (t.above ? -0.55 : 0.55) * m_sp;
        t.page = pageIdx;
        t.staffTop = staffTop;
        t.systemEndX = systemEndX;
        pending.push_back(t);
    }
}

void Builder::drawSystem(const System& sys, int pageIdx, qreal left, qreal top, bool firstSystem)
{
    PageLayout& pg = page(pageIdx);
    const int nStaves = int(m_score.staves.size());
    qreal xEnd = left + sys.headerW;
    for (int m = sys.first; m <= sys.last; ++m)
        xEnd += m_spacing[size_t(m)].width(sys.stretch) + (m > sys.first ? m_spacing[size_t(m)].prefix : 0);

    flushMelismas(); // lines left open by the previous system end where its last note ended
    m_bands = bandsFor(sys.first, sys.last);
    m_offsets = offsetsFor(m_bands);
    const qreal sysTop = top;
    const qreal sysBottom = top + m_offsets.back() + 4 * m_sp;

    for (int s = 0; s < nStaves; ++s) {
        const qreal st = top + m_offsets[size_t(s)];
        for (int l = 0; l < 5; ++l) line(pg, left, st + l * m_sp, xEnd, st + l * m_sp, kStaffLineW);
        drawHeader(pg, s, left, st, sys.first, sys.showTime);

        if (firstSystem && !m_score.staves[size_t(s)].name.isEmpty()) {
            TextItem t;
            t.text = m_score.staves[size_t(s)].name;
            t.pos = QPointF(left - 1.0 * m_sp, st + 2 * m_sp + 3.5);
            t.size = 10;
            t.align = -1;
            pg.texts.push_back(t);
        }
    }
    // System start line connecting staves.
    if (nStaves > 1) line(pg, left, sysTop, left, sysBottom, kThinBarW);

    // Measure number at the start of each system (except the first).
    if (sys.first > 0) {
        TextItem t;
        t.text = QString::number(sys.first + 1);
        t.pos = QPointF(left + sys.headerW, sysTop - 2.2 * m_sp);
        t.size = 8;
        t.italic = true;
        pg.texts.push_back(t);
    }

    qreal mx = left + sys.headerW;
    std::vector<qreal> measureX0, measureX1; // content start / end of each measure of the system
    for (int m = sys.first; m <= sys.last; ++m) {
        const MeasureSpacing& ms = m_spacing[size_t(m)];
        const qreal pre = m > sys.first ? ms.prefix : 0; // clef/key/time change drawn at the measure start
        const qreal w = ms.width(sys.stretch) + pre;
        const MeasureMarks mk = m_score.marksAt(m);
        measureX0.push_back(mx + pre);
        measureX1.push_back(mx + w);
        if (mk.repeatStart) {
            // Thick + thin line and dots. Right after a plain barline, the thick line replaces it.
            const MeasureMarks prev = m_score.marksAt(m - 1);
            const bool prevThick = m > sys.first && pre == 0 && (prev.repeatEnd || prev.bar == BarStyle::Final);
            const qreal x0 = mx + pre + (m == sys.first ? 0.4 * m_sp : 0);
            if (!prevThick) line(pg, x0 + (kThickBarW / 2) * m_sp, sysTop, x0 + (kThickBarW / 2) * m_sp, sysBottom, kThickBarW);
            const qreal tx = x0 + (kThickBarW + 0.4 + kThinBarW / 2) * m_sp;
            line(pg, tx, sysTop, tx, sysBottom, kThinBarW);
            for (int s = 0; s < nStaves; ++s) repeatDots(pg, tx + (0.45 + kRepeatDotR) * m_sp, top + m_offsets[size_t(s)]);
        }
        for (int s = 0; s < nStaves; ++s) {
            const qreal st = top + m_offsets[size_t(s)];
            const Measure& meas = m_score.staves[size_t(s)].measures[size_t(m)];
            const StaffInfo& sinfo = ms.staves[size_t(s)];
            MeasureSlot slot;
            slot.page = pageIdx;
            slot.staff = s;
            slot.measure = m;
            slot.x0 = mx;
            slot.x1 = mx + w;
            slot.staffTop = st;
            slot.clef = m_score.clefAt(s, m);
            slot.lyricBase = st + m_bands[size_t(s)].lyricBase;
            slot.chordBase = st + m_bands[size_t(s)].chordBase;
            if (pre > 0) drawChanges(pg, s, m, mx, st);
            if (meas.isEmpty()) {
                drawMeasureRest(pg, mx + pre, mx + w, st);
                for (int v = 0; v < kVoices; ++v) m_pendingTies[size_t(s * kVoices + v)].clear();
            }
            const qreal headW = m_font.advance(smufl::NoteheadBlack, m_sp);
            for (int v = 0; v < kVoices; ++v) {
                const std::vector<Event>& evs = meas.voice(v);
                const StaffMeasureInfo& vi = sinfo.v[size_t(v)];
                std::vector<qreal>& xs = slot.eventX[size_t(v)];
                for (size_t k = 0; k < evs.size(); ++k) {
                    const EventInfo& info = vi.events[k];
                    const qreal ex = mx + pre + ms.xAt(ms.indexOf(info.onset), sys.stretch) + (info.shiftRight ? 1.1 * headW : 0);
                    drawEvent(pg, evs[k], info, ex, st, info.beamGroup >= 0);
                    drawMarks(pg, evs[k], info, ex, st);
                    drawWords(pg, pageIdx, s, v, evs[k], ex, st, xEnd);
                    handleTies(pageIdx, s, v, evs[k], info, ex, st, left + sys.headerW, xEnd);
                    handleShapes(pageIdx, s, v, m, int(k), evs[k], info, ex, st, left + sys.headerW, xEnd);
                    xs.push_back(ex);
                }
                for (const std::vector<int>& group : vi.beams) drawBeamGroup(pg, evs, vi, group, xs, st);
                drawTuplets(pg, evs, vi, xs, st);
                if (evs.empty()) m_pendingTies[size_t(s * kVoices + v)].clear();
                const size_t ai = ms.indexOf(vi.filled);
                slot.appendX[size_t(v)] = ai + 1 < ms.onsets.size() || vi.filled < m_score.capacityAt(m)
                                              ? mx + pre + ms.xAt(ai, sys.stretch)
                                              : mx + w - 1.6 * m_sp;
            }
            m_layout.measureSlots.push_back(std::move(slot));
        }
        mx += w;
        const bool last = m == m_score.measureCount() - 1;
        const MeasureMarks next = m_score.marksAt(m + 1);
        if (mk.repeatEnd || last || mk.bar == BarStyle::Final) {
            line(pg, mx - (kThickBarW / 2) * m_sp, sysTop, mx - (kThickBarW / 2) * m_sp, sysBottom, kThickBarW);
            const qreal tx = mx - (kThickBarW + (mk.repeatEnd ? 0.4 : 0.6) + kThinBarW / 2) * m_sp;
            line(pg, tx, sysTop, tx, sysBottom, kThinBarW);
            if (mk.repeatEnd) {
                for (int s = 0; s < nStaves; ++s) repeatDots(pg, tx - (0.45 + kRepeatDotR) * m_sp, top + m_offsets[size_t(s)]);
                if (mk.repeatTimes > 2) { // "3x" over the barline
                    pg.texts.push_back({ QStringLiteral("%1x").arg(mk.repeatTimes), QPointF(mx, sysTop - 1.0 * m_sp), 9, false, true, -1 });
                }
            }
        } else if (mk.bar == BarStyle::Double) {
            const qreal bx = mx - (kThinBarW / 2) * m_sp;
            line(pg, bx, sysTop, bx, sysBottom, kThinBarW);
            line(pg, bx - 0.5 * m_sp, sysTop, bx - 0.5 * m_sp, sysBottom, kThinBarW);
        } else if (!(next.repeatStart && m < sys.last && m_spacing[size_t(m + 1)].prefix == 0)) {
            const qreal bx = mx - (kThinBarW / 2) * m_sp;
            line(pg, bx, sysTop, bx, sysBottom, kThinBarW);
        }
    }
    if (hasMarksAbove(sys.first, sys.last)) drawRepeatMarks(pg, sys, measureX0, measureX1, sysTop);
}

void Builder::repeatDots(PageLayout& pg, qreal cx, qreal staffTop)
{
    const qreal r = kRepeatDotR * m_sp;
    QPainterPath path;
    path.addEllipse(QPointF(cx, staffTop + 1.5 * m_sp), r, r);
    path.addEllipse(QPointF(cx, staffTop + 2.5 * m_sp), r, r);
    pg.fills.push_back(path);
}

// Endings (volta brackets), segno, coda and jump instructions over the top staff of a system.
void Builder::drawRepeatMarks(PageLayout& pg, const System& sys, const std::vector<qreal>& x0s, const std::vector<qreal>& x1s, qreal sysTop)
{
    const qreal sp = m_sp;
    const qreal base = sysTop + m_bands.front().markBase; // text baseline
    const qreal bracketY = base - 1.4 * sp;              // ending bracket line

    for (int m = sys.first; m <= sys.last; ++m) {
        const size_t i = size_t(m - sys.first);
        const MeasureMarks mk = m_score.marksAt(m);

        // Ending: one bracket over each run of measures with the same numbers.
        if (mk.endings && (m == sys.first || m_score.marksAt(m - 1).endings != mk.endings)) {
            int e = m;
            while (e < sys.last && m_score.marksAt(e + 1).endings == mk.endings) ++e;
            const bool opens = m == 0 || m_score.marksAt(m - 1).endings != mk.endings;
            int realEnd = e;
            while (realEnd + 1 < m_score.measureCount() && m_score.marksAt(realEnd + 1).endings == mk.endings) ++realEnd;
            const bool closes = realEnd == e && m_score.marksAt(e).repeatEnd;
            const qreal xa = x0s[i] + (mk.repeatStart ? 0.2 * sp : 0.3 * sp);
            const qreal xb = x1s[size_t(e - sys.first)] - (realEnd == e ? 0.6 * sp : 0);
            line(pg, xa, bracketY, xb, bracketY, 0.13);
            if (opens) {
                line(pg, xa, bracketY, xa, bracketY + 2.2 * sp, 0.13);
                pg.texts.push_back({ endingLabel(mk.endings), QPointF(xa + 0.5 * sp, bracketY + 1.9 * sp), 10, true, false, 0 });
            }
            if (closes) line(pg, xb, bracketY, xb, bracketY + 2.2 * sp, 0.13);
        }

        // Segno / coda at the start of the measure.
        qreal gx = x0s[i] + 0.3 * sp;
        for (const auto [on, cp] : { std::pair{ mk.segno, char16_t(0xE047) }, std::pair{ mk.coda, char16_t(0xE048) } }) {
            if (!on) continue;
            const QRectF bb = m_font.bbox(cp, sp);
            glyph(pg, cp, gx - bb.left(), base - (mk.endings ? 2.6 * sp : 0));
            gx += bb.width() + 0.5 * sp;
        }

        // Jump / Fine at the end of the measure, right-aligned.
        if (mk.jump) {
            qreal xr = x1s[i] - 0.4 * sp;
            const qreal y = base - (mk.endings ? 2.6 * sp : 0);
            if (mk.jump == JumpToCoda) {
                const QRectF bb = m_font.bbox(0xE048, sp);
                glyph(pg, 0xE048, xr - bb.right(), y);
                xr -= bb.width() + 0.5 * sp;
            }
            pg.texts.push_back({ jumpText(mk.jump), QPointF(xr, y), 11, true, true, -1 });
        }
    }
}

Layout Builder::run()
{
    const int nMeasures = m_score.measureCount();
    if (m_score.staves.empty() || nMeasures == 0 || !m_font.isValid()) {
        page(0);
        return m_layout;
    }
    m_spacing.reserve(size_t(nMeasures));
    for (int m = 0; m < nMeasures; ++m) {
        m_spacing.push_back(computeSpacing(m_score, m, m_sp, m_headW));
        m_spacing.back().prefix = changePrefix(m);
        const MeasureMarks mk = m_score.marksAt(m);
        MeasureSpacing& ms = m_spacing.back();
        if (mk.repeatStart) { // |: before the first note
            const qreal w = kRepeatStartW * m_sp;
            for (qreal& f : ms.fixedAt) f += w;
            ms.fixedTotal += w;
        }
        if (mk.repeatEnd) ms.fixedTotal += kRepeatEndW * m_sp;
        else if (mk.bar == BarStyle::Double) ms.fixedTotal += 0.4 * m_sp;
    }

    // Scroll view: one endless system on a single strip, no title, natural spacing.
    if (m_opt.continuous) {
        const qreal margin = 6 * m_sp;
        qreal indent = 0;
        for (const Staff& st : m_score.staves)
            if (!st.name.isEmpty()) indent = std::max(indent, textAdvance(st.name, 10) + 1.6 * m_sp);
        System sys;
        sys.first = 0;
        sys.last = nMeasures - 1;
        sys.showTime = true;
        sys.headerW = headerWidth(0, true);
        qreal width = sys.headerW;
        for (int mm = 0; mm < nMeasures; ++mm) width += m_spacing[size_t(mm)].width(1.0) + m_spacing[size_t(mm)].prefix;
        m_layout.pageWidth = 2 * margin + indent + width;
        m_layout.pageHeight = 2 * margin + 8 * m_sp + systemHeightFor(0, nMeasures - 1)
                              + bandsFor(0, nMeasures - 1).front().above + bandsFor(0, nMeasures - 1).back().below;
        m_opt.pageHeight = m_layout.pageHeight;
        drawSystem(sys, 0, margin + indent, margin + 4 * m_sp, true);
        flushMelismas();
        return std::move(m_layout);
    }

    // Title block.
    const qreal margin = m_opt.margin;
    qreal contentTop = margin;
    PageLayout& first = page(0);
    if (!m_score.title.isEmpty()) {
        first.texts.push_back({ m_score.title, QPointF(m_opt.pageWidth / 2, margin + 18), 20, true, false, 1 });
        contentTop = margin + 34;
    }
    if (!m_score.composer.isEmpty()) {
        first.texts.push_back({ m_score.composer, QPointF(m_opt.pageWidth - margin, contentTop + 10), 10, false, false, -1 });
        contentTop += 16;
    }

    // First-system indent for instrument names.
    qreal indent = 0;
    for (const Staff& st : m_score.staves)
        if (!st.name.isEmpty()) indent = std::max(indent, textAdvance(st.name, 10) + 1.6 * m_sp);
    indent = std::min(indent, 110.0);

    // Break measures into systems (greedy) and compute the stretch factor of each.
    const qreal fullWidth = m_opt.pageWidth - 2 * margin;
    std::vector<System> systems;
    int m = 0;
    while (m < nMeasures) {
        System sys;
        sys.first = m;
        sys.showTime = systems.empty() || m_score.timeChangesAt(m);
        sys.headerW = headerWidth(m, sys.showTime);
        const qreal avail = fullWidth - (systems.empty() ? indent : 0) - sys.headerW;
        qreal fixed = 0, stretch = 0;
        while (m < nMeasures) {
            const MeasureSpacing& ms = m_spacing[size_t(m)];
            const qreal pre = m > sys.first ? ms.prefix : 0;
            if (m > sys.first && fixed + stretch + ms.fixedTotal + pre + ms.stretchTotal > avail) break;
            fixed += ms.fixedTotal + pre;
            stretch += ms.stretchTotal;
            ++m;
        }
        sys.last = m - 1;
        const bool lastSystem = m >= nMeasures;
        if (stretch > 0) {
            sys.stretch = (avail - fixed) / stretch;
            if (lastSystem && fixed + stretch < 0.6 * avail) sys.stretch = 1.0;
            sys.stretch = std::max(sys.stretch, 0.5);
        }
        systems.push_back(sys);
    }

    // Stack systems on pages.
    // Extra room above/below a system for notes and stems that stick out of the outer staves
    // (the default room is ~3 staff spaces).
    auto extents = [&](const System& sys, qreal& above, qreal& below) {
        int top = 4, bottom = -4;
        const size_t last = m_score.staves.size() - 1;
        for (int mm = sys.first; mm <= sys.last; ++mm) {
            const MeasureSpacing& ms = m_spacing[size_t(mm)];
            const Measure& upper = m_score.staves.front().measures[size_t(mm)];
            const Measure& lower = m_score.staves[last].measures[size_t(mm)];
            for (int v = 0; v < kVoices; ++v) {
                const auto& ue = ms.staves.front().v[size_t(v)].events;
                for (size_t k = 0; k < ue.size(); ++k) {
                    if (ue[k].pos.empty()) continue;
                    const bool stem = upper.voice(v)[k].durLog >= 1 && ue[k].stemUp;
                    top = std::max(top, ue[k].pos.back() + (stem ? 7 : 1));
                }
                const auto& le = ms.staves[last].v[size_t(v)].events;
                for (size_t k = 0; k < le.size(); ++k) {
                    if (le[k].pos.empty()) continue;
                    const bool stem = lower.voice(v)[k].durLog >= 1 && !le[k].stemUp;
                    bottom = std::min(bottom, le[k].pos.front() - (stem ? 7 : 1));
                    if (lower.voice(v)[k].dynamic || lower.voice(v)[k].hairpin) bottom = std::min(bottom, std::min(-10, le[k].pos.front() - 9));
                }
            }
        }
        above = std::max(0.0, (top - 4) * m_sp / 2 - 3 * m_sp);
        below = std::max(0.0, (-4 - bottom) * m_sp / 2 - 3 * m_sp);
        const auto bands = bandsFor(sys.first, sys.last);
        above = std::max(above, bands.front().above - 3 * m_sp);
        below = std::max(below, bands.back().below - 3 * m_sp);
    };

    int pageIdx = 0;
    qreal yTop = contentTop + 4 * m_sp;
    for (size_t i = 0; i < systems.size(); ++i) {
        qreal above = 0, below = 0;
        extents(systems[i], above, below);
        const qreal sysH = systemHeightFor(systems[i].first, systems[i].last);
        if (i > 0 && yTop + above + sysH + below > m_opt.pageHeight - margin) {
            ++pageIdx;
            yTop = margin + 4 * m_sp;
        }
        yTop += above;
        const qreal left = margin + (i == 0 ? indent : 0);
        drawSystem(systems[i], pageIdx, left, yTop, i == 0);
        yTop += sysH + below + kSystemGap * m_sp;
    }
    flushMelismas();
    return std::move(m_layout);
}

} // namespace

Layout engrave(const Score& score, const EngraveOptions& opt)
{
    return Builder(score, opt).run();
}

} // namespace pauta
