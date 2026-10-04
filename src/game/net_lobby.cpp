// The client base's lobby (0073BE30): finding servers and joining one, and the server base's half
// (0073BBB4) that answers. The client keeps
//   +0x2568 the host it joins (sockaddr_in), +0x2578 the join request's extra data (a growing
//   buffer), +0x2594 the join id, +0x2598 the join state (0 none, 1 asking, 2 joined), +0x259C the
//   reason of a refusal, +0x25A0 / +0x25A4 the retry timer and interval (seconds),
//   +0x25A8 the servers found (STLport list of counted server entries),
//   +0x25AC the servers asked directly (vector of 0x2C-byte queries: an address and the reply
//   last seen; then a dword), +0x25BC addresses waiting to be asked (STLport list),
//   +0x25C0 an address asked on every refresh, +0x25D0 the query the refresh repeats.
// The packets (first byte): 3 ping (the session time), 4 join (magic, id, rate, extra data),
// 5 info (the echoed time, magic, the server entry), 6 accepted (id), 7 refused (id, reason).
// The server base keeps its pending joins at +0x2568 (address, id, rate, age).
// Plain float maths (rule 7). See docs/notes/unmapped_410_470.md ("Ninth round").
#include "core_refcount.h"
#include "net_classes.h"
#include "net_coder.h"
#include "lib/crt.h"
#include "lib/stlp.h"

#include "ei/hook.h"
#include "ei/vcall.h"

#include <cstdint>
#include <cstring>

namespace net {

// Rewritten elsewhere (net_session_tick.cpp).
StringRep* __fastcall AddressString(const void* address, void*, StringRep* out);
bool __fastcall SessionOnUpdate(SessionBase* self, void*, const void* from, BufferCursor* cursor);
void __fastcall SessionRemoveClient(SessionBase* self, void*, void* node);
bool __fastcall SessionSend(SessionBase* self, void*, const void* address, GameBuffer* buffer);
std::uint32_t __fastcall SessionRateFor(SessionBase* self, void*, const void* address);
void __fastcall SessionAddClient(SessionBase* self, void*, const std::uint8_t* address, std::uint32_t id, int rate);
GameBuffer* __fastcall BufferWriteRaw(GameBuffer* buffer, void*, const void* from, unsigned size);
unsigned __fastcall BufferWrite(GameBuffer* buffer, void*, const void* from, unsigned size);
void __fastcall BufferUpdateHigh(GameBuffer* buffer);

namespace lobby {

// A server entry's base (vtable 0073BB40, 0x28 bytes; the entries are 0073BB28, 0x48 bytes).
struct EntryBase {
    void* vtable;
    std::uint32_t refHeader;     // +0x04 (CRefObject)
    std::uint8_t address[16];    // +0x08
    float timeout;               // +0x18 seconds until the entry is checked again (1e38: never)
    std::uint32_t ping;          // +0x1C float; the constant at 0073BFA0 when unknown
    std::uint8_t foreign;        // +0x20 the reply's echoed word differs from the magic
    std::uint8_t unknown_21[3];
    std::uint32_t state;         // +0x24 0 local, 1 asked, 2 from the server list
};
static_assert(sizeof(EntryBase) == 0x28);

// A server asked directly (0x2C bytes).
struct Query {
    std::uint8_t address[16];
    GameBuffer reply;            // +0x10
};
static_assert(sizeof(Query) == 0x2C);

struct QueryVector {
    Query* begin;
    Query* end;
    Query* eos;
    std::uint32_t extra;         // +0x0C saved with the queries
};
static_assert(sizeof(QueryVector) == 0x10);

// An STLport list node; the value follows the links.
struct Node {
    Node* next;
    Node* prev;
};
template <typename T>
T& ValueOf(Node* node) { return *reinterpret_cast<T*>(reinterpret_cast<std::uint8_t*>(node) + 8); }

} // namespace lobby

using lobby::EntryBase;
using lobby::Node;
using lobby::Query;
using lobby::QueryVector;
using lobby::ValueOf;

namespace {

template <typename T>
T& At(void* base, std::size_t offset) { return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(base) + offset); }

constexpr std::size_t kMagic = 0x08;
constexpr std::size_t kTime = 0x20;
constexpr std::size_t kClients = 0x28;
constexpr std::size_t kSocket = 0x54;
constexpr std::size_t kHost = 0x2568;
constexpr std::size_t kJoinData = 0x2578;
constexpr std::size_t kJoinId = 0x2594;
constexpr std::size_t kJoinState = 0x2598;
constexpr std::size_t kRefusal = 0x259C;
constexpr std::size_t kRetryTimer = 0x25A0;
constexpr std::size_t kRetryInterval = 0x25A4;
constexpr std::size_t kServers = 0x25A8;
constexpr std::size_t kQueries = 0x25AC;
constexpr std::size_t kWaiting = 0x25BC;
constexpr std::size_t kAlwaysAsk = 0x25C0;
constexpr std::size_t kNextQuery = 0x25D0;
constexpr std::size_t kServerJoins = 0x2568;

constexpr int kSlotCreateEntry = 16;   // client: a new server entry into a RefPtr
constexpr int kSlotDescribe = 15;      // server: its own entry into a RefPtr
constexpr int kSlotJoined = 15;        // client: told that the join was accepted
constexpr int kEntryWrite = 4;
constexpr int kEntryRead = 5;
constexpr int kBufferWriteMore = 4;
constexpr int kBufferSeek = 6;
constexpr int kSinkCommit = 2;

constexpr std::uint32_t kNever = 0x7E967699;   // 1e38f
constexpr std::uint32_t kFirstRetry = 0x3DCCCCCD;   // 0.1f

// The socket driver (net_socket_driver.cpp), through its addresses.
__declspec(naked) bool __fastcall DriverIsLocalAt(SocketDriver*, void*, const void*) EI_ORIGINAL(0x00440AF0)
__declspec(naked) bool __fastcall DriverBroadcastAt(SocketDriver*, void*, void* /*out*/, unsigned /*index*/, int /*port*/) EI_ORIGINAL(0x00440A90)

// Growing buffers and the session's receive step.
__declspec(naked) void __fastcall GrowingBufferConstruct(GameBuffer*) EI_ORIGINAL(0x004DA7F0)
__declspec(naked) void __fastcall GrowingBufferDestruct(GameBuffer*) EI_ORIGINAL(0x004DA910)
__declspec(naked) void __fastcall GameBufferCopy(GameBuffer* /*to*/, void*, const GameBuffer* /*from*/) EI_ORIGINAL(0x004DA9E0)
__declspec(naked) void __fastcall GameBufferAssign(GameBuffer* /*to*/, void*, const GameBuffer* /*from*/) EI_ORIGINAL(0x004DAA80)
__declspec(naked) void __fastcall GameBufferGrow(GameBuffer*, void*, std::uint32_t) EI_ORIGINAL(0x004DA5C0)
__declspec(naked) void __fastcall SessionReceiveAt(void*, void*, double) EI_ORIGINAL(0x00437370)

// STLport containers (library): the servers list (004436C0 end, 004437A0 insert of a counted
// pointer, 004436D0 erase), the waiting addresses (0041FCD0 end, 00421AF0 insert, 004437D0
// erase), the server's joins (00443730 erase); the query vector (00443E30 construct, 004439C0
// insert overflow, 00443FF0 copy, 00443BF0 assign, 004442B0 erase, 00444090 resize).
__declspec(naked) Node** __fastcall ServersEnd(void* /*list*/, void*, Node** /*out*/) EI_ORIGINAL(0x004436C0)
__declspec(naked) Node** __fastcall ServersInsert(void* /*list*/, void*, Node** /*out*/, Node* /*pos*/, const RefPtr* /*value*/) EI_ORIGINAL(0x004437A0)
__declspec(naked) Node** __fastcall ServersErase(void* /*list*/, void*, Node** /*out*/, Node* /*pos*/) EI_ORIGINAL(0x004436D0)
__declspec(naked) Node** __fastcall WaitingEnd(void* /*list*/, void*, Node** /*out*/) EI_ORIGINAL(0x0041FCD0)
__declspec(naked) Node** __fastcall WaitingInsert(void* /*list*/, void*, Node** /*out*/, Node* /*pos*/, const void* /*value*/) EI_ORIGINAL(0x00421AF0)
__declspec(naked) Node** __fastcall WaitingErase(void* /*list*/, void*, Node** /*out*/, Node* /*pos*/) EI_ORIGINAL(0x004437D0)
__declspec(naked) Node** __fastcall JoinsErase(void* /*list*/, void*, Node** /*out*/, Node* /*pos*/) EI_ORIGINAL(0x00443730)
__declspec(naked) void __fastcall QueryConstructAt(Query* /*p*/) EI_ORIGINAL(0x00443E30)
__declspec(naked) void __fastcall QueriesInsertOverflow(QueryVector*, void*, Query* /*pos*/, const Query* /*value*/, unsigned /*n*/) EI_ORIGINAL(0x004439C0)
__declspec(naked) Query* __fastcall QueriesCopy(Query* /*first*/, Query* /*last*/, Query* /*dest*/, const void* /*unused*/, int /*unused*/) EI_ORIGINAL(0x00443FF0)
__declspec(naked) QueryVector* __fastcall QueriesAssign(QueryVector*, void*, const QueryVector* /*from*/) EI_ORIGINAL(0x00443BF0)
__declspec(naked) Query* __fastcall QueriesErase(QueryVector*, void*, Query* /*first*/, Query* /*last*/) EI_ORIGINAL(0x004442B0)
__declspec(naked) void __fastcall QueriesResize(QueryVector*, void*, unsigned /*n*/) EI_ORIGINAL(0x00444090)
// The query's own functions, through their addresses (rewritten below).
__declspec(naked) Query* __fastcall QueryConstructDefaultAt(Query*) EI_ORIGINAL(0x00443770)
__declspec(naked) Query* __fastcall QueryDeleteAt(Query*, void*, unsigned /*flags*/) EI_ORIGINAL(0x00443E90)
__declspec(naked) RefPtr* __fastcall HolderInitAt(RefPtr*, CRefObject*) EI_ORIGINAL(0x00443E20)
__declspec(naked) std::int32_t __fastcall BufferLengthAt(GameBuffer*) EI_ORIGINAL(0x00441640)
__declspec(naked) Query* __fastcall QueriesFindAt(QueryVector*, void*, const void*) EI_ORIGINAL(0x00441410)
__declspec(naked) void __fastcall ClientSendPingAt(void*, void*, const void*) EI_ORIGINAL(0x00441660)
__declspec(naked) void __fastcall ClientSendJoinAt(void*, void*, const void*, const GameBuffer*) EI_ORIGINAL(0x00441740)
__declspec(naked) void __fastcall ServerSendInfoAt(void*, void*, const void*, std::uint32_t) EI_ORIGINAL(0x00442C60)
__declspec(naked) void __fastcall FlushBitsAt(BufferCursor*) EI_ORIGINAL(0x00442570)

// The chunk archive (004098D0 enter, 00409A60 count, 004092B0 field, 00409980 leave); +0x58 is
// its loading flag, +0x50 its chunk stack (the current chunk's next index at +4 -> +0x14).
__declspec(naked) bool __fastcall ArchiveEnter(void*, void*, int /*id*/) EI_ORIGINAL(0x004098D0)
__declspec(naked) int __fastcall ArchiveCount(void*, void*, int /*id*/) EI_ORIGINAL(0x00409A60)
__declspec(naked) void __fastcall ArchiveField(void*, void*, int /*id*/, void* /*data*/, unsigned /*size*/) EI_ORIGINAL(0x004092B0)
__declspec(naked) void __fastcall ArchiveLeave(void*) EI_ORIGINAL(0x00409980)

// The network log's old iostream inserters (library).
struct LogStream {
    LogStream* Put(const char* text);        // 006FC31D
    LogStream* PutChar(char c);              // 006FC0B0
    LogStream* Apply(void* manipulator);     // 00434FE0
};
__declspec(naked) LogStream* LogStream::Put(const char*) EI_ORIGINAL(0x006FC31D)
__declspec(naked) LogStream* LogStream::PutChar(char) EI_ORIGINAL(0x006FC0B0)
__declspec(naked) LogStream* LogStream::Apply(void*) EI_ORIGINAL(0x00434FE0)
constexpr std::uintptr_t kFlush = 0x00435000;
LogStream* Log() { return *reinterpret_cast<LogStream**>(0x0079B91C); }
const char* Text(std::uintptr_t a) { return reinterpret_cast<const char*>(a); }

// "<text>\n" and a flush.
void LogLine(std::uintptr_t text) {
    if (LogStream* log = Log()) log->Put(Text(text))->PutChar('\n')->Apply(reinterpret_cast<void*>(kFlush));
}
// "<text><address>\n" and a flush; the address string is freed again.
void LogAddressLine(std::uintptr_t text, const void* address) {
    if (!Log()) return;
    StringRep s;
    AddressString(address, nullptr, &s);
    Log()->Put(Text(text))->Put(s.start)->PutChar('\n')->Apply(reinterpret_cast<void*>(kFlush));
    if (s.start) stlp::deallocate(s.start, static_cast<std::size_t>(s.eos - s.start));
}

float GameFloat(std::uintptr_t a) {
    float f;
    std::memcpy(&f, reinterpret_cast<const void*>(a), 4);
    return f;
}
void SetBits(float& f, std::uint32_t bits) { std::memcpy(&f, &bits, 4); }

bool SameAddress(const void* a, const void* b) { return std::memcmp(a, b, 16) == 0; }

SessionBase* Session(void* self) { return static_cast<SessionBase*>(self); }
SocketDriver* Socket(void* self) { return &At<SocketDriver>(self, kSocket); }
Node* Sentinel(void* self, std::size_t list) { return At<Node*>(self, list); }
QueryVector& Queries(void* self) { return At<QueryVector>(self, kQueries); }

// The servers list node whose entry has `address`, or the sentinel.
Node* FindServer(void* self, const void* address) {
    Node* end = Sentinel(self, kServers);
    Node* node = end->next;
    while (node != end && !SameAddress(ValueOf<EntryBase*>(node)->address, address)) node = node->next;
    return node;
}
// The query with `address`, or the end.
Query* FindQuery(QueryVector& v, const void* address) {
    Query* q = v.begin;
    while (q != v.end && !SameAddress(q->address, address)) ++q;
    return q;
}

// A write the way these functions inline it: in place (marking the buffer written) when it fits
// before `end`, otherwise through slot 4.
void Put(GameBuffer* b, const void* from, unsigned size) {
    if (b->cur + size > b->end) {
        ei::VCall<int>(b, kBufferWriteMore, from, size);
        return;
    }
    b->flags |= 1;
    std::memcpy(b->cur, from, size);
    b->cur += size;
}

// A cursor's end as inlined: the pending bits stored, then `p` committed to the sink if any.
void EndCursor(BufferCursor& c) {
    if (c.bits != 0) {
        c.bits = 0;
        if (c.bytePtr) *c.bytePtr = static_cast<std::uint8_t>(c.acc);
    }
    if (c.sink) {
        c.sink->cur = c.p;
        ei::VCall<int>(c.sink, kSinkCommit);
    }
}

// Hands `entry` (non-null) over to a new node at the end of the servers list: the reference the
// local pointer held is dropped without release (the copy-on-share mark cleared), and the list
// shares the raw pointer.
void AppendServer(void* self, RefPtr& entry) {
    CRefObject* raw = entry.ptr;
    --raw->refs_;
    reinterpret_cast<std::uint16_t*>(reinterpret_cast<std::uint8_t*>(raw) + 6)[0] &= 0xFFFE;
    entry.ptr = nullptr;
    RefPtr holder;
    HolderInitAt(&holder, raw);
    void* list = &At<void*>(self, kServers);
    Node* pos;
    ServersEnd(list, nullptr, &pos);
    Node* out;
    ServersInsert(list, nullptr, &out, pos, &holder);
}

} // namespace

// ---- Small members --------------------------------------------------------------------------------

// 004413E0: the server entry base's constructor body (the reference header is left alone).
EntryBase* __fastcall EntryConstructBase(EntryBase* self) {
    std::memset(self->address, 0, sizeof(self->address));
    self->vtable = reinterpret_cast<void*>(0x0073BB40);
    SetBits(self->timeout, 0);
    self->ping = *reinterpret_cast<const std::uint32_t*>(0x0073BFA0);
    self->foreign = 0;
    self->state = 0;
    return self;
}
EI_REPLACE(0x004413E0, EntryConstructBase);

// 00441410: the query with `address`, or the end.
Query* __fastcall QueriesFind(QueryVector* v, void*, const void* address) { return FindQuery(*v, address); }
EI_REPLACE(0x00441410, QueriesFind);

// 00441640: a buffer's length (its highest position brought up to date first).
std::int32_t __fastcall BufferLength(GameBuffer* b) {
    if (b->cur > b->high) b->high = b->cur;
    return b->base - static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(b->begin)) +
           static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(b->high));
}
EI_REPLACE(0x00441640, BufferLength);

// 00442570: BufferCursor::FlushBits (net_coder.cpp), at its own address.
EI_REPLACE_METHOD(0x00442570, BufferCursor::FlushBits);

// 00443770 / 00443790 / 00443E90 / 00444270: the query's constructor (a zero address and an empty
// growing buffer), destructor, deleting destructor and copy constructor.
Query* __fastcall QueryConstruct(Query* self) {
    std::memset(self->address, 0, sizeof(self->address));
    GrowingBufferConstruct(&self->reply);
    return self;
}
EI_REPLACE(0x00443770, QueryConstruct);
void __fastcall QueryDestruct(Query* self) { GrowingBufferDestruct(&self->reply); }
EI_REPLACE(0x00443790, QueryDestruct);
Query* __fastcall QueryDelete(Query* self, void*, unsigned flags) {
    GrowingBufferDestruct(&self->reply);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x00443E90, QueryDelete);
Query* __fastcall QueryCopy(Query* self, void*, const Query* from) {
    std::memcpy(self->address, from->address, sizeof(self->address));
    GameBufferCopy(&self->reply, nullptr, &from->reply);
    return self;
}
EI_REPLACE(0x00444270, QueryCopy);

// 00443E20 / 00443760: the temporary counted pointer handed to the list insert (set from a raw
// pointer without a reference; cleared when unwinding).
RefPtr* __fastcall HolderInit(RefPtr* self, CRefObject* raw) {
    self->ptr = raw;
    return self;
}
EI_REPLACE(0x00443E20, HolderInit);
void __fastcall HolderClear(RefPtr* self) { self->ptr = nullptr; }
EI_REPLACE(0x00443760, HolderClear);

// ---- Client: sending ------------------------------------------------------------------------------

// 00441660: a ping (3, the session time) to `address`.
void __fastcall ClientSendPing(void* self, void*, const void* address) {
    GameBuffer b;
    GrowingBufferConstruct(&b);
    std::uint8_t kind = 3;
    Put(&b, &kind, 1);
    float time = static_cast<float>(At<double>(self, kTime));
    Put(&b, &time, 4);
    SessionSend(Session(self), nullptr, address, &b);
    GrowingBufferDestruct(&b);
}
EI_REPLACE(0x00441660, ClientSendPing);

// 00441740: a join request (4, magic, join id, the send rate for the host, then `data`).
void __fastcall ClientSendJoin(void* self, void*, const void* address, GameBuffer* data) {
    std::uint32_t rate = SessionRateFor(Session(self), nullptr, address);
    GameBuffer b;
    GrowingBufferConstruct(&b);
    std::uint8_t kind = 4;
    Put(&b, &kind, 1);
    Put(&b, &At<std::uint32_t>(self, kMagic), 4);
    Put(&b, &At<std::uint32_t>(self, kJoinId), 4);
    Put(&b, &rate, 4);
    if (data->cur > data->high) data->high = data->cur;
    unsigned length = static_cast<unsigned>(data->base - static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(data->begin)) +
                                            static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(data->high)));
    Put(&b, data->begin, length);
    SessionSend(Session(self), nullptr, address, &b);
    GrowingBufferDestruct(&b);
}
EI_REPLACE(0x00441740, ClientSendJoin);

// ---- Client: packets (slots 3, 7, 8, 9) and the connection's loss (slot 2) ---------------------

// 004418F0 (slot 3): updates count only once joined.
bool __fastcall ClientOnUpdate(void* self, void*, const void* from, BufferCursor* cursor) {
    if (At<std::uint32_t>(self, kJoinState) != 2) {
        LogLine(0x0078405C);   // "Normal update when not in game"
        return false;
    }
    return SessionOnUpdate(Session(self), nullptr, from, cursor);
}
EI_REPLACE(0x004418F0, ClientOnUpdate);

// 00441940 (slot 8, packet 6): the host accepted the join.
void __fastcall ClientOnAccepted(void* self, void*, const void* from, BufferCursor* cursor) {
    std::uint32_t id;
    std::memcpy(&id, cursor->p, 4);
    cursor->p += 4;
    void* host = &At<std::uint8_t>(self, kHost);
    if (!SameAddress(host, from)) {
        LogAddressLine(0x00784100, from);
        return;
    }
    if (id != At<std::uint32_t>(self, kJoinId)) {
        LogLine(0x007840D0);
        return;
    }
    if (At<std::uint32_t>(self, kJoinState) == 1) {
        At<std::uint32_t>(self, kJoinState) = 2;
        std::uint32_t rate = SessionRateFor(Session(self), nullptr, host);
        SessionAddClient(Session(self), nullptr, static_cast<const std::uint8_t*>(host), id, static_cast<int>(rate));
        LogLine(0x007840BC);
        ei::VCall<void>(self, kSlotJoined);
    } else {
        LogAddressLine(0x0078408C, from);
    }
}
EI_REPLACE(0x00441940, ClientOnAccepted);

// 00441B40 (slot 9, packet 7): the host refused the join (a reason byte follows the id).
void __fastcall ClientOnRefused(void* self, void*, const void* from, BufferCursor* cursor) {
    std::uint8_t* p = cursor->p;
    std::uint32_t id;
    std::memcpy(&id, p, 4);
    p += 4;
    cursor->p = p;
    if (!SameAddress(&At<std::uint8_t>(self, kHost), from)) {
        LogAddressLine(0x00784194, from);
        return;
    }
    if (id != At<std::uint32_t>(self, kJoinId)) {
        LogAddressLine(0x0078415C, from);
        return;
    }
    if (At<std::uint32_t>(self, kJoinState) == 1) {
        At<std::uint8_t>(self, kRefusal) = *p;
        ++cursor->p;
        At<std::uint32_t>(self, kJoinState) = 0;
    } else {
        LogAddressLine(0x00784130, from);
    }
}
EI_REPLACE(0x00441B40, ClientOnRefused);

// 00441D40 (slot 7, packet 5): a server's description. Its magic must match in all but the low
// byte. A new entry (slot 16) reads itself from the packet and gets the sender's address, the
// ping (now - the echoed time) and when to look again: local servers and those asked directly
// soon, others from the list never; unknown servers neither local nor asked are ignored. A known
// server's entry is replaced (and its reply kept in its query, if asked); a new one is appended.
void __fastcall ClientOnInfo(void* self, void*, const void* from, BufferCursor* cursor) {
    RefPtr entry{nullptr};
    std::uint32_t echo, magic;
    cursor->ReadBytes(&echo, 4);
    cursor->ReadBytes(&magic, 4);
    if (((magic ^ At<std::uint32_t>(self, kMagic)) & 0xFFFFFF00u) != 0) {
        entry.Destroy();
        return;
    }
    Node* node = FindServer(self, from);
    ei::VCall<void>(self, kSlotCreateEntry, &entry);
    ei::VCall<void>(entry.ptr, kEntryRead, cursor);
    if (!entry.ptr) {
        entry.Destroy();
        return;
    }
    auto* e = reinterpret_cast<EntryBase*>(entry.ptr);
    std::memcpy(e->address, from, 16);
    e->foreign = echo != At<std::uint32_t>(self, kMagic);
    bool local = DriverIsLocalAt(Socket(self), nullptr, from);
    QueryVector& v = Queries(self);
    Query* end = v.end;
    Query* query = QueriesFindAt(&v, nullptr, from);
    if (local) {
        e->state = query != end ? 1 : 0;
        e->timeout = GameFloat(0x0073BF98);
    } else if (query != end) {
        e->state = 1;
        auto count = static_cast<std::uint32_t>(v.end - v.begin);
        e->timeout = static_cast<float>(static_cast<double>(count) * GameFloat(0x0073B86C) + GameFloat(0x0073BF98));
    } else {
        if (node == Sentinel(self, kServers)) {
            entry.Destroy();
            return;
        }
        e->state = 2;
        SetBits(e->timeout, kNever);
    }
    float echoed;
    std::memcpy(&echoed, &echo, 4);
    float ping = static_cast<float>(At<double>(self, kTime) - echoed);
    std::memcpy(&e->ping, &ping, 4);
    if (node == Sentinel(self, kServers)) {
        AppendServer(self, entry);
    } else {
        RefPtr& slot = ValueOf<RefPtr>(node);
        if (entry.ptr != slot.ptr) {
            slot.Destroy();
            slot.Share(entry.ptr);
        }
        Query* q = FindQuery(v, from);
        if (q != v.end) {
            BufferCursor c;
            c.Reset(nullptr, 0, 0);
            c.sink = nullptr;
            GameBuffer& r = q->reply;
            r.high = r.begin;
            r.cur = r.begin;
            r.flags = 6;
            c.BeginWrite(&r, 0x400);
            ei::VCall<void>(entry.ptr, kEntryWrite, &c);
            EndCursor(c);
        }
    }
    entry.Destroy();
}
EI_REPLACE(0x00441D40, ClientOnInfo);

// 004420E0 (slot 2): removes a connection; when none is left the connection to the host is lost.
void __fastcall ClientRemoveClient(void* self, void*, void* node) {
    if (node == At<void*>(self, kClients)) return;
    SessionRemoveClient(Session(self), nullptr, node);
    void* sentinel = At<void*>(self, kClients);
    if (*static_cast<void**>(sentinel) != sentinel) return;
    LogLine(0x007841C4);   // "CONNECTION LOST"
    At<std::uint32_t>(self, kJoinState) = 0;
}
EI_REPLACE(0x004420E0, ClientRemoveClient);

// ---- Client: the lobby's requests ---------------------------------------------------------------

// 00442180: pings every broadcast address on the game port (00783E30); then the first waiting
// address (taken off the list), or else the next query in turn (and the always-asked address is
// queued again when set).
void __fastcall ClientRefresh(void* self) {
    std::uint8_t address[16] = {};
    for (unsigned i = 0; DriverBroadcastAt(Socket(self), nullptr, address, i, *reinterpret_cast<const int*>(0x00783E30)); ++i)
        ClientSendPingAt(self, nullptr, address);
    void* waiting = &At<void*>(self, kWaiting);
    Node* sentinel = Sentinel(self, kWaiting);
    if (sentinel->next != sentinel) {
        Node* first = sentinel->next;
        std::uint8_t next[16];
        std::memcpy(next, &ValueOf<std::uint8_t>(first), 16);
        Node* out;
        WaitingErase(waiting, nullptr, &out, first);
        ClientSendPingAt(self, nullptr, next);
        return;
    }
    std::uint32_t& index = At<std::uint32_t>(self, kNextQuery);
    ++index;
    QueryVector& v = Queries(self);
    if (index >= static_cast<std::uint32_t>(v.end - v.begin)) index = 0;
    if (index < static_cast<std::uint32_t>(v.end - v.begin)) ClientSendPingAt(self, nullptr, v.begin[index].address);
    if (At<std::uint32_t>(self, kAlwaysAsk + 4) != 0) {
        Node* pos;
        WaitingEnd(waiting, nullptr, &pos);
        Node* out;
        WaitingInsert(waiting, nullptr, &out, pos, &At<std::uint8_t>(self, kAlwaysAsk));
    }
}
EI_REPLACE(0x00442180, ClientRefresh);

// 004422D0: sets the address asked on every refresh.
void __fastcall ClientSetAlwaysAsk(void* self, void*, const void* address) {
    std::memcpy(&At<std::uint8_t>(self, kAlwaysAsk), address, 16);
}
EI_REPLACE(0x004422D0, ClientSetAlwaysAsk);

// 00442300: takes over the queries (from the options): each server not yet listed is listed with
// an entry read from its saved reply (or an empty entry), as asked.
void __fastcall ClientSetQueries(void* self, void*, const QueryVector* from) {
    QueryVector& v = Queries(self);
    QueriesAssign(&v, nullptr, from);
    v.extra = from->extra;
    if (v.end == v.begin) return;
    for (unsigned i = 0; i < static_cast<unsigned>(v.end - v.begin); ++i) {
        Query* q = v.begin + i;
        if (FindServer(self, q->address) != Sentinel(self, kServers)) continue;
        RefPtr entry{nullptr};
        GameBuffer& r = q->reply;
        if (r.cur > r.high) r.high = r.cur;
        if (r.base - static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(r.begin)) +
                static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(r.high)) != 0) {
            BufferCursor c;
            c.Reset(nullptr, 0, 0);
            c.sink = nullptr;
            ei::VCall<int>(&r, kBufferSeek, 0);
            BufferUpdateHigh(&r);
            c.BeginRead(&r, r.base - static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(r.begin)) +
                                static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(r.high)));
            ei::VCall<void>(self, kSlotCreateEntry, &entry);
            ei::VCall<void>(entry.ptr, kEntryRead, &c);
            FlushBitsAt(&c);
            if (c.sink) {
                c.sink->cur = c.p;
                ei::VCall<int>(c.sink, kSinkCommit);
            }
        } else {
            ei::VCall<void>(self, kSlotCreateEntry, &entry);
        }
        if (!entry.ptr) {
            entry.Destroy();
            continue;
        }
        auto* e = reinterpret_cast<EntryBase*>(entry.ptr);
        e->state = 1;
        std::memcpy(e->address, q->address, 16);
        AppendServer(self, entry);
    }
}
EI_REPLACE(0x00442300, ClientSetQueries);

// 00442590: asks the server at `address` directly: a new query is added and pinged (3, the
// session time); the server is listed (an empty entry) or marked as asked.
void __fastcall ClientAsk(void* self, void*, const void* address) {
    QueryVector& v = Queries(self);
    if (FindQuery(v, address) == v.end) {
        if (v.end != v.eos) {
            QueryConstructAt(v.end);
            ++v.end;
        } else {
            Query temporary;
            QueryConstructDefaultAt(&temporary);
            QueriesInsertOverflow(&v, nullptr, v.end, &temporary, 1);
            GrowingBufferDestruct(&temporary.reply);
        }
        std::memcpy((v.end - 1)->address, address, 16);
        GameBuffer b;
        GrowingBufferConstruct(&b);
        std::uint8_t kind = 3;
        BufferWrite(&b, nullptr, &kind, 1);
        float time = static_cast<float>(At<double>(self, kTime));
        BufferWrite(&b, nullptr, &time, 4);
        SessionSend(Session(self), nullptr, address, &b);
        GrowingBufferDestruct(&b);
    }
    Node* node = FindServer(self, address);
    if (node != Sentinel(self, kServers)) {
        ValueOf<EntryBase*>(node)->state = 1;
        return;
    }
    RefPtr entry{nullptr};
    ei::VCall<void>(self, kSlotCreateEntry, &entry);
    if (entry.ptr) {
        auto* e = reinterpret_cast<EntryBase*>(entry.ptr);
        std::memcpy(e->address, address, 16);
        e->state = 1;
        AppendServer(self, entry);
    }
    if (entry.ptr) {
        if (entry.ptr->refs_ != 1) {
            entry.ptr->flags7_ |= CRefObject::kDetached;
            entry.ptr->Shutdown();
        }
        entry.ptr->Release();
    }
}
EI_REPLACE(0x00442590, ClientAsk);

// 004427F0: forgets the server at `address`: its query and every list entry with that address.
void __fastcall ClientForget(void* self, void*, const void* address) {
    QueryVector& v = Queries(self);
    Query* q = FindQuery(v, address);
    if (q != v.end) {
        if (q + 1 != v.end) QueriesCopy(q + 1, v.end, q, address, 0);
        --v.end;
        QueryDeleteAt(v.end, nullptr, 0);
    }
    void* list = &At<void*>(self, kServers);
    Node* node = Sentinel(self, kServers)->next;
    while (node != Sentinel(self, kServers)) {
        if (SameAddress(ValueOf<EntryBase*>(node)->address, address)) {
            Node* out;
            ServersErase(list, nullptr, &out, node);
            node = out;
        } else {
            node = node->next;
        }
    }
}
EI_REPLACE(0x004427F0, ClientForget);

// 004428A0: a server from the master server's list: listed (never checked again) unless known,
// and its address queued to be asked.
void __fastcall ClientAddServer(void* self, void*, EntryBase* entry) {
    if (FindServer(self, entry->address) == Sentinel(self, kServers)) {
        entry->state = 2;
        SetBits(entry->timeout, kNever);
        RefPtr holder;
        HolderInitAt(&holder, reinterpret_cast<CRefObject*>(entry));
        void* list = &At<void*>(self, kServers);
        Node* pos;
        ServersEnd(list, nullptr, &pos);
        Node* out;
        ServersInsert(list, nullptr, &out, pos, &holder);
    }
    void* waiting = &At<void*>(self, kWaiting);
    Node* pos;
    WaitingEnd(waiting, nullptr, &pos);
    Node* out;
    WaitingInsert(waiting, nullptr, &out, pos, entry->address);
}
EI_REPLACE(0x004428A0, ClientAddServer);

// 00442980: starts joining the host at `address` (unless already asking it) with `data` sent
// along: a new join id (rand() << 16 | GetTickCount() & 0xFFFF), the first retry at once.
void __fastcall ClientJoin(void* self, void*, const void* address, const GameBuffer* data) {
    if (At<std::uint32_t>(self, kJoinState) == 1 && SameAddress(address, &At<std::uint8_t>(self, kHost))) return;
    std::memcpy(&At<std::uint8_t>(self, kHost), address, 16);
    std::uint32_t id = static_cast<std::uint32_t>(gamecrt::rand()) << 16;
    using TickCount = std::uint32_t(__stdcall*)();
    id |= (*reinterpret_cast<TickCount const*>(0x0073B344))() & 0xFFFF;
    At<std::uint32_t>(self, kJoinId) = id;
    At<std::uint32_t>(self, kJoinState) = 1;
    At<std::uint32_t>(self, kRetryTimer) = 0;
    At<std::uint32_t>(self, kRetryInterval) = kFirstRetry;
    At<std::uint8_t>(self, kRefusal) = 0;
    GameBufferAssign(&At<GameBuffer>(self, kJoinData), nullptr, data);
}
EI_REPLACE(0x00442980, ClientJoin);

// 00442A40: the entry of the `index`th server found, or null.
EntryBase* __fastcall ClientServerAt(void* self, void*, int index) {
    Node* sentinel = Sentinel(self, kServers);
    Node* node = sentinel->next;
    for (int i = 0; i < index; ++i) {
        if (node == sentinel) return nullptr;
        node = node->next;
    }
    if (node == sentinel) return nullptr;
    return ValueOf<EntryBase*>(node);
}
EI_REPLACE(0x00442A40, ClientServerAt);

// 00442AD0 (slot 14): counts the servers' timeouts down; a server whose time ran out is dropped
// unless asked directly (then its ping is reset). Then the session's receive step, and while
// joining the request is repeated with a doubling interval until it passes the limit (0073BAA4).
void __fastcall ClientTick(void* self, void*, double dt) {
    void* list = &At<void*>(self, kServers);
    Node* node = Sentinel(self, kServers)->next;
    while (node != Sentinel(self, kServers)) {
        EntryBase* e = ValueOf<EntryBase*>(node);
        if (e->timeout > 0.0f) {
            e->timeout = static_cast<float>(static_cast<double>(e->timeout) - dt);
            e = ValueOf<EntryBase*>(node);
            if (!(e->timeout > 0.0f)) {
                QueryVector& v = Queries(self);
                if (v.begin == v.end || FindQuery(v, e->address) == v.end) {
                    Node* out;
                    ServersErase(list, nullptr, &out, node);
                    node = out;
                    continue;
                }
                e->ping = *reinterpret_cast<const std::uint32_t*>(0x0073BFA0);
            }
        }
        node = node->next;
    }
    SessionReceiveAt(self, nullptr, dt);
    if (At<std::uint32_t>(self, kJoinState) != 1) return;
    float& timer = At<float>(self, kRetryTimer);
    double left = static_cast<double>(timer) - dt;
    timer = static_cast<float>(left);
    if (left > 0.0) return;
    float& interval = At<float>(self, kRetryInterval);
    if (interval > GameFloat(0x0073BAA4)) {
        LogLine(0x007841F8);
        At<std::uint32_t>(self, kJoinState) = 0;
        return;
    }
    ClientSendJoinAt(self, nullptr, &At<std::uint8_t>(self, kHost), &At<GameBuffer>(self, kJoinData));
    interval = interval + interval;
    timer = interval;
}
EI_REPLACE(0x00442AD0, ClientTick);

// ---- Server base ------------------------------------------------------------------------------------

// 00442C60: answers a ping from `address` with the server's description (5, the echoed time,
// then through a cursor the magic and the entry).
void __fastcall ServerSendInfo(void* self, void*, const void* address, std::uint32_t echo) {
    GameBuffer b;
    GrowingBufferConstruct(&b);
    RefPtr entry{nullptr};
    ei::VCall<void>(self, kSlotDescribe, &entry);
    if (entry.ptr) {
        std::uint8_t kind = 5;
        if (b.cur + 1 <= b.end) {
            b.flags |= 1;
            BufferWriteRaw(&b, nullptr, &kind, 1);
        } else {
            ei::VCall<int>(&b, kBufferWriteMore, &kind, 1u);
        }
        if (b.cur + 4 <= b.end) {
            b.flags |= 1;
            BufferWriteRaw(&b, nullptr, &echo, 4);
        } else {
            ei::VCall<int>(&b, kBufferWriteMore, &echo, 4u);
        }
        BufferCursor c;
        c.p = nullptr;
        c.bits = 0;
        c.bytePtr = nullptr;
        c.sink = nullptr;
        c.BeginWrite(&b, 0x400);
        std::memcpy(c.p, &At<std::uint32_t>(self, kMagic), 4);
        c.p += 4;
        ei::VCall<void>(entry.ptr, kEntryWrite, &c);
        EndCursor(c);
        SessionSend(Session(self), nullptr, address, &b);
        entry.Destroy();
    }
    GrowingBufferDestruct(&b);
}
EI_REPLACE(0x00442C60, ServerSendInfo);

// 00443060 (slot 4, packet 3): a ping; the time it carries is echoed.
void __fastcall ServerOnPing(void* self, void*, const void* from, BufferCursor* cursor) {
    std::uint32_t echo;
    std::memcpy(&echo, cursor->p, 4);
    cursor->p += 4;
    ServerSendInfoAt(self, nullptr, from, echo);
}
EI_REPLACE(0x00443060, ServerOnPing);

// 00443050 (slot 16): admits everybody.
bool __fastcall ServerAdmitAll(void*, void*, std::uint32_t, const std::uint8_t**, std::uint8_t*) { return true; }
EI_REPLACE(0x00443050, ServerAdmitAll);

// 004434F0 (slot 3): an update from a peer that is not connected yet but was told to join
// becomes a connection (with the id and rate of its join), then counts.
bool __fastcall ServerOnUpdate(void* self, void*, const void* from, BufferCursor* cursor) {
    if (SessionOnUpdate(Session(self), nullptr, from, cursor)) return true;
    Node* sentinel = Sentinel(self, kServerJoins);
    Node* node = sentinel->next;
    while (node != sentinel && !SameAddress(&ValueOf<std::uint8_t>(node), from)) node = node->next;
    if (node == sentinel) return false;
    auto* join = &ValueOf<std::uint8_t>(node);
    SessionAddClient(Session(self), nullptr, join, *reinterpret_cast<std::uint32_t*>(join + 0x10),
                     *reinterpret_cast<int*>(join + 0x14));
    Node* out;
    JoinsErase(&At<void*>(self, kServerJoins), nullptr, &out, node);
    return SessionOnUpdate(Session(self), nullptr, from, cursor);
}
EI_REPLACE(0x004434F0, ServerOnUpdate);

// 00443590 (slot 14): ages the pending joins (dropping those older than 0073BF9C, logged), then
// the session's receive step.
void __fastcall ServerTick(void* self, void*, double dt) {
    void* list = &At<void*>(self, kServerJoins);
    Node* node = Sentinel(self, kServerJoins)->next;
    while (node != Sentinel(self, kServerJoins)) {
        float& age = *reinterpret_cast<float*>(&ValueOf<std::uint8_t>(node) + 0x18);
        double sum = static_cast<double>(age) + dt;
        age = static_cast<float>(sum);
        if (sum > static_cast<double>(GameFloat(0x0073BF9C))) {
            LogAddressLine(0x00784298, &ValueOf<std::uint8_t>(node));
            Node* out;
            JoinsErase(list, nullptr, &out, node);
            node = out;
        } else {
            node = node->next;
        }
    }
    SessionReceiveAt(self, nullptr, dt);
}
EI_REPLACE(0x00443590, ServerTick);

// ---- Saving the queries -----------------------------------------------------------------------

// 004414E0: the queries in a chunk archive (chunk 1 of chunks 1: field 1 the address, 2 the
// reply's length, 3 the reply), then field 2 the extra dword.
void __fastcall QueriesSerialize(QueryVector* v, void*, void* archive) {
    if (ArchiveEnter(archive, nullptr, 1)) {
        bool loading = At<std::uint8_t>(archive, 0x58) != 0;
        int n;
        if (loading) {
            QueriesErase(v, nullptr, v->begin, v->end);
            n = ArchiveCount(archive, nullptr, 1);
            QueriesResize(v, nullptr, static_cast<unsigned>(n));
        } else {
            n = static_cast<int>(v->end - v->begin);
        }
        for (int i = 0; i < n; ++i) {
            void* chunk = At<void*>(At<void*>(archive, 0x50), 4);
            At<int>(chunk, 0x14) = i + 1;
            Query* q = v->begin + i;
            if (!ArchiveEnter(archive, nullptr, 1)) continue;
            ArchiveField(archive, nullptr, 1, q->address, 0x10);
            std::uint32_t length;
            if (At<std::uint8_t>(archive, 0x58)) {
                ArchiveField(archive, nullptr, 2, &length, 4);
                GameBuffer& r = q->reply;
                r.cur = r.begin;
                r.high = r.begin + length;
                if (r.high > r.end) GameBufferGrow(&r, nullptr, length);
            } else {
                length = static_cast<std::uint32_t>(BufferLengthAt(&q->reply));
                ArchiveField(archive, nullptr, 2, &length, 4);
            }
            ArchiveField(archive, nullptr, 3, q->reply.begin, length);
            ArchiveLeave(archive);
        }
        ArchiveLeave(archive);
    }
    ArchiveField(archive, nullptr, 2, &v->extra, 4);
}
EI_REPLACE(0x004414E0, QueriesSerialize);

} // namespace net
