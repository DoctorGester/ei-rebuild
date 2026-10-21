// The client base's lobby (0073BE30): finding servers and joining one, and the server base's half
// (0073BBB4) that answers. See net_classes.h (ClientBase, ServerBase) for the fields.
// The packets (first byte): 3 ping (the session time), 4 join (magic, id, rate, extra data),
// 5 info (the echoed time, magic, the server entry), 6 accepted (id), 7 refused (id, reason).
// Plain float maths (rule 7). See docs/notes/network.md.
#include "chunk_archive.h"
#include "game_strings.h"
#include "core_refcount.h"
#include "net_classes.h"
#include "net_coder.h"
#include "lib/crt.h"
#include "lib/stlp.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/vcall.h"
#include "ei/x87.h"
#include "originals_script.h"
#include "originals_library.h"
#include "record_vectors.h"
#include "../hook/log.h"
#include "game_data.h"
#include "lib/nan_audit.h"

#include <cstdint>
#include <cstring>

namespace net {

RefPtr* __fastcall HolderInit(RefPtr*, CRefObject*);
namespace {

// The nodes of the servers list, the waiting addresses and the server's pending joins.
using ServerNode = stlp::SharedRefList::Node;
using AddressNode = stlp::list<NetAddress>::Node;
using JoinNode = stlp::list<PendingJoin>::Node;
ServerEntry* EntryOf(ServerNode* node) { return static_cast<ServerEntry*>(node->data.ptr); }

constexpr std::uint32_t kNever = 0x7E967699;   // 1e38f
constexpr std::uint32_t kFirstRetry = 0x3DCCCCCD;   // 0.1f

// Growing buffers.

// STLport containers (library): the query vector (00443E30 construct, 004439C0 insert overflow,
// 00443FF0 copy, 00443BF0 assign, 004442B0 erase, 00444090 resize; recordvec::QueryElement).
// The game's stlp::vector<Query> instantiation: 004439C0 _M_insert_overflow(pos, x, n), 00443BF0
// operator=, 004442B0 erase(first, last), 00444090 resize(n).
using QueryList = stlp::vector<Query>;


// "<text>\n" and a flush.
void LogLine(const char* text) {
    if (LogStream* log = NetLog()) log->Put(text)->PutChar('\n')->Flush();
}
// "<text><address>\n" and a flush; the address string is freed again.
void LogAddressLine(const char* text, const NetAddress* address) {
    if (!NetLog()) return;
    stlp::string s;
    address->ToString(&s);
    NetLog()->Put(text)->Put(s.data())->PutChar('\n')->Flush();
}

void SetBits(float& f, std::uint32_t bits) { std::memcpy(&f, &bits, 4); }

bool SameAddress(const NetAddress* a, const NetAddress* b) { return std::memcmp(a, b, sizeof(NetAddress)) == 0; }

// The servers list node whose entry has `address`, or the sentinel.
ServerNode* FindServer(ClientBase* self, const NetAddress* address) {
    ServerNode* end = self->servers_.End();
    ServerNode* node = end->next;
    while (node != end && !SameAddress(&EntryOf(node)->address_, address)) node = node->next;
    return node;
}
// The query with `address`, or the end.
Query* FindQuery(ClientQueries& v, const NetAddress* address) {
    Query* q = v.list.begin();
    while (q != v.list.end() && !SameAddress(&q->address, address)) ++q;
    return q;
}

// A write the way these functions inline it: in place (marking the buffer written) when it fits
// before `end`, otherwise through slot 4.
void Put(GameBuffer* b, const void* from, unsigned size) {
    if (b->cur + size > b->end) {
        b->WriteMore(from, size);
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
        c.sink->Commit();
    }
}

// Hands `entry` (non-null) over to a new node at the end of the servers list: the reference the
// local pointer held is dropped without release (the copy-on-share mark cleared), and the list
// shares the raw pointer.
void AppendServer(ClientBase* self, RefPtr& entry) {
    CRefObject* raw = entry.ptr;
    --raw->refs_;
    raw->flags6_ &= static_cast<std::uint8_t>(~CRefCounted::kCopyOnShare);   // the original ANDs the word +6 with 0xFFFE
    entry.ptr = nullptr;
    RefPtr holder;
    HolderInit(&holder, raw);
    self->servers_.Insert(self->servers_.End(), holder);
}

ServerEntry* EntryIn(const RefPtr& entry) { return static_cast<ServerEntry*>(entry.ptr); }

} // namespace

// ---- Small members --------------------------------------------------------------------------------

ServerEntryBase* ServerEntryBase::ConstructBase() {
    std::memset(&address_, 0, sizeof(address_));
    ei::SetVtable(this, kVtableAddress);
    SetBits(timeout_, 0);
    ping_ = 9.999f /*0073BFA0*/;
    foreign_ = 0;
    state_ = kLocal;
    return this;
}
EI_REPLACE_METHOD(0x004413E0, ServerEntryBase::ConstructBase);

// 00441410: the query with `address`, or the end.
Query* ClientQueries::QueriesFind(const NetAddress* address) { return FindQuery(*this, address); }

// 00441640: a buffer's length (its highest position brought up to date first).
std::int32_t __fastcall BufferLength(GameBuffer* b) {
    if (b->cur > b->high) b->high = b->cur;
    return b->base - static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(b->begin)) +
           static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(b->high));
}

// 00443770 / 00443790 / 00443E90 / 00444270: the query's constructor (a zero address and an empty
// growing buffer), destructor, deleting destructor and copy constructor.
Query* __fastcall QueryConstruct(Query* self) {
    std::memset(&self->address, 0, sizeof(self->address));
    static_cast<GameBuffer*>(&self->reply)->ConstructGrowing();
    return self;
}
void __fastcall QueryDestruct(Query* self) { GrowingBufferDestruct(&self->reply); }
EI_REPLACE(0x00443790, QueryDestruct);
Query* Query::QueryDelete(unsigned flags) {
    GrowingBufferDestruct(&reply);
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00443E90, Query::QueryDelete);
Query* Query::QueryCopy(const Query* from) {
    address = from->address;
    reply.ConstructCopy(&from->reply);
    return this;
}
EI_REPLACE_METHOD(0x00444270, Query::QueryCopy);

// 00443E20 / 00443760: the temporary counted pointer handed to the list insert (set from a raw
// pointer without a reference; cleared when unwinding).
RefPtr* __fastcall HolderInit(RefPtr* self, CRefObject* raw) {
    self->ptr = raw;
    return self;
}

// ---- Client: sending ------------------------------------------------------------------------------

void ClientBase::SendPing(const NetAddress* address) {
    GameBuffer b;
    static_cast<GameBuffer*>(&b)->ConstructGrowing();
    std::uint8_t kind = 3;
    Put(&b, &kind, 1);
    float time = static_cast<float>(time_);
    Put(&b, &time, 4);
    Send(address, &b);
    GrowingBufferDestruct(&b);
}
EI_REPLACE_METHOD(0x00441660, ClientBase::SendPing);

void ClientBase::SendJoin(const NetAddress* address, GameBuffer* data) {
    std::uint32_t rate = RateFor(address);
    GameBuffer b;
    static_cast<GameBuffer*>(&b)->ConstructGrowing();
    std::uint8_t kind = 4;
    Put(&b, &kind, 1);
    Put(&b, &magic_, 4);
    Put(&b, &joinId_, 4);
    Put(&b, &rate, 4);
    if (data->cur > data->high) data->high = data->cur;
    unsigned length = static_cast<unsigned>(data->base - static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(data->begin)) +
                                            static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(data->high)));
    Put(&b, data->begin, length);
    Send(address, &b);
    GrowingBufferDestruct(&b);
}
EI_REPLACE_METHOD(0x00441740, ClientBase::SendJoin);

// ---- Client: packets (slots 3, 7, 8, 9) and the connection's loss (slot 2) ---------------------

// 004418F0 (slot 3): updates count only once joined.
bool ClientBase::OnUpdateImpl(const NetAddress* from, BufferCursor* cursor) {
    if (joinState_ != kJoined) {
        LogLine("ERROR: Normal update when not in active state");
        return false;
    }
    return SessionBase::OnUpdateImpl(from, cursor);
}

// 00441940 (slot 8, packet 6): the host accepted the join.
void ClientBase::OnAcceptedImpl(const NetAddress* from, BufferCursor* cursor) {
    std::uint32_t id;
    std::memcpy(&id, cursor->p, 4);
    cursor->p += 4;
    if (!SameAddress(&host_, from)) {
        LogAddressLine("ERROR: Accepted msg received from unknown host ", from);
        return;
    }
    if (id != joinId_) {
        LogLine("ERROR: Accepted msg received with wrong linkID");
        return;
    }
    if (joinState_ == kAsking) {
        joinState_ = kJoined;
        std::uint32_t rate = RateFor(&host_);
        AddClient(&host_, id, static_cast<int>(rate));
        LogLine("Start connection");
        OnJoined();
    } else {
        LogAddressLine("ERROR: Accept for already started session from ", from);
    }
}

// 00441B40 (slot 9, packet 7): the host refused the join (a reason byte follows the id).
void ClientBase::OnRejectedImpl(const NetAddress* from, BufferCursor* cursor) {
    std::uint8_t* p = cursor->p;
    std::uint32_t id;
    std::memcpy(&id, p, 4);
    p += 4;
    cursor->p = p;
    if (!SameAddress(&host_, from)) {
        LogAddressLine("ERROR: Rejected msg received from unknown host ", from);
        return;
    }
    if (id != joinId_) {
        LogAddressLine("ERROR: Rejected msg received with wrong linkID from ", from);
        return;
    }
    if (joinState_ == kAsking) {
        refusal_ = *p;
        ++cursor->p;
        joinState_ = kNotJoined;
    } else {
        LogAddressLine("ERROR: Reject not during connecting from ", from);
    }
}

// 00441D40 (slot 7, packet 5): a server's description. Its magic must match in all but the low
// byte. A new entry (slot 16) reads itself from the packet and gets the sender's address, the
// ping (now - the echoed time) and when to look again: local servers and those asked directly
// soon, others from the list never; unknown servers neither local nor asked are ignored. A known
// server's entry is replaced (and its reply kept in its query, if asked); a new one is appended.
void ClientBase::OnInfoImpl(const NetAddress* from, BufferCursor* cursor) {
    RefPtr entry{nullptr};
    std::uint32_t echo, magic;
    cursor->ReadBytes(&echo, 4);
    cursor->ReadBytes(&magic, 4);
    if (((magic ^ magic_) & 0xFFFFFF00u) != 0) {
        entry.Destroy();
        return;
    }
    ServerNode* node = FindServer(this, from);
    CreateEntry(&entry);
    EntryIn(entry)->Read(cursor);
    if (!entry.ptr) {
        entry.Destroy();
        return;
    }
    ServerEntry* e = EntryIn(entry);
    e->address_ = *from;
    e->foreign_ = magic != magic_;   // another version (low byte differs): Connect refuses it
    bool local = socket_.IsLocal(from);
    ClientQueries& v = queries_;
    Query* end = v.list.end();
    Query* query = v.QueriesFind(from);
    if (local) {
        e->state_ = query != end ? ServerEntry::kAsked : ServerEntry::kLocal;
        e->timeout_ = 20.0f /*0073BF98*/;
    } else if (query != end) {
        e->state_ = ServerEntry::kAsked;
        auto count = static_cast<std::uint32_t>(v.list.size());
        e->timeout_ = static_cast<float>(x87::FromUnsigned(count) * 0.5f /*0073B86C*/ + 20.0f /*0073BF98*/);   // FILD qword
    } else {
        if (node == servers_.End()) {
            entry.Destroy();
            return;
        }
        e->state_ = ServerEntry::kFromList;
        SetBits(e->timeout_, kNever);
    }
    float echoed;
    std::memcpy(&echoed, &echo, 4);
    e->ping_ = static_cast<float>(time_ - echoed);
    if (node == servers_.End()) {
        AppendServer(this, entry);
    } else {
        RefPtr& slot = node->data;
        if (entry.ptr != slot.ptr) {
            slot.Destroy();
            slot.Share(entry.ptr);
        }
        Query* q = FindQuery(v, from);
        if (q != v.list.end()) {
            BufferCursor c;
            c.Reset(nullptr, 0, 0);
            c.sink = nullptr;
            GameBuffer& r = q->reply;
            r.high = r.begin;
            r.cur = r.begin;
            r.flags = 6;
            c.BeginWrite(&r, 0x400);
            EntryIn(entry)->Write(&c);
            EndCursor(c);
        }
    }
    entry.Destroy();
}

// 004420E0 (slot 2): removes a connection; when none is left the connection to the host is lost.
void ClientBase::RemoveClientImpl(ClientNode* node) {
    if (node == clients_) return;
    SessionBase::RemoveClientImpl(node);
    if (clients_->next != clients_) return;
    LogLine("CONNECTION LOST server link is lost, entering idle");
    ei::Log("net: the client lost its connection to the host");   // not the game's (N0c)
    joinState_ = kNotJoined;
}

EI_REPLACE_METHOD(0x004418F0, ClientBase::OnUpdateImpl);
EI_REPLACE_METHOD(0x00441940, ClientBase::OnAcceptedImpl);
EI_REPLACE_METHOD(0x00441B40, ClientBase::OnRejectedImpl);
EI_REPLACE_METHOD(0x00441D40, ClientBase::OnInfoImpl);
EI_REPLACE_METHOD(0x004420E0, ClientBase::RemoveClientImpl);

// ---- Client: the lobby's requests ---------------------------------------------------------------

// Pings go to the game port (00783E30).
void ClientBase::Refresh() {
    NetAddress address{};
    for (unsigned i = 0; socket_.Broadcast(&address, i, ei::GameData<const int>(0x00783E30)); ++i)
        SendPing(&address);
    AddressNode* sentinel = waiting_.End();
    if (sentinel->next != sentinel) {
        AddressNode* first = sentinel->next;
        NetAddress next = first->data;
        waiting_.Erase(first);
        SendPing(&next);
        return;
    }
    std::uint32_t& index = nextQuery_;
    ++index;
    QueryList& v = queries_.list;
    if (index >= static_cast<std::uint32_t>(v.size())) index = 0;
    if (index < static_cast<std::uint32_t>(v.size())) SendPing(&v[index].address);
    if (alwaysAsk_.ip != 0) {
        waiting_.Insert(waiting_.End(), alwaysAsk_);
    }
}
EI_REPLACE_METHOD(0x00442180, ClientBase::Refresh);

void ClientBase::SetAlwaysAsk(const NetAddress* address) { alwaysAsk_ = *address; }

// Each server not yet listed is listed with an entry read from its saved reply (or an empty
// entry), as asked.
void ClientBase::SetQueries(const ClientQueries* from) {
    QueryList& v = queries_.list;
    recordvec::QueryVector::Assign(&v, nullptr, &from->list);
    queries_.extra = from->extra;
    if (v.end() == v.begin()) return;
    for (unsigned i = 0; i < static_cast<unsigned>(v.size()); ++i) {
        Query* q = v.begin() + i;
        if (FindServer(this, &q->address) != servers_.End()) continue;
        RefPtr entry{nullptr};
        GameBuffer& r = q->reply;
        if (r.cur > r.high) r.high = r.cur;
        if (r.base - static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(r.begin)) +
                static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(r.high)) != 0) {
            BufferCursor c;
            c.Reset(nullptr, 0, 0);
            c.sink = nullptr;
            r.Seek(0);
            BufferUpdateHigh(&r);
            c.BeginRead(&r, r.base - static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(r.begin)) +
                                static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(r.high)));
            CreateEntry(&entry);
            EntryIn(entry)->Read(&c);
            c.FlushBits();
            if (c.sink) {
                c.sink->cur = c.p;
                c.sink->Commit();
            }
        } else {
            CreateEntry(&entry);
        }
        if (!entry.ptr) {
            entry.Destroy();
            continue;
        }
        ServerEntry* e = EntryIn(entry);
        e->state_ = ServerEntry::kAsked;
        e->address_ = q->address;
        AppendServer(this, entry);
    }
}
EI_REPLACE_METHOD(0x00442300, ClientBase::SetQueries);

// A new query is added and pinged (3, the session time); the server is listed (an empty entry) or
// marked as asked.
void ClientBase::Ask(const NetAddress* address) {
    QueryList& v = queries_.list;
    if (FindQuery(queries_, address) == v.end()) {
        // push_back of a default query, inlined.
        if (v.end() != v.EndOfStorage()) {
            stlp::ConstructDefaultAt<Query, recordvec::QueryElement>(v.end());
            ++v.Finish();
        } else {
            Query temporary;
            QueryConstruct(&temporary);
            v.InsertOverflow<recordvec::QueryElement>(v.end(), temporary, 1);
            GrowingBufferDestruct(&temporary.reply);
        }
        v.back().address = *address;
        GameBuffer b;
        static_cast<GameBuffer*>(&b)->ConstructGrowing();
        std::uint8_t kind = 3;
        b.BufferWrite(&kind, 1);
        float time = static_cast<float>(time_);
        b.BufferWrite(&time, 4);
        Send(address, &b);
        GrowingBufferDestruct(&b);
    }
    ServerNode* node = FindServer(this, address);
    if (node != servers_.End()) {
        EntryOf(node)->state_ = ServerEntry::kAsked;
        return;
    }
    RefPtr entry{nullptr};
    CreateEntry(&entry);
    if (entry.ptr) {
        ServerEntry* e = EntryIn(entry);
        e->address_ = *address;
        e->state_ = ServerEntry::kAsked;
        AppendServer(this, entry);
    }
    if (entry.ptr) {
        if (entry.ptr->refs_ != 1) {
            entry.ptr->flags7_ |= CRefObject::kDetached;
            entry.ptr->Shutdown();
        }
        entry.ptr->Release();
    }
}
EI_REPLACE_METHOD(0x00442590, ClientBase::Ask);

void ClientBase::Forget(const NetAddress* address) {
    QueryList& v = queries_.list;
    Query* q = FindQuery(queries_, address);
    if (q != v.end()) {
        // erase(q), inlined: the tail copied down, the last query destroyed.
        if (q + 1 != v.end()) stlp::CopyAssignTagged<Query, recordvec::QueryElement>(q + 1, v.end(), q, address, nullptr);
        --v.Finish();
        v.end()->QueryDelete(0);
    }
    ServerNode* node = servers_.End()->next;
    while (node != servers_.End()) {
        if (SameAddress(&EntryOf(node)->address_, address)) {
            node = servers_.Erase(node);
        } else {
            node = node->next;
        }
    }
}
EI_REPLACE_METHOD(0x004427F0, ClientBase::Forget);

// Listed (never checked again) unless known, and its address queued to be asked.
void ClientBase::AddServer(ServerEntry* entry) {
    if (FindServer(this, &entry->address_) == servers_.End()) {
        entry->state_ = ServerEntry::kFromList;
        SetBits(entry->timeout_, kNever);
        RefPtr holder;
        HolderInit(&holder, entry);
        servers_.Insert(servers_.End(), holder);
    }
    waiting_.Insert(waiting_.End(), entry->address_);
}

// Unless already asking that host: a new join id (rand() << 16 | GetTickCount() & 0xFFFF), the
// first retry at once.
void ClientBase::Join(const NetAddress* address, const GameBuffer* data) {
    if (joinState_ == kAsking && SameAddress(address, &host_)) return;
    host_ = *address;
    std::uint32_t id = static_cast<std::uint32_t>(gamecrt::rand()) << 16;
    using TickCount = std::uint32_t(__stdcall*)();
    id |= (ei::GameData<TickCount const>(0x0073B344))() & 0xFFFF;
    joinId_ = id;
    joinState_ = kAsking;
    SetBits(retryTimer_, 0);
    SetBits(retryInterval_, kFirstRetry);
    refusal_ = 0;
    joinData_.AssignFrom(data);
}
EI_REPLACE_METHOD(0x00442980, ClientBase::Join);

ServerEntry* ClientBase::ServerAt(int index) {
    ServerNode* sentinel = servers_.End();
    ServerNode* node = sentinel->next;
    for (int i = 0; i < index; ++i) {
        if (node == sentinel) return nullptr;
        node = node->next;
    }
    if (node == sentinel) return nullptr;
    return EntryOf(node);
}

// 00442AD0 (slot 14): counts the servers' timeouts down; a server whose time ran out is dropped
// unless asked directly (then its ping is reset). Then the session's receive step, and while
// joining the request is repeated with a doubling interval until it passes the limit (0073BAA4).
void ClientBase::TickImpl(double dt) {
    ServerNode* node = servers_.End()->next;
    while (node != servers_.End()) {
        ServerEntry* e = EntryOf(node);
        if (e->timeout_ > 0.0f) {
            e->timeout_ = static_cast<float>(static_cast<double>(e->timeout_) - dt);
            e = EntryOf(node);
            EI_NAN_AUDIT(e->timeout_);
            if (e->timeout_ <= 0.0f) {
                ClientQueries& v = queries_;
                if (v.list.begin() == v.list.end() || FindQuery(v, &e->address_) == v.list.end()) {
                    node = servers_.Erase(node);
                    continue;
                }
                e->ping_ = 9.999f /*0073BFA0*/;
            }
        }
        node = node->next;
    }
    SessionBase::TickImpl(dt);   // 00437370, the receive step
    if (joinState_ != kAsking) return;
    double left = static_cast<double>(retryTimer_) - dt;
    retryTimer_ = static_cast<float>(left);
    if (left > 0.0) return;
    if (retryInterval_ > 3.0f /*0073BAA4*/) {
        LogLine("Connection failed");
        joinState_ = kNotJoined;
        return;
    }
    SendJoin(&host_, &joinData_);
    retryInterval_ = retryInterval_ + retryInterval_;
    retryTimer_ = retryInterval_;
}
EI_REPLACE_METHOD(0x00442AD0, ClientBase::TickImpl);

// ---- Server base ------------------------------------------------------------------------------------

// The magic and the entry go through a cursor.
void ServerBase::SendInfo(const NetAddress* address, std::uint32_t echo) {
    GameBuffer b;
    GameBuffer* buffer = &b;   // slot 4 through the vtable
    static_cast<GameBuffer*>(&b)->ConstructGrowing();
    RefPtr entry{nullptr};
    Describe(&entry);
    if (entry.ptr) {
        std::uint8_t kind = 5;
        if (b.cur + 1 <= b.end) {
            b.flags |= 1;
            b.BufferWriteRaw(&kind, 1);
        } else {
            buffer->WriteMore(&kind, 1u);
        }
        if (b.cur + 4 <= b.end) {
            b.flags |= 1;
            b.BufferWriteRaw(&echo, 4);
        } else {
            buffer->WriteMore(&echo, 4u);
        }
        BufferCursor c;
        c.p = nullptr;
        c.bits = 0;
        c.bytePtr = nullptr;
        c.sink = nullptr;
        c.BeginWrite(&b, 0x400);
        std::memcpy(c.p, &magic_, 4);
        c.p += 4;
        EntryIn(entry)->Write(&c);
        EndCursor(c);
        Send(address, &b);
        entry.Destroy();
    }
    GrowingBufferDestruct(&b);
}
EI_REPLACE_METHOD(0x00442C60, ServerBase::SendInfo);

// 00443060 (slot 4, packet 3): a ping; the time it carries is echoed.
void ServerBase::OnGetInfoImpl(const NetAddress* from, BufferCursor* cursor) {
    std::uint32_t echo;
    std::memcpy(&echo, cursor->p, 4);
    cursor->p += 4;
    SendInfo(from, echo);
}

// 00443050 (slot 16): admits everybody.
bool ServerBase::AdmitImpl(std::uint32_t, const std::uint8_t**, std::uint8_t*) { return true; }

// 004434F0 (slot 3): an update from a peer that is not connected yet but was told to join
// becomes a connection (with the id and rate of its join), then counts.
bool ServerBase::OnUpdateImpl(const NetAddress* from, BufferCursor* cursor) {
    if (SessionBase::OnUpdateImpl(from, cursor)) return true;
    JoinNode* sentinel = joins_.End();
    JoinNode* node = sentinel->next;
    while (node != sentinel && !SameAddress(&node->data.address, from)) node = node->next;
    if (node == sentinel) return false;
    PendingJoin& join = node->data;
    AddClient(&join.address, join.id, join.rate);
    joins_.Erase(node);
    return SessionBase::OnUpdateImpl(from, cursor);
}

// 00443590 (slot 14): ages the pending joins (dropping those older than 0073BF9C, logged), then
// the session's receive step.
void ServerBase::TickImpl(double dt) {
    JoinNode* node = joins_.End()->next;
    while (node != joins_.End()) {
        float& age = node->data.age;
        double sum = static_cast<double>(age) + dt;
        age = static_cast<float>(sum);
        if (sum > static_cast<double>(60.0f /*0073BF9C*/)) {
            LogAddressLine("Accept period elapsed for ", &node->data.address);
            node = joins_.Erase(node);
        } else {
            node = node->next;
        }
    }
    SessionBase::TickImpl(dt);   // 00437370, the receive step
}

namespace {
// "Accepted" (packet 6) with the join's id, to `to`.
void SendAccepted(ServerBase* self, const NetAddress* to, std::uint32_t id) {
    GameBuffer b;
    static_cast<GameBuffer*>(&b)->ConstructGrowing();
    std::uint8_t kind = 6;
    b.BufferWrite(&kind, 1);
    b.BufferWrite(&id, 4);
    self->Send(to, &b);
    GrowingBufferDestruct(&b);
}
} // namespace

// 00443080 (slot 5, packet 4): a login. The magic must be the session's; the id and the rate
// follow (the rate is cut to what the session allows for the address, compared signed). A peer
// that is connected already is ignored. One that was told to join before is told again (its id
// and rate replaced). Anybody else is asked about (slot 16) and either put on the joins list and
// accepted, or refused with the reason slot 16 gave.
//
// The original builds the new join on the stack and never sets its age (+0x18): the list gets
// whatever the stack held, so the time the join may wait for its first update (ServerBase::Tick)
// is not defined. Rebirth's FixProcessLogin (a jump at 004433D6 to a stub that clears it) is what
// this body always does: garbage cannot be reproduced, so the flag changes nothing here.
void ServerBase::OnLoginImpl(const NetAddress* from, BufferCursor* cursor) {
    std::uint32_t magic;
    cursor->ReadBytes(&magic, 4);
    if (magic != magic_) return;
    std::uint32_t id;
    cursor->ReadBytes(&id, 4);
    std::int32_t rate;
    cursor->ReadBytes(&rate, 4);
    const std::int32_t limit = static_cast<std::int32_t>(RateFor(from));
    if (rate >= limit) rate = limit;

    ClientNode* client;
    FindClient(&client, from);
    if (client != clients_) {
        LogAddressLine("ERROR: Ignoring login request for active session from ", from);
        return;
    }

    JoinNode* sentinel = joins_.End();
    JoinNode* node = sentinel->next;
    while (node != sentinel && !SameAddress(&node->data.address, from)) node = node->next;
    if (node != sentinel) {
        PendingJoin& join = node->data;
        if (id != join.id) {
            join.id = id;
            LogAddressLine("WARNING: LinkID mismatch from ", from);
        }
        join.rate = rate;
        LogAddressLine("Reaccepting client from ", from);
        SendAccepted(this, from, id);
        return;
    }

    std::uint8_t reason;
    if (Admit(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(from)), reinterpret_cast<const std::uint8_t**>(cursor), &reason)) {
        LogAddressLine("Accepting client from ", from);
        PendingJoin join;
        join.address = *from;
        join.id = id;
        join.rate = rate;
        join.age = 0.0f;
        joins_.Insert(joins_.End(), join);
        SendAccepted(this, from, id);
        return;
    }

    GameBuffer b;
    static_cast<GameBuffer*>(&b)->ConstructGrowing();
    std::uint8_t kind = 7;
    b.BufferWrite(&kind, 1);
    b.BufferWrite(&id, 4);
    b.BufferWrite(&reason, 1);
    Send(from, &b);
    GrowingBufferDestruct(&b);
}

EI_REPLACE_METHOD(0x00443080, ServerBase::OnLoginImpl);
EI_REPLACE_METHOD(0x00443060, ServerBase::OnGetInfoImpl);
EI_REPLACE_METHOD(0x00443050, ServerBase::AdmitImpl);
EI_REPLACE_METHOD(0x004434F0, ServerBase::OnUpdateImpl);
EI_REPLACE_METHOD(0x00443590, ServerBase::TickImpl);

// ---- Saving the queries -----------------------------------------------------------------------

// 004414E0: the queries in a chunk archive (chunk 1 of chunks 1: field 1 the address, 2 the
// reply's length, 3 the reply), then field 2 the extra dword.
void ClientQueries::Serialize(ChunkArchive* archive) {
    if (archive->EnterChunk(1)) {
        bool loading = archive->loading_ != 0;
        int n;
        if (loading) {
            list.EraseRange<recordvec::QueryElement>(list.begin(), list.end());
            n = archive->CountChunks(1);
            recordvec::QueryVector::ResizeDefault(&list, nullptr, static_cast<unsigned>(n));
        } else {
            n = static_cast<int>(list.size());
        }
        for (int i = 0; i < n; ++i) {
            archive->Current()->occurrence = i + 1;
            Query* q = list.begin() + i;
            if (!archive->EnterChunk(1)) continue;
            archive->Field(1, &q->address, 0x10);
            std::uint32_t length;
            if (archive->loading_) {
                archive->Field(2, &length, 4);
                GameBuffer& r = q->reply;
                r.cur = r.begin;
                r.high = r.begin + length;
                if (r.high > r.end) static_cast<GameBuffer*>(&r)->Grow(length);
            } else {
                length = static_cast<std::uint32_t>(BufferLength(&q->reply));
                archive->Field(2, &length, 4);
            }
            archive->Field(3, q->reply.begin, length);
            archive->LeaveChunk();
        }
        archive->LeaveChunk();
    }
    archive->Field(2, &extra, 4);
}
EI_REPLACE_METHOD(0x004414E0, ClientQueries::Serialize);

} // namespace net
