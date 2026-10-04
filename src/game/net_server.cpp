// The game server session (vtable 0073BB70, server base 0073BBB4; pointer at 0079B5DC). See
// net_classes.h and docs/notes/unmapped_410_470.md ("Third round").
#include "net_classes.h"

#include "ei/hook.h"

#include <cstring>

namespace net {

// The handler registration templates (net_handler_registrars.cpp).
void __fastcall RegisterRequestHandler(SessionBase* self, void*, int channel, void* handler);
void __fastcall RegisterPartyHandler(SessionBase* self, void*, int channel, void* handler);
void __fastcall RegisterMemberHandler(SessionBase* self, void*, int channel, void* handler);

namespace {

// 004409A0: opens the session's UDP socket, bound to `port` when it is positive. Returns 1 / 0.
__declspec(naked) int __fastcall SocketDriverOpen(SocketDriver* /*driver*/, void*, int /*port*/) EI_ORIGINAL(0x004409A0)
// 00421800: STLport string constructor with room for n bytes, library.
__declspec(naked) StringRep* __fastcall StringReserve(StringRep* /*s*/, void*, const void* /*alloc*/, std::size_t /*n*/) EI_ORIGINAL(0x00421800)
// 00418530: fills the server's list at +0x25AC from the game's settings; not rewritten yet.
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

void SetVtable(void* object, std::uintptr_t vtable) { std::memcpy(object, &vtable, sizeof(vtable)); }

void* Handler(std::uintptr_t address) { return reinterpret_cast<void*>(address); }

// 00783E30: the game's UDP port (read at run time, as the original does).
int GamePort() { return *reinterpret_cast<const volatile int*>(0x00783E30); }

} // namespace

ServerSession* ServerSession::Construct(std::uint32_t arg) {
    char allocator = 0;   // the containers' (empty) allocator argument
    base.Construct();
    ListConstruct41FD30(&list_2568, nullptr, &allocator);
    SetVtable(this, kServerBaseVtable);
    SocketDriverOpen(&base.socket, nullptr, GamePort());
    unknown_257C[0] = 0;
    unknown_257C[1] = 0;
    unknown_257C[2] = 0;
    unknown_257C[3] = 0;
    ListConstruct422400(&list_2590, nullptr, &allocator);
    ListConstruct4225E0(&list_2594, nullptr, &allocator);
    ListConstruct422800(&list_2598, nullptr, &allocator);
    ListConstruct4229E0(&list_259C, nullptr, &allocator);
    unknown_25A0 = 0;
    ListConstruct422BE0(&list_25A4, nullptr, &allocator);
    unknown_25A8 = 0;
    ListConstruct422DC0(&list_25AC, nullptr, &allocator);
    ListConstruct4221E0(&list_25B0, nullptr, &allocator);
    ListConstruct422FE0(&list_25B4, nullptr, &allocator);
    ListConstruct4231C0(&list_25B8, nullptr, &allocator);
    ListConstruct423440(&list_25BC, nullptr, &allocator);
    ListConstruct423700(&list_25C0, nullptr, &allocator);
    ListConstruct421B20(&list_25C4, nullptr, &allocator);
    StringReserve(&string_25C8, nullptr, &allocator, 8);
    *string_25C8.finish = 0;
    StringReserve(&string_25D4, nullptr, &allocator, 8);
    *string_25D4.finish = 0;
    SetVtable(this, kServerVtable);
    base.unknown_08 = kSessionMagic;
    base.RegisterMessageTypes();

    base.AddHandlerFor(0x34, ConstructMessage73BBF8, 0, Handler(0x0041ADB0));
    base.AddHandlerFor(0x34, ConstructMessage73BBF8, 1, Handler(0x0041AD80));
    base.AddHandlerFor(0x34, ConstructMessage73BBF8, 2, Handler(0x0041B0C0));
    base.AddHandlerFor(0x18, ConstructMessage73BC28, 0, Handler(0x0041B1C0));
    base.AddHandlerFor(0x1C, ConstructMessage73BC40, 0, Handler(0x0041B2A0));
    base.AddHandlerFor(0x14, ConstructMessage73BC58, 0, Handler(0x0041B690));

    RegisterRequestHandler(&base, nullptr, 2, Handler(0x0041B720));
    RegisterPartyHandler(&base, nullptr, 0, Handler(0x0041B7A0));
    RegisterPartyHandler(&base, nullptr, 1, Handler(0x0041B970));
    RegisterPartyHandler(&base, nullptr, 2, Handler(0x0041BA30));
    RegisterMemberHandler(&base, nullptr, 0, Handler(0x0041BA20));
    RegisterMemberHandler(&base, nullptr, 1, Handler(0x0041BA20));

    unknown_258C = 0;
    unknown_25E4 = arg;
    unknown_25E0 = 8;
    ServerLoadList(this);
    unknown_2570 = 0;
    unknown_2574 = 0;
    unknown_2578 = 0;
    for (std::uint32_t& v : unknown_25E8) v = 0;
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

// 00417EB0 (slot 13). What the connection being updated receives: the shared objects (+0x2590,
// +0x25B0), its own player slot (+0x25A4: the slot whose connection it is; its record at +0xC,
// hosting when the record's +0x28 is set), the newer entries of +0x2594 meant for everyone or for
// it (+0xC above the record's +0x38), every object of +0x2598 with, for the one +0x259C pairs with
// this connection, its vector at +0x20 (priority 1), the host's extras (+0x25AC), +0x25A0 / +0x25A8,
// the entries of +0x25B4 (value at +0xC) for this connection, the groups of +0x25B8 owned by the
// record whose members all exist (the group and its members), and the entries of +0x25BC / +0x25C0
// naming the record at +0xC or +0x10. Priority 2 unless noted.
void __fastcall ServerCollectReplicated(ServerSession* self, void*, ReplicatedVector** out) {
    void* record = nullptr;
    bool hosting = false;
    for (void* n = Next(Head(&self->list_2590)); n != Head(&self->list_2590); n = Next(n)) PushReplicated(*out, ValueOf(n), 2);
    for (void* n = Next(Head(&self->list_25B0)); n != Head(&self->list_25B0); n = Next(n)) PushReplicated(*out, ValueOf(n), 2);
    void* players = Head(&self->list_25A4);
    if (Next(players) != players) {
        for (void* n = Next(players); n != Head(&self->list_25A4); n = Next(n)) {
            std::uint8_t* slot = ValueOf(n);
            if (At<void*>(slot, 0x10) != CurrentConnection()) continue;
            record = At<void*>(slot, 0xC);
            if (At<std::uint8_t>(record, 0x28)) hosting = true;
            PushReplicated(*out, slot, 2);
        }
        if (record && !(At<std::uint32_t>(record, 4) & 0x80000000u)) {
            for (void* n = Next(Head(&self->list_2594)); n != Head(&self->list_2594); n = Next(n)) {
                std::uint8_t* e = ValueOf(n);
                void* target = At<void*>(e, 0x1C);
                if ((!target || target == CurrentConnection()) && At<std::int32_t>(e, 0xC) > At<std::int32_t>(record, 0x38))
                    PushReplicated(*out, e, 2);
            }
        }
    }
    for (void* n = Next(Head(&self->list_2598)); n != Head(&self->list_2598); n = Next(n)) {
        PushReplicated(*out, ValueOf(n), 2);
        void* pairs = Head(&self->list_259C);
        for (void* m = Next(pairs); m != pairs; m = Next(m)) {
            std::uint8_t* pair = ValueOf(m);
            if (At<void*>(pair, 0xC) != ValueOf(n) || At<void*>(pair, 0x10) != CurrentConnection()) continue;
            for (void** p = At<void**>(ValueOf(n), 0x20); p != At<void**>(ValueOf(n), 0x24); ++p) PushReplicated(*out, *p, 1);
            break;
        }
    }
    if (hosting)
        for (void* n = Next(Head(&self->list_25AC)); n != Head(&self->list_25AC); n = Next(n)) PushReplicated(*out, ValueOf(n), 2);
    if (void* object = reinterpret_cast<void*>(self->unknown_25A0)) PushReplicated(*out, object, 2);
    if (void* object = reinterpret_cast<void*>(self->unknown_25A8)) PushReplicated(*out, object, 2);
    for (void* n = Next(Head(&self->list_25B4)); n != Head(&self->list_25B4); n = Next(n)) {
        std::uint8_t* e = ValueOf<0xC>(n);
        if (At<void*>(e, 0xC) == CurrentConnection()) PushReplicated(*out, e, 2);
    }
    for (void* n = Next(Head(&self->list_25B8)); n != Head(&self->list_25B8); n = Next(n)) {
        std::uint8_t* group = ValueOf(n);
        if (At<void*>(group, 0x2C) != record) continue;
        bool complete = true;
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(At<void**>(group, 0x10) - At<void**>(group, 0xC)); ++i) {
            void* member = At<void**>(group, 0xC)[i];
            if (!member || (At<std::uint8_t>(member, 7) & 0x80)) complete = false;
        }
        if (!complete) continue;
        PushReplicated(*out, group, 2);
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(At<void**>(ValueOf(n), 0x10) - At<void**>(ValueOf(n), 0xC)); ++i)
            PushReplicated(*out, At<void**>(ValueOf(n), 0xC)[i], 2);
    }
    for (void* n = Next(Head(&self->list_25BC)); n != Head(&self->list_25BC); n = Next(n)) {
        std::uint8_t* e = ValueOf(n);
        if (At<void*>(e, 0xC) == record || At<void*>(e, 0x10) == record) PushReplicatedInline(*out, e, 2);
    }
    for (void* n = Next(Head(&self->list_25C0)); n != Head(&self->list_25C0); n = Next(n)) {
        std::uint8_t* e = ValueOf<0xC>(n);
        if (At<void*>(e, 0xC) == record || At<void*>(e, 0x10) == record) PushReplicatedInline(*out, e, 2);
    }
}
EI_REPLACE(0x00417EB0, ServerCollectReplicated);

} // namespace net
