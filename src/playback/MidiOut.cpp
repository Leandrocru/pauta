#include "playback/MidiOut.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#elif defined(PAUTA_HAVE_ALSA)
#include "playback/SoftSynth.h"
#endif

namespace pauta::playback {

namespace {

class NullOut : public MidiOut {
public:
    explicit NullOut(QString why) : m_why(std::move(why)) {}
    bool ok() const override { return false; }
    QString name() const override { return m_why; }
    void send(uint8_t, uint8_t, uint8_t) override {}

private:
    QString m_why;
};

#if defined(_WIN32)
class WinMidiOut : public MidiOut {
public:
    WinMidiOut()
    {
        // MIDI_MAPPER = the default synthesizer chosen in Windows (normally the GS Wavetable Synth).
        m_ok = midiOutOpen(&m_h, MIDI_MAPPER, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR;
        if (m_ok) {
            MIDIOUTCAPSW caps{};
            if (midiOutGetDevCapsW(UINT_PTR(MIDI_MAPPER), &caps, sizeof(caps)) == MMSYSERR_NOERROR)
                m_name = QString::fromWCharArray(caps.szPname);
        }
    }
    ~WinMidiOut() override
    {
        if (m_ok) {
            allNotesOff();
            midiOutReset(m_h);
            midiOutClose(m_h);
        }
    }
    bool ok() const override { return m_ok; }
    QString name() const override { return m_name.isEmpty() ? QStringLiteral("Sintetizador MIDI do Windows") : m_name; }
    void send(uint8_t status, uint8_t d1, uint8_t d2) override
    {
        if (m_ok) midiOutShortMsg(m_h, DWORD(status) | (DWORD(d1) << 8) | (DWORD(d2) << 16));
    }

private:
    HMIDIOUT m_h = nullptr;
    bool m_ok = false;
    QString m_name;
};
#endif

} // namespace

std::unique_ptr<MidiOut> createMidiOut()
{
#if defined(_WIN32)
    auto out = std::make_unique<WinMidiOut>();
    if (out->ok()) return out;
    return std::make_unique<NullOut>(QStringLiteral("Nenhum sintetizador MIDI disponível no Windows"));
#elif defined(PAUTA_HAVE_ALSA)
    auto out = std::make_unique<SoftSynth>();
    if (out->ok()) return out;
    return std::make_unique<NullOut>(QStringLiteral("Não foi possível abrir a saída de áudio (ALSA)"));
#else
    return std::make_unique<NullOut>(QStringLiteral("Reprodução indisponível neste sistema"));
#endif
}

} // namespace pauta::playback
