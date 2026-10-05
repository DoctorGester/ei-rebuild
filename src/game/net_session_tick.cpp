// The session's receive step (00437370, slot 14), its packet handlers, sends and destruction,
// and the byte buffer helpers they use. See net_classes.h and docs/notes/unmapped_410_470.md.
#include "net_classes.h"
#include "net_connection.h"

#include "core_streams.h"
#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "originals_library.h"
#include "originals_script.h"

#include <cstring>

// Precise semantics keep the x87 division in SessionBase::AddClient as written.
#pragma float_control(precise, on)

namespace net {

// The network log's inserters (library), declared in net_classes.h.
LogStream* LogStream::Put(const char* text) { return static_cast<LogStream*>(OstreamPutString(this, nullptr, text)); }
LogStream* LogStream::PutChar(char c) { return static_cast<LogStream*>(OstreamPutChar(this, nullptr, static_cast<char>(c))); }
__declspec(naked) LogStream* LogStream::PutInt(int) EI_ORIGINAL(0x006FC14E)
LogStream* LogStream::Apply(void* manipulator) { return static_cast<LogStream*>(OstreamManipulate(this, nullptr, manipulator)); }

namespace {

// 0040C770: STLport string destructor; 00426450: the string allocator's deallocate(p, n).
__declspec(naked) void __fastcall StringDeallocate(void* /*allocator*/, void*, char* /*p*/, std::size_t /*n*/) EI_ORIGINAL(0x00426450)
// 004DA170: reading past the buffer's end (zeroes the target, sets error bit 3).

const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }
constexpr std::uintptr_t kZeroLength = 0x00784020;   // "ZERO length packet received from "
constexpr std::uintptr_t kTimeout = 0x00784008;      // "TIMEOUT client, drop "

// "<text><address>\n" and a flush to the network log.
void LogAddress(std::uintptr_t text, const char* address) { NetLog()->Put(Text(text))->Put(address)->PutChar('\n')->Flush(); }

} // namespace

// 00419CC0: the buffer's highest written position follows the current one upwards.
void __fastcall BufferUpdateHigh(GameBuffer* buffer) {
    if (buffer->cur > buffer->high) buffer->high = buffer->cur;
}
EI_REPLACE(0x00419CC0, BufferUpdateHigh);

StringRep* NetAddress::ToString(StringRep* out) const {
    Format(out, 0);
    return out;
}
EI_REPLACE_METHOD(0x00435010, NetAddress::ToString);

// 00437370 (slot 14): advances the session clock by `dt`, then hands every datagram waiting on the
// socket to the handler slot of its kind (its first byte) with the sender's address and a cursor
// after that byte (an empty datagram is only logged). Then ticks every connection; one that timed
// out is reported (slot 11) and removed (slot 2). Counts the call in `frame_`. The current session
// is this one meanwhile.
void SessionBase::Tick(double dt) {
    CurrentSession() = this;
    time_ = dt + time_;
    NetAddress from{};
    GameBuffer* buffer = ReceiveBuffer();
    while (socket_.Receive(&from, buffer)) {
        BufferUpdateHigh(buffer);
        if (buffer->high - buffer->begin + buffer->base == 0) {
            if (NetLog()) {
                StringRep address;
                const char* text = from.ToString(&address)->start;
                LogAddress(kZeroLength, text);
                StringRepDestroy(&address);
            }
            continue;
        }
        char kind;
        if (buffer->cur + 1 > buffer->high) BufferReadPastEnd(buffer, nullptr, &kind, 1);
        else if (buffer->cur + 1 > buffer->end) buffer->ReadMore(&kind, 1u);
        else BufferCopyOut(buffer, nullptr, &kind, 1);
        BufferUpdateHigh(buffer);
        BufferCursor cursor{};
        cursor.p = buffer->begin + 1;
        cursor.bytePtr = nullptr;
        cursor.bits = 0;
        switch (kind) {
        case -1: OnMaster(&from, &cursor); break;
        case 0: OnUpdate(&from, &cursor); break;
        case 1: OnLeave(&from, &cursor); break;
        case 3: OnGetInfo(&from, &cursor); break;
        case 4: OnLogin(&from, &cursor); break;
        case 5: OnInfo(&from, &cursor); break;
        case 6: OnAccepted(&from, &cursor); break;
        case 7: OnRejected(&from, &cursor); break;
        default: break;   // 2 and anything else: ignored
        }
    }
    for (ClientNode* node = clients_->next; node != clients_;) {
        ClientNode* next = node->next;
        Connection* connection = node->connection;
        if (!connection->Tick(dt)) {
            if (NetLog()) {
                StringRep address;
                const char* text = connection->address_.ToString(&address)->start;
                LogAddress(kTimeout, text);
                StringDeallocate(&address.eos, nullptr, address.start, static_cast<std::size_t>(address.eos - address.start));
            }
            OnClientLost(connection);
            RemoveClient(node);
        }
        node = next;
    }
    ++frame_;
    CurrentSession() = nullptr;
}
namespace {
void __fastcall TickHook(SessionBase* self, void*, double dt) { self->SessionBase::Tick(dt); }
} // namespace
EI_REPLACE(0x00437370, TickHook);

} // namespace net

// ---- Session vtable slots (0073BF5C) ---------------------------------------------------------------

namespace net {

namespace {

// The clients list (STLport list<RefPtr<Connection>>, +0x28): end, begin, find by peer address (an
// instantiation of find_if over the nodes), erase.
__declspec(naked) ClientNode** __fastcall ClientsEnd(ClientNode** /*list*/, void*, ClientNode** /*out*/) EI_ORIGINAL(0x00438830)
__declspec(naked) ClientNode** __fastcall ClientsBegin(ClientNode** /*list*/, void*, ClientNode** /*out*/) EI_ORIGINAL(0x00438820)
__declspec(naked) ClientNode** __fastcall ClientFind(ClientNode** /*out*/, void*, ClientNode* /*first*/, ClientNode* /*last*/,
                                                     const NetAddress* /*address*/) EI_ORIGINAL(0x0043D780)
__declspec(naked) ClientNode** __fastcall ClientsErase(ClientNode** /*list*/, void*, ClientNode** /*out*/, ClientNode* /*pos*/) EI_ORIGINAL(0x00438840)
// 0043B310: the node allocator's deallocate (p in ECX, size in EDX), used for the address strings.
__declspec(naked) void __fastcall NodeFree(char* /*p*/, std::size_t /*n*/) EI_ORIGINAL(0x0043B310)

constexpr std::uintptr_t kUnknownHost = 0x00783F2C;   // "ERROR: Update from unknown host"
constexpr std::uintptr_t kBadClient = 0x00783F1C;     // "ERROR: client "
constexpr std::uintptr_t kBadUpdate = 0x00783F04;     // " sent incorrect update"
constexpr std::uintptr_t kRemove = 0x00783EE8;        // "REMOVE client "

// "<text>\n" and a flush, when there is a network log.
void LogText(std::uintptr_t text) {
    if (LogStream* log = NetLog()) log->Put(Text(text))->PutChar('\n')->Flush();
}

// The address string of a connection, logged as "<before><address><after>\n", then freed.
void LogConnection(std::uintptr_t before, Connection* connection, std::uintptr_t after) {
    StringRep peer;
    const char* text = connection->address_.ToString(&peer)->start;
    LogStream* log = NetLog()->Put(Text(before))->Put(text);
    if (after) log = log->Put(Text(after));
    log->PutChar('\n')->Flush();
    NodeFree(peer.start, static_cast<std::size_t>(peer.eos - peer.start));
}

} // namespace

ClientNode** SessionBase::FindClient(ClientNode** out, const NetAddress* address) {
    ClientNode* last;
    ClientsEnd(&clients_, nullptr, &last);
    ClientNode* first;
    ClientsBegin(&clients_, nullptr, &first);
    ClientFind(out, nullptr, first, last, address);
    return out;
}
EI_REPLACE_METHOD(0x00436CC0, SessionBase::FindClient);

// 00436CF0 (slot 2): logs it, closes the connection and erases the node.
void SessionBase::RemoveClient(ClientNode* node) {
    if (node == clients_) return;
    if (NetLog()) LogConnection(kRemove, node->connection, 0);
    node->connection->Close();
    ClientNode* after;
    ClientsErase(&clients_, nullptr, &after, node);
}

// 00437080 (slot 3, packet kind 0): an update from a client. Unknown senders are logged (returns
// false); a client whose update does not read is logged, reported (slot 11) and removed (slot 2).
bool SessionBase::OnUpdate(const NetAddress* from, BufferCursor* cursor) {
    ClientNode* node = ClientAt(from);
    if (node == clients_) {
        LogText(kUnknownHost);
        return false;
    }
    if (!node->connection->ReadUpdate(cursor)) {
        if (NetLog()) LogConnection(kBadClient, node->connection, kBadUpdate);
        OnClientLost(node->connection);
        RemoveClient(node);
    }
    return true;
}

// 00437240 (slot 6, packet kind 1): a client leaves: its connection id follows; when it matches,
// the client is reported (slot 12) and removed (slot 2).
void SessionBase::OnLeave(const NetAddress* from, BufferCursor* cursor) {
    std::uint32_t id;
    std::memcpy(&id, cursor->p, 4);
    cursor->p += 4;
    ClientNode* node = ClientAt(from);
    if (node != clients_ && node->connection->id_ == id) {
        OnClientLeft(node->connection);
        RemoveClient(node);
    }
}

// The base session's handlers of the kinds it does not serve: each logs an error.
void SessionBase::OnGetInfo(const NetAddress*, BufferCursor*) { LogText(0x00783F4C); }    // "ERROR: Can't handle get info"
void SessionBase::OnLogin(const NetAddress*, BufferCursor*) { LogText(0x00783F6C); }      // "ERROR: Can't handle login"
void SessionBase::OnInfo(const NetAddress*, BufferCursor*) { LogText(0x00783F88); }       // "ERROR: Can't handle info"
void SessionBase::OnAccepted(const NetAddress*, BufferCursor*) { LogText(0x00783FA4); }   // "ERROR: Can't handle accepted"
void SessionBase::OnRejected(const NetAddress*, BufferCursor*) { LogText(0x00783FC4); }   // "ERROR: Can't handle rejected"
void SessionBase::OnMaster(const NetAddress*, BufferCursor*) { LogText(0x00783FE4); }     // "ERROR: Can't handle master packet"

namespace {
void __fastcall RemoveClientHook(SessionBase* self, void*, ClientNode* node) { self->SessionBase::RemoveClient(node); }
bool __fastcall OnUpdateHook(SessionBase* self, void*, const NetAddress* from, BufferCursor* cursor) {
    return self->SessionBase::OnUpdate(from, cursor);
}
void __fastcall OnLeaveHook(SessionBase* self, void*, const NetAddress* from, BufferCursor* cursor) {
    self->SessionBase::OnLeave(from, cursor);
}
void __fastcall OnGetInfoHook(SessionBase* self, void*, const NetAddress* from, BufferCursor* cursor) {
    self->SessionBase::OnGetInfo(from, cursor);
}
void __fastcall OnLoginHook(SessionBase* self, void*, const NetAddress* from, BufferCursor* cursor) {
    self->SessionBase::OnLogin(from, cursor);
}
void __fastcall OnInfoHook(SessionBase* self, void*, const NetAddress* from, BufferCursor* cursor) {
    self->SessionBase::OnInfo(from, cursor);
}
void __fastcall OnAcceptedHook(SessionBase* self, void*, const NetAddress* from, BufferCursor* cursor) {
    self->SessionBase::OnAccepted(from, cursor);
}
void __fastcall OnRejectedHook(SessionBase* self, void*, const NetAddress* from, BufferCursor* cursor) {
    self->SessionBase::OnRejected(from, cursor);
}
void __fastcall OnMasterHook(SessionBase* self, void*, const NetAddress* from, BufferCursor* cursor) {
    self->SessionBase::OnMaster(from, cursor);
}
} // namespace
EI_REPLACE(0x00436CF0, RemoveClientHook);
EI_REPLACE(0x00437080, OnUpdateHook);
EI_REPLACE(0x00437240, OnLeaveHook);
EI_REPLACE(0x004371E0, OnGetInfoHook);    // slot 4, kind 3
EI_REPLACE(0x00437210, OnLoginHook);      // slot 5, kind 4
EI_REPLACE(0x004372B0, OnInfoHook);       // slot 7, kind 5
EI_REPLACE(0x004372E0, OnAcceptedHook);   // slot 8, kind 6
EI_REPLACE(0x00437310, OnRejectedHook);   // slot 9, kind 7
EI_REPLACE(0x00437340, OnMasterHook);     // slot 10, kind -1

} // namespace net

// ---- Byte buffer writes and small socket helpers ---------------------------------------------------

namespace net {

namespace {
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
    if (buffer->cur + size > buffer->end) return static_cast<unsigned>(buffer->WriteMore(from, size));
    buffer->flags |= 1;
    std::memcpy(buffer->cur, from, size);
    buffer->cur += size;
    return size;
}
EI_REPLACE(0x00436C60, BufferWrite);

bool SessionBase::Send(const NetAddress* address, GameBuffer* buffer) { return socket_.SendTo(address, buffer); }
EI_REPLACE_METHOD(0x00436F60, SessionBase::Send);

std::uint32_t SessionBase::RateFor(const NetAddress* address) {
    bool local = socket_.IsLocal(address);
    return *reinterpret_cast<const std::uint32_t*>(local ? 0x00783E38 : 0x00783E34);
}
EI_REPLACE_METHOD(0x00436F80, SessionBase::RateFor);

void SessionBase::SendLeave(const NetAddress* address, std::uint32_t id) {
    alignas(4) std::uint8_t storage[sizeof(GameBuffer)];
    auto* buffer = reinterpret_cast<GameBuffer*>(storage);
    GrowingBufferConstruct(buffer);
    std::uint8_t kind = 1;
    if (buffer->cur + 1 <= buffer->end) {
        buffer->flags |= 1;
        *buffer->cur = kind;
        ++buffer->cur;
    } else {
        buffer->WriteMore(&kind, 1u);
    }
    if (buffer->cur + 4 <= buffer->end) {
        buffer->flags |= 1;
        std::memcpy(buffer->cur, &id, 4);
        buffer->cur += 4;
    } else {
        buffer->WriteMore(&id, 4u);
    }
    socket_.SendTo(address, buffer);
    GrowingBufferDestruct(buffer);
}
EI_REPLACE_METHOD(0x00436FA0, SessionBase::SendLeave);

} // namespace net

// ---- Destruction ----------------------------------------------------------------------------------

namespace net {

namespace {
// CRT eh vector destructor iterator (ptr, element size, count, destructor), the handler vector's
// destructor.
void __stdcall VectorDestructorIterator(void* a0, unsigned a1, int a2, void* a3) { EhVectorDestructorIterator(a0, a1, a2, a3); }
constexpr std::uintptr_t kHandlerVectorDestructor = 0x004389C0;
// The type registry's tables: clear and bucket-vector destruction of byType and byVtable.
__declspec(naked) void __fastcall ByTypeClear(TypeRegistry::ByType* /*table*/) EI_ORIGINAL(0x0043D590)
__declspec(naked) void __fastcall ByTypeBucketsDestroy(void* /*buckets*/) EI_ORIGINAL(0x0043AAB0)
__declspec(naked) void __fastcall ByVtableClear(TypeRegistry::ByVtable* /*table*/) EI_ORIGINAL(0x0043B4D0)
__declspec(naked) void __fastcall ByVtableBucketsDestroy(void* /*buckets*/) EI_ORIGINAL(0x0043A2B0)
// The clients list: release of a node's counted connection pointer, node deallocation, sentinel
// deallocation (pointer in ECX, count in EDX).
__declspec(naked) void __fastcall ClientRefRelease(Connection** /*ref*/, void*, int /*flag*/) EI_ORIGINAL(0x0043DCA0)
__declspec(naked) void __fastcall ClientNodeFree(ClientNode** /*list*/, void*, ClientNode* /*node*/, int /*n*/) EI_ORIGINAL(0x00439E60)
__declspec(naked) void __fastcall ClientSentinelFree(ClientNode* /*sentinel*/, int /*n*/) EI_ORIGINAL(0x0043AB30)
} // namespace

void SessionBase::Destruct() {
    ei::SetVtable(this, kVtableAddress);
    for (ClientNode* node = clients_->next; node != clients_; node = node->next) {
        Connection* connection = node->connection;
        std::uint32_t id = connection->id_;
        alignas(4) std::uint8_t storage[sizeof(GameBuffer)];
        auto* buffer = reinterpret_cast<GameBuffer*>(storage);
        GrowingBufferConstruct(buffer);
        std::uint8_t kind = 1;
        BufferWrite(buffer, nullptr, &kind, 1);
        if (buffer->cur + 4 <= buffer->end) {
            buffer->flags |= 1;
            BufferWriteRaw(buffer, nullptr, &id, 4);
        } else {
            buffer->WriteMore(&id, 4u);
        }
        socket_.SendTo(&connection->address_, buffer);
        GrowingBufferDestruct(buffer);
    }
    VectorDestructorIterator(handlers_[2], sizeof(HandlerVector), kMessageTypes, reinterpret_cast<void*>(kHandlerVectorDestructor));
    VectorDestructorIterator(handlers_[1], sizeof(HandlerVector), kMessageTypes, reinterpret_cast<void*>(kHandlerVectorDestructor));
    VectorDestructorIterator(handlers_[0], sizeof(HandlerVector), kMessageTypes, reinterpret_cast<void*>(kHandlerVectorDestructor));
    socket_.Destruct();
    ByTypeClear(&types_.byType);
    ByTypeBucketsDestroy(types_.byType.BucketVector());
    ByVtableClear(&types_.byVtable);
    ByVtableBucketsDestroy(types_.byVtable.BucketVector());
    ClientNode* sentinel = clients_;
    for (ClientNode* node = sentinel->next; node != clients_;) {
        ClientNode* next = node->next;
        ClientRefRelease(&node->connection, nullptr, 0);
        ClientNodeFree(&clients_, nullptr, node, 1);
        node = next;
        if (node == clients_) break;
    }
    clients_->next = clients_;
    clients_->prev = clients_;
    ClientSentinelFree(clients_, 1);
    ei::SetVtable(this, kRefObjectRootVtable);
}
EI_REPLACE_METHOD(0x00436A50, SessionBase::Destruct);

SessionBase* SessionBase::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x004368D0, SessionBase::DeletingDestruct);

} // namespace net

// ---- Adding a client ---------------------------------------------------------------------------------

namespace net {

namespace {
// 0043D7D0: a counted connection pointer from a raw one (pointer in ECX, connection in EDX);
// 00439E30: the clients list's insert(pos, value); 00425040: another STLport string destructor.
__declspec(naked) Connection** __fastcall ClientRefInit(Connection** /*ref*/, Connection* /*connection*/) EI_ORIGINAL(0x0043D7D0)
__declspec(naked) ClientNode** __fastcall ClientsInsert(ClientNode** /*list*/, void*, ClientNode** /*out*/, ClientNode* /*pos*/,
                                                        Connection* const* /*value*/) EI_ORIGINAL(0x00439E30)
constexpr std::uintptr_t kAddClient = 0x00783EF8;   // "ADD client "
float GameFloatAt(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }
} // namespace

// The send interval is the constant at 0073BF48 over the rate clamped to [500, 50000].
void SessionBase::AddClient(const NetAddress* address, std::uint32_t id, int rate) {
    if (NetLog()) {
        StringRep peer;
        const char* text = address->ToString(&peer)->start;
        LogAddress(kAddClient, text);
        StringBufferFree(&peer);
    }
    RemoveClient(ClientAt(address));
    auto* memory = static_cast<Connection*>(gamecrt::operator_new(sizeof(Connection)));
    std::uint32_t zero = 0;   // the count and flags, before the null check (as the original)
    std::memcpy(reinterpret_cast<std::uint8_t*>(memory) + 4, &zero, 4);
    Connection* connection = memory ? memory->Construct(id) : nullptr;
    Connection* ref;
    ClientRefInit(&ref, connection);
    ClientNode* end;
    ClientsEnd(&clients_, nullptr, &end);
    ClientNode* inserted;
    ClientsInsert(&clients_, nullptr, &inserted, end, &ref);
    Connection* added = clients_->prev->connection;
    added->address_ = *address;
    if (rate <= 500) rate = 500;
    else if (rate >= 50000) rate = 50000;
    added->sendInterval_ = static_cast<float>(static_cast<double>(GameFloatAt(0x0073BF48)) / static_cast<double>(rate));
}
EI_REPLACE_METHOD(0x00436DD0, SessionBase::AddClient);

} // namespace net
