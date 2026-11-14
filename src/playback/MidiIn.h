#pragma once
// MIDI keyboard input.
//  - Windows: WinMM (any connected USB/MIDI keyboard).
//  - Linux: ALSA sequencer (the keyboard's port is connected to a "Pauta" input port).
// Events are delivered on the GUI thread.
#include <QObject>
#include <QStringList>

#include <cstdint>
#include <memory>

namespace pauta::playback {

class MidiIn : public QObject {
    Q_OBJECT
public:
    explicit MidiIn(QObject* parent = nullptr);
    ~MidiIn() override;

    static QStringList devices();      // names of the MIDI inputs found now
    bool open(const QString& device);  // by name (from devices()); closes the previous one
    void close();
    bool isOpen() const;
    QString deviceName() const { return m_name; }

    // A raw MIDI message, as if it came from the keyboard (also used by the hardware callbacks).
    void inject(uint8_t status, uint8_t data1, uint8_t data2);

    struct Impl; // platform part (public so the system callback can reach it)

signals:
    void noteOn(int pitch, int velocity);
    void noteOff(int pitch);

private:
    std::unique_ptr<Impl> d;
    QString m_name;
};

} // namespace pauta::playback
