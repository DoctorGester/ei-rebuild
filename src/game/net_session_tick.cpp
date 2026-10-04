// The session's receive step (00437370, vtable slot 14) and two small helpers it calls. See
// net_classes.h and docs/notes/unmapped_410_470.md ("Sixth round").
#include "net_classes.h"
#include "net_connection.h"

#include "ei/hook.h"
#include "ei/vcall.h"

#include <cstring>

namespace net {

namespace {

// 00440BE0: the socket driver's receive: the next datagram into `buffer`, its sender into `from`;
// false when there is none.
__declspec(naked) bool __fastcall SocketReceive(SocketDriver* /*driver*/, void*, void* /*from*/, GameBuffer* /*buffer*/) EI_ORIGINAL(0x00440BE0)
// 00440690: formats a sockaddr_in as "a.b.c.d:port" into an STLport string (the flag is 0 here).
__declspec(naked) void __fastcall AddressFormat(const void* /*address*/, void*, StringRep* /*out*/, int /*flag*/) EI_ORIGINAL(0x00440690)
// 00433BD0: a connection's tick (sends, timeouts); false when the peer timed out.
__declspec(naked) bool __fastcall ConnectionTick(Connection* /*connection*/, void*, double /*dt*/) EI_ORIGINAL(0x00433BD0)
// 0040C770: STLport string destructor; 00426450: the string allocator's deallocate(p, n).
__declspec(naked) void __fastcall StringDestroy(StringRep* /*s*/) EI_ORIGINAL(0x0040C770)
__declspec(naked) void __fastcall StringDeallocate(void* /*allocator*/, void*, char* /*p*/, std::size_t /*n*/) EI_ORIGINAL(0x00426450)
// 004DA170: reading past the buffer's end (zeroes the target, sets error bit 3).
__declspec(naked) unsigned __fastcall BufferReadPastEnd(GameBuffer* /*buffer*/, void*, void* /*to*/, unsigned /*size*/) EI_ORIGINAL(0x004DA170)

// The old iostream inserters of the network log (library).
struct LogStream {
    LogStream* Put(const char* text);        // 006FC31D
    LogStream* PutChar(char c);              // 006FC0B0
    LogStream* Apply(void* manipulator);     // 00434FE0
};
__declspec(naked) LogStream* LogStream::Put(const char*) EI_ORIGINAL(0x006FC31D)
__declspec(naked) LogStream* LogStream::PutChar(char) EI_ORIGINAL(0x006FC0B0)
__declspec(naked) LogStream* LogStream::Apply(void*) EI_ORIGINAL(0x00434FE0)
constexpr std::uintptr_t kFlush = 0x00435000;
constexpr std::uintptr_t kZeroLength = 0x00784020;   // "ZERO length packet received from "
constexpr std::uintptr_t kTimeout = 0x00784008;      // "TIMEOUT client, drop "

// "<text><address>\n" and a flush to the network log.
void LogAddress(std::uintptr_t text, const char* address) {
    auto* log = static_cast<LogStream*>(NetLog());
    log->Put(reinterpret_cast<const char*>(text))->Put(address)->PutChar('\n')->Apply(reinterpret_cast<void*>(kFlush));
}

// 0079B8E0: the receive buffer every session reads its datagrams into.
GameBuffer* ReceiveBuffer() { return reinterpret_cast<GameBuffer*>(0x0079B8E0); }

// The buffer's slot 3: reads `size` more bytes.
constexpr int kBufferReadMore = 3;

// The handlers of the packet kinds (first byte): kind -1 .. 7 -> vtable slot (0: none).
constexpr int kKindSlots[9] = {10, 3, 6, 0, 4, 5, 7, 8, 9};

} // namespace

// 00419CC0: the buffer's highest written position follows the current one upwards.
void __fastcall BufferUpdateHigh(GameBuffer* buffer) {
    if (buffer->cur > buffer->high) buffer->high = buffer->cur;
}
EI_REPLACE(0x00419CC0, BufferUpdateHigh);

// 00435010: the address as an STLport string ("a.b.c.d:port"); returns `out`.
StringRep* __fastcall AddressString(const void* address, void*, StringRep* out) {
    AddressFormat(address, nullptr, out, 0);
    return out;
}
EI_REPLACE(0x00435010, AddressString);

// 00437370 (slot 14): advances the session clock by `dt`, then hands every datagram waiting on the
// socket to the handler slot of its kind (its first byte) with the sender's address and a cursor
// after that byte (an empty datagram is only logged). Then ticks every connection; one that timed
// out is reported (slot 11) and removed (slot 2). Counts the call in `frame`. The current session
// (0079B920) is this one meanwhile.
void SessionBase::Receive(double dt) {
    *reinterpret_cast<SessionBase**>(0x0079B920) = this;
    time = dt + time;
    std::uint8_t from[0x10] = {};
    GameBuffer* buffer = ReceiveBuffer();
    while (SocketReceive(&socket, nullptr, from, buffer)) {
        BufferUpdateHigh(buffer);
        if (buffer->high - buffer->begin + buffer->base == 0) {
            if (NetLog()) {
                StringRep address;
                const char* text = AddressString(from, nullptr, &address)->start;
                LogAddress(kZeroLength, text);
                StringDestroy(&address);
            }
            continue;
        }
        char kind;
        if (buffer->cur + 1 > buffer->high) BufferReadPastEnd(buffer, nullptr, &kind, 1);
        else if (buffer->cur + 1 > buffer->end) ei::VCall<int>(buffer, kBufferReadMore, &kind, 1u);
        else BufferCopyOut(buffer, nullptr, &kind, 1);
        BufferUpdateHigh(buffer);
        BufferCursor cursor{};
        cursor.p = buffer->begin + 1;
        cursor.bytePtr = nullptr;
        cursor.bits = 0;
        unsigned index = static_cast<unsigned>(static_cast<int>(kind) + 1);
        if (index > 8 || kKindSlots[index] == 0) continue;
        ei::VCall<void>(this, kKindSlots[index], static_cast<void*>(from), &cursor);
    }
    void* sentinel = clients;
    for (void** node = *static_cast<void***>(sentinel); node != sentinel;) {
        void** next = static_cast<void**>(node[0]);
        auto* connection = static_cast<Connection*>(node[2]);
        if (!ConnectionTick(connection, nullptr, dt)) {
            if (NetLog()) {
                StringRep address;
                const char* text = AddressString(connection->address, nullptr, &address)->start;
                LogAddress(kTimeout, text);
                StringDeallocate(&address.eos, nullptr, address.start, static_cast<std::size_t>(address.eos - address.start));
            }
            ei::VCall<void>(this, 11, static_cast<void*>(connection));
            ei::VCall<void>(this, 2, static_cast<void*>(node));
        }
        node = next;
    }
    ++frame;
    *reinterpret_cast<SessionBase**>(0x0079B920) = nullptr;
}
namespace {
void __fastcall ReceiveHook(SessionBase* self, void*, double dt) { self->Receive(dt); }
} // namespace
EI_REPLACE(0x00437370, ReceiveHook);

} // namespace net
