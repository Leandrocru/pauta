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

void analyseEvents(const Score& score, const Staff& staff, const Measure& m, StaffInfo& out)
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
            for (size_t k = 0; k < n; ++k) info.pos[k] = staffPosition(e.pitches[k].diatonic(), staff.clef);
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
            const int current = it != state.end() ? it->second : keyAlterFor(p.step, score.keyFifths);
            if (p.alter != current) info.acc[k] = p.alter;
            state[p.diatonic()] = p.alter;
        }
    }

    // 3) Beam groups per layer: consecutive eighth-or-shorter notes inside one beaming unit;
    //    rests, longer notes and unit boundaries break the beam.
    const Frac unit = beamUnit(score.time);
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
    const Frac capacity = score.time.capacity();
    std::set<Frac> onsetSet{ Frac(0) };
    Frac end = capacity;
    ms.staves.resize(score.staves.size());
    for (size_t s = 0; s < score.staves.size(); ++s) {
        const Staff& st = score.staves[s];
        analyseEvents(score, st, st.measures[size_t(mi)], ms.staves[s]);
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
    }

    Layout run();

private:
    struct System {
        int first = 0, last = 0; // measure range [first, last]
        qreal stretch = 1;
        qreal headerW = 0;
        bool showTime = false;
    };

    qreal headerWidth(bool showTime) const;
    void drawSystem(const System& sys, int page, qreal left, qreal top, bool firstSystem);
    void drawHeader(PageLayout& pg, int staff, qreal x, qreal staffTop, bool showTime);
    void drawEvent(PageLayout& pg, const Event& e, const EventInfo& info, qreal x, qreal staffTop, bool beamed);
    void drawBeamGroup(PageLayout& pg, const std::vector<Event>& evs, const StaffMeasureInfo& smi, const std::vector<int>& group,
                       const std::vector<qreal>& xs, qreal staffTop);
    void drawTie(int page, qreal x0, qreal x1, qreal y, bool above);
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
    qreal staffDist() const { return (4 + kStaffGap) * m_sp; }
    qreal systemHeight() const { return (qreal(m_score.staves.size()) - 1) * staffDist() + 4 * m_sp; }
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

qreal Builder::headerWidth(bool showTime) const
{
    qreal clefW = 0;
    for (const Staff& st : m_score.staves) clefW = std::max(clefW, m_font.advance(clefGlyph(st.clef), m_sp));
    qreal w = 0.7 * m_sp + clefW + 0.8 * m_sp;
    const int nAcc = std::abs(m_score.keyFifths);
    if (nAcc > 0) {
        const char16_t acc = m_score.keyFifths > 0 ? smufl::AccSharp : smufl::AccFlat;
        w += nAcc * (m_font.advance(acc, m_sp) + 0.1 * m_sp) + 0.6 * m_sp;
    }
    if (showTime) {
        const QString top = QString::number(m_score.time.beats);
        const QString bot = QString::number(m_score.time.beatType);
        auto numW = [&](const QString& s) {
            qreal t = 0;
            for (QChar c : s) t += m_font.advance(char16_t(smufl::TimeSig0 + (c.unicode() - '0')), m_sp);
            return t;
        };
        w += std::max(numW(top), numW(bot)) + 1.0 * m_sp;
    }
    return w;
}

void Builder::drawHeader(PageLayout& pg, int staffIdx, qreal x, qreal staffTop, bool showTime)
{
    const Staff& st = m_score.staves[size_t(staffIdx)];
    x += 0.7 * m_sp;
    glyph(pg, clefGlyph(st.clef), x, y(staffTop, clefPos(st.clef)));
    qreal clefW = 0;
    for (const Staff& s2 : m_score.staves) clefW = std::max(clefW, m_font.advance(clefGlyph(s2.clef), m_sp));
    x += clefW + 0.8 * m_sp;

    const int nAcc = std::abs(m_score.keyFifths);
    const bool sharps = m_score.keyFifths > 0;
    const char16_t acc = sharps ? smufl::AccSharp : smufl::AccFlat;
    for (int i = 0; i < nAcc; ++i) {
        glyph(pg, acc, x, y(staffTop, keySigPos(i, sharps, st.clef)));
        x += m_font.advance(acc, m_sp) + 0.1 * m_sp;
    }
    if (nAcc > 0) x += 0.6 * m_sp;

    if (showTime) {
        auto digitsW = [&](const QString& s) {
            qreal t = 0;
            for (QChar c : s) t += m_font.advance(char16_t(smufl::TimeSig0 + (c.unicode() - '0')), m_sp);
            return t;
        };
        const QString top = QString::number(m_score.time.beats);
        const QString bot = QString::number(m_score.time.beatType);
        const qreal w = std::max(digitsW(top), digitsW(bot));
        auto drawNum = [&](const QString& s, int pos) {
            qreal xx = x + (w - digitsW(s)) / 2;
            for (QChar c : s) {
                const char16_t cp = char16_t(smufl::TimeSig0 + (c.unicode() - '0'));
                glyph(pg, cp, xx, y(staffTop, pos));
                xx += m_font.advance(cp, m_sp);
            }
        };
        drawNum(top, 2);
        drawNum(bot, -2);
    }
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
    for (int m = sys.first; m <= sys.last; ++m) xEnd += m_spacing[size_t(m)].width(sys.stretch);

    const qreal sysTop = top;
    const qreal sysBottom = top + systemHeight();

    for (int s = 0; s < nStaves; ++s) {
        const qreal st = top + s * staffDist();
        for (int l = 0; l < 5; ++l) line(pg, left, st + l * m_sp, xEnd, st + l * m_sp, kStaffLineW);
        drawHeader(pg, s, left, st, sys.showTime);

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
    for (int m = sys.first; m <= sys.last; ++m) {
        const MeasureSpacing& ms = m_spacing[size_t(m)];
        const qreal w = ms.width(sys.stretch);
        for (int s = 0; s < nStaves; ++s) {
            const qreal st = top + s * staffDist();
            const Measure& meas = m_score.staves[size_t(s)].measures[size_t(m)];
            const StaffInfo& sinfo = ms.staves[size_t(s)];
            MeasureSlot slot;
            slot.page = pageIdx;
            slot.staff = s;
            slot.measure = m;
            slot.x0 = mx;
            slot.x1 = mx + w;
            slot.staffTop = st;
            slot.clef = m_score.staves[size_t(s)].clef;
            if (meas.isEmpty()) {
                drawMeasureRest(pg, mx, mx + w, st);
                for (int v = 0; v < kVoices; ++v) m_pendingTies[size_t(s * kVoices + v)].clear();
            }
            const qreal headW = m_font.advance(smufl::NoteheadBlack, m_sp);
            for (int v = 0; v < kVoices; ++v) {
                const std::vector<Event>& evs = meas.voice(v);
                const StaffMeasureInfo& vi = sinfo.v[size_t(v)];
                std::vector<qreal>& xs = slot.eventX[size_t(v)];
                for (size_t k = 0; k < evs.size(); ++k) {
                    const EventInfo& info = vi.events[k];
                    const qreal ex = mx + ms.xAt(ms.indexOf(info.onset), sys.stretch) + (info.shiftRight ? 1.1 * headW : 0);
                    drawEvent(pg, evs[k], info, ex, st, info.beamGroup >= 0);
                    handleTies(pageIdx, s, v, evs[k], info, ex, st, left + sys.headerW, xEnd);
                    xs.push_back(ex);
                }
                for (const std::vector<int>& group : vi.beams) drawBeamGroup(pg, evs, vi, group, xs, st);
                drawTuplets(pg, evs, vi, xs, st);
                if (evs.empty()) m_pendingTies[size_t(s * kVoices + v)].clear();
                const size_t ai = ms.indexOf(vi.filled);
                slot.appendX[size_t(v)] = ai + 1 < ms.onsets.size() || vi.filled < m_score.time.capacity()
                                              ? mx + ms.xAt(ai, sys.stretch)
                                              : mx + w - 1.6 * m_sp;
            }
            m_layout.measureSlots.push_back(std::move(slot));
        }
        mx += w;
        const bool last = m == m_score.measureCount() - 1;
        if (last) {
            line(pg, mx - (kThickBarW / 2) * m_sp, sysTop, mx - (kThickBarW / 2) * m_sp, sysBottom, kThickBarW);
            const qreal tx = mx - (kThickBarW + 0.6 + kThinBarW / 2) * m_sp;
            line(pg, tx, sysTop, tx, sysBottom, kThinBarW);
        } else {
            const qreal bx = mx - (kThinBarW / 2) * m_sp;
            line(pg, bx, sysTop, bx, sysBottom, kThinBarW);
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
    for (int m = 0; m < nMeasures; ++m) m_spacing.push_back(computeSpacing(m_score, m, m_sp, m_headW));

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
        sys.headerW = headerWidth(true);
        qreal width = sys.headerW;
        for (const MeasureSpacing& ms : m_spacing) width += ms.width(1.0);
        m_layout.pageWidth = 2 * margin + indent + width;
        m_layout.pageHeight = 2 * margin + 8 * m_sp + systemHeight();
        m_opt.pageHeight = m_layout.pageHeight;
        drawSystem(sys, 0, margin + indent, margin + 4 * m_sp, true);
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
        sys.showTime = systems.empty();
        sys.headerW = headerWidth(sys.showTime);
        const qreal avail = fullWidth - (systems.empty() ? indent : 0) - sys.headerW;
        qreal fixed = 0, stretch = 0;
        while (m < nMeasures) {
            const MeasureSpacing& ms = m_spacing[size_t(m)];
            if (m > sys.first && fixed + stretch + ms.fixedTotal + ms.stretchTotal > avail) break;
            fixed += ms.fixedTotal;
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
                }
            }
        }
        above = std::max(0.0, (top - 4) * m_sp / 2 - 3 * m_sp);
        below = std::max(0.0, (-4 - bottom) * m_sp / 2 - 3 * m_sp);
    };

    const qreal sysH = systemHeight();
    int pageIdx = 0;
    qreal yTop = contentTop + 4 * m_sp;
    for (size_t i = 0; i < systems.size(); ++i) {
        qreal above = 0, below = 0;
        extents(systems[i], above, below);
        if (i > 0 && yTop + above + sysH + below > m_opt.pageHeight - margin) {
            ++pageIdx;
            yTop = margin + 4 * m_sp;
        }
        yTop += above;
        const qreal left = margin + (i == 0 ? indent : 0);
        drawSystem(systems[i], pageIdx, left, yTop, i == 0);
        yTop += sysH + below + kSystemGap * m_sp;
    }
    return std::move(m_layout);
}

} // namespace

Layout engrave(const Score& score, const EngraveOptions& opt)
{
    return Builder(score, opt).run();
}

} // namespace pauta
