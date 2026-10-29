// The game server session. See
// net_classes.h and docs/notes/network.md.
#include "core_streams.h"
#include "net_classes.h"
#include "net_connection.h"
#include "net_handlers.h"
#include "net_messages.h"

#include <cstring>

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
            record = binding->Player();
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
