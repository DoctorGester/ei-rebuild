// The game server session. See
// net/net_session.h and docs/notes/network.md.
#include "engine/core/core_streams.h"

#include "engine/core/core_refcount.h"
#include "game/app/game_flow.h"
#include "game/net/net_session.h"
#include "engine/core/coder.h"
#include "game/net/net_connection.h"
#include "game/net/net_messages.h"
#include "game/net/net_second_master.h"
#include "engine/data/options.h"
#include "lib/crt.h"

#include <cstdint>
#include <cstring>
#include <format>
#include <new>
#include <string>

namespace net {

ServerBase::ServerBase() {
    socket.Open(g_gamePort);
}

ServerSession::ServerSession(std::uint32_t newKind) {
    masterAddress = NetAddress{};
    magic = kSessionMagic;
    kind = newKind;
    QueueMapList();
    std::memset(password, 0, sizeof(password));
}

// What `connection` receives: the players and the variables, its own binding (the binding
// whose connection it is; hosting when its player is the host), the newer chat lines meant for everyone or
// for it (numbered after the player's first line), every shared file with, for the one a request of this
// connection names, its chunks (priority 1), the queued maps for the host, the host message and the map
// message, the awards for this connection, the trade offers to the player whose members all exist (the offer
// and its members), and the trades naming the player. Priority 2 unless noted.
void ServerSession::CollectReplicated(const Connection* connection, std::vector<Replicated>* out) {
    PlayerMessage* record = nullptr;
    bool hosting = false;
    for (auto& n : players) PushReplicated(out, n.Get(), 2);
    for (auto& n : variables) PushReplicated(out, n.Get(), 2);
    if (!bindings.empty()) {
        for (auto& n : bindings) {
            auto* binding = n.Get();
            if (binding->connection.Get() != connection) continue;
            record = binding->player.Get();
            if (record->host) hosting = true;
            PushReplicated(out, binding, 2);
        }
        if (IsLive(record)) {
            for (auto& n : lines) {
                auto* line = n.Get();
                RefObject* target = line->connection.Get();
                if ((!target || target == connection) &&
                    static_cast<std::int32_t>(line->number) > static_cast<std::int32_t>(record->firstLine))
                    PushReplicated(out, line, 2);
            }
        }
    }
    for (auto& n : files) {
        PushReplicated(out, n.Get(), 2);
        for (auto& m : fileRequests) {
            auto* request = m.Get();
            if (request->file.Get() != n.Get() || request->connection.Get() != connection) continue;
            for (const auto& chunk : n.Get()->chunks) PushReplicated(out, chunk.Get(), 1);
            break;
        }
    }
    if (hosting)
        for (auto& n : queuedMaps) PushReplicated(out, n.Get(), 2);
    if (HostMessage* object = hostInfo.Get()) PushReplicated(out, object, 2);
    if (MapMessage* object = map.Get()) PushReplicated(out, object, 2);
    for (auto& n : awards) {
        AwardMessage* award = n.ref.Get();
        if (award->connection.Get() == connection) PushReplicated(out, award, 2);
    }
    for (auto& n : offers) {
        auto* offer = n.Get();
        if (offer->partner.Get() != record) continue;
        bool complete = true;
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(offer->members.size()); ++i) {
            RefObject* member = offer->members[i].Get();
            if (!IsLive(member)) complete = false;
        }
        if (!complete) continue;
        PushReplicated(out, offer, 2);
        const auto& members = n.Get()->members;
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(members.size()); ++i)
            PushReplicated(out, n.Get()->members[i].Get(), 2);
    }
    for (auto& n : trades) {
        auto* trade = n.Get();
        if (trade->player.Get() == record || trade->partner.Get() == record) PushReplicated(out, trade, 2);
    }
    for (auto& n : finishedTrades) {
        TradeMessage* trade = n.ref.Get();
        if (trade->player.Get() == record || trade->partner.Get() == record) PushReplicated(out, trade, 2);
    }
}

// --- Entry -------------------------------------------------------------------------------------------

// The game server's description of itself: a server entry like those the
// client reads from the server list (net_server.cpp). See net/net_session.h and docs/notes/network.md.

// Makes `*out` a new server entry: the name, the kind, the current map's title as the description when a map
// message is held (else empty), the number of clients, the maximum and whether a password is set.
void ServerSession::Describe(Ref<ServerEntry>* out) {
    auto* entry = new ServerEntry();
    entry->name = name;
    entry->kind = kind;
    std::string* description = &entry->description;
    description->clear();

    RefObject* newMap = map.Get();
    if (IsLive(newMap)) description->assign(SessionMapName());    // overwrite, then cut or append

    entry->players = static_cast<std::uint8_t>(ClientCount());
    entry->maxPlayers = static_cast<std::uint8_t>(maxPlayers);
    entry->password = password[0] != 0;

    ShareAssignOwned(*out, entry);
}
// When announced, the master's address is set from the host name and port 28004.
void ServerSession::SetPublic(bool announce) {
    announced = announce;
    if (announce) masterAddress.Parse(options::g_options.masterServerName, 28004);
}

// --- Life --------------------------------------------------------------------------------------------

// The game server's destructor and its base layer's. The members are torn down in reverse order: the lists are
// cleared between the counted pointers' releases; then the session base. See net/net_session.h and
// docs/notes/network.md.

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

ServerBase::~ServerBase() {}

// --- Handlers ----------------------------------------------------------------------------------------

// The game server's talk with the master server and a chat handler: the announcement (from the tick), the
// master server's answer and a client's chat line (registered for SayMessage by ServerSession's constructor).
// All run from the session's tick on the main thread (net::Pump). Rebirth's EI_Plugin.asi patches the
// announcement and the master answer (NewMaster; both reproduced: the answer's NOPs and the announcement's
// copy of the announcement to the second master server, net_second_master.h) and the addon's ChatLog has a
// site in the chat handler (disabled in this install). See docs/notes/network.md and rebirth.md.

// The record is written into a growing buffer and sent from the session's socket to the master's address; the
// result of the send is not looked at.
void ServerSession::AnnounceToMaster() {
    GrowingBuffer buffer;
    GameInfoRecord info;
    std::string& infoName = info.name;
    std::string& description = info.description;

    infoName = name;
    info.hasPassword = password[0] != 0;
    info.masterKey = masterKey;
    info.kind = static_cast<std::uint8_t>(kind);
    description.clear();
    RefObject* newMap = map.Get();
    if (IsLive(newMap)) description.assign(SessionMapName());
    info.players = static_cast<std::uint8_t>(ClientCount());
    info.maxPlayers = static_cast<std::uint8_t>(maxPlayers);
    info.masterToken = masterToken;
    info.magic = kSessionMagic;
    info.Write(&buffer);
    socket.SendTo(&masterAddress, &buffer);
    // EI_Plugin's NewMaster (its call sends as SendTo does, then the copy to the second master when one is set).
    second_master::CopyAnnouncement(socket.socket, buffer);

}

// A PacketKind::Master packet, from any address (EI_Plugin's NewMaster, always on, NOPs the original's test of the
// master server's address, so the answer of its second master server counts). Its first dword must be the session's
// master key; the second is then kept (the next announcement sends it back). The first dword is consumed either way.
void ServerSession::OnMaster(const NetAddress*, BufferCursor* cursor) {
    std::uint32_t key;
    cursor->Take(key);
    if (key != masterKey) return;
    cursor->Take(masterToken);
}

// The clients' messages. A file request keeps its sender's connection while it is listed; a trade offer's
// member moves its version on when created or changed, so the connections send it again.
void ServerSession::OnMessage(Connection* from, MessageEvent event, Message* message) {
    using enum MessageEvent;
    switch (message->Type()) {
    case LoginMessage::kType: {
        auto* login = static_cast<LoginMessage*>(message);
        if (event == Created) OnJoin(from, login);
        if (event == Changed) OnPlayerChanged(login);
        if (event == Removed) OnPlayerGone(login);
        return;
    }
    case SayMessage::kType:
        if (event == Created) OnSay(from, static_cast<SayMessage*>(message));
        return;
    case CommandMessage::kType:
        if (event == Created) OnCommand(from, static_cast<CommandMessage*>(message));
        return;
    case FileRequestMessage::kType:
        if (event == Created) {
            auto* request = static_cast<FileRequestMessage*>(message);
            request->connection.Assign(from);
            fileRequests.emplace_back(request);
        }
        if (event == Removed) std::erase_if(fileRequests, [message](const auto& request) { return request.Get() == message; });
        return;
    case PartyMessage::kType: {
        auto* offer = static_cast<PartyMessage*>(message);
        if (event == Created) OnTradeOffer(offer);
        if (event == Changed) OnTradeUpdate(offer);
        if (event == Removed) OnTradeCancel(offer);
        return;
    }
    case PartyMemberMessage::kType:
        if (event != Removed) ++message->version;
        return;
    default:
        return;   // the server's own messages
    }
}

// A chat line from `from` (nothing when it is gone or detached): the first binding of that connection names the
// player (nothing without one, or when the binding is detached), and the line is announced to everybody as
// "^<number><name>^7: <text>".
void ServerSession::OnSay(Connection* from, SayMessage* message) {
    if (!IsLive(from)) return;
    for (auto& node : bindings) {
        auto* binding = node.Get();
        if (binding->connection.Get() != from) continue;
        // (The original also tests the binding for null here, after having read it.)
        if (!IsLive(binding)) return;
        auto* player = binding->player.Get();
        std::string line;
        line = std::format("^{}{}^7: {}", player->number, player->name.data(), message->text.data());
        Announce(line, nullptr);
        return;
    }
}

// --- Tick --------------------------------------------------------------------------------------------

// The game server's tick and a player state query (called by the file chunk sender). Float maths in plain C++
// (rewrite-guide rule 7). See net/net_session.h and docs/notes/network.md.

// The base layer's tick, then the timed entries of the finished trades and the awards count down by `dt` (as
// a float), and when the game is public the announcement timer does too: when it runs out it is reset and,
// with a master server host set, the game is announced. `dt` holds a float value (net::Pump).
void ServerSession::Tick(double dt) {
    ServerBase::Tick(dt);
    float step = static_cast<float>(dt);
    CountDown(finishedTrades, step);
    CountDown(awards, step);
    if (!announced) return;
    announceTimer -= step;
    if (announceTimer < 0.0f) {    // run out
        announceTimer = 20.0f;
        if (!options::g_options.masterServerName.empty()) AnnounceToMaster();
    }
}

// Whether the player bound to `connection` (its first binding) is Connecting.
// False for a dead connection, binding or player.
bool ServerSession::IsConnecting(const Connection* connection) {
    if (!IsLive(connection)) return false;
    auto* binding = BindingOf(connection);
    if (!binding || !IsLive(binding)) return false;
    auto* player = binding->player.Get();
    return IsLive(player) && player->state == PlayerStatus::Connecting;
}

BindingMessage* ServerSession::BindingOf(const Connection* connection) {
    for (auto& node : bindings)
        if (node->connection.Get() == connection) return node.Get();
    return nullptr;
}

// --- List --------------------------------------------------------------------------------------------

// The server list reply: a compressed run of {address, GameInfoRecord} turned into the
// client base's server entries. See net/net_session.h and docs/notes/network.md.

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
    in->Get(size);
    bool complete = false;
    if (size == static_cast<std::uint32_t>(in->Size())) {
        coder.Decompress(in, &buffer);
        buffer.Seek(0);
        for (;;) {
            if (buffer.Unread() <= 0 || buffer.readPastEnd) {
                complete = true;
                break;
            }
            auto* entry = new ServerEntry();
            buffer.Get(entry->address);
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
