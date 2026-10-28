// The game server session. See
// net_classes.h and docs/notes/network.md.
#include "net_classes.h"
#include "net_handlers.h"
#include "net_messages.h"
#include "net_connection.h"

#include "core_streams.h"
#include "game_data.h"

#include <cstring>

namespace net {


namespace {



} // namespace

ServerBase::ServerBase() {
    joins_.Construct();
    socket_.Open(g_gamePort);
}

ServerSession::ServerSession(std::uint32_t kind) {
    masterAddress_ = NetAddress{};
    players_.Construct();
    lines_.Construct();
    files_.Construct();
    fileRequests_.Construct();
    hostInfo_.ptr = nullptr;
    bindings_.Construct();
    map_.ptr = nullptr;
    queuedMaps_.Construct();
    variables_.Construct();
    awards_.Construct();
    offers_.Construct();
    trades_.Construct();
    finishedTrades_.Construct();
    banned_.Construct();
    stlp::string::Reserve(name_, 8);
    stlp::string::Reserve(mapName_, 8);
    magic_ = kSessionMagic;
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

    nextLine_ = 0;
    kind_ = kind;
    maxPlayers_ = 8;
    QueueMapList();
    std::memset(&announceTimer_, 0, sizeof(announceTimer_));
    public_ = 0;
    masterToken_ = 0;
    std::memset(password_, 0, sizeof(password_));
}

namespace {

// A timed list node's message (TimedNode: {seconds, object}).
template <typename T>
T* TimedValueOf(const TimedNode* node) { return static_cast<T*>(node->data.ref.ptr); }

} // namespace

// What the connection being updated receives: the players and the variables, its own binding (the binding
// whose connection it is; hosting when its player is the host), the newer chat lines meant for everyone or
// for it (numbered after the player's first line), every shared file with, for the one a request of this
// connection names, its chunks (priority 1), the queued maps for the host, the host message and the map
// message, the awards for this connection, the trade offers to the player whose members all exist (the offer
// and its members), and the trades naming the player. Priority 2 unless noted.
void ServerSession::CollectReplicated(ReplicatedVector** out) {
    PlayerMessage* record = nullptr;
    bool hosting = false;
    for (RefNode* n = players_.First(); n != players_.End(); n = n->next) PushReplicated(*out, players_.At(n), 2);
    for (RefNode* n = variables_.First(); n != variables_.End(); n = n->next) PushReplicated(*out, variables_.At(n), 2);
    if (!bindings_.Empty()) {
        for (RefNode* n = bindings_.First(); n != bindings_.End(); n = n->next) {
            auto* binding = bindings_.At(n);
            if (binding->ConnectionRef().ptr != g_currentConnection) continue;
            record = binding->Player();
            if (record->host_) hosting = true;
            PushReplicated(*out, binding, 2);
        }
        if (record && !(record->flags7_ & CRefObject::kDetached)) {
            for (RefNode* n = lines_.First(); n != lines_.End(); n = n->next) {
                auto* line = lines_.At(n);
                CRefObject* target = line->connection_.ptr;
                if ((!target || target == g_currentConnection) &&
                    static_cast<std::int32_t>(line->number_) > static_cast<std::int32_t>(record->firstLine_))
                    PushReplicated(*out, line, 2);
            }
        }
    }
    for (RefNode* n = files_.First(); n != files_.End(); n = n->next) {
        PushReplicated(*out, files_.At(n), 2);
        RefNode* requests = fileRequests_.End();
        for (RefNode* m = requests->next; m != requests; m = m->next) {
            auto* request = fileRequests_.At(m);
            if (request->FileRef().ptr != files_.At(n) || request->ConnectionRef().ptr != g_currentConnection) continue;
            const RefVector& chunks = files_.At(n)->chunks_;
            for (RefPtr* p = chunks.begin(); p != chunks.end(); ++p) PushReplicated(*out, p->ptr, 1);
            break;
        }
    }
    if (hosting)
        for (RefNode* n = queuedMaps_.First(); n != queuedMaps_.End(); n = n->next) PushReplicated(*out, queuedMaps_.At(n), 2);
    if (CRefObject* object = hostInfo_.ptr) PushReplicated(*out, object, 2);
    if (CRefObject* object = map_.ptr) PushReplicated(*out, object, 2);
    for (TimedNode* n = awards_.First(); n != awards_.End(); n = n->next) {
        auto* award = TimedValueOf<AwardMessage>(n);
        if (award->connection_.ptr == g_currentConnection) PushReplicated(*out, award, 2);
    }
    for (RefNode* n = offers_.First(); n != offers_.End(); n = n->next) {
        auto* offer = offers_.At(n);
        if (offer->partner_.ptr != record) continue;
        bool complete = true;
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(offer->members_.size()); ++i) {
            CRefObject* member = offer->members_.begin()[i].ptr;
            if (!member || (member->flags7_ & CRefObject::kDetached)) complete = false;
        }
        if (!complete) continue;
        PushReplicated(*out, offer, 2);
        const RefVector& members = offers_.At(n)->members_;
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(members.size()); ++i)
            PushReplicated(*out, offers_.At(n)->members_.begin()[i].ptr, 2);
    }
    for (RefNode* n = trades_.First(); n != trades_.End(); n = n->next) {
        auto* trade = trades_.At(n);
        if (trade->player_.ptr == record || trade->partner_.ptr == record) PushReplicatedInline(*out, trade, 2);
    }
    for (TimedNode* n = finishedTrades_.First(); n != finishedTrades_.End(); n = n->next) {
        auto* trade = TimedValueOf<TradeMessage>(n);
        if (trade->player_.ptr == record || trade->partner_.ptr == record) PushReplicatedInline(*out, trade, 2);
    }
}

} // namespace net
