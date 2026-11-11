// The client base's lobby: finding servers and joining one, and the server base's half that answers. See
// net/net_session.h (ClientBase, ServerBase) for the fields. The packets (first byte): 3 ping (the session time),
// 4 join (magic, id, rate, extra data), 5 info (the echoed time, magic, the server entry), 6 accepted (id), 7
// refused (id, reason); ours add their mod mark to 4, 5, 6 and 7 (WriteModMark). Plain float maths (rule 7). See
// docs/notes/network.md. The game announcement record (GameInfoRecord) is at the end.

#include "game/console/console.h"
#include "engine/core/coder.h"
#include "engine/core/core_refcount.h"
#include "engine/data/chunk_archive.h"
#include "game/net/net_messages.h"
#include "game/net/net_session.h"
#include "lib/crt.h"
#include "platform/log.h"
#include "platform/platform.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <list>
#include <string>

namespace net {

namespace {


constexpr float kNever = 1e38f;      // a listed server's timeout: never checked again
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

bool SameAddress(const NetAddress* a, const NetAddress* b) { return std::memcmp(a, b, sizeof(NetAddress)) == 0; }

// The servers list node whose entry has `address`, or the end.
std::vector<SharedRef<ServerEntry>>::iterator FindServer(ClientBase* self, const NetAddress* address) {
    auto end = self->servers.end();
    auto node = self->servers.begin();
    while (node != end && !SameAddress(&node->Get()->address, address)) ++node;
    return node;
}

// Hands `entry` (non-null) over to a new node at the end of the servers list, marked copy-on-share as a shared
// object is.
void AppendServer(ClientBase* self, SharedRef<ServerEntry>& entry) {
    entry.Get()->flags6 |= RefFlag::CopyOnShare;
    self->servers.push_back(std::move(entry));
}

// PacketKind::Accepted with the join's id, to `to`; ours add the mod mark.
void SendAccepted(ServerBase* self, const NetAddress* to, std::uint32_t id) {
    GrowingBuffer b;
    b.Put(static_cast<std::uint8_t>(PacketKind::Accepted));
    b.Put(id);
    if (!g_originalPeer) WriteModMark(&b, modidentity::Played());
    self->Send(to, &b);
}

// Control characters (below 0x20) of a received string become '|'.
void Sanitize(std::string* s) {
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(s->size()); ++i) {
        if (static_cast<std::uint8_t>((*s)[i]) < 0x20) (*s)[i] = '|';
    }
}

} // namespace

// ---- Our mod mark -------------------------------------------------------------------------------------

void WriteModMark(GameBuffer* out, const modidentity::Identity& mod) {
    out->Write(kModMark, sizeof(kModMark));
    out->Put(kModMarkFormat);
    out->Put(static_cast<std::uint32_t>(mod.hash));
    out->Put(static_cast<std::uint32_t>(mod.hash >> 32));
    const auto length = static_cast<std::uint8_t>(std::min<std::size_t>(mod.title.size(), 0xFF));
    out->Put(length);
    out->Write(mod.title.data(), length);
}

std::optional<modidentity::Identity> ReadModMark(BufferCursor* cursor) {
    char mark[sizeof(kModMark)];
    cursor->ReadBytes(mark, sizeof(mark));
    std::uint8_t format = 0;
    cursor->Take(format);
    if (std::memcmp(mark, kModMark, sizeof(mark)) != 0 || format != kModMarkFormat) return std::nullopt;
    modidentity::Identity mod;
    std::uint32_t low = 0, high = 0;
    cursor->Take(low);
    cursor->Take(high);
    mod.hash = static_cast<std::uint64_t>(high) << 32 | low;
    std::uint8_t length = 0;
    cursor->Take(length);
    mod.title.resize(length);
    cursor->ReadBytes(mod.title.data(), length);
    return mod;
}

// ---- Small members --------------------------------------------------------------------------------

ServerEntryBase::ServerEntryBase() {
    std::memset(&address, 0, sizeof(address));
}

std::vector<Query>::iterator ClientQueries::Find(const NetAddress* address) {
    return std::find_if(list.begin(), list.end(), [address](const Query& q) { return SameAddress(&q.address, address); });
}

Query::Query() { std::memset(&address, 0, sizeof(address)); }
Query::~Query() {}

// ---- Client: sending ------------------------------------------------------------------------------

void ClientBase::SendPing(const NetAddress* address) {
    GrowingBuffer b;
    b.Put(static_cast<std::uint8_t>(PacketKind::GetInfo));
    b.Put(static_cast<float>(time));
    Send(address, &b);
}

void ClientBase::SendJoin(const NetAddress* address, GameBuffer* data) {
    std::uint32_t rate = RateFor(address);
    GrowingBuffer b;
    b.Put(static_cast<std::uint8_t>(PacketKind::Login));
    b.Put(magic);
    b.Put(joinId);
    b.Put(rate);
    b.Write(data->begin, static_cast<std::uint32_t>(data->Size()));
    Send(address, &b);
}

// ---- Client: packets and the connection's loss ---------------------------------------

// Updates count only once joined.
bool ClientBase::OnUpdate(const NetAddress* from, BufferCursor* cursor) {
    if (joinState != JoinState::Joined) {
        LogLine("ERROR: Normal update when not in active state");
        return false;
    }
    return SessionBase::OnUpdate(from, cursor);
}

void ClientBase::OnAccepted(const NetAddress* from, BufferCursor* cursor) {
    std::uint32_t id;
    cursor->Take(id);
    if (!SameAddress(&host, from)) {
        LogAddressLine("ERROR: Accepted msg received from unknown host ", from);
        return;
    }
    if (id != joinId) {
        LogLine("ERROR: Accepted msg received with wrong linkID");
        return;
    }
    if (joinState == JoinState::Asking && !g_originalPeer) {
        // Ours: a host whose mod is not this game's (or the original game while this one plays a mod) is not joined.
        hostMod = ReadModMark(cursor);
        if (!modidentity::Agrees(hostMod)) {
            ei::Log("mod: the host %s plays %s, this game %s: not joined", host.ToString(),
                    modidentity::Described(hostMod), modidentity::Described(modidentity::Played()));
            refusal = RefusalReason::ModMismatch;
            joinState = JoinState::NotJoined;
            return;
        }
    }
    if (joinState == JoinState::Asking) {
        joinState = JoinState::Joined;
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
    std::uint32_t id;
    cursor->Take(id);
    if (!SameAddress(&host, from)) {
        LogAddressLine("ERROR: Rejected msg received from unknown host ", from);
        return;
    }
    if (id != joinId) {
        LogAddressLine("ERROR: Rejected msg received with wrong linkID from ", from);
        return;
    }
    if (joinState == JoinState::Asking) {
        cursor->Take(refusal);
        joinState = JoinState::NotJoined;
        if (!g_originalPeer) hostMod = ReadModMark(cursor);
        if (refusal == RefusalReason::ModMismatch)
            ei::Log("mod: the host %s refused the join: it plays %s, this game %s", host.ToString(),
                    modidentity::Described(hostMod), modidentity::Described(modidentity::Played()));
    } else {
        LogAddressLine("ERROR: Reject not during connecting from ", from);
    }
}

// Its magic must match in all but the low byte. A new entry reads itself from the packet (ours: then the mod mark,
// none from an original server) and gets the sender's address, the ping (now - the echoed time) and when to look again: local servers and those asked directly soon,
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
    if (!entry) return;
    ServerEntry* e = entry.Get();
    e->Read(cursor);
    if (!g_originalPeer) {
        e->mod = ReadModMark(cursor);
        e->modRead = true;
    }
    e->address = *from;
    e->foreign = magicNumber != magic;     // another version (low byte differs): Connect refuses it
    bool local = socket.IsLocal(from);
    ClientQueries& v = queries;
    auto end = v.list.end();
    auto query = v.Find(from);
    if (local) {
        e->state = query != end ? ServerEntry::State::Asked : ServerEntry::State::Local;
        e->timeout = 20.0f;
    } else if (query != end) {
        e->state = ServerEntry::State::Asked;
        auto count = static_cast<std::uint32_t>(v.list.size());
        e->timeout = static_cast<float>(count) * 0.5f + 20.0f;
    } else {
        if (node == servers.end()) return;
        e->state = ServerEntry::State::FromList;
        e->timeout = kNever;
    }
    e->ping = static_cast<float>(time) - std::bit_cast<float>(echo);
    if (node == servers.end()) {
        AppendServer(this, entry);
    } else {
        ShareAssignOwned(*node, entry.Get());
        auto q = v.Find(from);
        if (q != v.list.end()) {
            BufferCursor c;
            q->reply.Clear();
            c.BeginWrite(&q->reply, 0x400);
            entry.Get()->Write(&c);
            c.End();
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
    joinState = JoinState::NotJoined;
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
        if (r.Size() != 0) {
            BufferCursor c;
            r.Seek(0);
            c.BeginRead(&r, r.Size());
            CreateEntry(&entry);
            entry.Get()->Read(&c);
            c.End();
        } else {
            CreateEntry(&entry);
        }
        if (!entry) continue;
        ServerEntry* e = entry.Get();
        e->state = ServerEntry::State::Asked;
        e->address = q->address;
        AppendServer(this, entry);
    }
}

// A new query is added and pinged (the session time); the server is listed (an empty entry) or
// marked as asked.
void ClientBase::Ask(const NetAddress* address) {
    std::vector<Query>& v = queries.list;
    if (queries.Find(address) == v.end()) {
        v.emplace_back().address = *address;
        SendPing(address);
    }
    auto node = FindServer(this, address);
    if (node != servers.end()) {
        node->Get()->state = ServerEntry::State::Asked;
        return;
    }
    SharedRef<ServerEntry> entry;
    CreateEntry(&entry);
    if (entry.Get()) {
        ServerEntry* e = entry.Get();
        e->address = *address;
        e->state = ServerEntry::State::Asked;
        AppendServer(this, entry);
    }
}

void ClientBase::Forget(const NetAddress* address) {
    std::vector<Query>& v = queries.list;
    auto q = queries.Find(address);
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
        entry->state = ServerEntry::State::FromList;
        entry->timeout = kNever;
        servers.emplace_back(entry);
    }
    waiting.push_back(entry->address);
}

// Unless already asking that host: a new join id (rand() << 16 | GetTickCount() & 0xFFFF), the
// first retry at once.
void ClientBase::Join(const NetAddress* address, const GrowingBuffer* data) {
    if (joinState == JoinState::Asking && SameAddress(address, &host)) return;
    host = *address;
    std::uint32_t id = static_cast<std::uint32_t>(libcrt::Rand()) << 16;
    id |= platform::CoarseClock() & 0xFFFF;
    joinId = id;
    joinState = JoinState::Asking;
    retryTimer = 0.0f;
    retryInterval = kFirstRetry;
    refusal = RefusalReason::None;
    hostMod.reset();
    joinData = *data;
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
            if (e->timeout <= 0.0f) {
                ClientQueries& v = queries;
                if (v.list.begin() == v.list.end() || v.Find(&e->address) == v.list.end()) {
                    node = servers.erase(node);
                    continue;
                }
                e->ping = kNoPingSeconds;
            }
        }
        ++node;
    }
    SessionBase::Tick(dt);
    if (joinState != JoinState::Asking) return;
    retryTimer -= static_cast<float>(dt);
    if (retryTimer > 0.0f) return;
    if (retryInterval > 3.0f) {
        LogLine("Connection failed");
        joinState = JoinState::NotJoined;
        return;
    }
    SendJoin(&host, &joinData);
    retryInterval += retryInterval;
    retryTimer = retryInterval;
}

// ---- Server base ------------------------------------------------------------------------------------

void ServerBase::SendInfo(const NetAddress* address, std::uint32_t echo) {
    GrowingBuffer b;
    SharedRef<ServerEntry> entry;
    Describe(&entry);
    if (entry.Get()) {
        b.Put(static_cast<std::uint8_t>(PacketKind::Info));
        b.Put(echo);
        BufferCursor c;
        c.BeginWrite(&b, 0x400);
        c.Put(magic);
        entry.Get()->Write(&c);
        c.End();
        if (!g_originalPeer) WriteModMark(&b, modidentity::Played());
        Send(address, &b);
    }
}

void ServerBase::OnGetInfo(const NetAddress* from, BufferCursor* cursor) {
    std::uint32_t echo;
    cursor->Take(echo);
    SendInfo(from, echo);
}

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
    cursor->Take(magicNumber);
    if (magicNumber != magic) return;
    std::uint32_t id;
    cursor->Take(id);
    std::int32_t rate;
    cursor->Take(rate);
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

    RefusalReason reason;
    if (Admit(from, id, cursor, &reason)) {
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
    b.Put(static_cast<std::uint8_t>(PacketKind::Rejected));
    b.Put(id);
    b.Put(reason);
    if (reason == RefusalReason::ModMismatch) WriteModMark(&b, modidentity::Played());
    Send(from, &b);
}

// ---- Saving the queries -----------------------------------------------------------------------

void ClientQueries::Serialize(ChunkArchive* archive) {
    RecordChunks(archive, 1, &list, [](Query& q, ChunkArchive* ar) {
        ar->Field(1, &q.address);
        std::uint32_t length;
        if (ar->loading) {
            ar->Field(2, &length);
            q.reply.SetSize(length);
        } else {
            length = static_cast<std::uint32_t>(q.reply.Size());
            ar->Field(2, &length);
        }
        ar->Field(3, q.reply.begin, length);
    });
    archive->Field(2, &extra);
}

// --- The game announcement record ---------------------------------------------------------------

void GameInfoRecord::Write(GameBuffer* buffer) const {
    buffer->Put(masterKey);
    buffer->Put(masterToken);
    buffer->WriteString(name);
    buffer->WriteString(description);
    buffer->Put(players);
    buffer->Put(maxPlayers);
    buffer->Put(hasPassword);
    buffer->Put(kind);
    buffer->Put(magic);
}

// Reads the record in the same order (strings of at most 0x100 bytes), replaces control
// characters in both strings with '|'; returns 1 unless the buffer ran short.
int GameInfoRecord::Read(GameBuffer* buffer) {
    buffer->Get(masterKey);
    buffer->Get(masterToken);
    buffer->ReadString(&name, 0x100);
    buffer->ReadString(&description, 0x100);
    buffer->Get(players);
    buffer->Get(maxPlayers);
    buffer->Get(hasPassword);
    buffer->Get(kind);
    buffer->Get(magic);
    Sanitize(&name);
    Sanitize(&description);
    return buffer->readPastEnd ? 0 : 1;
}

} // namespace net
