// The game server session (vtable 0073BB70, server base 0073BBB4; pointer at 0079B5DC). See
// net_classes.h and docs/notes/network.md.
#include "net_classes.h"
#include "net_messages.h"
#include "net_connection.h"

#include "core_streams.h"
#include "ei/hook.h"
#include "ei/vcall.h"
#include "originals_library.h"

#include <cstring>

namespace net {


namespace {

// 00421800: STLport string constructor with room for n bytes, library.

void* Handler(std::uintptr_t address) { return reinterpret_cast<void*>(address); }

// 00783E30: the game's UDP port (read at run time, as the original does).
int GamePort() { return *reinterpret_cast<const volatile int*>(0x00783E30); }

} // namespace

// The server base's constructor is inlined: the session base, the pending joins, the socket bound
// to the game port.
ServerSession* ServerSession::Construct(std::uint32_t kind) {
    SessionBase::Construct();
    joins_.Construct();
    ei::SetVtable(this, ServerBase::kVtableAddress);
    socket_.Open(GamePort());
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
    stlp::StringElement::Default(&name_);
    stlp::StringElement::Default(&mapName_);
    ei::SetVtable(this, kVtableAddress);
    magic_ = kSessionMagic;
    RegisterMessageTypes();

    AddHandlerFor<LoginMessage>(0, Handler(0x0041ADB0));
    AddHandlerFor<LoginMessage>(1, Handler(0x0041AD80));
    AddHandlerFor<LoginMessage>(2, Handler(0x0041B0C0));
    AddHandlerFor<SayMessage>(0, Handler(0x0041B1C0));
    AddHandlerFor<CommandMessage>(0, Handler(0x0041B2A0));
    AddHandlerFor<FileRequestMessage>(0, Handler(0x0041B690));

    RegisterRequestHandler(2, Handler(0x0041B720));
    RegisterPartyHandler(0, Handler(0x0041B7A0));
    RegisterPartyHandler(1, Handler(0x0041B970));
    RegisterPartyHandler(2, Handler(0x0041BA30));
    RegisterMemberHandler(0, Handler(0x0041BA20));
    RegisterMemberHandler(1, Handler(0x0041BA20));

    nextLine_ = 0;
    kind_ = kind;
    maxPlayers_ = 8;
    QueueMapList();
    std::memset(&announceTimer_, 0, sizeof(announceTimer_));
    public_ = 0;
    masterToken_ = 0;
    std::memset(password_, 0, sizeof(password_));
    return this;
}
EI_REPLACE_METHOD(0x00412D80, ServerSession::Construct);

namespace {

// A list node's message: lists of counted messages (RefNode), and the timed lists (TimedNode:
// {seconds, object}).
template <typename T = Message>
T* ValueOf(const RefNode* node) { return static_cast<T*>(node->data.ptr); }
template <typename T>
T* TimedValueOf(const TimedNode* node) { return static_cast<T*>(node->data.ref.ptr); }

} // namespace

// 00417EB0 (slot 13). What the connection being updated receives: the players and the variables,
// its own binding (the binding whose connection it is; hosting when its player is the host), the
// newer chat lines meant for everyone or for it (numbered after the player's first line), every
// shared file with, for the one a request of this connection names, its chunks (priority 1), the
// queued maps for the host, the host message and the map message, the awards for this connection,
// the trade offers to the player whose members all exist (the offer and its members), and the
// trades naming the player. Priority 2 unless noted.
void ServerSession::CollectReplicatedImpl(ReplicatedVector** out) {
    PlayerMessage* record = nullptr;
    bool hosting = false;
    for (RefNode* n = players_.First(); n != players_.End(); n = n->next) PushReplicated(*out, ValueOf(n), 2);
    for (RefNode* n = variables_.First(); n != variables_.End(); n = n->next) PushReplicated(*out, ValueOf(n), 2);
    if (!bindings_.Empty()) {
        for (RefNode* n = bindings_.First(); n != bindings_.End(); n = n->next) {
            auto* binding = ValueOf<BindingMessage>(n);
            if (binding->ConnectionRef().ptr != CurrentConnection()) continue;
            record = static_cast<PlayerMessage*>(binding->PlayerRef().ptr);
            if (record->host_) hosting = true;
            PushReplicated(*out, binding, 2);
        }
        if (record && !(record->flags7_ & CRefObject::kDetached)) {
            for (RefNode* n = lines_.First(); n != lines_.End(); n = n->next) {
                auto* line = ValueOf<LineMessage>(n);
                CRefObject* target = line->connection_.ptr;
                if ((!target || target == CurrentConnection()) &&
                    static_cast<std::int32_t>(line->number_) > static_cast<std::int32_t>(record->firstLine_))
                    PushReplicated(*out, line, 2);
            }
        }
    }
    for (RefNode* n = files_.First(); n != files_.End(); n = n->next) {
        PushReplicated(*out, ValueOf(n), 2);
        RefNode* requests = fileRequests_.End();
        for (RefNode* m = requests->next; m != requests; m = m->next) {
            auto* request = ValueOf<FileRequestMessage>(m);
            if (request->FileRef().ptr != ValueOf(n) || request->ConnectionRef().ptr != CurrentConnection()) continue;
            const RefVector& chunks = ValueOf<FileMessage>(n)->chunks_;
            for (RefPtr* p = chunks.begin(); p != chunks.end(); ++p) PushReplicated(*out, p->ptr, 1);
            break;
        }
    }
    if (hosting)
        for (RefNode* n = queuedMaps_.First(); n != queuedMaps_.End(); n = n->next) PushReplicated(*out, ValueOf(n), 2);
    if (CRefObject* object = hostInfo_.ptr) PushReplicated(*out, object, 2);
    if (CRefObject* object = map_.ptr) PushReplicated(*out, object, 2);
    for (TimedNode* n = awards_.First(); n != awards_.End(); n = n->next) {
        auto* award = TimedValueOf<AwardMessage>(n);
        if (award->connection_.ptr == CurrentConnection()) PushReplicated(*out, award, 2);
    }
    for (RefNode* n = offers_.First(); n != offers_.End(); n = n->next) {
        auto* offer = ValueOf<PartyMessage>(n);
        if (offer->partner_.ptr != record) continue;
        bool complete = true;
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(offer->members_.size()); ++i) {
            CRefObject* member = offer->members_.begin()[i].ptr;
            if (!member || (member->flags7_ & CRefObject::kDetached)) complete = false;
        }
        if (!complete) continue;
        PushReplicated(*out, offer, 2);
        const RefVector& members = ValueOf<PartyMessage>(n)->members_;
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(members.size()); ++i)
            PushReplicated(*out, ValueOf<PartyMessage>(n)->members_.begin()[i].ptr, 2);
    }
    for (RefNode* n = trades_.First(); n != trades_.End(); n = n->next) {
        auto* trade = ValueOf<TradeMessage>(n);
        if (trade->player_.ptr == record || trade->partner_.ptr == record) PushReplicatedInline(*out, trade, 2);
    }
    for (TimedNode* n = finishedTrades_.First(); n != finishedTrades_.End(); n = n->next) {
        auto* trade = TimedValueOf<TradeMessage>(n);
        if (trade->player_.ptr == record || trade->partner_.ptr == record) PushReplicatedInline(*out, trade, 2);
    }
}
EI_REPLACE_METHOD(0x00417EB0, ServerSession::CollectReplicatedImpl);

} // namespace net
