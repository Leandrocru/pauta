#include "playback/Player.h"

#include <algorithm>

namespace pauta::playback {

Player::Player(QObject* parent) : QObject(parent)
{
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(4);
    connect(&m_timer, &QTimer::timeout, this, &Player::tick);
}

Player::~Player()
{
    if (m_out) m_out->allNotesOff();
}

MidiOut& Player::out()
{
    if (!m_out) m_out = createMidiOut();
    return *m_out;
}

bool Player::available() const
{
    return const_cast<Player*>(this)->out().ok();
}

QString Player::outputName() const
{
    return const_cast<Player*>(this)->out().name();
}

void Player::play(const Score& score, int fromMeasure)
{
    stop();
    if (!out().ok()) return;
    m_seq = buildSequence(score, fromMeasure);
    m_next = 0;
    m_measure = -1;
    m_clock.start();
    m_timer.start();
    emit stateChanged(true);
    tick();
}

void Player::stop()
{
    const bool was = m_timer.isActive();
    m_timer.stop();
    if (m_out) m_out->allNotesOff();
    if (was) {
        m_measure = -1;
        emit measureChanged(-1);
        emit stateChanged(false);
    }
}

void Player::tick()
{
    const double now = double(m_clock.nsecsElapsed()) / 1e9;
    while (m_next < m_seq.messages.size() && m_seq.messages[m_next].time <= now) {
        const MidiMsg& m = m_seq.messages[m_next++];
        out().send(m.status, m.data1, m.data2);
    }
    // Which measure is sounding now.
    const auto& st = m_seq.measureStart;
    if (!st.empty()) {
        const auto it = std::upper_bound(st.begin(), st.end(), now);
        const size_t idx = size_t(std::max<ptrdiff_t>(0, it - st.begin() - 1));
        const int m = idx < m_seq.order.size() ? m_seq.order[idx] : m_seq.fromMeasure;
        if (m != m_measure && now < m_seq.length) {
            m_measure = m;
            emit measureChanged(m);
        }
    }
    if (now > m_seq.length + 0.3 && m_next >= m_seq.messages.size()) stop();
}

} // namespace pauta::playback
