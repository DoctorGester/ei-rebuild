// The sessions' UDP socket driver (net_classes.h, SocketDriver: the socket and the host's broadcast
// addresses), address parsing and formatting, and the TCP link to the master server (0x40 bytes:
// an outgoing and an incoming growing buffer, the socket, a state). Winsock (WSOCK32) is called
// through the game's import thunks. See docs/notes/unmapped_410_470.md ("Ninth round").
#include "net_classes.h"
#include "net_coder.h"
#include "lib/crt.h"
#include "lib/stlp.h"

#include "ei/hook.h"
#include "ei/vcall.h"

#include <cstdint>
#include <cstring>

namespace net {

// The TCP link to the master server (constructor 00440C60, destructor 00440D40, tick 00440DE0).
struct MasterLink {
    GameBuffer out;              // +0x00 what is sent once connected
    GameBuffer in;               // +0x1C what was received
    std::uint32_t socket;        // +0x38 (-1: none)
    std::int32_t state;          // +0x3C 0 closed, 1 connecting, 2 receiving, 3 done, 4 sending
};
static_assert(sizeof(MasterLink) == 0x40);

namespace {

// Winsock through the import thunks (006E60E2 ...).
__declspec(naked) std::uint16_t __stdcall WsHtons(std::uint16_t) EI_ORIGINAL(0x006E60E2)
__declspec(naked) void* __stdcall WsGetHostByName(const char*) EI_ORIGINAL(0x006E60E8)
__declspec(naked) std::uint32_t __stdcall WsInetAddr(const char*) EI_ORIGINAL(0x006E60EE)
__declspec(naked) std::uint16_t __stdcall WsNtohs(std::uint16_t) EI_ORIGINAL(0x006E60F4)
__declspec(naked) void* __stdcall WsGetHostByAddr(const void*, int, int) EI_ORIGINAL(0x006E60FA)
__declspec(naked) int __stdcall WsGetHostName(char*, int) EI_ORIGINAL(0x006E6100)
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

__declspec(naked) void __fastcall GrowingBufferConstruct(GameBuffer*) EI_ORIGINAL(0x004DA7F0)
__declspec(naked) void __fastcall GrowingBufferDestruct(GameBuffer*) EI_ORIGINAL(0x004DA910)
__declspec(naked) void __fastcall GameBufferGrow(GameBuffer*, void*, std::uint32_t) EI_ORIGINAL(0x004DA5C0)
// STLport (library): vector<sockaddr_in>::_M_insert_overflow (pos, value, n), the out_of_range
// thrower of basic_string::substr.
__declspec(naked) void __fastcall AddressesInsertOverflow(void* /*vector*/, void*, void* /*pos*/, const void* /*value*/, unsigned /*n*/) EI_ORIGINAL(0x00441180)
__declspec(naked) void __fastcall ThrowOutOfRange(void*) EI_ORIGINAL(0x004410A0)
__declspec(naked) int __cdecl GameAtoi(const char*) EI_ORIGINAL(0x006E80D8)

template <typename T>
T& At(void* base, std::size_t offset) { return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(base) + offset); }

struct Addresses {   // SocketDriver +4: vector<sockaddr_in>
    std::uint8_t* begin;
    std::uint8_t* end;
    std::uint8_t* eos;
};
Addresses& AddressesOf(SocketDriver* d) { return *reinterpret_cast<Addresses*>(&d->addresses); }

std::int32_t Length(GameBuffer* b) {
    if (b->cur > b->high) b->high = b->cur;
    return b->base - static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(b->begin)) +
           static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(b->high));
}

} // namespace

// ---- Addresses --------------------------------------------------------------------------------------

// 00440440: `out` from "host[:port]" (an IPv4 number or a host name looked up); `port` unless the
// text names one. False when the host is unknown.
bool __fastcall AddressParse(std::uint8_t* out, void*, const char* text, int port) {
    std::memset(out, 0, 16);
    *reinterpret_cast<std::uint16_t*>(out) = 2;
    stlp::string s(text, text + std::strlen(text));
    const std::size_t npos = *reinterpret_cast<const std::size_t*>(0x0073BDE8);
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
        if (first > s.size()) ThrowOutOfRange(nullptr);
        std::size_t n = s.size() - first;
        if (n >= npos) n = npos;
        {
            stlp::string number(s.data() + first, s.data() + first + n);
            port = GameAtoi(number.data());
        }
        std::size_t m = s.size() < colon ? s.size() : colon;
        stlp::string host(s.data(), s.data() + m);
        s.Assign(host.data(), host.data() + host.size());
    }
    std::uint32_t ip = WsInetAddr(s.data());
    std::memcpy(out + 4, &ip, 4);
    if (ip == 0xFFFFFFFFu) {
        void* h = WsGetHostByName(s.data());
        if (!h) return false;
        std::uint32_t found = **At<std::uint32_t**>(h, 0xC);
        std::memcpy(out + 4, &found, 4);
    }
    *reinterpret_cast<std::uint16_t*>(out + 2) = WsHtons(static_cast<std::uint16_t>(port));
    return true;
}
EI_REPLACE(0x00440440, AddressParse);

// 00440690: "a.b.c.d:port" (or, with `resolve`, "hostname:port" when the look-up succeeds) as an
// STLport string in `out`. The look-up is given the whole sockaddr, as the original does.
StringRep* __fastcall AddressFormat(const std::uint8_t* address, void*, StringRep* out, int resolve) {
    char text[0x400];
    bool named = false;
    if (static_cast<std::uint8_t>(resolve)) {
        void* h = WsGetHostByAddr(address, 0x10, 2);
        if (h && *static_cast<char**>(h)) {
            std::uint16_t port = WsNtohs(*reinterpret_cast<const std::uint16_t*>(address + 2));
            gamecrt::sprintf(text, reinterpret_cast<const char*>(0x00784054), *static_cast<char**>(h), port & 0xFFFF);
            named = true;
        }
    }
    if (!named) {
        std::uint16_t port = WsNtohs(*reinterpret_cast<const std::uint16_t*>(address + 2));
        gamecrt::sprintf(text, reinterpret_cast<const char*>(0x00784044), address[4], address[5], address[6], address[7], port & 0xFFFF);
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
EI_REPLACE(0x00440690, AddressFormat);

// ---- The UDP driver ---------------------------------------------------------------------------------

// 004407F0: starts Winsock and notes the broadcast address of the host's first address (by its
// class: A, B or C).
SocketDriver* __fastcall DriverConstruct(SocketDriver* self) {
    Addresses& list = AddressesOf(self);
    list.begin = nullptr;
    list.end = nullptr;
    list.eos = nullptr;
    alignas(4) std::uint8_t wsa[0x190];
    WsStartup(0x101, wsa);
    self->socket = kNoSocket;
    char name[1000];
    if (WsGetHostName(name, 1000) != 0) return self;
    void* h = WsGetHostByName(name);
    if (!h) return self;
    std::uint8_t a[16] = {};
    a[0] = 2;
    std::uint32_t ip = **At<std::uint32_t**>(h, 0xC);
    if (ip == 0) return self;
    std::memcpy(a + 4, &ip, 4);
    std::uint8_t first = static_cast<std::uint8_t>(ip);
    if (first >= 1 && first <= 0x7E) a[5] = a[6] = a[7] = 0xFF;
    if (first >= 0x80 && first <= 0xBF) a[6] = a[7] = 0xFF;
    if (first >= 0xC0 && first <= 0xDF) a[7] = 0xFF;
    if (list.end != list.eos) {
        if (list.end) std::memcpy(list.end, a, 16);
        list.end += 16;
    } else {
        AddressesInsertOverflow(&list, nullptr, list.end, a, 1);
    }
    return self;
}
EI_REPLACE(0x004407F0, DriverConstruct);

// 00440940: closes the socket, stops Winsock, frees the addresses.
void __fastcall DriverDestruct(SocketDriver* self) {
    if (self->socket != kNoSocket) WsCloseSocket(self->socket);
    self->socket = kNoSocket;
    WsCleanup();
    Addresses& list = AddressesOf(self);
    if (list.begin) stlp::deallocate(list.begin, static_cast<std::size_t>((list.eos - list.begin) >> 4 << 4));
}
EI_REPLACE(0x00440940, DriverDestruct);

// 004409A0: a new non-blocking broadcast UDP socket, bound to `port` when positive.
bool __fastcall DriverOpen(SocketDriver* self, void*, int port) {
    if (self->socket != kNoSocket) WsCloseSocket(self->socket);
    self->socket = kNoSocket;
    self->socket = WsSocket(2, 2, 0x11);
    if (self->socket == kNoSocket) return false;
    std::uint8_t a[16] = {};
    a[0] = 2;
    *reinterpret_cast<std::uint16_t*>(a + 2) = WsHtons(static_cast<std::uint16_t>(port));
    if (port > 0 && WsBind(self->socket, a, 0x10) != 0) {
        WsCloseSocket(self->socket);
        self->socket = kNoSocket;
        return false;
    }
    std::uint32_t one = 1;
    WsIoctlSocket(self->socket, kNonBlocking, &one);
    WsSetSockOpt(self->socket, 0xFFFF, 0x20, &one, 4);   // SOL_SOCKET, SO_BROADCAST
    return true;
}
EI_REPLACE(0x004409A0, DriverOpen);

// 00440A90: the `index`th broadcast address with `port`; false past the last.
bool __fastcall DriverBroadcast(SocketDriver* self, void*, std::uint8_t* out, unsigned index, int port) {
    Addresses& list = AddressesOf(self);
    if (index >= static_cast<unsigned>((list.end - list.begin) >> 4)) return false;
    std::memcpy(out, list.begin + index * 16, 16);
    *reinterpret_cast<std::uint16_t*>(out + 2) = WsHtons(static_cast<std::uint16_t>(port));
    return true;
}
EI_REPLACE(0x00440A90, DriverBroadcast);

// 00440AF0: whether `address` is the loopback or within the network of a broadcast address (its
// trailing 0xFF bytes are the host part).
bool __fastcall DriverIsLocal(SocketDriver* self, void*, const std::uint8_t* address) {
    std::uint32_t ip;
    std::memcpy(&ip, address + 4, 4);
    if (ip == 0x0100007Fu) return true;
    Addresses& list = AddressesOf(self);
    if (((list.end - list.begin) >> 4) == 0) return false;
    for (unsigned i = 0; i < static_cast<unsigned>((list.end - list.begin) >> 4); ++i) {
        std::uint32_t broadcast;
        std::memcpy(&broadcast, list.begin + i * 16 + 4, 4);
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
EI_REPLACE(0x00440AF0, DriverIsLocal);

// 00440B90: sends the buffer's contents to `address`; true when all of it went.
bool __fastcall DriverSendTo(SocketDriver* self, void*, const void* address, GameBuffer* buffer) {
    std::int32_t length = Length(buffer);
    return WsSendTo(self->socket, buffer->begin, length, 0, address, 0x10) == length;
}
EI_REPLACE(0x00440B90, DriverSendTo);

// 00440BE0: the next datagram (up to 0x800 bytes) into `buffer` (rewound), its sender into `from`.
bool __fastcall DriverReceive(SocketDriver* self, void*, std::uint8_t* from, GameBuffer* buffer) {
    ei::VCall<int>(buffer, 6, 0x800);
    int fromLength = 0x10;
    int n = WsRecvFrom(self->socket, buffer->begin, 0x800, 0, from, &fromLength);
    if (n >= 0) {
        *reinterpret_cast<std::uint16_t*>(from) = 2;
        std::memset(from + 8, 0, 8);
        buffer->cur = buffer->begin;
        buffer->high = buffer->begin + n;
        if (buffer->high > buffer->end) GameBufferGrow(buffer, nullptr, static_cast<std::uint32_t>(n));
    }
    return n >= 0;
}
EI_REPLACE(0x00440BE0, DriverReceive);

// ---- The master server link ----------------------------------------------------------------------

// 00440C60: a non-blocking TCP socket connecting to `address` (state 1), or state 0.
MasterLink* __fastcall MasterLinkConstruct(MasterLink* self, void*, const void* address) {
    GrowingBufferConstruct(&self->out);
    GrowingBufferConstruct(&self->in);
    self->socket = WsSocket(2, 1, 6);
    if (self->socket == kNoSocket) {
        self->state = 0;
        return self;
    }
    std::uint8_t any[16] = {};
    any[0] = 2;
    if (WsBind(self->socket, any, 0x10) != 0) {
        if (self->socket != kNoSocket) WsCloseSocket(self->socket);
        self->socket = kNoSocket;
        self->state = 0;
        return self;
    }
    std::uint32_t one = 1;
    WsIoctlSocket(self->socket, kNonBlocking, &one);
    WsConnect(self->socket, address, 0x10);
    self->state = 1;
    return self;
}
EI_REPLACE(0x00440C60, MasterLinkConstruct);

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
            close(3);
            return self->state;
        }
        GameBuffer* in = &self->in;
        ei::VCall<int>(in, 6, Length(in));
        if (in->cur + n > in->end) {
            ei::VCall<int>(in, 4, data, n);
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

} // namespace net
