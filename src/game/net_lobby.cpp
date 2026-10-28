// The client base's lobby: finding servers and joining one, and the server base's half that answers. See
// net_classes.h (ClientBase, ServerBase) for the fields. The packets (first byte): 3 ping (the session time),
// 4 join (magic, id, rate, extra data), 5 info (the echoed time, magic, the server entry), 6 accepted (id), 7
// refused (id, reason). Plain float maths (rule 7). See docs/notes/network.md.
#include "console.h"
#include "chunk_archive.h"
#include "game_strings.h"
#include "core_refcount.h"
#include "net_classes.h"
#include "net_messages.h"
#include "net_coder.h"
#include "lib/crt.h"
#include "lib/stlp.h"

#include "ei/rebirth.h"
#include "ei/x87.h"
#include "../hook/log.h"
#include "game_data.h"
#include "lib/gameapi.h"
#include "lib/nan_audit.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <list>

namespace net {

RefPtr* HolderInit(RefPtr*, CRefObject*);
namespace {

// The nodes of the servers list, the waiting addresses and the server's pending joins.
using ServerNode = stlp::SharedRefList::Node;
using JoinNode = std::list<PendingJoin>::iterator;
ServerEntry* EntryOf(ServerNode* node) { return stlp::SharedRefListOf<ServerEntry>::At(node); }

constexpr std::uint32_t kNever = 0x7E967699;   // 1e38f
constexpr std::uint32_t kFirstRetry = 0x3DCCCCCD;   // 0.1f

using QueryList = std::vector<Query>;


// "<text>\n" and a flush.
void LogLine(const char* text) {
    if (console::Stream* log = g_netLog) console::EndLine(*log << text);
}
// "<text><address>\n" and a flush; the address string is freed again.
void LogAddressLine(const char* text, const NetAddress* address) {
    if (!g_netLog) return;
    std::string s = address->ToString();
    console::EndLine(*g_netLog << text << s.data());
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
QueryList::iterator FindQuery(ClientQueries& v, const NetAddress* address) {
    return std::find_if(v.list.begin(), v.list.end(), [address](const Query& q) { return SameAddress(&q.address, address); });
}

// A write the way these functions inline it: in place (marking the buffer written) when it fits
// before `end`, otherwise through WriteMore.
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

// Hands `entry` (non-null) over to a new node at the end of the servers list: the reference the local pointer
// held is dropped without release (the copy-on-share mark cleared), and the list shares the raw pointer.
void AppendServer(ClientBase* self, RefPtr& entry) {
    CRefObject* raw = entry.ptr;
    --raw->refs_;
    raw->flags6_ &= static_cast<std::uint8_t>(~CRefCounted::kCopyOnShare);
    entry.ptr = nullptr;
    RefPtr holder;
    HolderInit(&holder, raw);
    self->servers_.Insert(self->servers_.End(), holder);
}

} // namespace

// ---- Small members --------------------------------------------------------------------------------

ServerEntryBase::ServerEntryBase() {
    std::memset(&address_, 0, sizeof(address_));
    SetBits(timeout_, 0);
    ping_ = 9.999f /*0073BFA0*/;
    foreign_ = 0;
    state_ = kLocal;
}

QueryList::iterator ClientQueries::QueriesFind(const NetAddress* address) { return FindQuery(*this, address); }

// A buffer's length (its highest position brought up to date first).
std::int32_t BufferLength(GameBuffer* b) {
    if (b->cur > b->high) b->high = b->cur;
    return b->PositionOf(b->high);
}

Query::Query() { std::memset(&address, 0, sizeof(address)); }
Query::~Query() {}
Query::Query(const Query& from) : address(from.address), reply(from.reply) {}

// The temporary counted pointer handed to the list insert (set from a raw
// pointer without a reference; cleared when unwinding).
RefPtr* HolderInit(RefPtr* self, CRefObject* raw) {
    self->ptr = raw;
    return self;
}

// ---- Client: sending ------------------------------------------------------------------------------

void ClientBase::SendPing(const NetAddress* address) {
    GrowingBuffer b;
    std::uint8_t kind = 3;
    Put(&b, &kind, 1);
    float time = static_cast<float>(time_);
    Put(&b, &time, 4);
    Send(address, &b);
}

void ClientBase::SendJoin(const NetAddress* address, GameBuffer* data) {
    std::uint32_t rate = RateFor(address);
    GrowingBuffer b;
    std::uint8_t kind = 4;
    Put(&b, &kind, 1);
    Put(&b, &magic_, 4);
    Put(&b, &joinId_, 4);
    Put(&b, &rate, 4);
    if (data->cur > data->high) data->high = data->cur;
    unsigned length = static_cast<unsigned>(data->PositionOf(data->high));
    Put(&b, data->begin, length);
    Send(address, &b);
}

// ---- Client: packets and the connection's loss ---------------------------------------

// Updates count only once joined.
bool ClientBase::OnUpdate(const NetAddress* from, BufferCursor* cursor) {
    if (joinState_ != kJoined) {
        LogLine("ERROR: Normal update when not in active state");
        return false;
    }
    return SessionBase::OnUpdate(from, cursor);
}

void ClientBase::OnAccepted(const NetAddress* from, BufferCursor* cursor) {
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

// A reason byte follows the join id.
void ClientBase::OnRejected(const NetAddress* from, BufferCursor* cursor) {
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

// Its magic must match in all but the low byte. A new entry reads itself from the packet and gets the sender's
// address, the ping (now - the echoed time) and when to look again: local servers and those asked directly soon,
// others from the list never; unknown servers neither local nor asked are ignored. A known server's entry is
// replaced (and its reply kept in its query, if asked); a new one is appended.
void ClientBase::OnInfo(const NetAddress* from, BufferCursor* cursor) {
    RefPtrTo<ServerEntry> entry{nullptr};
    std::uint32_t echo, magic;
    cursor->ReadBytes(&echo, 4);
    cursor->ReadBytes(&magic, 4);
    if (((magic ^ magic_) & 0xFFFFFF00u) != 0) {
        entry.Destroy();
        return;
    }
    ServerNode* node = FindServer(this, from);
    CreateEntry(&entry);
    entry.Get()->Read(cursor);
    if (!entry.ptr) {
        entry.Destroy();
        return;
    }
    ServerEntry* e = entry.Get();
    e->address_ = *from;
    e->foreign_ = magic != magic_;   // another version (low byte differs): Connect refuses it
    bool local = socket_.IsLocal(from);
    ClientQueries& v = queries_;
    auto end = v.list.end();
    auto query = v.QueriesFind(from);
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
        auto q = FindQuery(v, from);
        if (q != v.list.end()) {
            BufferCursor c;
            c.Reset(nullptr, 0, 0);
            c.sink = nullptr;
            GameBuffer& r = q->reply;
            r.high = r.begin;
            r.cur = r.begin;
            r.flags = 6;
            c.BeginWrite(&r, 0x400);
            entry.Get()->Write(&c);
            EndCursor(c);
        }
    }
    entry.Destroy();
}

// Removes a connection; when none is left the connection to the host is lost.
void ClientBase::RemoveClient(ClientNode* node) {
    if (node == clients_) return;
    SessionBase::RemoveClient(node);
    if (clients_->next != clients_) return;
    LogLine("CONNECTION LOST server link is lost, entering idle");
    ei::Log("net: the client lost its connection to the host");   // not the game's
    joinState_ = kNotJoined;
}

// ---- Client: the lobby's requests ---------------------------------------------------------------

// Pings go to the game port (net::g_gamePort).
void ClientBase::Refresh() {
    NetAddress address{};
    for (unsigned i = 0; socket_.Broadcast(&address, i, g_gamePort); ++i)
        SendPing(&address);
    if (!waiting_.empty()) {
        NetAddress next = waiting_.front();
        waiting_.pop_front();
        SendPing(&next);
        return;
    }
    std::uint32_t& index = nextQuery_;
    ++index;
    QueryList& v = queries_.list;
    if (index >= static_cast<std::uint32_t>(v.size())) index = 0;
    if (index < static_cast<std::uint32_t>(v.size())) SendPing(&v[index].address);
    if (alwaysAsk_.ip != 0) {
        waiting_.push_back(alwaysAsk_);
    }
}

void ClientBase::SetAlwaysAsk(const NetAddress* address) { alwaysAsk_ = *address; }

// Each server not yet listed is listed with an entry read from its saved reply (or an empty entry), as asked.
void ClientBase::SetQueries(const ClientQueries* from) {
    QueryList& v = queries_.list;
    v = from->list;
    queries_.extra = from->extra;
    if (v.empty()) return;
    for (unsigned i = 0; i < static_cast<unsigned>(v.size()); ++i) {
        Query* q = &v[i];
        if (FindServer(this, &q->address) != servers_.End()) continue;
        RefPtrTo<ServerEntry> entry{nullptr};
        GameBuffer& r = q->reply;
        if (r.cur > r.high) r.high = r.cur;
        if (r.PositionOf(r.high) != 0) {
            BufferCursor c;
            c.Reset(nullptr, 0, 0);
            c.sink = nullptr;
            r.Seek(0);
            BufferUpdateHigh(&r);
            c.BeginRead(&r, r.PositionOf(r.high));
            CreateEntry(&entry);
            entry.Get()->Read(&c);
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
        ServerEntry* e = entry.Get();
        e->state_ = ServerEntry::kAsked;
        e->address_ = q->address;
        AppendServer(this, entry);
    }
}

// A new query is added and pinged (3, the session time); the server is listed (an empty entry) or
// marked as asked.
void ClientBase::Ask(const NetAddress* address) {
    QueryList& v = queries_.list;
    if (FindQuery(queries_, address) == v.end()) {
        v.emplace_back().address = *address;
        GrowingBuffer b;
        std::uint8_t kind = 3;
        b.BufferWrite(&kind, 1);
        float time = static_cast<float>(time_);
        b.BufferWrite(&time, 4);
        Send(address, &b);
    }
    ServerNode* node = FindServer(this, address);
    if (node != servers_.End()) {
        EntryOf(node)->state_ = ServerEntry::kAsked;
        return;
    }
    RefPtrTo<ServerEntry> entry{nullptr};
    CreateEntry(&entry);
    if (entry.ptr) {
        ServerEntry* e = entry.Get();
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

void ClientBase::Forget(const NetAddress* address) {
    QueryList& v = queries_.list;
    auto q = FindQuery(queries_, address);
    if (q != v.end()) v.erase(q);
    ServerNode* node = servers_.End()->next;
    while (node != servers_.End()) {
        if (SameAddress(&EntryOf(node)->address_, address)) {
            node = servers_.Erase(node);
        } else {
            node = node->next;
        }
    }
}

// Listed (never checked again) unless known, and its address queued to be asked.
void ClientBase::AddServer(ServerEntry* entry) {
    if (FindServer(this, &entry->address_) == servers_.End()) {
        entry->state_ = ServerEntry::kFromList;
        SetBits(entry->timeout_, kNever);
        RefPtr holder;
        HolderInit(&holder, entry);
        servers_.Insert(servers_.End(), holder);
    }
    waiting_.push_back(entry->address_);
}

// Unless already asking that host: a new join id (rand() << 16 | GetTickCount() & 0xFFFF), the
// first retry at once.
void ClientBase::Join(const NetAddress* address, const GrowingBuffer* data) {
    if (joinState_ == kAsking && SameAddress(address, &host_)) return;
    host_ = *address;
    std::uint32_t id = static_cast<std::uint32_t>(gamecrt::rand()) << 16;
    id |= gameapi::GetTickCount() & 0xFFFF;
    joinId_ = id;
    joinState_ = kAsking;
    SetBits(retryTimer_, 0);
    SetBits(retryInterval_, kFirstRetry);
    refusal_ = 0;
    joinData_ = *data;
}

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

// Counts the servers' timeouts down; a server whose time ran out is dropped
// unless asked directly (then its ping is reset). Then the session's receive step, and while
// joining the request is repeated with a doubling interval until it passes the limit.
void ClientBase::Tick(double dt) {
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
    SessionBase::Tick(dt);
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

// ---- Server base ------------------------------------------------------------------------------------

void ServerBase::SendInfo(const NetAddress* address, std::uint32_t echo) {
    GrowingBuffer b;
    GameBuffer* buffer = &b;   // writes go through the vtable
    RefPtrTo<ServerEntry> entry{nullptr};
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
        entry.Get()->Write(&c);
        EndCursor(c);
        Send(address, &b);
        entry.Destroy();
    }
}

void ServerBase::OnGetInfo(const NetAddress* from, BufferCursor* cursor) {
    std::uint32_t echo;
    std::memcpy(&echo, cursor->p, 4);
    cursor->p += 4;
    SendInfo(from, echo);
}

bool ServerBase::Admit(const NetAddress*, std::uint8_t**, std::uint8_t*) { return true; }

// An update from a peer that is not connected yet but was told to join
// becomes a connection (with the id and rate of its join), then counts.
bool ServerBase::OnUpdate(const NetAddress* from, BufferCursor* cursor) {
    if (SessionBase::OnUpdate(from, cursor)) return true;
    JoinNode node = joins_.begin();
    while (node != joins_.end() && !SameAddress(&node->address, from)) ++node;
    if (node == joins_.end()) return false;
    PendingJoin& join = *node;
    AddClient(&join.address, join.id, join.rate);
    joins_.erase(node);
    return SessionBase::OnUpdate(from, cursor);
}

// Ages the pending joins (dropping those older than the join timeout, logged), then
// the session's receive step.
void ServerBase::Tick(double dt) {
    JoinNode node = joins_.begin();
    while (node != joins_.end()) {
        float& age = node->age;
        double sum = static_cast<double>(age) + dt;
        age = static_cast<float>(sum);
        if (sum > static_cast<double>(60.0f /*0073BF9C*/)) {
            LogAddressLine("Accept period elapsed for ", &node->address);
            node = joins_.erase(node);
        } else {
            ++node;
        }
    }
    SessionBase::Tick(dt);
}

namespace {
// "Accepted" (packet 6) with the join's id, to `to`.
void SendAccepted(ServerBase* self, const NetAddress* to, std::uint32_t id) {
    GrowingBuffer b;
    std::uint8_t kind = 6;
    b.BufferWrite(&kind, 1);
    b.BufferWrite(&id, 4);
    self->Send(to, &b);
}
} // namespace

// Packet 4: a login. The magic must be the session's; the id and the rate follow (the rate is cut to what the
// session allows for the address, compared signed). A peer that is connected already is ignored. One that was
// told to join before is told again (its id and rate replaced). Anybody else is asked about and either put on
// the joins list and accepted, or refused with the reason slot 16 gave. The original builds the new join on
// the stack and never sets its age: the list gets whatever the stack held, so the time the join may wait for
// its first update (ServerBase::Tick) is not defined. Rebirth's FixProcessLogin (clears it) is what this body
// always does: garbage cannot be reproduced, so the flag changes nothing here.
void ServerBase::OnLogin(const NetAddress* from, BufferCursor* cursor) {
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

    JoinNode node = joins_.begin();
    while (node != joins_.end() && !SameAddress(&node->address, from)) ++node;
    if (node != joins_.end()) {
        PendingJoin& join = *node;
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
    if (Admit(from, &cursor->p, &reason)) {   // the password at the cursor
        LogAddressLine("Accepting client from ", from);
        PendingJoin join;
        join.address = *from;
        join.id = id;
        join.rate = rate;
        join.age = 0.0f;
        joins_.push_back(join);
        SendAccepted(this, from, id);
        return;
    }

    GrowingBuffer b;
    std::uint8_t kind = 7;
    b.BufferWrite(&kind, 1);
    b.BufferWrite(&id, 4);
    b.BufferWrite(&reason, 1);
    Send(from, &b);
}

// ---- Saving the queries -----------------------------------------------------------------------

void ClientQueries::Serialize(ChunkArchive* archive) {
    RecordChunks(archive, 1, &list, [](Query& q, ChunkArchive* ar) {
        ar->Field(1, &q.address, 0x10);
        std::uint32_t length;
        if (ar->loading_) {
            ar->Field(2, &length, 4);
            GameBuffer& r = q.reply;
            r.cur = r.begin;
            r.high = r.begin + length;
            if (r.high > r.end) r.Grow(length);
        } else {
            length = static_cast<std::uint32_t>(BufferLength(&q.reply));
            ar->Field(2, &length, 4);
        }
        ar->Field(3, q.reply.begin, length);
    });
    archive->Field(2, &extra, 4);
}

} // namespace net
