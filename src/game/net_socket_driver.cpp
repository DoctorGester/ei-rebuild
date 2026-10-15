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
#include "originals_library.h"
#include "../hook/log.h"

#include <cstdint>
#include <cstring>

namespace net {

namespace {

// Winsock through the import thunks (006E60E2 ...).
__declspec(naked) std::uint16_t __stdcall WsHtons(std::uint16_t) EI_ORIGINAL(0x006E60E2)
__declspec(naked) void* __stdcall WsGetHostByName(const char*) EI_ORIGINAL(0x006E60E8)
__declspec(naked) std::uint32_t __stdcall WsInetAddr(const char*) EI_ORIGINAL(0x006E60EE)
__declspec(naked) std::uint16_t __stdcall WsNtohs(std::uint16_t) EI_ORIGINAL(0x006E60F4)
__declspec(naked) void* __stdcall WsGetHostByAddr(const void*, int, int) EI_ORIGINAL(0x006E60FA)
__declspec(naked) int __stdcall WsStartup(std::uint16_t, void*) EI_ORIGINAL(0x006E6106)
__declspec(naked) int __stdcall WsCleanup() EI_ORIGINAL(0x006E610C)
__declspec(naked) int __stdcall WsCloseSocket(std::uint32_t) EI_ORIGINAL(0x006E6112)
__declspec(naked) int __stdcall WsSetSockOpt(std::uint32_t, int, int, const void*, int) EI_ORIGINAL(0x006E6118)
__declspec(naked) int __stdcall WsIoctlSocket(std::uint32_t, long, std::uint32_t*) EI_ORIGINAL(0x006E611E)
__declspec(naked) int __stdcall WsBind(std::uint32_t, const void*, int) EI_ORIGINAL(0x006E6124)
__declspec(naked) std::uint32_t __stdcall WsSocket(int, int, int) EI_ORIGINAL(0x006E612A)
__declspec(naked) int __stdcall WsSendTo(std::uint32_t, const void*, int, int, const void*, int) EI_ORIGINAL(0x006E6130)
__declspec(naked) int __stdcall WsRecvFrom(std::uint32_t, void*, int, int, void*, int*) EI_ORIGINAL(0x006E6136)
__declspec(naked) int __stdcall WsConnect(std::uint32_t, const void*, int) EI_ORIGINAL(0x006E613C)
__declspec(naked) int __stdcall WsRecv(std::uint32_t, void*, int, int) EI_ORIGINAL(0x006E6142)
__declspec(naked) int __stdcall WsSend(std::uint32_t, const void*, int, int) EI_ORIGINAL(0x006E6148)
__declspec(naked) int __stdcall WsSelect(int, void*, void*, void*, const void*) EI_ORIGINAL(0x006E614E)

constexpr std::uint32_t kNoSocket = 0xFFFFFFFFu;
constexpr long kNonBlocking = static_cast<long>(0x8004667E);   // FIONBIO
const void* SelectTimeout() { return reinterpret_cast<const void*>(0x0079B928); }
// STLport (library): vector<sockaddr_in>::_M_insert_overflow (pos, value, n).
__declspec(naked) void __fastcall AddressesInsertOverflow(void* /*vector*/, void*, void* /*pos*/, const void* /*value*/, unsigned /*n*/) EI_ORIGINAL(0x00441180)

// A hostent's first address (h_addr_list[0]).
std::uint32_t FirstAddress(const void* host) {
    auto* entry = static_cast<const std::uint8_t*>(host);
    const std::uint32_t* const* list;
    std::memcpy(&list, entry + 0xC, sizeof(list));
    return **list;
}
unsigned AddressCount(const SocketDriver* d) { return static_cast<unsigned>(d->addressesEnd - d->addresses); }

std::int32_t Length(GameBuffer* b) {
    if (b->cur > b->high) b->high = b->cur;
    return b->base - static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(b->begin)) +
           static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(b->high));
}

} // namespace

// ---- Addresses --------------------------------------------------------------------------------------

bool NetAddress::Parse(const char* text, int defaultPort) {
    std::memset(this, 0, sizeof(*this));
    family = 2;
    stlp::string s(text, text + std::strlen(text));
    const std::size_t npos = *reinterpret_cast<const std::size_t*>(0x0073BDE8);
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
            stlp::string digits(s.data() + first, s.data() + first + n);
            number = GameAtoi(digits.data());
        }
        std::size_t m = s.size() < colon ? s.size() : colon;
        stlp::string host(s.data(), s.data() + m);
        s.Assign(host.data(), host.data() + host.size());
    }
    ip = WsInetAddr(s.data());
    if (ip == 0xFFFFFFFFu) {
        void* h = WsGetHostByName(s.data());
        if (!h) return false;
        ip = FirstAddress(h);
    }
    port = WsHtons(static_cast<std::uint16_t>(number));
    return true;
}
EI_REPLACE_METHOD(0x00440440, NetAddress::Parse);

// The look-up is given the whole sockaddr, as the original does.
StringRep* NetAddress::Format(StringRep* out, int resolve) const {
    char text[0x400];
    bool named = false;
    if (static_cast<std::uint8_t>(resolve)) {
        void* h = WsGetHostByAddr(this, 0x10, 2);
        if (h && *static_cast<char**>(h)) {
            std::uint16_t value = WsNtohs(port);
            gamecrt::sprintf(text, reinterpret_cast<const char*>(0x00784054), *static_cast<char**>(h), value & 0xFFFF);
            named = true;
        }
    }
    if (!named) {
        std::uint16_t value = WsNtohs(port);
        auto* bytes = reinterpret_cast<const std::uint8_t*>(&ip);
        gamecrt::sprintf(text, reinterpret_cast<const char*>(0x00784044), bytes[0], bytes[1], bytes[2], bytes[3], value & 0xFFFF);
    }
    out->start = nullptr;
    out->finish = nullptr;
    out->eos = nullptr;
    std::size_t length = std::strlen(text);
    char* p = static_cast<char*>(stlp::allocate(length + 1));
    out->start = p;
    out->finish = p;
    out->eos = p + length + 1;
    std::memcpy(p, text, length);
    out->finish = p + length;
    *out->finish = 0;
    return out;
}
EI_REPLACE_METHOD(0x00440690, NetAddress::Format);

// ---- The UDP driver ---------------------------------------------------------------------------------

// The broadcast address is the host's first address with the host part (by its class: A, B or C)
// set to 0xFF.
SocketDriver* SocketDriver::Construct() {
    addresses = nullptr;
    addressesEnd = nullptr;
    addressesEos = nullptr;
    alignas(4) std::uint8_t wsa[0x190];
    WsStartup(0x101, wsa);
    socket = kNoSocket;
    char name[1000];
    if (WsGetHostName(name, 1000) != 0) return this;
    void* h = WsGetHostByName(name);
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
    if (addressesEnd != addressesEos) {
        if (addressesEnd) *addressesEnd = a;
        ++addressesEnd;
    } else {
        AddressesInsertOverflow(&addresses, nullptr, addressesEnd, &a, 1);
    }
    return this;
}
EI_REPLACE_METHOD(0x004407F0, SocketDriver::Construct);

void SocketDriver::Destruct() {
    if (socket != kNoSocket) WsCloseSocket(socket);
    socket = kNoSocket;
    WsCleanup();
    if (addresses) stlp::deallocate(addresses, static_cast<std::size_t>(addressesEos - addresses) * sizeof(NetAddress));
}
EI_REPLACE_METHOD(0x00440940, SocketDriver::Destruct);

bool SocketDriver::Open(int port) {
    if (socket != kNoSocket) WsCloseSocket(socket);
    socket = kNoSocket;
    socket = WsSocket(2, 2, 0x11);
    if (socket == kNoSocket) return false;
    NetAddress a{};
    a.family = 2;
    a.port = WsHtons(static_cast<std::uint16_t>(port));
    if (port > 0 && WsBind(socket, &a, 0x10) != 0) {
        // Not the game's: a second game on the machine holding the port leaves this session without a
        // socket, and its local client then talks to that game's server (N0c). The verify build's
        // check of the server's constructor builds a second server, which always gets this.
        ei::Log("net: UDP port %d is taken (bind failed): this session gets no socket", port);
        WsCloseSocket(socket);
        socket = kNoSocket;
        return false;
    }
    std::uint32_t one = 1;
    WsIoctlSocket(socket, kNonBlocking, &one);
    WsSetSockOpt(socket, 0xFFFF, 0x20, &one, 4);   // SOL_SOCKET, SO_BROADCAST
    return true;
}
EI_REPLACE_METHOD(0x004409A0, SocketDriver::Open);

bool SocketDriver::Broadcast(NetAddress* out, unsigned index, int port) {
    if (index >= AddressCount(this)) return false;
    *out = addresses[index];
    out->port = WsHtons(static_cast<std::uint16_t>(port));
    return true;
}
EI_REPLACE_METHOD(0x00440A90, SocketDriver::Broadcast);

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
EI_REPLACE_METHOD(0x00440AF0, SocketDriver::IsLocal);

bool SocketDriver::SendTo(const NetAddress* address, GameBuffer* buffer) {
    std::int32_t length = Length(buffer);
    return WsSendTo(socket, buffer->begin, length, 0, address, 0x10) == length;
}
EI_REPLACE_METHOD(0x00440B90, SocketDriver::SendTo);

// The buffer is first sought (its slot 6) to room for 0x800 bytes.
bool SocketDriver::Receive(NetAddress* from, GameBuffer* buffer) {
    buffer->Seek(0x800);
    int fromLength = 0x10;
    int n = WsRecvFrom(socket, buffer->begin, 0x800, 0, from, &fromLength);
    if (n >= 0) {
        from->family = 2;
        std::memset(from->zero, 0, sizeof(from->zero));
        buffer->cur = buffer->begin;
        buffer->high = buffer->begin + n;
        if (buffer->high > buffer->end) GameBufferGrowOriginal(buffer, nullptr, static_cast<std::uint32_t>(n));
    }
    return n >= 0;
}
EI_REPLACE_METHOD(0x00440BE0, SocketDriver::Receive);

// ---- The master server link ----------------------------------------------------------------------

// 00440C60: a non-blocking TCP socket connecting to `address` (state 1), or state 0.
MasterLink* MasterLink::Construct(const void* address) {
    GrowingBufferConstruct(&out);
    GrowingBufferConstruct(&in);
    socket = WsSocket(2, 1, 6);
    if (socket == kNoSocket) {
        state = 0;
        return this;
    }
    std::uint8_t any[16] = {};
    any[0] = 2;
    if (WsBind(socket, any, 0x10) != 0) {
        if (socket != kNoSocket) WsCloseSocket(socket);
        socket = kNoSocket;
        state = 0;
        return this;
    }
    std::uint32_t one = 1;
    WsIoctlSocket(socket, kNonBlocking, &one);
    WsConnect(socket, address, 0x10);
    state = 1;
    return this;
}
EI_REPLACE_METHOD(0x00440C60, MasterLink::Construct);

// 00440D40: closes the socket (left as it is) and destroys the buffers.
void __fastcall MasterLinkDestruct(MasterLink* self) {
    if (self->socket != kNoSocket) WsCloseSocket(self->socket);
    GrowingBufferDestruct(&self->in);
    GrowingBufferDestruct(&self->out);
}
EI_REPLACE(0x00440D40, MasterLinkDestruct);

// 00440DE0: one step: connecting (1) until writable (4) or failed; sending (4) the whole outgoing
// buffer at once, blocking (then 2); receiving (2) into the incoming buffer until the peer closes
// (3). An error closes the socket (0). Each step waits at most the timeout at 0079B928. Returns the
// state.
int __fastcall MasterLinkTick(MasterLink* self) {
    struct {
        std::uint32_t count;
        std::uint32_t sockets[1];
    } set = {1, {self->socket}};
    auto close = [self](std::int32_t state) {
        if (self->socket != kNoSocket) WsCloseSocket(self->socket);
        self->socket = kNoSocket;
        self->state = state;
    };
    switch (self->state) {
    case 1:
        if (WsSelect(0, nullptr, &set, nullptr, SelectTimeout()) == 1) {
            self->state = 4;
            return self->state;
        }
        if (WsSelect(0, nullptr, nullptr, &set, SelectTimeout()) == 1) close(0);
        return self->state;
    case 2: {
        if (WsSelect(0, &set, nullptr, nullptr, SelectTimeout()) != 1) {
            if (WsSelect(0, nullptr, nullptr, &set, SelectTimeout()) == 1) close(0);
            return self->state;
        }
        std::uint8_t data[0x800];
        int n = WsRecv(self->socket, data, 0x800, 0);
        if (n == -1) {
            close(0);
            return self->state;
        }
        if (n == 0) {
            // EI_Plugin's NewMaster (its 10005490 called at 00440F9F): a link closed before anything
            // arrived (base - begin + high of +0x1C, `high` as it stands) is left as it is.
            GameBuffer* received = &self->in;
            if (ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginNewMaster) &&
                received->base - static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(received->begin)) +
                        static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(received->high)) ==
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
        if (WsSelect(0, nullptr, &set, nullptr, SelectTimeout()) != 1) {
            if (WsSelect(0, nullptr, nullptr, &set, SelectTimeout()) == 1) close(0);
            return self->state;
        }
        self->state = 2;
        std::uint32_t mode = 0;
        WsIoctlSocket(self->socket, kNonBlocking, &mode);
        int sent = WsSend(self->socket, self->out.begin, Length(&self->out), 0);
        mode = 1;
        WsIoctlSocket(self->socket, kNonBlocking, &mode);
        if (sent != Length(&self->out)) close(0);
        return self->state;
    }
    default:
        return self->state;
    }
}
EI_REPLACE(0x00440DE0, MasterLinkTick);
EI_REBIRTH_IMPLEMENTS(0x00440DE0, ei::rebirth::Patch::EiPluginNewMaster);

} // namespace net
