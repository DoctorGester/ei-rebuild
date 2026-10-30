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
#include "lib/bytes.h"
#include "lib/crt.h"

#include "ei/rebirth.h"
#include "../hook/log.h"
#include "lib/gameapi.h"
#include "lib/nan_audit.h"

#include <bit>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <list>

namespace net {

namespace {


constexpr std::uint32_t kNever = 0x7E967699;   // 1e38f
constexpr float kFirstRetry = 0.1f;


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

void SetBits(float& f, std::uint32_t bits) { f = std::bit_cast<float>(bits); }

bool SameAddress(const NetAddress* a, const NetAddress* b) { return std::memcmp(a, b, sizeof(NetAddress)) == 0; }

// The servers list node whose entry has `address`, or the end.
std::vector<SharedRef<ServerEntry>>::iterator FindServer(ClientBase* self, const NetAddress* address) {
    auto end = self->servers.end();
    auto node = self->servers.begin();
    while (node != end && !SameAddress(&node->Get()->address, address)) ++node;
    return node;
}
// The query with `address`, or the end.
std::vector<Query>::iterator FindQuery(ClientQueries& v, const NetAddress* address) {
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
void AppendServer(ClientBase* self, Ref<ServerEntry>& entry) {
    ServerEntry* raw = entry.Take();
    --raw->refs;
    raw->flags6 &= static_cast<std::uint8_t>(~RefCounted::kCopyOnShare);
    self->servers.emplace_back(raw);
}

// "Accepted" (packet 6) with the join's id, to `to`.
void SendAccepted(ServerBase* self, const NetAddress* to, std::uint32_t id) {
    GrowingBuffer b;
    std::uint8_t kind = 6;
    b.BufferWrite(&kind, 1);
    b.BufferWrite(&id, 4);
    self->Send(to, &b);
}

} // namespace

// ---- Small members --------------------------------------------------------------------------------

ServerEntryBase::ServerEntryBase() {
    std::memset(&address, 0, sizeof(address));
    timeout = 0.0f;
    ping = 9.999f;
    foreign = 0;
    state = kLocal;
}

std::vector<Query>::iterator ClientQueries::QueriesFind(const NetAddress* address) { return FindQuery(*this, address); }

// A buffer's length (its highest position brought up to date first).
std::int32_t BufferLength(GameBuffer* b) {
    if (b->cur > b->high) b->high = b->cur;
    return b->PositionOf(b->high);
}

Query::Query() { std::memset(&address, 0, sizeof(address)); }
Query::~Query() {}
Query::Query(const Query& from) : address(from.address), reply(from.reply) {}

// ---- Client: sending ------------------------------------------------------------------------------

void ClientBase::SendPing(const NetAddress* address) {
    GrowingBuffer b;
    std::uint8_t kind = 3;
    Put(&b, &kind, 1);
    float seconds = static_cast<float>(time);
    Put(&b, &seconds, 4);
    Send(address, &b);
}

void ClientBase::SendJoin(const NetAddress* address, GameBuffer* data) {
    std::uint32_t rate = RateFor(address);
    GrowingBuffer b;
    std::uint8_t kind = 4;
    Put(&b, &kind, 1);
    Put(&b, &magic, 4);
    Put(&b, &joinId, 4);
    Put(&b, &rate, 4);
    if (data->cur > data->high) data->high = data->cur;
    unsigned length = static_cast<unsigned>(data->PositionOf(data->high));
    Put(&b, data->begin, length);
    Send(address, &b);
}

// ---- Client: packets and the connection's loss ---------------------------------------

// Updates count only once joined.
bool ClientBase::OnUpdate(const NetAddress* from, BufferCursor* cursor) {
    if (joinState != kJoined) {
        LogLine("ERROR: Normal update when not in active state");
        return false;
    }
    return SessionBase::OnUpdate(from, cursor);
}

void ClientBase::OnAccepted(const NetAddress* from, BufferCursor* cursor) {
    std::uint32_t id;
    lib::LoadBytes(id, cursor->p);
    cursor->p += 4;
    if (!SameAddress(&host, from)) {
        LogAddressLine("ERROR: Accepted msg received from unknown host ", from);
        return;
    }
    if (id != joinId) {
        LogLine("ERROR: Accepted msg received with wrong linkID");
        return;
    }
    if (joinState == kAsking) {
        joinState = kJoined;
        std::uint32_t rate = RateFor(&host);
        AddClient(&host, id, static_cast<int>(rate));
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
    lib::LoadBytes(id, p);
    p += 4;
    cursor->p = p;
    if (!SameAddress(&host, from)) {
        LogAddressLine("ERROR: Rejected msg received from unknown host ", from);
        return;
    }
    if (id != joinId) {
        LogAddressLine("ERROR: Rejected msg received with wrong linkID from ", from);
        return;
    }
    if (joinState == kAsking) {
        refusal = *p;
        ++cursor->p;
        joinState = kNotJoined;
    } else {
        LogAddressLine("ERROR: Reject not during connecting from ", from);
    }
}

// Its magic must match in all but the low byte. A new entry reads itself from the packet and gets the sender's
// address, the ping (now - the echoed time) and when to look again: local servers and those asked directly soon,
// others from the list never; unknown servers neither local nor asked are ignored. A known server's entry is
// replaced (and its reply kept in its query, if asked); a new one is appended.
void ClientBase::OnInfo(const NetAddress* from, BufferCursor* cursor) {
    SharedRef<ServerEntry> entry;
    std::uint32_t echo, magicNumber;
    cursor->ReadBytes(&echo, 4);
    cursor->ReadBytes(&magicNumber, 4);
    if (((magicNumber ^ magic) & 0xFFFFFF00u) != 0) return;
    auto node = FindServer(this, from);
    CreateEntry(&entry);
    entry.Get()->Read(cursor);
    if (!entry) return;
    ServerEntry* e = entry.Get();
    e->address = *from;
    e->foreign = magicNumber != magic;     // another version (low byte differs): Connect refuses it
    bool local = socket.IsLocal(from);
    ClientQueries& v = queries;
    auto end = v.list.end();
    auto query = v.QueriesFind(from);
    if (local) {
        e->state = query != end ? ServerEntry::kAsked : ServerEntry::kLocal;
        e->timeout = 20.0f;
    } else if (query != end) {
        e->state = ServerEntry::kAsked;
        auto count = static_cast<std::uint32_t>(v.list.size());
        e->timeout = static_cast<float>(count) * 0.5f + 20.0f;
    } else {
        if (node == servers.end()) return;
        e->state = ServerEntry::kFromList;
        SetBits(e->timeout, kNever);
    }
    float echoed;
    echoed = std::bit_cast<float>(echo);
    e->ping = static_cast<float>(time) - echoed;
    if (node == servers.end()) {
        AppendServer(this, entry);
    } else {
        ShareAssignOwned(*node, entry.Get());
        auto q = FindQuery(v, from);
        if (q != v.list.end()) {
            BufferCursor c;
            c.Reset();
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
}

// Removes a connection; when none is left the connection to the host is lost.
void ClientBase::RemoveClient(std::list<SharedRef<Connection>>::iterator node) {
    if (node == clients.end()) return;
    SessionBase::RemoveClient(node);
    if (!clients.empty()) return;
    LogLine("CONNECTION LOST server link is lost, entering idle");
    ei::Log("net: the client lost its connection to the host");   // not the game's
    joinState = kNotJoined;
}

// ---- Client: the lobby's requests ---------------------------------------------------------------

// Pings go to the game port (net::g_gamePort).
void ClientBase::Refresh() {
    NetAddress address{};
    for (unsigned i = 0; socket.Broadcast(&address, i, g_gamePort); ++i)
        SendPing(&address);
    if (!waiting.empty()) {
        NetAddress next = waiting.front();
        waiting.erase(waiting.begin());
        SendPing(&next);
        return;
    }
    std::uint32_t& index = nextQuery;
    ++index;
    std::vector<Query>& v = queries.list;
    if (index >= static_cast<std::uint32_t>(v.size())) index = 0;
    if (index < static_cast<std::uint32_t>(v.size())) SendPing(&v[index].address);
    if (alwaysAsk.ip != 0) {
        waiting.push_back(alwaysAsk);
    }
}

void ClientBase::SetAlwaysAsk(const NetAddress* address) { alwaysAsk = *address; }

// Each server not yet listed is listed with an entry read from its saved reply (or an empty entry), as asked.
void ClientBase::SetQueries(const ClientQueries* from) {
    std::vector<Query>& v = queries.list;
    v = from->list;
    queries.extra = from->extra;
    if (v.empty()) return;
    for (unsigned i = 0; i < static_cast<unsigned>(v.size()); ++i) {
        Query* q = &v[i];
        if (FindServer(this, &q->address) != servers.end()) continue;
        SharedRef<ServerEntry> entry;
        GameBuffer& r = q->reply;
        if (r.cur > r.high) r.high = r.cur;
        if (r.PositionOf(r.high) != 0) {
            BufferCursor c;
            c.Reset();
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
        if (!entry) continue;
        ServerEntry* e = entry.Get();
        e->state = ServerEntry::kAsked;
        e->address = q->address;
        AppendServer(this, entry);
    }
}

// A new query is added and pinged (3, the session time); the server is listed (an empty entry) or
// marked as asked.
void ClientBase::Ask(const NetAddress* address) {
    std::vector<Query>& v = queries.list;
    if (FindQuery(queries, address) == v.end()) {
        v.emplace_back().address = *address;
        GrowingBuffer b;
        std::uint8_t kind = 3;
        b.BufferWrite(&kind, 1);
        float seconds = static_cast<float>(time);
        b.BufferWrite(&seconds, 4);
        Send(address, &b);
    }
    auto node = FindServer(this, address);
    if (node != servers.end()) {
        node->Get()->state = ServerEntry::kAsked;
        return;
    }
    SharedRef<ServerEntry> entry;
    CreateEntry(&entry);
    if (entry.Get()) {
        ServerEntry* e = entry.Get();
        e->address = *address;
        e->state = ServerEntry::kAsked;
        AppendServer(this, entry);
    }
}

void ClientBase::Forget(const NetAddress* address) {
    std::vector<Query>& v = queries.list;
    auto q = FindQuery(queries, address);
    if (q != v.end()) v.erase(q);
    auto node = servers.begin();
    while (node != servers.end()) {
        if (SameAddress(&node->Get()->address, address)) {
            node = servers.erase(node);
        } else {
            ++node;
        }
    }
}

// Listed (never checked again) unless known, and its address queued to be asked.
void ClientBase::AddServer(ServerEntry* entry) {
    if (FindServer(this, &entry->address) == servers.end()) {
        entry->state = ServerEntry::kFromList;
        SetBits(entry->timeout, kNever);
        servers.emplace_back(entry);
    }
    waiting.push_back(entry->address);
}

// Unless already asking that host: a new join id (rand() << 16 | GetTickCount() & 0xFFFF), the
// first retry at once.
void ClientBase::Join(const NetAddress* address, const GrowingBuffer* data) {
    if (joinState == kAsking && SameAddress(address, &host)) return;
    host = *address;
    std::uint32_t id = static_cast<std::uint32_t>(libcrt::Rand()) << 16;
    id |= gameapi::GetTickCount() & 0xFFFF;
    joinId = id;
    joinState = kAsking;
    retryTimer = 0.0f;
    retryInterval = kFirstRetry;
    refusal = 0;
    joinData = *data;
}

ServerEntry* ClientBase::ServerAt(int index) {
    auto sentinel = servers.end();
    auto node = servers.begin();
    for (int i = 0; i < index; ++i) {
        if (node == sentinel) return nullptr;
        ++node;
    }
    if (node == sentinel) return nullptr;
    return node->Get();
}

// Counts the servers' timeouts down; a server whose time ran out is dropped
// unless asked directly (then its ping is reset). Then the session's receive step, and while
// joining the request is repeated with a doubling interval until it passes the limit.
void ClientBase::Tick(double dt) {
    auto node = servers.begin();
    while (node != servers.end()) {
        ServerEntry* e = node->Get();
        if (e->timeout > 0.0f) {
            e->timeout -= static_cast<float>(dt);
            e = node->Get();
            EI_NAN_AUDIT(e->timeout);
            if (e->timeout <= 0.0f) {
                ClientQueries& v = queries;
                if (v.list.begin() == v.list.end() || FindQuery(v, &e->address) == v.list.end()) {
                    node = servers.erase(node);
                    continue;
                }
                e->ping = 9.999f;
            }
        }
        ++node;
    }
    SessionBase::Tick(dt);
    if (joinState != kAsking) return;
    retryTimer -= static_cast<float>(dt);
    if (retryTimer > 0.0f) return;
    if (retryInterval > 3.0f) {
        LogLine("Connection failed");
        joinState = kNotJoined;
        return;
    }
    SendJoin(&host, &joinData);
    retryInterval = retryInterval + retryInterval;
    retryTimer = retryInterval;
}

// ---- Server base ------------------------------------------------------------------------------------

void ServerBase::SendInfo(const NetAddress* address, std::uint32_t echo) {
    GrowingBuffer b;
    GameBuffer* buffer = &b;   // writes go through the vtable
    SharedRef<ServerEntry> entry;
    Describe(&entry);
    if (entry.Get()) {
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
        lib::StoreBytes(c.p, magic);
        c.p += 4;
        entry.Get()->Write(&c);
        EndCursor(c);
        Send(address, &b);
    }
}

void ServerBase::OnGetInfo(const NetAddress* from, BufferCursor* cursor) {
    std::uint32_t echo;
    lib::LoadBytes(echo, cursor->p);
    cursor->p += 4;
    SendInfo(from, echo);
}

bool ServerBase::Admit(const NetAddress*, std::uint8_t**, std::uint8_t*) { return true; }

// An update from a peer that is not connected yet but was told to join
// becomes a connection (with the id and rate of its join), then counts.
bool ServerBase::OnUpdate(const NetAddress* from, BufferCursor* cursor) {
    if (SessionBase::OnUpdate(from, cursor)) return true;
    auto node = joins.begin();
    while (node != joins.end() && !SameAddress(&node->address, from)) ++node;
    if (node == joins.end()) return false;
    PendingJoin& join = *node;
    AddClient(&join.address, join.id, join.rate);
    joins.erase(node);
    return SessionBase::OnUpdate(from, cursor);
}

// Ages the pending joins (dropping those older than the join timeout, logged), then
// the session's receive step.
void ServerBase::Tick(double dt) {
    auto node = joins.begin();
    while (node != joins.end()) {
        float& age = node->age;
        age += static_cast<float>(dt);
        if (age > 60.0f) {
            LogAddressLine("Accept period elapsed for ", &node->address);
            node = joins.erase(node);
        } else {
            ++node;
        }
    }
    SessionBase::Tick(dt);
}

// Packet 4: a login. The magic must be the session's; the id and the rate follow (the rate is cut to what the
// session allows for the address, compared signed). A peer that is connected already is ignored. One that was
// told to join before is told again (its id and rate replaced). Anybody else is asked about and either put on
// the joins list and accepted, or refused with the reason the check gave. The original builds the new join on
// the stack and never sets its age: the list gets whatever the stack held, so the time the join may wait for
// its first update (ServerBase::Tick) is not defined. Rebirth's FixProcessLogin (clears it) is what this body
// always does: garbage cannot be reproduced, so the flag changes nothing here.
void ServerBase::OnLogin(const NetAddress* from, BufferCursor* cursor) {
    std::uint32_t magicNumber;
    cursor->ReadBytes(&magicNumber, 4);
    if (magicNumber != magic) return;
    std::uint32_t id;
    cursor->ReadBytes(&id, 4);
    std::int32_t rate;
    cursor->ReadBytes(&rate, 4);
    const std::int32_t limit = static_cast<std::int32_t>(RateFor(from));
    if (rate >= limit) rate = limit;

    if (FindClient(from) != clients.end()) {
        LogAddressLine("ERROR: Ignoring login request for active session from ", from);
        return;
    }

    auto node = joins.begin();
    while (node != joins.end() && !SameAddress(&node->address, from)) ++node;
    if (node != joins.end()) {
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
        joins.push_back(join);
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
        ar->Field(1, &q.address);
        std::uint32_t length;
        if (ar->loading) {
            ar->Field(2, &length);
            GrowingBuffer& r = q.reply;
            r.cur = r.begin;
            r.high = r.begin + length;
            if (r.high > r.end) r.Grow(length);
        } else {
            length = static_cast<std::uint32_t>(BufferLength(&q.reply));
            ar->Field(2, &length);
        }
        ar->Field(3, q.reply.begin, length);
    });
    archive->Field(2, &extra);
}

} // namespace net
