// The sessions' UDP socket driver (net_classes.h, SocketDriver: the socket and the host's broadcast
// addresses), address parsing and formatting, and the TCP link to the master server (0x40 bytes:
// an outgoing and an incoming growing buffer, the socket, a state), on the platform's sockets. See
// docs/notes/network.md.
#include "net_classes.h"
#include "net_coder.h"
#include "platform/net.h"

#include "ei/rebirth.h"
#include "../hook/log.h"

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

std::int32_t Length(GameBuffer* buffer) {
    if (buffer->cur > buffer->high) buffer->high = buffer->cur;
    return buffer->PositionOf(buffer->high);
}

} // namespace


bool NetAddress::Parse(const char* text, int defaultPort) {
    std::memset(this, 0, sizeof(*this));
    family = 2;
    std::string s(text, text + std::strlen(text));
    const std::size_t npos = 0xFFFFFFFFu;
    int number = defaultPort;
    std::size_t colon = npos;
    if (s.size() != 0) {
        for (const char* p = s.data(); p != s.data() + s.size(); ++p) {
            if (*p == ':') {
                colon = static_cast<std::size_t>(p - s.data());
                break;
            }
        }
    }
    if (colon != npos) {
        std::size_t first = colon + 1;   // substr's range check (out_of_range) can't fire: the colon is in s
        std::size_t n = s.size() - first;
        if (n >= npos) n = npos;
        {
            std::string digits(s.data() + first, s.data() + first + n);
            number = std::atoi(digits.data());
        }
        std::size_t m = s.size() < colon ? s.size() : colon;
        std::string host(s.data(), s.data() + m);
        s.assign(host.data(), host.data() + host.size());
    }
    ip = sockets::ParseIpv4(s.data());
    if (ip == sockets::kNoAddress && !sockets::LookUp(s.data(), &ip)) return false;
    port = sockets::ToNetwork(static_cast<std::uint16_t>(number));
    return true;
}

// The look-up is given the whole sockaddr (16 bytes, where an IPv4 address is 4).
std::string NetAddress::Format(int resolve) const {
    char text[0x400];
    bool named = false;
    if (static_cast<std::uint8_t>(resolve)) {
        const std::string name = sockets::HostNameAt(this, sizeof(*this));
        if (!name.empty()) {
            std::uint16_t value = sockets::FromNetwork(port);
            std::snprintf(text, sizeof(text), "%s:%i", name.c_str(), value & 0xFFFF);
            named = true;
        }
    }
    if (!named) {
        std::uint16_t value = sockets::FromNetwork(port);
        auto* bytes = reinterpret_cast<const std::uint8_t*>(&ip);
        std::snprintf(text, sizeof(text), "%i.%i.%i.%i:%i", bytes[0], bytes[1], bytes[2], bytes[3], value & 0xFFFF);
    }
    return text;
}

// ---- The UDP driver ---------------------------------------------------------------------------------

// The broadcast address is the host's first address with the host part (by its class: A, B or C) set to 0xFF.
SocketDriver::SocketDriver() {
    sockets::Start();
    socket = kNoSocket;
    char name[1000];
    if (!sockets::HostName(name, sizeof(name))) return;
    std::uint32_t ip;
    if (!sockets::LookUp(name, &ip)) return;
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
    sockets::Stop();
}

bool SocketDriver::Open(int port) {
    if (socket != kNoSocket) sockets::Close(socket);
    socket = kNoSocket;
    socket = sockets::Open(sockets::Kind::Udp);
    if (socket == kNoSocket) return false;
    NetAddress a{};
    a.family = sockets::kIpv4;
    a.port = sockets::ToNetwork(static_cast<std::uint16_t>(port));
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
    out->port = sockets::ToNetwork(static_cast<std::uint16_t>(port));
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
    std::int32_t length = Length(buffer);
    return sockets::SendTo(socket, buffer->begin, length, *address) == length;
}

// The buffer is first sought (a virtual call) to room for 0x800 bytes.
bool SocketDriver::Receive(NetAddress* from, GrowingBuffer* buffer) {
    buffer->Seek(0x800);
    int n = sockets::ReceiveFrom(socket, buffer->begin, 0x800, from);
    if (n >= 0) {
        from->family = sockets::kIpv4;
        std::memset(from->zero, 0, sizeof(from->zero));
        buffer->cur = buffer->begin;
        buffer->high = buffer->begin + n;
        if (buffer->high > buffer->end) buffer->Grow(static_cast<std::uint32_t>(n));
    }
    return n >= 0;
}

// ---- The master server link ----------------------------------------------------------------------

MasterLink::MasterLink(const NetAddress* address) {
    socket = sockets::Open(sockets::Kind::Tcp);
    if (socket == kNoSocket) {
        state = 0;
        return;
    }
    sockets::Address any{};
    any.family = sockets::kIpv4;
    if (!sockets::Bind(socket, any)) {
        if (socket != kNoSocket) sockets::Close(socket);
        socket = kNoSocket;
        state = 0;
        return;
    }
    sockets::SetNonBlocking(socket, true);
    sockets::Connect(socket, *address);
    state = 1;
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
int MasterLinkTick(MasterLink* self) {
    sockets::SocketGroup set(self->socket);
    auto close = [self](std::int32_t state) {
        if (self->socket != kNoSocket) sockets::Close(self->socket);
        self->socket = kNoSocket;
        self->state = state;
    };
    switch (self->state) {
    case 1:
        if (sockets::Select(nullptr, &set, nullptr, kSelectTimeout) == 1) {
            self->state = 4;
            return self->state;
        }
        if (sockets::Select(nullptr, nullptr, &set, kSelectTimeout) == 1) close(0);
        return self->state;
    case 2: {
        if (sockets::Select(&set, nullptr, nullptr, kSelectTimeout) != 1) {
            if (sockets::Select(nullptr, nullptr, &set, kSelectTimeout) == 1) close(0);
            return self->state;
        }
        std::uint8_t data[0x800];
        int n = sockets::Receive(self->socket, data, 0x800);
        if (n == -1) {
            close(0);
            return self->state;
        }
        if (n == 0) {
            // EI_Plugin's NewMaster (its 10005490): a link closed before anything
            // arrived (base - begin + high of +0x1C, `high` as it stands) is left as it is.
            GameBuffer* received = &self->in;
            if (ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginNewMaster) &&
                received->PositionOf(received->high) ==
                    0)
                return self->state;
            close(3);
            return self->state;
        }
        GameBuffer* in = &self->in;
        in->Seek(Length(in));
        if (in->cur + n > in->end) {
            in->WriteMore(data, n);
            return self->state;
        }
        in->flags |= 1;
        std::memcpy(in->cur, data, static_cast<std::size_t>(n));
        in->cur += n;
        return self->state;
    }
    case 4: {
        if (sockets::Select(nullptr, &set, nullptr, kSelectTimeout) != 1) {
            if (sockets::Select(nullptr, nullptr, &set, kSelectTimeout) == 1) close(0);
            return self->state;
        }
        self->state = 2;
        sockets::SetNonBlocking(self->socket, false);
        int sent = sockets::Send(self->socket, self->out.begin, Length(&self->out));
        sockets::SetNonBlocking(self->socket, true);
        if (sent != Length(&self->out)) close(0);
        return self->state;
    }
    default:
        return self->state;
    }
}

void StartWinsock(std::uint16_t, void*) { sockets::Start(); }

// The exit function of Winsock's static initialiser (WSAStartup 1.1): WSACleanup once the network
// threads are gone. The original waits while their count is non-zero; only the thread procedure
// counts them, and nothing starts it (config/dead_code.csv), so there is never one to wait for.
void CleanUpWinsock() { sockets::Stop(); }

} // namespace net
