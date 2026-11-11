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
    Frac onset;
};

struct StaffMeasureInfo {
    std::vector<EventInfo> events;
    Frac filled;
};

struct MeasureSpacing {
    std::vector<Frac> onsets;     // sorted unique; last = end of measure
    std::vector<qreal> fixedAt;   // fixed space before onset i
    std::vector<qreal> stretchAt; // stretchable space before onset i
    qreal fixedTotal = 0;
    qreal stretchTotal = 0;
    std::vector<StaffMeasureInfo> staves;

    qreal width(qreal f) const { return fixedTotal + stretchTotal * f; }
    qreal xAt(size_t i, qreal f) const { return fixedAt[i] + stretchAt[i] * f; }
    size_t indexOf(Frac t) const
    {
        auto it = std::lower_bound(onsets.begin(), onsets.end(), t);
        return size_t(it - onsets.begin());
    }
};

void analyseEvents(const Score& score, const Staff& staff, const Measure& m, StaffMeasureInfo& out)
{
    std::map<int, int> state; // diatonic -> current alteration within the measure
    Frac t;
    for (const Event& e : m.events) {
        EventInfo info;
        info.onset = t;
        t += e.duration();
        const size_t n = e.pitches.size();
        info.pos.resize(n);
        info.acc.assign(n, kNoAcc);
        info.accCol.assign(n, 0);
        info.offset.assign(n, false);
        for (size_t k = 0; k < n; ++k) {
            const Pitch& p = e.pitches[k];
            info.pos[k] = staffPosition(p.diatonic(), staff.clef);
            auto it = state.find(p.diatonic());
            const int current = it != state.end() ? it->second : keyAlterFor(p.step, score.keyFifths);
            if (p.alter != current) info.acc[k] = p.alter;
            state[p.diatonic()] = p.alter;
        }
        if (n > 0) {
            const int sum = info.pos.front() + info.pos.back();
            info.stemUp = sum < 0;
            // Seconds inside a chord: alternate noteheads to the other side of the stem.
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
        out.events.push_back(std::move(info));
    }
    out.filled = t;
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
        for (const EventInfo& e : ms.staves[s].events) onsetSet.insert(e.onset);
        onsetSet.insert(ms.staves[s].filled);
        end = std::max(end, ms.staves[s].filled);
    }
    onsetSet.insert(end);
    ms.onsets.assign(onsetSet.begin(), onsetSet.end());
    while (!ms.onsets.empty() && ms.onsets.back() > end) ms.onsets.pop_back();

    const size_t n = ms.onsets.size();
    std::vector<qreal> extraLeft(n, 0), extraRight(n, 0);
    for (size_t s = 0; s < score.staves.size(); ++s) {
        const Measure& m = score.staves[s].measures[size_t(mi)];
        for (size_t k = 0; k < m.events.size(); ++k) {
            const EventInfo& info = ms.staves[s].events[k];
            const size_t i = ms.indexOf(info.onset);
            qreal left = info.accCols > 0 ? info.accCols * kAccColW * sp + 0.2 * sp : 0;
            qreal right = m.events[k].dots > 0 ? (0.4 + 0.5 * m.events[k].dots) * sp : 0;
            if (info.hasSecond) (info.stemUp ? right : left) += headW;
            extraLeft[i] = std::max(extraLeft[i], left);
            extraRight[i] = std::max(extraRight[i], right);
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
    void drawEvent(PageLayout& pg, const Event& e, const EventInfo& info, qreal x, qreal staffTop);
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

void Builder::drawEvent(PageLayout& pg, const Event& e, const EventInfo& info, qreal x, qreal staffTop)
{
    if (e.isRest()) {
        const char16_t cp = smufl::restFor(e.durLog);
        const int pos = e.durLog <= 0 ? 2 : 0;
        glyph(pg, cp, x, y(staffTop, pos));
        const qreal w = m_font.advance(cp, m_sp);
        for (int d = 0; d < e.dots; ++d)
            glyph(pg, smufl::AugmentationDot, x + w + (0.4 + 0.5 * d) * m_sp, y(staffTop, 1));
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

    // Stem and flag.
    if (e.durLog >= 1) {
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
            const StaffMeasureInfo& smi = ms.staves[size_t(s)];
            MeasureSlot slot;
            slot.page = pageIdx;
            slot.staff = s;
            slot.measure = m;
            slot.x0 = mx;
            slot.x1 = mx + w;
            slot.staffTop = st;
            slot.clef = m_score.staves[size_t(s)].clef;
            if (meas.events.empty()) {
                drawMeasureRest(pg, mx, mx + w, st);
            } else {
                for (size_t k = 0; k < meas.events.size(); ++k) {
                    const EventInfo& info = smi.events[k];
                    const qreal ex = mx + ms.xAt(ms.indexOf(info.onset), sys.stretch);
                    drawEvent(pg, meas.events[k], info, ex, st);
                    slot.eventX.push_back(ex);
                }
            }
            const size_t ai = ms.indexOf(smi.filled);
            slot.appendX = ai + 1 < ms.onsets.size() || smi.filled < m_score.time.capacity()
                               ? mx + ms.xAt(ai, sys.stretch)
                               : mx + w - 1.6 * m_sp;
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
    const qreal sysH = systemHeight();
    int pageIdx = 0;
    qreal yTop = contentTop + 4 * m_sp;
    for (size_t i = 0; i < systems.size(); ++i) {
        if (i > 0 && yTop + sysH > m_opt.pageHeight - margin) {
            ++pageIdx;
            yTop = margin + 4 * m_sp;
        }
        const qreal left = margin + (i == 0 ? indent : 0);
        drawSystem(systems[i], pageIdx, left, yTop, i == 0);
        yTop += sysH + kSystemGap * m_sp;
    }
    return std::move(m_layout);
}

} // namespace

Layout engrave(const Score& score, const EngraveOptions& opt)
{
    return Builder(score, opt).run();
}

} // namespace pauta
