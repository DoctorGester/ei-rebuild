// The session's receive step (00437370, slot 14), its packet handlers, sends and destruction,
// and the byte buffer helpers they use. See net_classes.h and docs/notes/unmapped_410_470.md.
#include "net_classes.h"
#include "net_connection.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <cstring>

// Precise semantics keep the x87 division in SessionAddClient as written.
#pragma float_control(precise, on)

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

// ---- Session vtable slots (0073BF5C) ---------------------------------------------------------------

namespace net {

namespace {

// The clients list (STLport list<Connection*>, +0x28): end, begin, find by peer address (an
// instantiation of find_if over the nodes), erase.
__declspec(naked) void** __fastcall ClientsEnd(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x00438830)
__declspec(naked) void** __fastcall ClientsBegin(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x00438820)
__declspec(naked) void** __fastcall ClientFind(void** /*out*/, void*, void* /*first*/, void* /*last*/, const void* /*address*/) EI_ORIGINAL(0x0043D780)
__declspec(naked) void* __fastcall ClientsErase(void* /*list*/, void*, void** /*out*/, void* /*pos*/) EI_ORIGINAL(0x00438840)
// 0043B310: the node allocator's deallocate (p in ECX, size in EDX), used for the address strings.
__declspec(naked) void __fastcall NodeFree(char* /*p*/, std::size_t /*n*/) EI_ORIGINAL(0x0043B310)
// 00436620: closes a connection (not rewritten yet).
__declspec(naked) void __fastcall ConnectionClose(Connection* /*connection*/) EI_ORIGINAL(0x00436620)

constexpr std::uintptr_t kUnknownHost = 0x00783F2C;   // "ERROR: Update from unknown host"
constexpr std::uintptr_t kBadClient = 0x00783F1C;     // "ERROR: client "
constexpr std::uintptr_t kBadUpdate = 0x00783F04;     // " sent incorrect update"
constexpr std::uintptr_t kRemove = 0x00783EE8;        // "REMOVE client "

// "<text>\n" and a flush, when there is a network log.
void LogText(std::uintptr_t text) {
    if (auto* log = static_cast<LogStream*>(NetLog()))
        log->Put(reinterpret_cast<const char*>(text))->PutChar('\n')->Apply(reinterpret_cast<void*>(kFlush));
}

// The clients list node of the connection to `address` (the list sentinel when there is none), as
// the slots inline it.
void* FindClientNode(SessionBase* self, const void* address) {
    void* last;
    ClientsEnd(&self->clients, nullptr, &last);
    void* first;
    ClientsBegin(&self->clients, nullptr, &first);
    void* found = nullptr;
    ClientFind(&found, nullptr, first, last, address);
    return found;
}

Connection* ConnectionOf(void* node) { return static_cast<Connection*>(static_cast<void**>(node)[2]); }

// The address string of a connection, logged as "<before><address><after>\n", then freed.
void LogConnection(std::uintptr_t before, Connection* connection, std::uintptr_t after) {
    StringRep peer;
    const char* text = AddressString(connection->address, nullptr, &peer)->start;
    LogStream* log = static_cast<LogStream*>(NetLog())->Put(reinterpret_cast<const char*>(before))->Put(text);
    if (after) log = log->Put(reinterpret_cast<const char*>(after));
    log->PutChar('\n')->Apply(reinterpret_cast<void*>(kFlush));
    NodeFree(peer.start, static_cast<std::size_t>(peer.eos - peer.start));
}

} // namespace

// 00436CC0: the clients list node of the connection to `address` (end when none); returns `out`.
void** __fastcall SessionFindClient(SessionBase* self, void*, void** out, const void* address) {
    void* last;
    ClientsEnd(&self->clients, nullptr, &last);
    void* first;
    ClientsBegin(&self->clients, nullptr, &first);
    ClientFind(out, nullptr, first, last, address);
    return out;
}
EI_REPLACE(0x00436CC0, SessionFindClient);

// 00436CF0 (slot 2): RemoveClient(node). Logs it, closes the connection and erases the node.
void __fastcall SessionRemoveClient(SessionBase* self, void*, void* node) {
    if (node == self->clients) return;
    if (NetLog()) LogConnection(kRemove, ConnectionOf(node), 0);
    ConnectionClose(ConnectionOf(node));
    void* after;
    ClientsErase(&self->clients, nullptr, &after, node);
}
EI_REPLACE(0x00436CF0, SessionRemoveClient);

// 00437080 (slot 3, packet kind 0): an update from a client. Unknown senders are logged (returns
// false); a client whose update does not read is logged, reported (slot 11) and removed (slot 2).
bool __fastcall SessionOnUpdate(SessionBase* self, void*, const void* from, BufferCursor* cursor) {
    void* node = FindClientNode(self, from);
    if (node == self->clients) {
        LogText(kUnknownHost);
        return false;
    }
    if (!ConnectionOf(node)->ReadUpdate(cursor)) {
        if (NetLog()) LogConnection(kBadClient, ConnectionOf(node), kBadUpdate);
        ei::VCall<void>(self, 11, static_cast<void*>(ConnectionOf(node)));
        ei::VCall<void>(self, 2, node);
    }
    return true;
}
EI_REPLACE(0x00437080, SessionOnUpdate);

// 00437240 (slot 6, packet kind 1): a client leaves: its connection id follows; when it matches,
// the client is reported (slot 12) and removed (slot 2).
void __fastcall SessionOnLeave(SessionBase* self, void*, const void* from, BufferCursor* cursor) {
    std::uint32_t id;
    std::memcpy(&id, cursor->p, 4);
    cursor->p += 4;
    void* node = FindClientNode(self, from);
    if (node != self->clients && ConnectionOf(node)->connectionId == id) {
        ei::VCall<void>(self, 12, static_cast<void*>(ConnectionOf(node)));
        ei::VCall<void>(self, 2, node);
    }
}
EI_REPLACE(0x00437240, SessionOnLeave);

// The base session's handlers of the kinds it does not serve: each logs an error.
namespace {
template <std::uintptr_t Text>
void __fastcall CantHandle(SessionBase*, void*, const void*, BufferCursor*) {
    LogText(Text);
}
} // namespace
EI_REPLACE(0x004371E0, CantHandle<0x00783F4C>);   // slot 4, kind 3: "ERROR: Can't handle get info"
EI_REPLACE(0x00437210, CantHandle<0x00783F6C>);   // slot 5, kind 4: "ERROR: Can't handle login"
EI_REPLACE(0x004372B0, CantHandle<0x00783F88>);   // slot 7, kind 5: "ERROR: Can't handle info"
EI_REPLACE(0x004372E0, CantHandle<0x00783FA4>);   // slot 8, kind 6: "ERROR: Can't handle accepted"
EI_REPLACE(0x00437310, CantHandle<0x00783FC4>);   // slot 9, kind 7: "ERROR: Can't handle rejected"
EI_REPLACE(0x00437340, CantHandle<0x00783FE4>);   // slot 10, kind -1: "ERROR: Can't handle master packet"

} // namespace net

// ---- Byte buffer writes and small socket helpers ---------------------------------------------------

namespace net {

namespace {
__declspec(naked) bool __fastcall SocketSendTo(SocketDriver* /*driver*/, void*, const void* /*address*/, GameBuffer* /*buffer*/) EI_ORIGINAL(0x00440B90)
// 00440AF0: whether the address is on the local network.
__declspec(naked) bool __fastcall SocketIsLocal(SocketDriver* /*driver*/, void*, const void* /*address*/) EI_ORIGINAL(0x00440AF0)
__declspec(naked) void __fastcall GrowingBufferConstruct(GameBuffer* /*buffer*/) EI_ORIGINAL(0x004DA7F0)
__declspec(naked) void __fastcall GrowingBufferDestruct(GameBuffer* /*buffer*/) EI_ORIGINAL(0x004DA910)
constexpr int kBufferWriteMore = 4;
} // namespace

// 00436C30: copies `size` bytes to the write position and moves it on (no room check); returns
// the buffer.
GameBuffer* __fastcall BufferWriteRaw(GameBuffer* buffer, void*, const void* from, unsigned size) {
    std::memcpy(buffer->cur, from, size);
    buffer->cur += size;
    return buffer;
}
EI_REPLACE(0x00436C30, BufferWriteRaw);

// 00436C60: writes `size` bytes: in place (marking the buffer written) when they fit before `end`,
// otherwise through slot 4. Returns `size` or what slot 4 left.
unsigned __fastcall BufferWrite(GameBuffer* buffer, void*, const void* from, unsigned size) {
    if (buffer->cur + size > buffer->end) return static_cast<unsigned>(ei::VCall<int>(buffer, kBufferWriteMore, from, size));
    buffer->flags |= 1;
    std::memcpy(buffer->cur, from, size);
    buffer->cur += size;
    return size;
}
EI_REPLACE(0x00436C60, BufferWrite);

// 00436F60: sends the buffer's contents to `address` through the session's socket.
bool __fastcall SessionSend(SessionBase* self, void*, const void* address, GameBuffer* buffer) {
    return SocketSendTo(&self->socket, nullptr, address, buffer);
}
EI_REPLACE(0x00436F60, SessionSend);

// 00436F80: the send rate for a peer: the dword at 00783E38 (12500) on the local network, else the
// one at 00783E34 (2500).
std::uint32_t __fastcall SessionRateFor(SessionBase* self, void*, const void* address) {
    bool local = SocketIsLocal(&self->socket, nullptr, address);
    return *reinterpret_cast<const std::uint32_t*>(local ? 0x00783E38 : 0x00783E34);
}
EI_REPLACE(0x00436F80, SessionRateFor);

// 00436FA0: sends a leave packet (kind 1, then the connection id) to `address`.
void __fastcall SessionSendLeave(SessionBase* self, void*, const void* address, std::uint32_t id) {
    alignas(4) std::uint8_t storage[sizeof(GameBuffer)];
    auto* buffer = reinterpret_cast<GameBuffer*>(storage);
    GrowingBufferConstruct(buffer);
    std::uint8_t kind = 1;
    if (buffer->cur + 1 <= buffer->end) {
        buffer->flags |= 1;
        *buffer->cur = kind;
        ++buffer->cur;
    } else {
        ei::VCall<int>(buffer, kBufferWriteMore, &kind, 1u);
    }
    if (buffer->cur + 4 <= buffer->end) {
        buffer->flags |= 1;
        std::memcpy(buffer->cur, &id, 4);
        buffer->cur += 4;
    } else {
        ei::VCall<int>(buffer, kBufferWriteMore, &id, 4u);
    }
    SocketSendTo(&self->socket, nullptr, address, buffer);
    GrowingBufferDestruct(buffer);
}
EI_REPLACE(0x00436FA0, SessionSendLeave);

} // namespace net

// ---- Destruction ----------------------------------------------------------------------------------

namespace net {

namespace {
// CRT eh vector destructor iterator (ptr, element size, count, destructor), the handler vector's
// destructor, the socket driver's destructor.
__declspec(naked) void __stdcall VectorDestructorIterator(void* /*ptr*/, unsigned /*size*/, int /*count*/, void* /*dtor*/) EI_ORIGINAL(0x006E859F)
constexpr std::uintptr_t kHandlerVectorDestructor = 0x004389C0;
__declspec(naked) void __fastcall SocketDriverDestruct(SocketDriver* /*driver*/) EI_ORIGINAL(0x00440940)
// The type registry's tables: clear and bucket-vector destruction of byType and byVtable.
__declspec(naked) void __fastcall ByTypeClear(HashTable* /*table*/) EI_ORIGINAL(0x0043D590)
__declspec(naked) void __fastcall ByTypeBucketsDestroy(void* /*buckets*/) EI_ORIGINAL(0x0043AAB0)
__declspec(naked) void __fastcall ByVtableClear(HashTable* /*table*/) EI_ORIGINAL(0x0043B4D0)
__declspec(naked) void __fastcall ByVtableBucketsDestroy(void* /*buckets*/) EI_ORIGINAL(0x0043A2B0)
// The clients list: release of a node's counted connection pointer, node deallocation, sentinel
// deallocation (pointer in ECX, count in EDX).
__declspec(naked) void __fastcall ClientRefRelease(void* /*ref*/, void*, int /*flag*/) EI_ORIGINAL(0x0043DCA0)
__declspec(naked) void __fastcall ClientNodeFree(void* /*list*/, void*, void* /*node*/, int /*n*/) EI_ORIGINAL(0x00439E60)
__declspec(naked) void __fastcall ClientSentinelFree(void* /*sentinel*/, int /*n*/) EI_ORIGINAL(0x0043AB30)
constexpr std::uintptr_t kRefObjectRoot = 0x0073BB68;
} // namespace

void SessionBase::Destruct() {
    vtable = reinterpret_cast<void*>(kSessionBaseVtable);
    for (void** node = *static_cast<void***>(clients); node != clients; node = static_cast<void**>(node[0])) {
        auto* connection = static_cast<Connection*>(node[2]);
        std::uint32_t id = connection->connectionId;
        alignas(4) std::uint8_t storage[sizeof(GameBuffer)];
        auto* buffer = reinterpret_cast<GameBuffer*>(storage);
        GrowingBufferConstruct(buffer);
        std::uint8_t kind = 1;
        BufferWrite(buffer, nullptr, &kind, 1);
        if (buffer->cur + 4 <= buffer->end) {
            buffer->flags |= 1;
            BufferWriteRaw(buffer, nullptr, &id, 4);
        } else {
            ei::VCall<int>(buffer, kBufferWriteMore, &id, 4u);
        }
        SocketSendTo(&socket, nullptr, connection->address, buffer);
        GrowingBufferDestruct(buffer);
    }
    VectorDestructorIterator(handlers[2], sizeof(HandlerVector), kMessageTypes, reinterpret_cast<void*>(kHandlerVectorDestructor));
    VectorDestructorIterator(handlers[1], sizeof(HandlerVector), kMessageTypes, reinterpret_cast<void*>(kHandlerVectorDestructor));
    VectorDestructorIterator(handlers[0], sizeof(HandlerVector), kMessageTypes, reinterpret_cast<void*>(kHandlerVectorDestructor));
    SocketDriverDestruct(&socket);
    ByTypeClear(&types.byType);
    ByTypeBucketsDestroy(&types.byType.buckets);
    ByVtableClear(&types.byVtable);
    ByVtableBucketsDestroy(&types.byVtable.buckets);
    void* sentinel = clients;
    for (void** node = *static_cast<void***>(sentinel); node != clients;) {
        void** next = static_cast<void**>(node[0]);
        ClientRefRelease(node + 2, nullptr, 0);
        ClientNodeFree(&clients, nullptr, node, 1);
        node = next;
        if (node == clients) break;
    }
    static_cast<void**>(clients)[0] = clients;
    static_cast<void**>(clients)[1] = clients;
    ClientSentinelFree(clients, 1);
    vtable = reinterpret_cast<void*>(kRefObjectRoot);
}
EI_REPLACE_METHOD(0x00436A50, SessionBase::Destruct);

// 004368D0 (slot 0): the deleting destructor.
SessionBase* __fastcall SessionDeletingDestructor(SessionBase* self, void*, unsigned flags) {
    self->Destruct();
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x004368D0, SessionDeletingDestructor);

} // namespace net

// ---- Adding a client ---------------------------------------------------------------------------------

namespace net {

namespace {
// 00432DA0: the connection constructor (id); 0043D7D0: a counted connection pointer from a raw one
// (pointer in ECX, connection in EDX); 00439E30: the clients list's insert(pos, value);
// 00425040: another STLport string destructor.
__declspec(naked) Connection* __fastcall ConnectionConstruct(void* /*memory*/, void*, std::uint32_t /*id*/) EI_ORIGINAL(0x00432DA0)
__declspec(naked) void* __fastcall ClientRefInit(void* /*ref*/, Connection* /*connection*/) EI_ORIGINAL(0x0043D7D0)
__declspec(naked) void* __fastcall ClientsInsert(void* /*list*/, void*, void** /*out*/, void* /*pos*/, const void* /*value*/) EI_ORIGINAL(0x00439E30)
__declspec(naked) void __fastcall StringDestroy2(StringRep* /*s*/) EI_ORIGINAL(0x00425040)
constexpr std::uintptr_t kAddClient = 0x00783EF8;   // "ADD client "
float GameFloatAt(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }
} // namespace

// 00436DD0: AddClient(address, id, rate). Logs it, removes a connection to the same address (slot
// 2), appends a new connection with that id and address; the send interval is the constant at
// 0073BF48 over the rate clamped to [500, 50000].
void __fastcall SessionAddClient(SessionBase* self, void*, const std::uint8_t* address, std::uint32_t id, int rate) {
    if (NetLog()) {
        StringRep peer;
        const char* text = AddressString(address, nullptr, &peer)->start;
        LogAddress(kAddClient, text);
        StringDestroy2(&peer);
    }
    ei::VCall<void>(self, 2, FindClientNode(self, address));
    auto* memory = static_cast<std::uint8_t*>(gamecrt::operator_new(sizeof(Connection)));
    std::uint32_t zero = 0;
    std::memcpy(memory + 4, &zero, 4);
    Connection* connection = memory ? ConnectionConstruct(memory, nullptr, id) : nullptr;
    void* ref;
    ClientRefInit(&ref, connection);
    void* end;
    ClientsEnd(&self->clients, nullptr, &end);
    void* inserted;
    ClientsInsert(&self->clients, nullptr, &inserted, end, &ref);
    auto* added = ConnectionOf(static_cast<void**>(self->clients)[1]);
    std::memcpy(added->address, address, sizeof(added->address));
    if (rate <= 500) rate = 500;
    else if (rate >= 50000) rate = 50000;
    added->sendInterval = static_cast<float>(static_cast<double>(GameFloatAt(0x0073BF48)) / static_cast<double>(rate));
}
EI_REPLACE(0x00436DD0, SessionAddClient);

} // namespace net
