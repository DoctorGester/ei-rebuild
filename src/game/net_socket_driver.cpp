// The sessions' UDP socket driver (net_classes.h, SocketDriver: the socket and the host's broadcast
// addresses), address parsing and formatting, and the TCP link to the master server (0x40 bytes:
// an outgoing and an incoming growing buffer, the socket, a state). Winsock (WSOCK32) is called
// through the game's import thunks. See docs/notes/network.md.
#include "net_classes.h"
#include "net_coder.h"
#include "lib/crt.h"
#include "lib/stlp.h"
#include "lib/stlp_string.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "../hook/log.h"
#include "game_data.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace net {

namespace {

constexpr std::uint32_t kNoSocket = 0xFFFFFFFFu;
constexpr long kNonBlocking = static_cast<long>(0x8004667E);   // FIONBIO
// The timeval the socket steps' select waits at most: 0 s, 0 µs (a poll).
constexpr std::int32_t kSelectTimeout[2] = /*0079B928*/ {};

// A hostent's first address (h_addr_list[0]).
std::uint32_t FirstAddress(const void* host) {
    auto* entry = static_cast<const std::uint8_t*>(host);
    const std::uint32_t* const* list;
    std::memcpy(&list, entry + 0xC, sizeof(list));
    return **list;
}
unsigned AddressCount(const SocketDriver* d) { return static_cast<unsigned>(d->addresses.size()); }

std::int32_t Length(GameBuffer* b) {
    if (b->cur > b->high) b->high = b->cur;
    return b->PositionOf(b->high);
}

} // namespace


bool NetAddress::Parse(const char* text, int defaultPort) {
    std::memset(this, 0, sizeof(*this));
    family = 2;
    std::string s(text, text + std::strlen(text));
    const std::size_t npos = 0xFFFFFFFFu /*0073BDE8*/;
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
        std::size_t first = colon + 1;
        if (first > s.size()) stlp::ThrowOutOfRange();
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
    ip = libcrt::imports::InetAddr(s.data());
    if (ip == 0xFFFFFFFFu) {
        void* h = libcrt::imports::GetHostByName(s.data());
        if (!h) return false;
        ip = FirstAddress(h);
    }
    port = libcrt::imports::Htons(static_cast<std::uint16_t>(number));
    return true;
}
EI_REWRITTEN(0x00440440, NetAddress::Parse);

// The look-up is given the whole sockaddr.
std::string NetAddress::Format(int resolve) const {
    char text[0x400];
    bool named = false;
    if (static_cast<std::uint8_t>(resolve)) {
        void* h = libcrt::imports::GetHostByAddr(this, 0x10, 2);
        if (h && *static_cast<char**>(h)) {
            std::uint16_t value = libcrt::imports::Ntohs(port);
            gamecrt::sprintf(text, "%s:%i", *static_cast<char**>(h), value & 0xFFFF);
            named = true;
        }
    }
    if (!named) {
        std::uint16_t value = libcrt::imports::Ntohs(port);
        auto* bytes = reinterpret_cast<const std::uint8_t*>(&ip);
        gamecrt::sprintf(text, "%i.%i.%i.%i:%i", bytes[0], bytes[1], bytes[2], bytes[3], value & 0xFFFF);
    }
    return text;
}
EI_REWRITTEN(0x00440690, NetAddress::Format);

// ---- The UDP driver ---------------------------------------------------------------------------------

// The broadcast address is the host's first address with the host part (by its class: A, B or C) set to 0xFF.
SocketDriver* SocketDriver::Construct() {
    addresses.ConstructEmpty();
    alignas(4) std::uint8_t wsa[0x190];
    libcrt::imports::WsaStartup(0x101, wsa);
    socket = kNoSocket;
    char name[1000];
    if (libcrt::imports::GetHostName(name, 1000) != 0) return this;
    void* h = libcrt::imports::GetHostByName(name);
    if (!h) return this;
    NetAddress a{};
    a.family = 2;
    std::uint32_t ip = FirstAddress(h);
    if (ip == 0) return this;
    a.ip = ip;
    auto* bytes = reinterpret_cast<std::uint8_t*>(&a.ip);
    std::uint8_t first = static_cast<std::uint8_t>(ip);
    if (first >= 1 && first <= 0x7E) bytes[1] = bytes[2] = bytes[3] = 0xFF;
    if (first >= 0x80 && first <= 0xBF) bytes[2] = bytes[3] = 0xFF;
    if (first >= 0xC0 && first <= 0xDF) bytes[3] = 0xFF;
    NetAddress*& end = addresses.Finish();
    if (end != addresses.EndOfStorage()) {
        if (end) *end = a;
        ++end;
    } else {
        addresses.InsertOverflow<stlp::PlainElement<NetAddress>>(end, a, 1);
    }
    return this;
}
EI_REWRITTEN(0x004407F0, SocketDriver::Construct);

void SocketDriver::Destruct() {
    if (socket != kNoSocket) libcrt::imports::CloseSocket(socket);
    socket = kNoSocket;
    libcrt::imports::WsaCleanup();
    addresses.FreeStorage();
}
EI_REWRITTEN(0x00440940, SocketDriver::Destruct);

bool SocketDriver::Open(int port) {
    if (socket != kNoSocket) libcrt::imports::CloseSocket(socket);
    socket = kNoSocket;
    socket = libcrt::imports::Socket(2, 2, 0x11);
    if (socket == kNoSocket) return false;
    NetAddress a{};
    a.family = 2;
    a.port = libcrt::imports::Htons(static_cast<std::uint16_t>(port));
    if (port > 0 && libcrt::imports::Bind(socket, &a, 0x10) != 0) {
        // Not the game's: a second game on the machine holding the port leaves this session without a
        // socket, and its local client then talks to that game's server. The verify build's
        // check of the server's constructor builds a second server, which always gets this.
        ei::Log("net: UDP port %d is taken (bind failed): this session gets no socket", port);
        libcrt::imports::CloseSocket(socket);
        socket = kNoSocket;
        return false;
    }
    std::uint32_t one = 1;
    libcrt::imports::IoctlSocket(socket, kNonBlocking, &one);
    libcrt::imports::SetSockOpt(socket, 0xFFFF, 0x20, &one, 4);   // SOL_SOCKET, SO_BROADCAST
    return true;
}
EI_REWRITTEN(0x004409A0, SocketDriver::Open);

bool SocketDriver::Broadcast(NetAddress* out, unsigned index, int port) {
    if (index >= AddressCount(this)) return false;
    *out = addresses[index];
    out->port = libcrt::imports::Htons(static_cast<std::uint16_t>(port));
    return true;
}
EI_REWRITTEN(0x00440A90, SocketDriver::Broadcast);

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
EI_REWRITTEN(0x00440AF0, SocketDriver::IsLocal);

bool SocketDriver::SendTo(const NetAddress* address, GameBuffer* buffer) {
    std::int32_t length = Length(buffer);
    return libcrt::imports::SendTo(socket, buffer->begin, length, 0, address, 0x10) == length;
}
EI_REWRITTEN(0x00440B90, SocketDriver::SendTo);

// The buffer is first sought (its slot 6) to room for 0x800 bytes.
bool SocketDriver::Receive(NetAddress* from, GameBuffer* buffer) {
    buffer->Seek(0x800);
    int fromLength = 0x10;
    int n = libcrt::imports::RecvFrom(socket, buffer->begin, 0x800, 0, from, &fromLength);
    if (n >= 0) {
        from->family = 2;
        std::memset(from->zero, 0, sizeof(from->zero));
        buffer->cur = buffer->begin;
        buffer->high = buffer->begin + n;
        if (buffer->high > buffer->end) buffer->Grow(static_cast<std::uint32_t>(n));
    }
    return n >= 0;
}
EI_REWRITTEN(0x00440BE0, SocketDriver::Receive);

// ---- The master server link ----------------------------------------------------------------------

MasterLink::MasterLink(const void* address) {
    socket = libcrt::imports::Socket(2, 1, 6);
    if (socket == kNoSocket) {
        state = 0;
        return;
    }
    std::uint8_t any[16] = {};
    any[0] = 2;
    if (libcrt::imports::Bind(socket, any, 0x10) != 0) {
        if (socket != kNoSocket) libcrt::imports::CloseSocket(socket);
        socket = kNoSocket;
        state = 0;
        return;
    }
    std::uint32_t one = 1;
    libcrt::imports::IoctlSocket(socket, kNonBlocking, &one);
    libcrt::imports::Connect(socket, address, 0x10);
    state = 1;
}
EI_REWRITTEN_SPECIAL(0x00440C60, MasterLink::MasterLink);

// Closes the socket (left as it is); the buffers are destroyed after it (in, then out).
MasterLink::~MasterLink() {
    if (socket != kNoSocket) libcrt::imports::CloseSocket(socket);
}
EI_REWRITTEN_SPECIAL(0x00440D40, MasterLink::~MasterLink);

// One step: connecting (1) until writable (4) or failed; sending (4) the whole outgoing buffer at once,
// blocking (then 2); receiving (2) into the incoming buffer until the peer closes (3). An error closes the
// socket (0). Each step waits at most kSelectTimeout (a poll). Returns the state.
int __fastcall MasterLinkTick(MasterLink* self) {
    struct {
        std::uint32_t count;
        std::uint32_t sockets[1];
    } set = {1, {self->socket}};
    auto close = [self](std::int32_t state) {
        if (self->socket != kNoSocket) libcrt::imports::CloseSocket(self->socket);
        self->socket = kNoSocket;
        self->state = state;
    };
    switch (self->state) {
    case 1:
        if (libcrt::imports::Select(0, nullptr, &set, nullptr, kSelectTimeout) == 1) {
            self->state = 4;
            return self->state;
        }
        if (libcrt::imports::Select(0, nullptr, nullptr, &set, kSelectTimeout) == 1) close(0);
        return self->state;
    case 2: {
        if (libcrt::imports::Select(0, &set, nullptr, nullptr, kSelectTimeout) != 1) {
            if (libcrt::imports::Select(0, nullptr, nullptr, &set, kSelectTimeout) == 1) close(0);
            return self->state;
        }
        std::uint8_t data[0x800];
        int n = libcrt::imports::Recv(self->socket, data, 0x800, 0);
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
        if (libcrt::imports::Select(0, nullptr, &set, nullptr, kSelectTimeout) != 1) {
            if (libcrt::imports::Select(0, nullptr, nullptr, &set, kSelectTimeout) == 1) close(0);
            return self->state;
        }
        self->state = 2;
        std::uint32_t mode = 0;
        libcrt::imports::IoctlSocket(self->socket, kNonBlocking, &mode);
        int sent = libcrt::imports::Send(self->socket, self->out.begin, Length(&self->out), 0);
        mode = 1;
        libcrt::imports::IoctlSocket(self->socket, kNonBlocking, &mode);
        if (sent != Length(&self->out)) close(0);
        return self->state;
    }
    default:
        return self->state;
    }
}
EI_REWRITTEN(0x00440DE0, MasterLinkTick);

// The exit function of Winsock's static initialiser (WSAStartup 1.1): WSACleanup once the network
// threads are gone. The original waits while their count is non-zero; only the thread procedure
// counts them, and nothing starts it (config/dead_code.csv), so there is never one to wait for.
void __cdecl CleanUpWinsock() { libcrt::imports::WsaCleanup(); }
EI_REWRITTEN(0x0047C330, CleanUpWinsock);

} // namespace net
