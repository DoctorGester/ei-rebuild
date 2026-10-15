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
inline StringRep* __fastcall StringReserve(StringRep* a0, void*, const void* a2, std::size_t a3) { return (StringRep*)StringReserveOriginal((void*)a0, nullptr, a2, a3); }
// 00418530: fills the queued maps from the game flow (ServerSession::QueueMapList), through its address.
__declspec(naked) void __fastcall ServerLoadList(ServerSession* /*server*/) EI_ORIGINAL(0x00418530)

// STLport list constructors, list taking the allocator; one per element type, library.
__declspec(naked) void* __fastcall ListConstruct41FD30(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x0041FD30)
__declspec(naked) void* __fastcall ListConstruct422400(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00422400)
__declspec(naked) void* __fastcall ListConstruct4225E0(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x004225E0)
__declspec(naked) void* __fastcall ListConstruct422800(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00422800)
__declspec(naked) void* __fastcall ListConstruct4229E0(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x004229E0)
__declspec(naked) void* __fastcall ListConstruct422BE0(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00422BE0)
__declspec(naked) void* __fastcall ListConstruct422DC0(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00422DC0)
__declspec(naked) void* __fastcall ListConstruct4221E0(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x004221E0)
__declspec(naked) void* __fastcall ListConstruct422FE0(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00422FE0)
__declspec(naked) void* __fastcall ListConstruct423440(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00423440)
__declspec(naked) void* __fastcall ListConstruct423700(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00423700)
__declspec(naked) void* __fastcall ListConstruct421B20(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00421B20)

void* Handler(std::uintptr_t address) { return reinterpret_cast<void*>(address); }

// 00783E30: the game's UDP port (read at run time, as the original does).
int GamePort() { return *reinterpret_cast<const volatile int*>(0x00783E30); }

} // namespace

// The server base's constructor is inlined: the session base, the pending joins, the socket bound
// to the game port.
ServerSession* ServerSession::Construct(std::uint32_t kind) {
    char allocator = 0;   // the containers' (empty) allocator argument
    SessionBase::Construct();
    ListConstruct41FD30(&joins_, nullptr, &allocator);
    ei::SetVtable(this, ServerBase::kVtableAddress);
    socket_.Open(GamePort());
    masterAddress_ = NetAddress{};
    ListConstruct422400(&players_, nullptr, &allocator);
    ListConstruct4225E0(&lines_, nullptr, &allocator);
    ListConstruct422800(&files_, nullptr, &allocator);
    ListConstruct4229E0(&fileRequests_, nullptr, &allocator);
    hostInfo_.ptr = nullptr;
    ListConstruct422BE0(&bindings_, nullptr, &allocator);
    map_.ptr = nullptr;
    ListConstruct422DC0(&queuedMaps_, nullptr, &allocator);
    ListConstruct4221E0(&variables_, nullptr, &allocator);
    ListConstruct422FE0(&awards_, nullptr, &allocator);
    ListConstruct4231C0(&offers_, nullptr, &allocator);
    ListConstruct423440(&trades_, nullptr, &allocator);
    ListConstruct423700(&finishedTrades_, nullptr, &allocator);
    ListConstruct421B20(&banned_, nullptr, &allocator);
    StringReserve(&name_, nullptr, &allocator, 8);
    *name_.finish = 0;
    StringReserve(&mapName_, nullptr, &allocator, 8);
    *mapName_.finish = 0;
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
    ServerLoadList(this);
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
T* ValueOf(void* node) { return static_cast<T*>(static_cast<RefNode*>(node)->data.ptr); }
template <typename T>
T* TimedValueOf(void* node) { return static_cast<T*>(static_cast<TimedNode*>(node)->data.object.ptr); }
void* Head(void* list) { return *static_cast<void**>(list); }
void* Next(void* node) { return *static_cast<void**>(node); }

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
    for (void* n = Next(Head(&players_)); n != Head(&players_); n = Next(n)) PushReplicated(*out, ValueOf(n), 2);
    for (void* n = Next(Head(&variables_)); n != Head(&variables_); n = Next(n)) PushReplicated(*out, ValueOf(n), 2);
    void* bindings = Head(&bindings_);
    if (Next(bindings) != bindings) {
        for (void* n = Next(bindings); n != Head(&bindings_); n = Next(n)) {
            auto* binding = ValueOf<BindingMessage>(n);
            if (binding->ConnectionRef().ptr != CurrentConnection()) continue;
            record = static_cast<PlayerMessage*>(binding->PlayerRef().ptr);
            if (record->host_) hosting = true;
            PushReplicated(*out, binding, 2);
        }
        if (record && !(record->flags7_ & CRefObject::kDetached)) {
            for (void* n = Next(Head(&lines_)); n != Head(&lines_); n = Next(n)) {
                auto* line = ValueOf<LineMessage>(n);
                CRefObject* target = line->connection_.ptr;
                if ((!target || target == CurrentConnection()) &&
                    static_cast<std::int32_t>(line->number_) > static_cast<std::int32_t>(record->firstLine_))
                    PushReplicated(*out, line, 2);
            }
        }
    }
    for (void* n = Next(Head(&files_)); n != Head(&files_); n = Next(n)) {
        PushReplicated(*out, ValueOf(n), 2);
        void* requests = Head(&fileRequests_);
        for (void* m = Next(requests); m != requests; m = Next(m)) {
            auto* request = ValueOf<FileRequestMessage>(m);
            if (request->FileRef().ptr != ValueOf(n) || request->ConnectionRef().ptr != CurrentConnection()) continue;
            const RefVector& chunks = ValueOf<FileMessage>(n)->chunks_;
            for (RefPtr* p = chunks.begin(); p != chunks.end(); ++p) PushReplicated(*out, p->ptr, 1);
            break;
        }
    }
    if (hosting)
        for (void* n = Next(Head(&queuedMaps_)); n != Head(&queuedMaps_); n = Next(n)) PushReplicated(*out, ValueOf(n), 2);
    if (CRefObject* object = hostInfo_.ptr) PushReplicated(*out, object, 2);
    if (CRefObject* object = map_.ptr) PushReplicated(*out, object, 2);
    for (void* n = Next(Head(&awards_)); n != Head(&awards_); n = Next(n)) {
        auto* award = TimedValueOf<AwardMessage>(n);
        if (award->connection_.ptr == CurrentConnection()) PushReplicated(*out, award, 2);
    }
    for (void* n = Next(Head(&offers_)); n != Head(&offers_); n = Next(n)) {
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
    for (void* n = Next(Head(&trades_)); n != Head(&trades_); n = Next(n)) {
        auto* trade = ValueOf<TradeMessage>(n);
        if (trade->player_.ptr == record || trade->partner_.ptr == record) PushReplicatedInline(*out, trade, 2);
    }
    for (void* n = Next(Head(&finishedTrades_)); n != Head(&finishedTrades_); n = Next(n)) {
        auto* trade = TimedValueOf<TradeMessage>(n);
        if (trade->player_.ptr == record || trade->partner_.ptr == record) PushReplicatedInline(*out, trade, 2);
    }
}
namespace {
} // namespace
EI_REPLACE_METHOD(0x00417EB0, ServerSession::CollectReplicatedImpl);

} // namespace net
