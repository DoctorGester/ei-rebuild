// The game server session (vtable 0073BB70, server base 0073BBB4; pointer at 0079B5DC). See
// net_classes.h and docs/notes/unmapped_410_470.md ("Third round").
#include "net_classes.h"

#include "core_streams.h"
#include "ei/hook.h"
#include "ei/vcall.h"

#include <cstring>

namespace net {

// The handler registration templates (net_handler_registrars.cpp).
void __fastcall RegisterRequestHandler(SessionBase* self, void*, int channel, void* handler);
void __fastcall RegisterPartyHandler(SessionBase* self, void*, int channel, void* handler);
void __fastcall RegisterMemberHandler(SessionBase* self, void*, int channel, void* handler);

namespace {

// 00421800: STLport string constructor with room for n bytes, library.
__declspec(naked) StringRep* __fastcall StringReserve(StringRep* /*s*/, void*, const void* /*alloc*/, std::size_t /*n*/) EI_ORIGINAL(0x00421800)
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
__declspec(naked) void* __fastcall ListConstruct4231C0(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x004231C0)
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
    unknown_25A0.ptr = nullptr;
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

    AddHandlerFor(0x34, ConstructMessage73BBF8, 0, Handler(0x0041ADB0));
    AddHandlerFor(0x34, ConstructMessage73BBF8, 1, Handler(0x0041AD80));
    AddHandlerFor(0x34, ConstructMessage73BBF8, 2, Handler(0x0041B0C0));
    AddHandlerFor(0x18, ConstructMessage73BC28, 0, Handler(0x0041B1C0));
    AddHandlerFor(0x1C, ConstructMessage73BC40, 0, Handler(0x0041B2A0));
    AddHandlerFor(0x14, ConstructMessage73BC58, 0, Handler(0x0041B690));

    RegisterRequestHandler(this, nullptr, 2, Handler(0x0041B720));
    RegisterPartyHandler(this, nullptr, 0, Handler(0x0041B7A0));
    RegisterPartyHandler(this, nullptr, 1, Handler(0x0041B970));
    RegisterPartyHandler(this, nullptr, 2, Handler(0x0041BA30));
    RegisterMemberHandler(this, nullptr, 0, Handler(0x0041BA20));
    RegisterMemberHandler(this, nullptr, 1, Handler(0x0041BA20));

    nextLine_ = 0;
    kind_ = kind;
    maxPlayers_ = 8;
    ServerLoadList(this);
    std::memset(&announceTimer_, 0, sizeof(announceTimer_));
    public_ = 0;
    unknown_2578 = 0;
    std::memset(password_, 0, sizeof(password_));
    return this;
}
EI_REPLACE_METHOD(0x00412D80, ServerSession::Construct);

namespace {

// A list node's value (STLport list<T*>: next, prev, value at +8; some lists keep it at +0xC).
template <std::size_t At = 8>
std::uint8_t* ValueOf(void* node) { return *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(node) + At); }
void* Head(void* list) { return *static_cast<void**>(list); }
void* Next(void* node) { return *static_cast<void**>(node); }
template <typename T> T& At(const void* object, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(const_cast<void*>(object)) + offset);
}

} // namespace

// 00417EB0 (slot 13). What the connection being updated receives: the players and the variables,
// its own binding (the binding whose connection it is; its player record at +0xC, hosting when the
// record's +0x28 is set), the newer chat lines meant for everyone or for it (number +0xC above the
// record's +0x38), every shared file with, for the one a request of this connection names, its
// chunks (priority 1), the queued maps for the host, +0x25A0 and the map message, the awards for
// this connection (value at +0xC), the trade offers of the record whose items all exist (the offer
// and its items), and the trades naming the record at +0xC or +0x10. Priority 2 unless noted.
void ServerSession::CollectReplicated(ReplicatedVector** out) {
    void* record = nullptr;
    bool hosting = false;
    for (void* n = Next(Head(&players_)); n != Head(&players_); n = Next(n)) PushReplicated(*out, ValueOf(n), 2);
    for (void* n = Next(Head(&variables_)); n != Head(&variables_); n = Next(n)) PushReplicated(*out, ValueOf(n), 2);
    void* bindings = Head(&bindings_);
    if (Next(bindings) != bindings) {
        for (void* n = Next(bindings); n != Head(&bindings_); n = Next(n)) {
            std::uint8_t* slot = ValueOf(n);
            if (At<void*>(slot, 0x10) != CurrentConnection()) continue;
            record = At<void*>(slot, 0xC);
            if (At<std::uint8_t>(record, 0x28)) hosting = true;
            PushReplicated(*out, slot, 2);
        }
        if (record && !(static_cast<CRefObject*>(record)->flags7_ & CRefObject::kDetached)) {
            for (void* n = Next(Head(&lines_)); n != Head(&lines_); n = Next(n)) {
                std::uint8_t* e = ValueOf(n);
                void* target = At<void*>(e, 0x1C);
                if ((!target || target == CurrentConnection()) && At<std::int32_t>(e, 0xC) > At<std::int32_t>(record, 0x38))
                    PushReplicated(*out, e, 2);
            }
        }
    }
    for (void* n = Next(Head(&files_)); n != Head(&files_); n = Next(n)) {
        PushReplicated(*out, ValueOf(n), 2);
        void* requests = Head(&fileRequests_);
        for (void* m = Next(requests); m != requests; m = Next(m)) {
            std::uint8_t* request = ValueOf(m);
            if (At<void*>(request, 0xC) != ValueOf(n) || At<void*>(request, 0x10) != CurrentConnection()) continue;
            for (void** p = At<void**>(ValueOf(n), 0x20); p != At<void**>(ValueOf(n), 0x24); ++p) PushReplicated(*out, *p, 1);
            break;
        }
    }
    if (hosting)
        for (void* n = Next(Head(&queuedMaps_)); n != Head(&queuedMaps_); n = Next(n)) PushReplicated(*out, ValueOf(n), 2);
    if (CRefObject* object = unknown_25A0.ptr) PushReplicated(*out, object, 2);
    if (CRefObject* object = map_.ptr) PushReplicated(*out, object, 2);
    for (void* n = Next(Head(&awards_)); n != Head(&awards_); n = Next(n)) {
        std::uint8_t* e = ValueOf<0xC>(n);
        if (At<void*>(e, 0xC) == CurrentConnection()) PushReplicated(*out, e, 2);
    }
    for (void* n = Next(Head(&offers_)); n != Head(&offers_); n = Next(n)) {
        std::uint8_t* offer = ValueOf(n);
        if (At<void*>(offer, 0x2C) != record) continue;
        bool complete = true;
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(At<void**>(offer, 0x10) - At<void**>(offer, 0xC)); ++i) {
            void* member = At<void**>(offer, 0xC)[i];
            if (!member || (At<std::uint8_t>(member, 7) & 0x80)) complete = false;
        }
        if (!complete) continue;
        PushReplicated(*out, offer, 2);
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(At<void**>(ValueOf(n), 0x10) - At<void**>(ValueOf(n), 0xC)); ++i)
            PushReplicated(*out, At<void**>(ValueOf(n), 0xC)[i], 2);
    }
    for (void* n = Next(Head(&trades_)); n != Head(&trades_); n = Next(n)) {
        std::uint8_t* e = ValueOf(n);
        if (At<void*>(e, 0xC) == record || At<void*>(e, 0x10) == record) PushReplicatedInline(*out, e, 2);
    }
    for (void* n = Next(Head(&finishedTrades_)); n != Head(&finishedTrades_); n = Next(n)) {
        std::uint8_t* e = ValueOf<0xC>(n);
        if (At<void*>(e, 0xC) == record || At<void*>(e, 0x10) == record) PushReplicatedInline(*out, e, 2);
    }
}
namespace {
void __fastcall ServerCollectReplicatedHook(ServerSession* self, void*, ReplicatedVector** out) {
    self->ServerSession::CollectReplicated(out);
}
} // namespace
EI_REPLACE(0x00417EB0, ServerCollectReplicatedHook);

} // namespace net
