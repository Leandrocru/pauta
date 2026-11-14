#include "playback/MidiIn.h"

#include <QMetaObject>

#include <algorithm>
#include <QPointer>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#elif defined(PAUTA_HAVE_ALSA)
#include <QSocketNotifier>
#include <alsa/asoundlib.h>
#include <poll.h>
#include <vector>
#endif

namespace pauta::playback {

void MidiIn::inject(uint8_t status, uint8_t data1, uint8_t data2)
{
    const uint8_t type = status & 0xF0;
    if (type == 0x90 && data2 > 0) emit noteOn(data1 & 0x7F, data2);
    else if (type == 0x80 || type == 0x90) emit noteOff(data1 & 0x7F);
}

#if defined(_WIN32)

struct MidiIn::Impl {
    HMIDIIN handle = nullptr;
    QPointer<MidiIn> owner;
};

namespace {
// Runs on a WinMM thread: hand the message over to the GUI thread.
void CALLBACK midiInProc(HMIDIIN, UINT msg, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR)
{
    if (msg != MIM_DATA) return;
    auto* impl = reinterpret_cast<MidiIn::Impl*>(instance);
    MidiIn* self = impl->owner.data();
    if (!self) return;
    const auto status = uint8_t(param1 & 0xFF), d1 = uint8_t((param1 >> 8) & 0xFF), d2 = uint8_t((param1 >> 16) & 0xFF);
    QMetaObject::invokeMethod(self, [self, status, d1, d2] { self->inject(status, d1, d2); }, Qt::QueuedConnection);
}
} // namespace

MidiIn::MidiIn(QObject* parent) : QObject(parent), d(std::make_unique<Impl>()) { d->owner = this; }
MidiIn::~MidiIn() { close(); }

QStringList MidiIn::devices()
{
    QStringList out;
    const UINT n = midiInGetNumDevs();
    for (UINT i = 0; i < n; ++i) {
        MIDIINCAPSW caps{};
        if (midiInGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) out << QString::fromWCharArray(caps.szPname);
    }
    return out;
}

bool MidiIn::open(const QString& device)
{
    close();
    const int idx = int(devices().indexOf(device));
    if (idx < 0) return false;
    if (midiInOpen(&d->handle, UINT(idx), DWORD_PTR(&midiInProc), DWORD_PTR(d.get()), CALLBACK_FUNCTION) != MMSYSERR_NOERROR) {
        d->handle = nullptr;
        return false;
    }
    midiInStart(d->handle);
    m_name = device;
    return true;
}

void MidiIn::close()
{
    if (d->handle) {
        midiInStop(d->handle);
        midiInReset(d->handle);
        midiInClose(d->handle);
        d->handle = nullptr;
    }
    m_name.clear();
}

bool MidiIn::isOpen() const { return d->handle != nullptr; }

#elif defined(PAUTA_HAVE_ALSA)

struct MidiIn::Impl {
    snd_seq_t* seq = nullptr;
    int port = -1;
    snd_seq_addr_t source{};
    std::vector<QSocketNotifier*> notifiers;
};

namespace {
struct SeqPort { QString name; snd_seq_addr_t addr; };

// Readable hardware/software MIDI ports (keyboards), excluding the system and our own client.
std::vector<SeqPort> listPorts(snd_seq_t* seq)
{
    std::vector<SeqPort> out;
    snd_seq_client_info_t* ci;
    snd_seq_port_info_t* pi;
    snd_seq_client_info_alloca(&ci);
    snd_seq_port_info_alloca(&pi);
    const int self = snd_seq_client_id(seq);
    snd_seq_client_info_set_client(ci, -1);
    while (snd_seq_query_next_client(seq, ci) >= 0) {
        const int client = snd_seq_client_info_get_client(ci);
        if (client == 0 || client == self) continue;
        snd_seq_port_info_set_client(pi, client);
        snd_seq_port_info_set_port(pi, -1);
        while (snd_seq_query_next_port(seq, pi) >= 0) {
            const unsigned caps = snd_seq_port_info_get_capability(pi);
            if ((caps & (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ)) != (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ)) continue;
            if (caps & SND_SEQ_PORT_CAP_NO_EXPORT) continue;
            SeqPort p;
            p.name = QString::fromLocal8Bit(snd_seq_port_info_get_name(pi));
            p.addr = *snd_seq_port_info_get_addr(pi);
            out.push_back(p);
        }
    }
    return out;
}
} // namespace

MidiIn::MidiIn(QObject* parent) : QObject(parent), d(std::make_unique<Impl>()) {}
MidiIn::~MidiIn()
{
    close();
    if (d->seq) snd_seq_close(d->seq);
}

QStringList MidiIn::devices()
{
    QStringList out;
    snd_seq_t* seq = nullptr;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_INPUT, SND_SEQ_NONBLOCK) < 0) return out;
    for (const SeqPort& p : listPorts(seq)) out << p.name;
    snd_seq_close(seq);
    return out;
}

bool MidiIn::open(const QString& device)
{
    close();
    if (!d->seq) {
        if (snd_seq_open(&d->seq, "default", SND_SEQ_OPEN_INPUT, SND_SEQ_NONBLOCK) < 0) {
            d->seq = nullptr;
            return false;
        }
        snd_seq_set_client_name(d->seq, "Pauta");
        d->port = snd_seq_create_simple_port(d->seq, "Entrada MIDI", SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
                                             SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
        const int nfds = snd_seq_poll_descriptors_count(d->seq, POLLIN);
        std::vector<pollfd> fds(size_t(std::max(nfds, 0)));
        snd_seq_poll_descriptors(d->seq, fds.data(), unsigned(fds.size()), POLLIN);
        for (const pollfd& f : fds) {
            auto* n = new QSocketNotifier(f.fd, QSocketNotifier::Read, this);
            connect(n, &QSocketNotifier::activated, this, [this] {
                snd_seq_event_t* ev = nullptr;
                while (snd_seq_event_input(d->seq, &ev) >= 0 && ev) {
                    if (ev->type == SND_SEQ_EVENT_NOTEON)
                        inject(0x90, uint8_t(ev->data.note.note), uint8_t(ev->data.note.velocity));
                    else if (ev->type == SND_SEQ_EVENT_NOTEOFF)
                        inject(0x80, uint8_t(ev->data.note.note), 0);
                }
            });
            d->notifiers.push_back(n);
        }
    }
    for (const SeqPort& p : listPorts(d->seq)) {
        if (p.name != device) continue;
        if (snd_seq_connect_from(d->seq, d->port, p.addr.client, p.addr.port) < 0) return false;
        d->source = p.addr;
        m_name = device;
        return true;
    }
    return false;
}

void MidiIn::close()
{
    if (d->seq && !m_name.isEmpty()) snd_seq_disconnect_from(d->seq, d->port, d->source.client, d->source.port);
    m_name.clear();
}

bool MidiIn::isOpen() const { return !m_name.isEmpty(); }

#else

struct MidiIn::Impl {};
MidiIn::MidiIn(QObject* parent) : QObject(parent), d(std::make_unique<Impl>()) {}
MidiIn::~MidiIn() = default;
QStringList MidiIn::devices() { return {}; }
bool MidiIn::open(const QString&) { return false; }
void MidiIn::close() { m_name.clear(); }
bool MidiIn::isOpen() const { return false; }

#endif

} // namespace pauta::playback
