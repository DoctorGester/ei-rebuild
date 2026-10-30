// The game server session. See
// net_classes.h and docs/notes/network.md.
#include "core_streams.h"

#include "core_refcount.h"
#include "game_strings.h"
#include "net_classes.h"
#include "net_coder.h"
#include "net_connection.h"
#include "net_handlers.h"
#include "net_messages.h"
#include "net_second_master.h"
#include "options.h"
#include "world.h"
#include "world_data.h"
#include "ei/rebirth.h"
#include "lib/crt.h"
#include "lib/nan_audit.h"

#include <cstdint>
#include <cstring>
#include <format>
#include <new>
#include <string>

namespace net {

namespace {

// A timed entry's message ({seconds, object}).
template <typename T, typename Entry>
T* TimedValueOf(const Entry& entry) { return static_cast<T*>(entry.ref.Get()); }

} // namespace

ServerBase::ServerBase() {
    socket.Open(g_gamePort);
}

ServerSession::ServerSession(std::uint32_t newKind) {
    masterAddress = NetAddress{};
    magic = kSessionMagic;
    RegisterMessageTypes();

    AddHandlerFor<LoginMessage>(0, AsHandler(ServerOnJoin));
    AddHandlerFor<LoginMessage>(1, AsHandler(ServerOnPlayerChanged));
    AddHandlerFor<LoginMessage>(2, AsHandler(ServerOnPlayerGone));
    AddHandlerFor<SayMessage>(0, AsHandler(ServerOnSay));
    AddHandlerFor<CommandMessage>(0, AsHandler(ServerOnCommand));
    AddHandlerFor<FileRequestMessage>(0, AsHandler(ServerOnObjectAdded));

    RegisterRequestHandler(2, AsHandler(ServerOnObjectRemoved));
    RegisterPartyHandler(0, AsHandler(ServerOnTradeOffer));
    RegisterPartyHandler(1, AsHandler(ServerOnTradeUpdate));
    RegisterPartyHandler(2, AsHandler(ServerOnTradeCancel));
    RegisterMemberHandler(0, AsHandler(ServerOnMemberChanged));
    RegisterMemberHandler(1, AsHandler(ServerOnMemberChanged));

    nextLine = 0;
    kind = newKind;
    maxPlayers = 8;
    QueueMapList();
    announceTimer = 0.0f;
    announced = 0;
    masterToken = 0;
    std::memset(password, 0, sizeof(password));
}

// What the connection being updated receives: the players and the variables, its own binding (the binding
// whose connection it is; hosting when its player is the host), the newer chat lines meant for everyone or
// for it (numbered after the player's first line), every shared file with, for the one a request of this
// connection names, its chunks (priority 1), the queued maps for the host, the host message and the map
// message, the awards for this connection, the trade offers to the player whose members all exist (the offer
// and its members), and the trades naming the player. Priority 2 unless noted.
void ServerSession::CollectReplicated(std::vector<Replicated>** out) {
    PlayerMessage* record = nullptr;
    bool hosting = false;
    for (auto& n : players) PushReplicated(*out, n.Get(), 2);
    for (auto& n : variables) PushReplicated(*out, n.Get(), 2);
    if (!bindings.empty()) {
        for (auto& n : bindings) {
            auto* binding = n.Get();
            if (binding->ConnectionRef().Get() != g_currentConnection) continue;
            record = binding->PlayerInfo();
            if (record->host) hosting = true;
            PushReplicated(*out, binding, 2);
        }
        if (record && !(record->flags7 & RefObject::kDetached)) {
            for (auto& n : lines) {
                auto* line = n.Get();
                RefObject* target = line->connection.Get();
                if ((!target || target == g_currentConnection) &&
                    static_cast<std::int32_t>(line->number) > static_cast<std::int32_t>(record->firstLine))
                    PushReplicated(*out, line, 2);
            }
        }
    }
    for (auto& n : files) {
        PushReplicated(*out, n.Get(), 2);
        for (auto& m : fileRequests) {
            auto* request = m.Get();
            if (request->FileRef().Get() != n.Get() || request->ConnectionRef().Get() != g_currentConnection) continue;
            for (const SharedRef<RefObject>& chunk : n.Get()->chunks) PushReplicated(*out, chunk.Get(), 1);
            break;
        }
    }
    if (hosting)
        for (auto& n : queuedMaps) PushReplicated(*out, n.Get(), 2);
    if (RefObject* object = hostInfo.Get()) PushReplicated(*out, object, 2);
    if (RefObject* object = map.Get()) PushReplicated(*out, object, 2);
    for (auto& n : awards) {
        auto* award = TimedValueOf<AwardMessage>(n);
        if (award->connection.Get() == g_currentConnection) PushReplicated(*out, award, 2);
    }
    for (auto& n : offers) {
        auto* offer = n.Get();
        if (offer->partner.Get() != record) continue;
        bool complete = true;
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(offer->members.size()); ++i) {
            RefObject* member = offer->members[i].Get();
            if (!member || (member->flags7 & RefObject::kDetached)) complete = false;
        }
        if (!complete) continue;
        PushReplicated(*out, offer, 2);
        const std::vector<HeldRef<RefObject>>& members = n.Get()->members;
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(members.size()); ++i)
            PushReplicated(*out, n.Get()->members[i].Get(), 2);
    }
    for (auto& n : trades) {
        auto* trade = n.Get();
        if (trade->player.Get() == record || trade->partner.Get() == record) PushReplicated(*out, trade, 2);
    }
    for (auto& n : finishedTrades) {
        auto* trade = TimedValueOf<TradeMessage>(n);
        if (trade->player.Get() == record || trade->partner.Get() == record) PushReplicated(*out, trade, 2);
    }
}

} // namespace net

// --- Entry -------------------------------------------------------------------------------------------

// The game server's description of itself: a server entry like those the
// client reads from the server list (net_server.cpp). See net_classes.h and docs/notes/network.md.

namespace net {

// Makes `*out` a new server entry: the name, the kind, the current map's title as the description when a map
// message is held (else empty), the number of clients, the maximum and whether a password is set.
void ServerSession::Describe(Ref<ServerEntry>* out) {
    auto* entry = NewCounted<ServerEntry>();
    entry->name = name;
    entry->kind = kind;
    std::string* description = &entry->description;
    description->assign(game::kEmptyText);

    RefObject* newMap = map.Get();
    if (newMap && !(newMap->flags7 & RefObject::kDetached)) description->assign(SessionMapName());    // overwrite, then cut or append

    entry->players = static_cast<std::uint8_t>(ClientCount());
    entry->maxPlayers = static_cast<std::uint8_t>(maxPlayers);
    entry->password = password[0] != 0;

    ShareAssignOwned(*out, entry);
}
// When announced, the master's address is set from the host name and port 28004.
void ServerSession::SetPublic(bool announce) {
    announced = announce;
    if (announce) masterAddress.Parse(options::g_options.masterServerName.c_str(), 0x6D64);
}

} // namespace net

// --- Life --------------------------------------------------------------------------------------------

// The game server's destructor, scalar deleting destructor and its
// base layer's destructor body. The members are torn down in reverse order: the lists are cleared where the
// original destroys them, between the counted pointers' releases; then the session base. See net_classes.h and
// docs/notes/network.md.

namespace net {

// The original frees the two names first; ours last (they are members). The pending joins and the session base
// follow (their destructors).
ServerSession::~ServerSession() {
    finishedTrades.clear();
    trades.clear();
    offers.clear();
    awards.clear();
    variables.clear();
    queuedMaps.clear();
    DestroyOwned(map);
    bindings.clear();
    DestroyOwned(hostInfo);
    fileRequests.clear();
    files.clear();
    lines.clear();
    players.clear();
}

// Also reached from the constructor's unwind funclets.
ServerBase::~ServerBase() {  }

} // namespace net

// --- Handlers ----------------------------------------------------------------------------------------

// The game server's talk with the master server and a chat handler: the announcement (from the tick), the
// master server's answer and a client's chat line (registered for SayMessage by ServerSession's constructor).
// All run from the session's tick on the main thread (net::Pump). Rebirth's EI_Plugin.asi patches the
// announcement and the master answer (NewMaster; both reproduced: the answer's NOPs and the announcement's
// copy of the announcement to the second master server, net_second_master.h) and addon.dll's ChatLog has a
// site in the chat handler (disabled in this install). See docs/notes/network.md and rebirth.md.

namespace net {

namespace {
// The next dword of the packet (read through the cursor's pointer, as the original inlines it).
std::uint32_t TakeDword(BufferCursor* cursor) {
    std::uint32_t value;
    std::memcpy(&value, cursor->p, sizeof value);
    cursor->p += sizeof value;
    return value;
}

} // namespace

// The record is built on the stack (two strings with room for 8 bytes first, as their
// constructors make them), written into a growing buffer and sent from the session's socket to
// the master's address; the result of the send is not looked at.
void ServerSession::AnnounceToMaster() {
    GrowingBuffer buffer;
    GameInfoRecord info;
    std::string& infoName = info.name;
    std::string& description = info.description;

    infoName = name;
    info.hasPassword = password[0] != 0;
    info.masterKey = masterKey;
    info.kind = static_cast<std::uint8_t>(kind);
    const char* empty = game::kEmptyText;
    description.assign(empty);
    RefObject* newMap = map.Get();
    if (newMap && !(newMap->flags7 & RefCounted::kDetached)) description.assign(SessionMapName());
    info.players = static_cast<std::uint8_t>(ClientCount());
    info.maxPlayers = static_cast<std::uint8_t>(maxPlayers);
    info.masterToken = masterToken;
    info.magic = kSessionMagic;
    info.Write(&buffer);
    socket.SendTo(&masterAddress, &buffer);
    // EI_Plugin's NewMaster (its call sends as SendTo does, then the copy).
    if (ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginNewMaster)) second_master::CopyAnnouncement(socket.socket, buffer);

}

// A packet of kind -1: only from the master server's address (all 16 bytes compared; from any address with
// EI_Plugin's NewMaster, which NOPs a jump so the answer of its second master server counts). Its first dword
// must be the session's +0x25FC; the second is then kept (the next announcement sends it back). The first
// dword is consumed either way.
void ServerSession::OnMaster(const NetAddress* from, BufferCursor* cursor) {
    if (std::memcmp(from, &masterAddress, sizeof(NetAddress)) != 0 &&
        !ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginNewMaster))
        return;
    if (TakeDword(cursor) != masterKey) return;
    masterToken = TakeDword(cursor);
}

// A chat line from the connection being read
// (g_currentConnection; nothing when it is gone or detached): the first binding of that connection names the
// player (nothing without one, or when the binding is detached), and the line is announced to
// everybody as "^<number><name>^7: <text>".
void ServerOnSay(ServerSession* server, SayMessage* message) {
    Connection* connection = g_currentConnection;
    if (!connection || (connection->flags7 & RefCounted::kDetached)) return;
    const auto& bindings = server->bindings;
    for (auto& node : bindings) {
        auto* binding = node.Get();
        if (binding->ConnectionRef().Get() != connection) continue;
        // (The original also tests the binding for null here, after having read it.)
        if (binding->flags7 & RefCounted::kDetached) return;
        auto* player = binding->PlayerInfo();
        std::string line;
        line = std::format("^{}{}^7: {}", player->number, player->name.data(), message->text.data());
        server->Announce(line, nullptr);
        return;
    }
}

// The handler of the party members' channels 0 and 1 (ServerSession::Construct). The member message's version
// moves on, so the connections send it again.
void ServerOnMemberChanged(ServerSession*, Message* message) { ++message->version; }

} // namespace net

// --- Tick --------------------------------------------------------------------------------------------

// The game server's tick and a player state query (called by the file chunk sender). Float maths in plain C++
// (rewrite-guide rule 7). See net_classes.h and docs/notes/network.md.

namespace net {

namespace {

const char* MasterHost() { return options::g_options.masterServerName.c_str(); }    // the master server's host name

// Timed entries {seconds left (float), object}: each loses `dt`; those below zero are erased.
template <typename List>
void CountDown(List& list, float dt) {
    for (auto node = list.begin(); node != list.end();) {
        float& seconds = node->seconds;
        float left = seconds - dt;
        seconds = left;
        EI_NAN_AUDIT(left);
        if (left < 0.0f) {
            node = list.erase(node);
        } else {
            ++node;
        }
    }
}

} // namespace

// The base layer's tick, then the timed entries of the finished trades and the awards count down by `dt` (as
// a float), and when the game is public the announcement timer does too: when it runs out it is reset and,
// with a master server host set, the game is announced. `dt` holds a float value (net::Pump), so the float
// subtractions are the original's at 24-bit precision.
void ServerSession::Tick(double dt) {
    ServerBase::Tick(dt);
    float step = static_cast<float>(dt);
    CountDown(finishedTrades, step);
    CountDown(awards, step);
    if (!announced) return;
    announceTimer -= step;
    EI_NAN_AUDIT(announceTimer);
    if (announceTimer < 0.0f) {    // run out
        announceTimer = worlddata::AnnouncePeriodRef();
        const char* host = MasterHost();
        if (std::strcmp(host, game::kEmptyText) != 0) AnnounceToMaster();
    }
}

// Whether the player bound to `connection` (its first binding) is in state 3.
// False for a dead connection, binding or player.
bool ServerSession::PlayerStateIs3(const Connection* connection) {
    if (!IsLive(connection)) return false;
    for (auto& node : bindings) {
        auto* binding = node.Get();
        if (binding->ConnectionRef().Get() != connection) continue;
        if (!IsLive(binding)) return false;
        auto* player = binding->PlayerInfo();
        if (!IsLive(player)) return false;
        return player->state == 3;
    }
    return false;
}

} // namespace net

// --- List --------------------------------------------------------------------------------------------

// The server list reply: a compressed run of {address, GameInfoRecord} turned into the
// client base's server entries. See net_classes.h and docs/notes/network.md.

namespace net {

namespace {

// A read the way the buffer helpers inline it: past the end through the overflow handler, beyond `end`
// through the buffer's virtual read, else from memory.
void ReadBytes(GameBuffer* buffer, void* to, unsigned size) {
    if (buffer->cur + size > buffer->high) buffer->ReadPastEnd(to, size);
    else if (buffer->cur + size > buffer->end) buffer->ReadMore(to, size);
    else buffer->CopyOut(to, size);
}

} // namespace

// Reads a server list reply from `in` (rewound): its size, then the LZ-compressed list.
// Each item is a 16-byte address and a GameInfoRecord; it becomes a server entry with the record's
// player counts, kind, password flag and texts, added to the client. A wrong size or
// a short record ends the reading (the entry being filled is not added).
void ClientSession::ReadServerList(GameBuffer* in) {
    GameInfoRecord read;   // its strings freed at the end (description first)
    GameInfoRecord* record = &read;
    GrowingBuffer buffer;
    CoderState coder;
    in->Seek(0);
    std::uint32_t size;
    ReadBytes(in, &size, 4);
    if (in->cur > in->high) in->high = in->cur;
    bool complete = false;
    if (size == static_cast<std::uint32_t>(in->PositionOf(in->high))) {
        coder.Decompress(in, &buffer);
        BufferUpdateHigh(&buffer);
        std::uint8_t* begin = buffer.begin;
        buffer.cur = begin;
        if (begin > buffer.end) buffer.Grow(0);
        for (;;) {
            if (buffer.cur > buffer.high) buffer.high = buffer.cur;
            if (buffer.PositionOf(buffer.cur) >= buffer.PositionOf(buffer.high) || (buffer.flags & 8)) {
                complete = true;
                break;
            }
            auto* entry = NewCounted<ServerEntry>();
            ReadBytes(&buffer, &entry->address, 0x10);
            if (!record->Read(&buffer)) break;
            entry->kind = record->kind;
            entry->password = record->hasPassword;
            entry->maxPlayers = record->maxPlayers;
            entry->players = record->players;
            std::string* name = &entry->name;
            if (&record->name != name) *name = record->name;
            std::string* description = &entry->description;
            if (&record->description != description) *description = record->description;
            AddServer(entry);
        }
    }
    coder.out.End();
}

} // namespace net
