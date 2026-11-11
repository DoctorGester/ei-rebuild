// The sessions' UDP socket driver (net/net_session.h, SocketDriver: the socket and the host's broadcast
// addresses), address parsing and formatting, and the TCP link to the master server (0x40 bytes:
// an outgoing and an incoming growing buffer, the socket, a state), on the platform's sockets. See
// docs/notes/network.md.
#include "game/net/net_session.h"
#include "engine/core/coder.h"
#include "platform/platform.h"

#include "lib/ipv4.h"
#include "lib/str.h"
#include "platform/log.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace net {

namespace {

namespace sockets = platform::net;
using sockets::kNoSocket;

// What the link's selects wait at most, in microseconds: they poll.
constexpr std::int32_t kSelectTimeout = 0;

unsigned AddressCount(const SocketDriver* d) { return static_cast<unsigned>(d->addresses.size()); }

} // namespace


bool NetAddress::Parse(std::string_view text, int defaultPort) {
    std::memset(this, 0, sizeof(*this));
    family = 2;
    std::string s(text.substr(0, text.find('\0')));   // to the first NUL, where a C string ended
    int number = defaultPort;
    const std::size_t colon = s.find(':');
    if (colon != std::string::npos) {
        number = str::ToInt(std::string_view(s).substr(colon + 1));
        s.resize(colon);
    }
    ip = lib::ParseIpv4(s);
    if (ip == lib::kNoAddress) {
        const std::optional<std::uint32_t> found = sockets::LookUp(s);
        if (!found) return false;
        ip = *found;
    }
    port = lib::ToNetwork(static_cast<std::uint16_t>(number));
    return true;
}

std::string NetAddress::ToString() const {
    char text[0x400];
    const std::uint16_t value = lib::FromNetwork(port);
    auto* bytes = reinterpret_cast<const std::uint8_t*>(&ip);
    std::snprintf(text, sizeof(text), "%i.%i.%i.%i:%i", bytes[0], bytes[1], bytes[2], bytes[3], value & 0xFFFF);
    return text;
}

// ---- The UDP driver ---------------------------------------------------------------------------------

// The broadcast address is the host's first address with the host part (by its class: A, B or C) set to 0xFF.
SocketDriver::SocketDriver() {
    socket = kNoSocket;
    const std::string name = sockets::HostName();
    if (name.empty()) return;
    const std::optional<std::uint32_t> found = sockets::LookUp(name);
    if (!found) return;
    const std::uint32_t ip = *found;
    NetAddress a{};
    a.family = sockets::kIpv4;
    if (ip == 0) return;
    a.ip = ip;
    auto* bytes = reinterpret_cast<std::uint8_t*>(&a.ip);
    std::uint8_t first = static_cast<std::uint8_t>(ip);
    if (first >= 1 && first <= 0x7E) bytes[1] = bytes[2] = bytes[3] = 0xFF;
    if (first >= 0x80 && first <= 0xBF) bytes[2] = bytes[3] = 0xFF;
    if (first >= 0xC0 && first <= 0xDF) bytes[3] = 0xFF;
    addresses.push_back(a);
}

SocketDriver::~SocketDriver() {
    if (socket != kNoSocket) sockets::Close(socket);
}

bool SocketDriver::Open(int port) {
    if (socket != kNoSocket) sockets::Close(socket);
    socket = kNoSocket;
    socket = sockets::Open(sockets::Kind::Udp);
    if (socket == kNoSocket) return false;
    NetAddress a{};
    a.family = sockets::kIpv4;
    a.port = lib::ToNetwork(static_cast<std::uint16_t>(port));
    if (port > 0 && !sockets::Bind(socket, a)) {
        // Not the game's: a second game on the machine holding the port leaves this session without a
        // socket, and its local client then talks to that game's server. The verify build's
        // check of the server's constructor builds a second server, which always gets this.
        const bool taken = sockets::LastError() == sockets::Error::AddressInUse;
        ei::Log("net: UDP port %d %s (bind failed): this session gets no socket", port,
                taken ? "is taken" : "cannot be bound");
        sockets::Close(socket);
        socket = kNoSocket;
        return false;
    }
    sockets::SetNonBlocking(socket, true);
    sockets::AllowBroadcast(socket);
    return true;
}

bool SocketDriver::Broadcast(NetAddress* out, unsigned index, int port) {
    if (index >= AddressCount(this)) return false;
    *out = addresses[index];
    out->port = lib::ToNetwork(static_cast<std::uint16_t>(port));
    return true;
}

// A broadcast address's trailing 0xFF bytes are the host part.
bool SocketDriver::IsLocal(const NetAddress* address) {
    std::uint32_t ip = address->ip;
    if (ip == 0x0100007Fu) return true;
    if (AddressCount(this) == 0) return false;
    for (unsigned i = 0; i < AddressCount(this); ++i) {
        std::uint32_t broadcast = addresses[i].ip;
        std::uint32_t host = 0;
        for (int shift = 24; shift >= 0; shift -= 8) {
            std::uint32_t byte = 0xFFu << shift;
            if ((byte & broadcast) != byte) break;
            host |= byte;
        }
        std::uint32_t network = ~host;
        if ((network & broadcast) == (network & ip)) return true;
    }
    return false;
}

bool SocketDriver::SendTo(const NetAddress* address, GameBuffer* buffer) {
    std::int32_t length = buffer->Size();
    return sockets::SendTo(socket, buffer->begin, length, *address) == length;
}

// The buffer is first sought (a virtual call) to room for 0x800 bytes.
bool SocketDriver::Receive(NetAddress* from, GrowingBuffer* buffer) {
    buffer->Seek(0x800);
    int n = sockets::ReceiveFrom(socket, buffer->begin, 0x800, from);
    if (n >= 0) {
        from->family = sockets::kIpv4;
        std::memset(from->zero, 0, sizeof(from->zero));
        buffer->SetSize(static_cast<std::uint32_t>(n));
    }
    return n >= 0;
}

// ---- The master server link ----------------------------------------------------------------------

MasterLink::MasterLink(const NetAddress* address) { Connect(*address); }

void MasterLink::Connect(const NetAddress& address) {
    socket = sockets::Open(sockets::Kind::Tcp);
    if (socket == kNoSocket) return;
    sockets::Address any{};
    any.family = sockets::kIpv4;
    if (!sockets::Bind(socket, any)) {
        if (socket != kNoSocket) sockets::Close(socket);
        socket = kNoSocket;
        return;
    }
    sockets::SetNonBlocking(socket, true);
    sockets::Connect(socket, address);
    state = State::Connecting;
}

// Closes the socket (left as it is); the buffers are destroyed after it (in, then out).
MasterLink::~MasterLink() {
    if (socket != kNoSocket) sockets::Close(socket);
}

// One step: connecting (1) until writable (4) or failed; sending (4) the whole outgoing buffer at once,
// blocking (then 2); receiving (2) into the incoming buffer until the peer closes (3). An error closes the
// socket (0). Each step waits at most kSelectTimeout (a poll). The failure check after a select that found the
// socket not ready asks with the set that select emptied, which always fails: an error never closes the link
// there. Returns the state.
MasterLink::State MasterLinkTick(MasterLink* self) {
    sockets::SocketGroup set = sockets::SocketGroup::Of(self->socket);
    auto close = [self](MasterLink::State state) {
        if (self->socket != kNoSocket) sockets::Close(self->socket);
        self->socket = kNoSocket;
        self->state = state;
    };
    switch (self->state) {
    case MasterLink::State::Connecting:
        if (sockets::Select(nullptr, &set, nullptr, kSelectTimeout) == 1) {
            self->state = MasterLink::State::Sending;
            return self->state;
        }
        if (sockets::Select(nullptr, nullptr, &set, kSelectTimeout) == 1) close(MasterLink::State::Closed);
        return self->state;
    case MasterLink::State::Receiving: {
        if (sockets::Select(&set, nullptr, nullptr, kSelectTimeout) != 1) {
            if (sockets::Select(nullptr, nullptr, &set, kSelectTimeout) == 1) close(MasterLink::State::Closed);
            return self->state;
        }
        std::uint8_t data[0x800];
        int n = sockets::Receive(self->socket, data, 0x800);
        if (n == -1) {
            close(MasterLink::State::Closed);
            return self->state;
        }
        if (n == 0) {
            // EI_Plugin's NewMaster (always on): a link closed before anything arrived (nothing received) is left as
            // it is.
            GameBuffer* received = &self->in;
            if (received->PositionOf(received->high) == 0) return self->state;
            close(MasterLink::State::Done);
            return self->state;
        }
        self->in.Seek(self->in.Size());
        self->in.Write(data, static_cast<std::uint32_t>(n));
        return self->state;
    }
    case MasterLink::State::Sending: {
        if (sockets::Select(nullptr, &set, nullptr, kSelectTimeout) != 1) {
            if (sockets::Select(nullptr, nullptr, &set, kSelectTimeout) == 1) close(MasterLink::State::Closed);
            return self->state;
        }
        self->state = MasterLink::State::Receiving;
        sockets::SetNonBlocking(self->socket, false);
        int sent = sockets::Send(self->socket, self->out.begin, self->out.Size());
        sockets::SetNonBlocking(self->socket, true);
        if (sent != self->out.Size()) close(MasterLink::State::Closed);
        return self->state;
    }
    default:
        return self->state;
    }
}

} // namespace net
