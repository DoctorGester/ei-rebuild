// The session's receive step (00437370, slot 14), its packet handlers, sends and destruction,
// and the byte buffer helpers they use. See net_classes.h and docs/notes/network.md.
#include "library_net_lists.h"
#include "net_classes.h"
#include "game_strings.h"
#include "net_coder.h"
#include "net_connection.h"

#include "core_streams.h"
#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/crt_iostream.h"
#include "originals_library.h"
#include "originals_script.h"

#include <cstring>

namespace net {

// The network log's inserters (library), declared in net_classes.h.
LogStream* LogStream::Put(const char* text) { return reinterpret_cast<LogStream*>(reinterpret_cast<libcrt::Ostream*>(this)->PutText(text)); }
LogStream* LogStream::PutChar(char c) { return reinterpret_cast<LogStream*>(reinterpret_cast<libcrt::Ostream*>(this)->PutChar(static_cast<char>(c))); }
LogStream* LogStream::PutInt(int a0) { return reinterpret_cast<LogStream*>(reinterpret_cast<libcrt::Ostream*>(this)->PutUnsigned(static_cast<unsigned>(a0))); }
LogStream* LogStream::Apply(void* manipulator) { return reinterpret_cast<LogStream*>(reinterpret_cast<libcrt::Ostream*>(this)->Apply(reinterpret_cast<libcrt::Manipulator>(manipulator))); }

namespace {

// 004DA170: reading past the buffer's end (zeroes the target, sets error bit 3).


// "<text><address>\n" and a flush to the network log.
void LogAddress(const char* text, const char* address) { NetLog()->Put(text)->Put(address)->PutChar('\n')->Flush(); }

} // namespace

// 00419CC0: the buffer's highest written position follows the current one upwards.
void __fastcall BufferUpdateHigh(GameBuffer* buffer) {
    if (buffer->cur > buffer->high) buffer->high = buffer->cur;
}
EI_REPLACE(0x00419CC0, BufferUpdateHigh);

stlp::string* NetAddress::ToString(stlp::string* out) const {
    Format(out, 0);
    return out;
}
EI_REPLACE_METHOD(0x00435010, NetAddress::ToString);

// 00437370 (slot 14): advances the session clock by `dt`, then hands every datagram waiting on the
// socket to the handler slot of its kind (its first byte) with the sender's address and a cursor
// after that byte (an empty datagram is only logged). Then ticks every connection; one that timed
// out is reported (slot 11) and removed (slot 2). Counts the call in `frame_`. The current session
// is this one meanwhile.
void SessionBase::TickImpl(double dt) {
    CurrentSession() = this;
    time_ = dt + time_;
    NetAddress from{};
    GameBuffer* buffer = ReceiveBuffer();
    while (socket_.Receive(&from, buffer)) {
        BufferUpdateHigh(buffer);
        if (buffer->high - buffer->begin + buffer->base == 0) {
            if (NetLog()) {
                stlp::string address;
                const char* text = from.ToString(&address)->data();
                LogAddress(game::nettexts::ZeroLength(), text);
            }
            continue;
        }
        char kind;
        if (buffer->cur + 1 > buffer->high) buffer->ReadPastEnd(&kind, 1);
        else if (buffer->cur + 1 > buffer->end) buffer->ReadMore(&kind, 1u);
        else buffer->CopyOut(&kind, 1);
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
                stlp::string address;
                const char* text = connection->address_.ToString(&address)->data();
                LogAddress(game::nettexts::Timeout(), text);
            }
            OnClientLost(connection);
            RemoveClient(node);
        }
        node = next;
    }
    ++frame_;
    CurrentSession() = nullptr;
}
EI_REPLACE_METHOD(0x00437370, SessionBase::TickImpl);

} // namespace net

// ---- Session vtable slots (0073BF5C) ---------------------------------------------------------------

namespace net {

namespace {

// The clients list (STLport list<RefPtr<Connection>>, +0x28): end, begin, erase; the find by peer
// address (0043D780, an instantiation of find_if over the nodes) is FindClientByAddress
// (library_net_lists.h, over its ClientsNode of the same layout).
__declspec(naked) ClientNode** __fastcall ClientsEnd(ClientNode** /*list*/, void*, ClientNode** /*out*/) EI_ORIGINAL(0x00438830)
__declspec(naked) ClientNode** __fastcall ClientsBegin(ClientNode** /*list*/, void*, ClientNode** /*out*/) EI_ORIGINAL(0x00438820)
__declspec(naked) ClientNode** __fastcall ClientsErase(ClientNode** /*list*/, void*, ClientNode** /*out*/, ClientNode* /*pos*/) EI_ORIGINAL(0x00438840)


// "<text>\n" and a flush, when there is a network log.
void LogText(const char* text) {
    if (LogStream* log = NetLog()) log->Put(text)->PutChar('\n')->Flush();
}

// The address string of a connection, logged as "<before><address><after>\n", then freed.
void LogConnection(const char* before, Connection* connection, const char* after) {
    stlp::string peer;
    const char* text = connection->address_.ToString(&peer)->data();
    LogStream* log = NetLog()->Put(before)->Put(text);
    if (after) log = log->Put(after);
    log->PutChar('\n')->Flush();
}

} // namespace

ClientNode** SessionBase::FindClient(ClientNode** out, const NetAddress* address) {
    ClientNode* last;
    ClientsEnd(&clients_, nullptr, &last);
    ClientNode* first;
    ClientsBegin(&clients_, nullptr, &first);
    using netlists::ClientsNode;
    netlists::FindClientByAddress(reinterpret_cast<ClientsNode**>(out), nullptr, reinterpret_cast<ClientsNode*>(first),
                                  reinterpret_cast<ClientsNode*>(last), address);   // 0043D780
    return out;
}
EI_REPLACE_METHOD(0x00436CC0, SessionBase::FindClient);

// 00436CF0 (slot 2): logs it, closes the connection and erases the node.
void SessionBase::RemoveClientImpl(ClientNode* node) {
    if (node == clients_) return;
    if (NetLog()) LogConnection(game::nettexts::RemoveClient(), node->connection, nullptr);
    node->connection->Close();
    ClientNode* after;
    ClientsErase(&clients_, nullptr, &after, node);
}

// 00437080 (slot 3, packet kind 0): an update from a client. Unknown senders are logged (returns
// false); a client whose update does not read is logged, reported (slot 11) and removed (slot 2).
bool SessionBase::OnUpdateImpl(const NetAddress* from, BufferCursor* cursor) {
    ClientNode* node = ClientAt(from);
    if (node == clients_) {
        LogText(game::nettexts::UnknownHost());
        return false;
    }
    if (!node->connection->ReadUpdate(cursor)) {
        if (NetLog()) LogConnection(game::nettexts::BadClient(), node->connection, game::nettexts::BadUpdate());
        OnClientLost(node->connection);
        RemoveClient(node);
    }
    return true;
}

// 00437240 (slot 6, packet kind 1): a client leaves: its connection id follows; when it matches,
// the client is reported (slot 12) and removed (slot 2).
void SessionBase::OnLeaveImpl(const NetAddress* from, BufferCursor* cursor) {
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
void SessionBase::OnGetInfoImpl(const NetAddress*, BufferCursor*) { LogText(game::nettexts::CantHandleGetInfo()); }
void SessionBase::OnLoginImpl(const NetAddress*, BufferCursor*) { LogText(game::nettexts::CantHandleLogin()); }
void SessionBase::OnInfoImpl(const NetAddress*, BufferCursor*) { LogText(game::nettexts::CantHandleInfo()); }
void SessionBase::OnAcceptedImpl(const NetAddress*, BufferCursor*) { LogText(game::nettexts::CantHandleAccepted()); }
void SessionBase::OnRejectedImpl(const NetAddress*, BufferCursor*) { LogText(game::nettexts::CantHandleRejected()); }
void SessionBase::OnMasterImpl(const NetAddress*, BufferCursor*) { LogText(game::nettexts::CantHandleMaster()); }

EI_REPLACE_METHOD(0x00436CF0, SessionBase::RemoveClientImpl);
EI_REPLACE_METHOD(0x00437080, SessionBase::OnUpdateImpl);
EI_REPLACE_METHOD(0x00437240, SessionBase::OnLeaveImpl);
EI_REPLACE_METHOD(0x004371E0, SessionBase::OnGetInfoImpl);    // slot 4, kind 3
EI_REPLACE_METHOD(0x00437210, SessionBase::OnLoginImpl);      // slot 5, kind 4
EI_REPLACE_METHOD(0x004372B0, SessionBase::OnInfoImpl);       // slot 7, kind 5
EI_REPLACE_METHOD(0x004372E0, SessionBase::OnAcceptedImpl);   // slot 8, kind 6
EI_REPLACE_METHOD(0x00437310, SessionBase::OnRejectedImpl);   // slot 9, kind 7
EI_REPLACE_METHOD(0x00437340, SessionBase::OnMasterImpl);     // slot 10, kind -1

} // namespace net

// ---- Byte buffer writes and small socket helpers ---------------------------------------------------

// 00436C30: copies `size` bytes to the write position and moves it on (no room check); returns
// the buffer.
GameBuffer* GameBuffer::BufferWriteRaw(const void* from, unsigned size) {
    std::memcpy(cur, from, size);
    cur += size;
    return this;
}
EI_REPLACE_METHOD(0x00436C30, GameBuffer::BufferWriteRaw);

// 00436C60: writes `size` bytes: in place (marking the buffer written) when they fit before `end`,
// otherwise through slot 4. Returns `size` or what slot 4 left.
unsigned GameBuffer::BufferWrite(const void* from, unsigned size) {
    if (cur + size > end) return static_cast<unsigned>(WriteMore(from, size));
    flags |= 1;
    std::memcpy(cur, from, size);
    cur += size;
    return size;
}
EI_REPLACE_METHOD(0x00436C60, GameBuffer::BufferWrite);

namespace net {

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
    static_cast<GameBuffer*>(buffer)->ConstructGrowing();
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
void __stdcall VectorDestructorIterator(void* a0, unsigned a1, int a2, void* a3) { libcrt::DestroyElements(a0, a1, a2, reinterpret_cast<libcrt::ElementFunction>(a3)); }
// The handler vectors' destructor (the vector base's: the storage freed).
constexpr auto HandlerVectorDestructor = stlp::VectorAbi<void*>::DestroyBase;
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
        static_cast<GameBuffer*>(buffer)->ConstructGrowing();
        std::uint8_t kind = 1;
        buffer->BufferWrite(&kind, 1);
        if (buffer->cur + 4 <= buffer->end) {
            buffer->flags |= 1;
            buffer->BufferWriteRaw(&id, 4);
        } else {
            buffer->WriteMore(&id, 4u);
        }
        socket_.SendTo(&connection->address_, buffer);
        GrowingBufferDestruct(buffer);
    }
    VectorDestructorIterator(handlers_[2], sizeof(HandlerVector), kMessageTypes, reinterpret_cast<void*>(HandlerVectorDestructor));
    VectorDestructorIterator(handlers_[1], sizeof(HandlerVector), kMessageTypes, reinterpret_cast<void*>(HandlerVectorDestructor));
    VectorDestructorIterator(handlers_[0], sizeof(HandlerVector), kMessageTypes, reinterpret_cast<void*>(HandlerVectorDestructor));
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
    ei::SetVtable(this, CRefCounted::kVtableAddress);
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
// 00439E30: the clients list's insert(pos, value); 00425040: another STLport string destructor.
__declspec(naked) ClientNode** __fastcall ClientsInsert(ClientNode** /*list*/, void*, ClientNode** /*out*/, ClientNode* /*pos*/,
                                                        Connection* const* /*value*/) EI_ORIGINAL(0x00439E30)
float GameFloatAt(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }
} // namespace

// The send interval is the constant at 0073BF48 over the rate clamped to [500, 50000].
void SessionBase::AddClient(const NetAddress* address, std::uint32_t id, int rate) {
    if (NetLog()) {
        stlp::string peer;
        const char* text = address->ToString(&peer)->data();
        LogAddress(game::nettexts::AddClient(), text);
    }
    RemoveClient(ClientAt(address));
    auto* memory = NewMessageMemory<Connection>();
    Connection* connection = memory ? memory->Construct(id) : nullptr;
    Connection* ref;
    netlists::InitClientRef(&ref, connection);
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
