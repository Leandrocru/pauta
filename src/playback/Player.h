#pragma once
// Plays a Sequence in real time (a precise 4 ms timer sends the due MIDI messages).
#include "playback/MidiOut.h"
#include "playback/Sequence.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <memory>

namespace pauta::playback {

class Player : public QObject {
    Q_OBJECT
public:
    explicit Player(QObject* parent = nullptr);
    ~Player() override;

    bool available() const;
    QString outputName() const;
    bool isPlaying() const { return m_timer.isActive(); }

    void play(const Score& score, int fromMeasure);
    void sendNow(uint8_t status, uint8_t data1, uint8_t data2) { out().send(status, data1, data2); } // MIDI thru
    void stop();

signals:
    void measureChanged(int measure); // -1 when playback ends
    void stateChanged(bool playing);

private:
    void tick();
    MidiOut& out();

    std::unique_ptr<MidiOut> m_out; // opened on first use
    Sequence m_seq;
    size_t m_next = 0;
    int m_measure = -1;
    QElapsedTimer m_clock;
    QTimer m_timer;
};

} // namespace pauta::playback
